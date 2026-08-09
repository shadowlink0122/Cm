// MIR lowering - 制御フロー文（return/if/while/for/loop/switch/block）

#include "internal/base/debug.hpp"
#include "internal/base/target.hpp"
#include "internal/hir/slice_dispatch.hpp"
#include "internal/mir/lowering/layout.hpp"
#include "internal/mir/lowering/stmt.hpp"
#include "internal/mir/passes/scalar/const_eval.hpp"

#include <cinttypes>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cm::mir {

// return文のlowering
void StmtLowering::lower_return(const hir::HirReturn& ret, LoweringContext& ctx) {
    if (ret.value) {
        // 戻り値をlowering
        LocalId return_value = expr_lowering->lower_expression(*ret.value, ctx);

        // return self のように、戻り値のローカルがポインタで戻り値型が非ポインタの集約型のとき、
        // ポインタのビットをそのまま構造体値として返してしまう（C1）。
        // pointeeが戻り値型と一致する場合はデリファレンスを挟んで値を返す。
        MirPlace return_src{return_value};
        if (return_value < ctx.func->locals.size() &&
            ctx.func->return_local < ctx.func->locals.size()) {
            const auto& rv_type = ctx.func->locals[return_value].type;
            const auto& ret_type = ctx.func->locals[ctx.func->return_local].type;
            if (rv_type && rv_type->kind == hir::TypeKind::Pointer && rv_type->element_type &&
                ret_type && ret_type->kind != hir::TypeKind::Pointer &&
                (ret_type->kind == hir::TypeKind::Struct ||
                 ret_type->kind == hir::TypeKind::Union)) {
                const auto& pointee = rv_type->element_type;
                if (pointee->kind == ret_type->kind && pointee->name == ret_type->name) {
                    return_src.projections.push_back(PlaceProjection::deref(ret_type, pointee));
                }
            }
        }

        // 固定長配列をスライス戻り値型で返す場合、スタック上の配列ポインタが生のまま返り
        // 呼び出し側がCmSlice*として解釈してクラッシュしていた。
        // cm_array_to_slice（ヒープへ内容コピー）を挟んでスライスへ実体化してから返す
        bool returned_as_slice = false;
        if (ret.value->type && ctx.func->return_local < ctx.func->locals.size()) {
            auto value_type = ctx.resolve_typedef(ret.value->type);
            const auto& ret_type = ctx.func->locals[ctx.func->return_local].type;
            if (value_type && value_type->kind == hir::TypeKind::Array &&
                value_type->array_size.has_value() && ret_type &&
                ret_type->kind == hir::TypeKind::Array && !ret_type->array_size.has_value()) {
                const int64_t array_size = value_type->array_size.value_or(0);
                const int64_t elem_size = layout::array_elem_stride(ctx, value_type->element_type);

                LocalId addr_local = ctx.new_temp(hir::make_pointer(value_type->element_type));
                ctx.push_statement(
                    MirStatement::assign(MirPlace{addr_local}, MirRvalue::ref(return_src, false)));

                LocalId size_local = ctx.new_temp(hir::make_long());
                MirConstant size_const;
                size_const.value = array_size;
                size_const.type = hir::make_long();
                ctx.push_statement(MirStatement::assign(
                    MirPlace{size_local}, MirRvalue::use(MirOperand::constant(size_const))));

                LocalId elem_size_local = ctx.new_temp(hir::make_long());
                MirConstant elem_size_const;
                elem_size_const.value = elem_size;
                elem_size_const.type = hir::make_long();
                ctx.push_statement(
                    MirStatement::assign(MirPlace{elem_size_local},
                                         MirRvalue::use(MirOperand::constant(elem_size_const))));

                BlockId success_block = ctx.new_block();
                std::vector<MirOperandPtr> args;
                args.push_back(MirOperand::copy(MirPlace{addr_local}));
                args.push_back(MirOperand::copy(MirPlace{size_local}));
                args.push_back(MirOperand::copy(MirPlace{elem_size_local}));

                auto call_term = std::make_unique<MirTerminator>();
                call_term->kind = MirTerminator::Call;
                call_term->data =
                    MirTerminator::CallData{MirOperand::function_ref("cm_array_to_slice"),
                                            std::move(args),
                                            MirPlace{ctx.func->return_local},
                                            success_block,
                                            std::nullopt,
                                            "",
                                            "",
                                            false};
                ctx.set_terminator(std::move(call_term));
                ctx.switch_to_block(success_block);
                returned_as_slice = true;
            }
        }

        if (!returned_as_slice) {
            // 変換統一ドライバ第1段: numeric/ユニオン構築/固定長配列→スライスをcoerce_to_expected 1系統で挿入する（B2/Y2。直接returnの固定長配列は前段の専用経路が処理済み）
            if (return_src.projections.empty() &&
                ctx.func->return_local < ctx.func->locals.size()) {
                const auto& ret_type = ctx.func->locals[ctx.func->return_local].type;
                LocalId coerced = ctx.coerce_to_expected(return_src.local, ret_type);
                return_src = MirPlace{coerced};
            }
            // 戻り値をreturn用ローカル変数に代入
            ctx.push_statement(MirStatement::assign(MirPlace{ctx.func->return_local},
                                                    MirRvalue::use(MirOperand::copy(return_src))));
        }
    }

    // 現在のスコープのdefer文を実行（逆順）
    auto defers = ctx.get_defer_stmts();
    for (const auto* defer_stmt : defers) {
        lower_statement(*defer_stmt, ctx);
    }

    // デストラクタを呼び出す（逆順）
    auto destructor_vars = ctx.get_all_destructor_vars();
    for (const auto& [local_id, type_name] : destructor_vars) {
        // ネストジェネリック型名の正規化（Vector<int> → Vector__int）
        std::string normalized_name = type_name;
        if (normalized_name.find('<') != std::string::npos) {
            std::string result;
            for (char c : normalized_name) {
                if (c == '<' || c == '>') {
                    if (c == '<')
                        result += "__";
                    // '>'は省略
                } else if (c == ',' || c == ' ') {
                    // カンマと空白は省略
                } else {
                    result += c;
                }
            }
            normalized_name = result;
        }
        std::string dtor_name = normalized_name + "__dtor";

        // デストラクタ呼び出しを生成（selfはポインタとして渡す）
        // ローカル変数の型を取得
        hir::TypePtr local_type = std::make_shared<hir::Type>(hir::TypeKind::Struct);
        local_type->name = type_name;
        LocalId ref_temp = ctx.new_temp(hir::make_pointer(local_type));
        ctx.push_statement(
            MirStatement::assign(MirPlace{ref_temp}, MirRvalue::ref(MirPlace{local_id}, false)));

        std::vector<MirOperandPtr> args;
        args.push_back(MirOperand::copy(MirPlace{ref_temp}));

        BlockId success_block = ctx.new_block();

        auto func_operand = MirOperand::function_ref(dtor_name);
        auto call_term = std::make_unique<MirTerminator>();
        call_term->kind = MirTerminator::Call;
        call_term->data = MirTerminator::CallData{std::move(func_operand),
                                                  std::move(args),
                                                  std::nullopt,  // void戻り値
                                                  success_block,
                                                  std::nullopt,
                                                  "",
                                                  "",
                                                  false};  // 通常の関数呼び出し
        ctx.set_terminator(std::move(call_term));
        ctx.switch_to_block(success_block);
    }

    // return終端命令
    ctx.set_terminator(MirTerminator::return_value());

    // 新しいブロックを作成（到達不可能だが、CFGの整合性のため）
    ctx.switch_to_block(ctx.new_block());
}

