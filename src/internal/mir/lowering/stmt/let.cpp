// MIR lowering - let文（定数畳み込みヘルパーと配列/スライス初期化を含む変数宣言の展開）

#include "internal/base/debug.hpp"
#include "internal/base/target.hpp"
#include "internal/hir/slice_dispatch.hpp"
#include "internal/mir/lowering/layout.hpp"
#include "internal/mir/lowering/stmt.hpp"
#include "internal/mir/passes/scalar/const_eval.hpp"
#include "internal/syntax/ast/typekey.hpp"

#include <cinttypes>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cm::mir {

// 型の幅を数値化（二項演算の結果型決定用）
static int type_width(const hir::TypePtr& type) {
    if (!type)
        return 32;
    switch (type->kind) {
        case hir::TypeKind::ULong:
            return 65;  // ulongは最も広い
        case hir::TypeKind::Long:
            return 64;
        case hir::TypeKind::UInt:
            return 33;
        default:
            return 32;  // int
    }
}

// LHS/RHSの型のうち広い方を返す
static hir::TypePtr wider_type(const hir::TypePtr& lhs, const hir::TypePtr& rhs) {
    return type_width(lhs) >= type_width(rhs) ? lhs : rhs;
}

// コンパイル時定数評価（const folding）
// HIR式がコンパイル時に評価可能な場合、MirConstantを返す
static std::optional<MirConstant> try_const_eval(const hir::HirExpr& expr, LoweringContext& ctx) {
    // リテラルの場合
    if (auto* lit = std::get_if<std::unique_ptr<hir::HirLiteral>>(&expr.kind)) {
        if (*lit) {
            MirConstant c;
            c.type = expr.type ? expr.type : hir::make_int();
            c.value = (*lit)->value;
            return c;
        }
    }

    // 変数参照の場合（既登録のconst変数を伝搬）
    if (auto* var = std::get_if<std::unique_ptr<hir::HirVarRef>>(&expr.kind)) {
        if (*var) {
            auto cv = ctx.get_const_value((*var)->name);
            if (cv)
                return *cv;
        }
    }

    // 単項マイナスの場合
    if (auto* unary = std::get_if<std::unique_ptr<hir::HirUnary>>(&expr.kind)) {
        if (*unary && (*unary)->op == hir::HirUnaryOp::Neg && (*unary)->operand) {
            auto inner = try_const_eval(*(*unary)->operand, ctx);
            if (inner && std::holds_alternative<int64_t>(inner->value)) {
                MirConstant c;
                c.type = inner->type;
                c.value = -std::get<int64_t>(inner->value);
                return c;
            }
        }
    }

    // 二項演算の場合（ビット演算、算術演算）
    if (auto* bin = std::get_if<std::unique_ptr<hir::HirBinary>>(&expr.kind)) {
        if (*bin && (*bin)->lhs && (*bin)->rhs) {
            auto lhs = try_const_eval(*(*bin)->lhs, ctx);
            auto rhs = try_const_eval(*(*bin)->rhs, ctx);
            if (lhs && rhs && std::holds_alternative<int64_t>(lhs->value) &&
                std::holds_alternative<int64_t>(rhs->value)) {
                int64_t l = std::get<int64_t>(lhs->value);
                int64_t r = std::get<int64_t>(rhs->value);
                int64_t result = 0;
                bool ok = true;
                // 符号なし型は論理シフト・符号なし除算で畳み込む（folding.cppと同じ規則）
                const bool uns = const_eval::use_unsigned_op(lhs->type, rhs->type);
                switch ((*bin)->op) {
                    case hir::HirBinaryOp::Add:
                        result = l + r;
                        break;
                    case hir::HirBinaryOp::Sub:
                        result = l - r;
                        break;
                    case hir::HirBinaryOp::Mul:
                        result = l * r;
                        break;
                    case hir::HirBinaryOp::Div:
                        if (r != 0)
                            result = uns ? static_cast<int64_t>(static_cast<uint64_t>(l) /
                                                                static_cast<uint64_t>(r))
                                         : l / r;
                        else
                            ok = false;
                        break;
                    case hir::HirBinaryOp::Mod:
                        if (r != 0)
                            result = uns ? static_cast<int64_t>(static_cast<uint64_t>(l) %
                                                                static_cast<uint64_t>(r))
                                         : l % r;
                        else
                            ok = false;
                        break;
                    case hir::HirBinaryOp::BitAnd:
                        result = l & r;
                        break;
                    case hir::HirBinaryOp::BitOr:
                        result = l | r;
                        break;
                    case hir::HirBinaryOp::BitXor:
                        result = l ^ r;
                        break;
                    case hir::HirBinaryOp::Shl:
                        result = l << r;
                        break;
                    case hir::HirBinaryOp::Shr:
                        result = uns ? static_cast<int64_t>(static_cast<uint64_t>(l) >>
                                                            (static_cast<uint64_t>(r) & 63))
                                     : l >> (r & 63);
                        break;
                    default:
                        ok = false;
                        break;
                }
                if (ok) {
                    MirConstant c;
                    // LHS/RHSの型のうち広い方を結果型とする
                    c.type = wider_type(lhs->type, rhs->type);
                    // 型幅へ正規化する（int32のオーバーフローをラップ等。
                    // folding.cppと同じ規則。生のint64値を伝播すると
                    // 表示値と比較結果が実行時セマンティクスと食い違う）
                    c.value = const_eval::normalize_int(result, c.type);
                    return c;
                }
            }
        }
    }

    return std::nullopt;
}