// if文のlowering
void StmtLowering::lower_if(const hir::HirIf& if_stmt, LoweringContext& ctx) {
    // 条件をlowering
    LocalId cond = expr_lowering->lower_expression(*if_stmt.cond, ctx);

    // ブロックを作成
    BlockId then_block = ctx.new_block();
    BlockId else_block = ctx.new_block();
    BlockId merge_block = ctx.new_block();

    // 条件分岐
    ctx.set_terminator(
        MirTerminator::switch_int(MirOperand::copy(MirPlace{cond}), {{1, then_block}}, else_block));

    // then部をlowering
    ctx.switch_to_block(then_block);
    for (const auto& stmt : if_stmt.then_block) {
        lower_statement(*stmt, ctx);
    }
    if (!ctx.get_current_block()->terminator) {
        ctx.set_terminator(MirTerminator::goto_block(merge_block));
    }

    // else部をlowering
    ctx.switch_to_block(else_block);
    for (const auto& stmt : if_stmt.else_block) {
        lower_statement(*stmt, ctx);
    }
    if (!ctx.get_current_block()->terminator) {
        ctx.set_terminator(MirTerminator::goto_block(merge_block));
    }

    // マージポイント
    ctx.switch_to_block(merge_block);
}

// while文のlowering
void StmtLowering::lower_while(const hir::HirWhile& while_stmt, LoweringContext& ctx) {
    // ブロックを作成
    BlockId loop_header = ctx.new_block();
    BlockId loop_body = ctx.new_block();
    BlockId loop_exit = ctx.new_block();

    // ループヘッダへジャンプ
    ctx.set_terminator(MirTerminator::goto_block(loop_header));

    // ループヘッダ（条件評価）
    ctx.switch_to_block(loop_header);
    LocalId cond = expr_lowering->lower_expression(*while_stmt.cond, ctx);
    ctx.set_terminator(
        MirTerminator::switch_int(MirOperand::copy(MirPlace{cond}), {{1, loop_body}}, loop_exit));

    // ループボディ
    ctx.switch_to_block(loop_body);
    ctx.push_loop(loop_header, loop_exit);
    // ループボディ用のスコープを作成（各反復でdefer文を実行。forと対称にする。M16）
    ctx.push_scope();
    for (const auto& stmt : while_stmt.body) {
        lower_statement(*stmt, ctx);
    }
    // ループボディ終了時にdefer文を実行（逆順）
    auto defers = ctx.get_defer_stmts();
    for (const auto* defer_stmt : defers) {
        lower_statement(*defer_stmt, ctx);
    }
    // ループボディスコープのデストラクタを毎周期実行する（C13: 明示ブロックと同一順序に揃える。
    // これが無いとループ内のVector等が関数終了まで解放されず反復ごとにリークしていた）
    emit_scope_destructors(ctx);
    ctx.pop_scope();
    ctx.pop_loop();
    if (!ctx.get_current_block()->terminator) {
        ctx.set_terminator(MirTerminator::goto_block(loop_header));
    }

    // ループ出口
    ctx.switch_to_block(loop_exit);
}

// for文のlowering - whileループに展開
// for (init; cond; update) { body } を以下に変換:
// init; while (cond) { body; update; }
void StmtLowering::lower_for(const hir::HirFor& for_stmt, LoweringContext& ctx) {
    // 初期化部
    if (for_stmt.init) {
        lower_statement(*for_stmt.init, ctx);
    }

    // whileループとして処理
    // ブロックを作成
    BlockId loop_header = ctx.new_block();
    BlockId loop_body = ctx.new_block();
    BlockId loop_exit = ctx.new_block();

    // ループヘッダへジャンプ
    ctx.set_terminator(MirTerminator::goto_block(loop_header));

    // ループヘッダ（条件評価）
    ctx.switch_to_block(loop_header);
    if (for_stmt.cond) {
        LocalId cond = expr_lowering->lower_expression(*for_stmt.cond, ctx);
        ctx.set_terminator(MirTerminator::switch_int(MirOperand::copy(MirPlace{cond}),
                                                     {{1, loop_body}}, loop_exit));
    } else {
        // 条件なしの場合は無限ループ
        ctx.set_terminator(MirTerminator::goto_block(loop_body));
    }

    // ループボディ
    ctx.switch_to_block(loop_body);

    // continueの処理のため、更新部がある場合は特別なブロックを用意
    BlockId continue_target = loop_header;
    if (for_stmt.update) {
        // 更新式がある場合、continueは更新部を実行してからヘッダへ
        continue_target = ctx.new_block();
    }

    ctx.push_loop(loop_header, loop_exit, continue_target);

    // ループボディ用のスコープを作成（各反復でdefer文を実行）
    ctx.push_scope();

    // ボディの文を処理
    for (const auto& stmt : for_stmt.body) {
        lower_statement(*stmt, ctx);
    }

    // ループボディ終了時にdefer文を実行（逆順）
    auto defers = ctx.get_defer_stmts();
    for (const auto* defer_stmt : defers) {
        lower_statement(*defer_stmt, ctx);
    }

    // ループボディスコープのデストラクタを毎周期実行する（C13: whileと同一の順序規約）
    emit_scope_destructors(ctx);

    ctx.pop_scope();

    // ボディの最後に更新式を追加（whileループと同じ構造）
    if (for_stmt.update && !ctx.get_current_block()->terminator) {
        // 更新式を直接lowering（結果は破棄）
        expr_lowering->lower_expression(*for_stmt.update, ctx);
    }

    ctx.pop_loop();

    // ループヘッダへ戻る
    if (!ctx.get_current_block()->terminator) {
        ctx.set_terminator(MirTerminator::goto_block(loop_header));
    }

    // continueターゲット（更新式がある場合）
    if (for_stmt.update && continue_target != loop_header) {
        ctx.switch_to_block(continue_target);
        // 更新式を直接lowering（結果は破棄）
        expr_lowering->lower_expression(*for_stmt.update, ctx);
        // ヘッダへ戻る
        ctx.set_terminator(MirTerminator::goto_block(loop_header));
    }

    // ループ出口
    ctx.switch_to_block(loop_exit);
}