// let文のlowering
void StmtLowering::lower_let(const hir::HirLet& let, LoweringContext& ctx) {
    // move初期化の場合、新しいローカルを作成せずエイリアスとして登録（真のゼロコストmove）
    // is_moveフラグはHIR loweringでMoveExprから初期化された場合に立てられる
    if (let.is_move && let.init && !let.ctor_call) {
        if (auto* var_ref = std::get_if<std::unique_ptr<hir::HirVarRef>>(&let.init->kind)) {
            if (*var_ref && !(*var_ref)->is_function_ref && !(*var_ref)->is_closure) {
                auto src_local = ctx.resolve_variable((*var_ref)->name);
                if (src_local) {
                    // 元の変数を新しい名前で再登録（エイリアス）
                    ctx.register_variable(let.name, *src_local);
                    debug_msg("mir_move_alias", "[MIR] Move alias: '" + let.name + "' -> local " +
                                                    std::to_string(*src_local) + " (same as '" +
                                                    (*var_ref)->name + "')");
                    return;
                }
            }
        }
    }

    // 新しいローカル変数を作成
    // is_const = true なら変更不可、false なら変更可能
    // is_static = true なら関数呼び出し間で値が保持される

    // enum型の場合、Tagged Union構造体型に変換
    // enum型は型名がenum_defsに登録されている
    hir::TypePtr actual_type = let.type;

    if (let.type && !let.type->name.empty() && ctx.enum_defs &&
        ctx.enum_defs->count(let.type->name) &&
        (!ctx.tagged_union_names || ctx.tagged_union_names->count(let.type->name) > 0)) {
        // Q5: 他サイト（context.cpp/base.cpp）と同様にペイロード付きenumのみを__TaggedUnion_構造体へ変換する。値enum（checkerのint解決がenum名をnameへ保持するようになった）を無条件変換すると16バイト構造体ローカルになり値が壊れる
        auto tagged_union_type = std::make_shared<hir::Type>(hir::TypeKind::Struct);
        tagged_union_type->name = "__TaggedUnion_" + let.type->name;
        // 元の型引数を保持（補間ミニパイプラインでのペイロード型復元に使用）
        tagged_union_type->type_args = let.type->type_args;
        actual_type = tagged_union_type;
    }

    LocalId local = ctx.new_local(let.name, actual_type, !let.is_const, true, let.is_static);

    // 変数をスコープに登録
    ctx.register_variable(let.name, local);

    // デバッグ: 登録した変数を確認
    if (let.type && let.type->kind == hir::TypeKind::Function) {
        debug_msg("mir_let_func_ptr",
                  "[MIR] Registered variable '" + let.name + "' as local " + std::to_string(local));
    }

    // const変数の場合、初期値をコンパイル時評価して保存
    // リテラル、const参照、二項演算（ビット演算含む）に対応
    if (let.is_const && let.init) {
        auto const_val = try_const_eval(*let.init, ctx);
        if (const_val) {
            const_val->type = let.type ? let.type : const_val->type;
            ctx.register_const_value(let.name, *const_val);
        }
    }

    // static変数: 格納はゼロ初期化のグローバル（バックエンド側でfunc名_変数名の永続領域）とし、
    // 初期化子は初回到達時に1回だけ実行するガード付き代入として発行する（X1。
    // 従来は初期化コード自体を生成せず、非ゼロ初期値が全スコープで無視されていた）
    if (let.is_static) {
        if (let.init) {
            // ガード用のstatic bool（ゼロ初期化=未初期化）
            LocalId guard =
                ctx.new_local(let.name + "__static_guard", hir::make_bool(), true, false, true);
            LocalId guard_val = ctx.new_temp(hir::make_bool());
            ctx.push_statement(MirStatement::assign(
                MirPlace{guard_val}, MirRvalue::use(MirOperand::copy(MirPlace{guard}))));

            BlockId init_block = ctx.new_block();
            BlockId after_block = ctx.new_block();
            ctx.set_terminator(MirTerminator::switch_int(MirOperand::copy(MirPlace{guard_val}),
                                                         {{0, init_block}}, after_block));

            ctx.switch_to_block(init_block);
            MirConstant true_const;
            true_const.value = true;
            true_const.type = hir::make_bool();
            ctx.push_statement(MirStatement::assign(
                MirPlace{guard}, MirRvalue::use(MirOperand::constant(true_const))));
            LocalId init_value = expr_lowering->lower_expression(*let.init, ctx);
            ctx.push_statement(MirStatement::assign(
                MirPlace{local}, MirRvalue::use(MirOperand::copy(MirPlace{init_value}))));
            ctx.set_terminator(MirTerminator::goto_block(after_block));

            ctx.switch_to_block(after_block);
        }
        return;
    }

    // スライス型の変数で初期値がない場合、空のスライスを作成（容量0の確保。正準ヘルパへ委譲）
    if (!let.init && let.type && let.type->kind == hir::TypeKind::Array &&
        !let.type->array_size.has_value()) {
        expr_lowering->materialize_slice_literal({}, let.type, ctx, MirPlace{local});
        return;
    }

    // コンストラクタ呼び出しがある場合はlet.initをスキップ（コンストラクタが初期化を担当）
    if (let.init && !let.ctor_call) {
        // 配列→ポインタ暗黙変換のチェック
        // 左辺がポインタ型で右辺が配列型の場合、配列の先頭要素へのアドレスを取得
        bool is_array_to_pointer = false;
        if (let.type && let.init->type && let.type->kind == hir::TypeKind::Pointer &&
            let.init->type->kind == hir::TypeKind::Array) {
            is_array_to_pointer = true;
        }

        if (is_array_to_pointer) {
            // 配列変数への参照を取得
            if (auto* var_ref = std::get_if<std::unique_ptr<hir::HirVarRef>>(&let.init->kind)) {
                auto arr_local = ctx.resolve_variable((*var_ref)->name);
                if (arr_local) {
                    // 配列の先頭要素(&arr[0])へのRefを生成
                    // インデックス0のための一時変数
                    LocalId idx_zero = ctx.new_temp(hir::make_int());
                    MirConstant zero_const;
                    zero_const.value = int64_t(0);
                    zero_const.type = hir::make_int();
                    ctx.push_statement(MirStatement::assign(
                        MirPlace{idx_zero}, MirRvalue::use(MirOperand::constant(zero_const))));

                    // &arr[0] を生成
                    MirPlace arr_elem{*arr_local};
                    arr_elem.projections.push_back(PlaceProjection::index(idx_zero));

                    ctx.push_statement(
                        MirStatement::assign(MirPlace{local}, MirRvalue::ref(arr_elem, false)));
                } else {
                    // フォールバック: 通常のlowering
                    LocalId init_value = expr_lowering->lower_expression(*let.init, ctx);
                    ctx.push_statement(MirStatement::assign(
                        MirPlace{local}, MirRvalue::use(MirOperand::copy(MirPlace{init_value}))));
                }
            } else {
                // 変数参照でない場合は通常処理
                LocalId init_value = expr_lowering->lower_expression(*let.init, ctx);
                ctx.push_statement(MirStatement::assign(
                    MirPlace{local}, MirRvalue::use(MirOperand::copy(MirPlace{init_value}))));
            }
        } else {
            // スライスへの配列リテラル初期化をチェック
            bool is_slice_init_from_array = false;
            if (let.type && let.type->kind == hir::TypeKind::Array &&
                !let.type->array_size.has_value()) {
                // 左辺がスライス（動的配列）
                if (let.init->type && let.init->type->kind == hir::TypeKind::Array &&
                    let.init->type->array_size.has_value()) {
                    // 右辺が静的配列（配列リテラル）
                    is_slice_init_from_array = true;
                }
            }

            if (is_slice_init_from_array) {
                if (auto* arr_lit =
                        std::get_if<std::unique_ptr<hir::HirArrayLiteral>>(&let.init->kind)) {
                    // 配列リテラルからスライスへの初期化（cm_slice_new+要素push。正準ヘルパへ委譲）
                    expr_lowering->materialize_slice_literal((*arr_lit)->elements, let.type, ctx,
                                                             MirPlace{local});
                } else {
                    // 配列リテラルでない場合（変数参照など）はcm_array_to_sliceでヒープスライスへ変換して格納
                    LocalId init_value = expr_lowering->lower_expression(*let.init, ctx);
                    ctx.materialize_array_to_slice(MirPlace{init_value}, let.init->type, let.type,
                                                   MirPlace{local});
                }
            } else {
                // 通常の初期化
                LocalId init_value = expr_lowering->lower_expression(*let.init, ctx);

                // クロージャ情報を新しい変数にコピー
                if (init_value < ctx.func->locals.size()) {
                    auto& init_decl = ctx.func->locals[init_value];
                    if (init_decl.is_closure && !init_decl.captured_locals.empty()) {
                        auto& new_decl = ctx.func->locals[local];
                        new_decl.is_closure = true;
                        new_decl.closure_func_name = init_decl.closure_func_name;
                        new_decl.captured_locals = init_decl.captured_locals;
                        debug_msg("mir_closure_copy",
                                  "[MIR] Copied closure info to local " + std::to_string(local) +
                                      " from local " + std::to_string(init_value) +
                                      ", func=" + new_decl.closure_func_name + ", captures=" +
                                      std::to_string(new_decl.captured_locals.size()));
                    }
                }

                // デバッグ: 型を確認
                if (let.type) {
                    debug_msg("mir_let_type", "[MIR] Let variable '" + let.name +
                                                  "' has type kind: " +
                                                  std::to_string(static_cast<int>(let.type->kind)));
                }

                // デバッグ: 関数ポインタ型の初期化を確認
                if (let.type && let.type->kind == hir::TypeKind::Function) {
                    debug_msg("mir_let_func_ptr", "[MIR] Function pointer initialization: local " +
                                                      std::to_string(local) + " = copy(local " +
                                                      std::to_string(init_value) + ")");

                    // 実際にステートメントを生成
                    auto stmt = MirStatement::assign(
                        MirPlace{local}, MirRvalue::use(MirOperand::copy(MirPlace{init_value})));
                    debug_msg("mir_let_func_ptr",
                              "[MIR] Created assign statement for local " + std::to_string(local));
                    ctx.push_statement(std::move(stmt));
                    debug_msg("mir_let_func_ptr", "[MIR] Pushed statement to context");

                    // 現在のブロックを確認
                    auto* block = ctx.get_current_block();
                    if (block) {
                        debug_msg("mir_let_func_ptr", "[MIR] Current block has " +
                                                          std::to_string(block->statements.size()) +
                                                          " statements");
                    } else {
                        debug_msg("mir_let_func_ptr", "[MIR] ERROR: No current block!");
                    }
                } else {
                    // デバッグ: 通常の初期化
                    if (let.name == "result") {
                        auto* block = ctx.get_current_block();
                        if (block) {
                            debug_msg("mir_result_init",
                                      "[MIR] Before 'result' init, block has " +
                                          std::to_string(block->statements.size()) + " statements");
                        }
                    }
                    // 変換統一ドライバ第1段: numeric/ユニオン構築（タグ+ペイロード。直接storeするとタグ未設定でasパニック・is誤判定）/固定長配列→スライスをcoerce_to_expected 1系統で挿入する
                    hir::TypePtr resolved_let_type = ctx.resolve_typedef(let.type);
                    init_value = ctx.coerce_to_expected(init_value, resolved_let_type);
                    ctx.push_statement(MirStatement::assign(
                        MirPlace{local}, MirRvalue::use(MirOperand::copy(MirPlace{init_value}))));
                    if (let.name == "result") {
                        auto* block = ctx.get_current_block();
                        if (block) {
                            debug_msg("mir_result_init",
                                      "[MIR] After 'result' init, block has " +
                                          std::to_string(block->statements.size()) + " statements");
                        }
                    }
                }
            }
        }
    }

    // コンストラクタ呼び出しがある場合
    if (let.ctor_call) {
        // コンストラクタ呼び出しはHirCall形式
        if (auto* call = std::get_if<std::unique_ptr<hir::HirCall>>(&let.ctor_call->kind)) {
            const auto& hir_call = **call;

            // 引数をlowering
            std::vector<MirOperandPtr> args;

            // HIRのctor_call.argsには既にthis（変数への参照）が含まれている
            // 最初の引数は変数自身への参照なので、アドレスを渡す（selfはポインタ型）
            bool first_arg = true;
            for (const auto& arg : hir_call.args) {
                if (first_arg) {
                    // 最初の引数（this/self）はアドレスを渡す
                    hir::TypePtr local_type = let.type;
                    LocalId ref_temp = ctx.new_temp(hir::make_pointer(local_type));
                    ctx.push_statement(MirStatement::assign(
                        MirPlace{ref_temp}, MirRvalue::ref(MirPlace{local}, false)));
                    args.push_back(MirOperand::copy(MirPlace{ref_temp}));
                    first_arg = false;
                } else {
                    // 残りの引数を通常通りlowering
                    LocalId arg_local = expr_lowering->lower_expression(*arg, ctx);
                    args.push_back(MirOperand::copy(MirPlace{arg_local}));
                }
            }

            // コンストラクタ関数呼び出しを生成
            BlockId success_block = ctx.new_block();
            auto func_operand = MirOperand::function_ref(hir_call.func_name);

            auto call_term = std::make_unique<MirTerminator>();
            call_term->kind = MirTerminator::Call;
            call_term->data = MirTerminator::CallData{std::move(func_operand),
                                                      std::move(args),
                                                      std::nullopt,  // コンストラクタは戻り値なし
                                                      success_block,
                                                      std::nullopt,
                                                      "",
                                                      "",
                                                      false};  // 通常の関数呼び出し
            ctx.set_terminator(std::move(call_term));
            ctx.switch_to_block(success_block);
        }
    }

    // デストラクタを持つ型の変数を登録
    if (let.type && let.type->kind == hir::TypeKind::Struct) {
        std::string type_name = let.type->name;

        // ジェネリック型は関数名ドメインの正準接頭辞で登録する（Vector<Vector<int>> ->
        // Vector__Vector$1$3$int。移行計画①: 手組み再帰マングルの曖昧フラット名を廃止）
        if (!let.type->type_args.empty()) {
            type_name = ast::typekey::fn_prefix_from_tree(*let.type);
        }

        if (ctx.has_destructor(type_name)) {
            ctx.register_destructor_var(local, type_name);
        }
    }
}

}  // namespace cm::mir