// loop文のlowering
void StmtLowering::lower_loop(const hir::HirLoop& loop_stmt, LoweringContext& ctx) {
    // ブロックを作成
    BlockId loop_block = ctx.new_block();
    BlockId loop_exit = ctx.new_block();

    // ループブロックへジャンプ
    ctx.set_terminator(MirTerminator::goto_block(loop_block));

    // ループボディ
    ctx.switch_to_block(loop_block);
    ctx.push_loop(loop_block, loop_exit);
    for (const auto& stmt : loop_stmt.body) {
        lower_statement(*stmt, ctx);
    }
    ctx.pop_loop();
    if (!ctx.get_current_block()->terminator) {
        // 無限ループ
        ctx.set_terminator(MirTerminator::goto_block(loop_block));
    }

    // ループ出口（breakで到達）
    ctx.switch_to_block(loop_exit);
}

// switch文のlowering
void StmtLowering::lower_switch(const hir::HirSwitch& switch_stmt, LoweringContext& ctx) {
    // 判別式をlowering
    LocalId discriminant = expr_lowering->lower_expression(*switch_stmt.expr, ctx);

    // 文字列スクルーチニはswitch命令（整数専用）へ落とせないため、
    // 文字列Eq比較の逐次チェーンへ脱糖する（N3。従来はcase定数が整数として発行され
    // LLVM検証エラー、jsはポインタ同一性比較で常にelse落ちしていた）
    const bool is_string_switch = switch_stmt.expr && switch_stmt.expr->type &&
                                  switch_stmt.expr->type->kind == hir::TypeKind::String;
    if (is_string_switch) {
        BlockId default_block = ctx.new_block();
        BlockId exit_block = ctx.new_block();

        // 各caseの文字列リテラル値と本体ブロックを収集し、比較チェーンを構築する
        std::vector<BlockId> case_blocks;
        for (size_t i = 0; i < switch_stmt.cases.size(); ++i) {
            if (!switch_stmt.cases[i].pattern) {
                case_blocks.push_back(0);  // else/defaultはプレースホルダー
                continue;
            }
            BlockId case_block = ctx.new_block();
            case_blocks.push_back(case_block);

            // case文字列を抽出（SingleValueの文字列リテラルのみ許可）
            std::string case_str;
            bool have_str = false;
            const auto& pat = *switch_stmt.cases[i].pattern;
            const hir::HirExprPtr* value_expr = nullptr;
            if (pat.kind == hir::HirSwitchPattern::SingleValue && pat.value) {
                value_expr = &pat.value;
            } else if (switch_stmt.cases[i].value) {
                value_expr = &switch_stmt.cases[i].value;
            }
            if (value_expr) {
                if (auto lit =
                        std::get_if<std::unique_ptr<hir::HirLiteral>>(&(*value_expr)->kind)) {
                    if (*lit && std::holds_alternative<std::string>((*lit)->value)) {
                        case_str = std::get<std::string>((*lit)->value);
                        have_str = true;
                    }
                }
            }
            if (!have_str) {
                // 非リテラル/非文字列caseは一致しない扱い（型検査側の将来のエラー化対象）
                continue;
            }

            LocalId cmp = ctx.new_temp(hir::make_bool());
            MirConstant sc;
            sc.type = hir::make_string();
            sc.value = case_str;
            ctx.push_statement(MirStatement::assign(
                MirPlace{cmp},
                MirRvalue::binary(MirBinaryOp::Eq, MirOperand::copy(MirPlace{discriminant}),
                                  MirOperand::constant(sc), hir::make_bool())));
            BlockId next_check = ctx.new_block();
            ctx.set_terminator(MirTerminator::switch_int(MirOperand::copy(MirPlace{cmp}),
                                                         {{1, case_block}}, next_check));
            ctx.switch_to_block(next_check);
        }
        // どのcaseにも一致しなければdefaultへ
        ctx.set_terminator(MirTerminator::goto_block(default_block));

        // 各case本体をlowering
        for (size_t i = 0; i < switch_stmt.cases.size(); ++i) {
            if (!switch_stmt.cases[i].pattern) {
                continue;
            }
            ctx.switch_to_block(case_blocks[i]);
            for (const auto& stmt : switch_stmt.cases[i].stmts) {
                lower_statement(*stmt, ctx);
            }
            if (!ctx.get_current_block()->terminator) {
                ctx.set_terminator(MirTerminator::goto_block(exit_block));
            }
        }

        // default本体（else句）をlowering
        ctx.switch_to_block(default_block);
        for (const auto& case_item : switch_stmt.cases) {
            if (!case_item.pattern) {
                for (const auto& stmt : case_item.stmts) {
                    lower_statement(*stmt, ctx);
                }
                break;
            }
        }
        if (!ctx.get_current_block()->terminator) {
            ctx.set_terminator(MirTerminator::goto_block(exit_block));
        }

        ctx.switch_to_block(exit_block);
        return;
    }

    // ヘルパー: HirExprからcase値（int64_t）を抽出
    auto extract_case_value = [](const hir::HirExprPtr& expr) -> int64_t {
        if (!expr)
            return 0;
        if (auto lit = std::get_if<std::unique_ptr<hir::HirLiteral>>(&expr->kind)) {
            if (*lit) {
                auto& val = (*lit)->value;
                if (std::holds_alternative<int64_t>(val)) {
                    return std::get<int64_t>(val);
                } else if (std::holds_alternative<char>(val)) {
                    return static_cast<int64_t>(std::get<char>(val));
                }
            }
        }
        return 0;
    };

    // 各caseのブロックを作成
    std::vector<std::pair<int64_t, BlockId>> cases;
    std::vector<BlockId> case_blocks;
    for (size_t i = 0; i < switch_stmt.cases.size(); ++i) {
        // else/defaultケース（patternがnull）はスキップ
        if (!switch_stmt.cases[i].pattern) {
            case_blocks.push_back(0);  // プレースホルダー
            continue;
        }

        BlockId case_block = ctx.new_block();
        case_blocks.push_back(case_block);

        const auto& pat = *switch_stmt.cases[i].pattern;

        if (pat.kind == hir::HirSwitchPattern::SingleValue) {
            // 単一値パターン: patternのvalueから値を取得
            int64_t case_value = 0;
            if (pat.value) {
                case_value = extract_case_value(pat.value);
            }
            // 旧互換性のためvalueフィールドも確認
            else if (switch_stmt.cases[i].value) {
                case_value = extract_case_value(switch_stmt.cases[i].value);
            }
            cases.push_back({case_value, case_block});

        } else if (pat.kind == hir::HirSwitchPattern::Or) {
            // Orパターン: 各サブパターンの値を同じブロックに分岐
            for (const auto& sub_pat : pat.or_patterns) {
                if (sub_pat) {
                    if (sub_pat->kind == hir::HirSwitchPattern::SingleValue) {
                        int64_t sub_value = extract_case_value(sub_pat->value);
                        cases.push_back({sub_value, case_block});
                    } else if (sub_pat->kind == hir::HirSwitchPattern::Range) {
                        int64_t range_start = extract_case_value(sub_pat->range_start);
                        int64_t range_end = extract_case_value(sub_pat->range_end);
                        if (range_end - range_start <= 256) {
                            for (int64_t v = range_start; v <= range_end; ++v) {
                                cases.push_back({v, case_block});
                            }
                        }
                    }
                }
            }

        } else if (pat.kind == hir::HirSwitchPattern::Range) {
            // Rangeパターン: 範囲内の全値を個別のcaseとして展開
            int64_t range_start = extract_case_value(pat.range_start);
            int64_t range_end = extract_case_value(pat.range_end);
            // 安全制限: 最大256エントリまで
            if (range_end - range_start <= 256) {
                for (int64_t v = range_start; v <= range_end; ++v) {
                    cases.push_back({v, case_block});
                }
            }
        }
    }

    // defaultブロック
    BlockId default_block = ctx.new_block();
    BlockId exit_block = ctx.new_block();

    // switch終端命令
    ctx.set_terminator(
        MirTerminator::switch_int(MirOperand::copy(MirPlace{discriminant}), cases, default_block));

    // 各caseをlowering（else/default以外）
    for (size_t i = 0; i < switch_stmt.cases.size(); ++i) {
        // else/defaultケース（patternがnull）はスキップ
        if (!switch_stmt.cases[i].pattern) {
            continue;
        }
        ctx.switch_to_block(case_blocks[i]);
        for (const auto& stmt : switch_stmt.cases[i].stmts) {
            lower_statement(*stmt, ctx);
        }
        if (!ctx.get_current_block()->terminator) {
            ctx.set_terminator(MirTerminator::goto_block(exit_block));
        }
    }

    // defaultをlowering
    ctx.switch_to_block(default_block);
    // else句（patternがnullのcase）を探して処理
    for (const auto& case_item : switch_stmt.cases) {
        if (!case_item.pattern) {  // else/defaultケース
            for (const auto& stmt : case_item.stmts) {
                lower_statement(*stmt, ctx);
            }
            break;
        }
    }
    if (!ctx.get_current_block()->terminator) {
        ctx.set_terminator(MirTerminator::goto_block(exit_block));
    }

    // 出口ブロック
    ctx.switch_to_block(exit_block);
}

// block文のlowering
void StmtLowering::lower_block(const hir::HirBlock& block, LoweringContext& ctx) {
    // 新しいスコープを開始
    ctx.push_scope();

    // ブロック内の各文をlowering
    for (const auto& stmt : block.stmts) {
        lower_statement(*stmt, ctx);
    }

    // スコープ終了時にdefer文を実行（逆順）
    auto defers = ctx.get_defer_stmts();
    for (const auto* defer_stmt : defers) {
        lower_statement(*defer_stmt, ctx);
    }

    // スコープ終了時にデストラクタを呼び出し
    emit_scope_destructors(ctx);

    // スコープを終了
    ctx.pop_scope();
}

}  // namespace cm::mir
