// ============================================================
// MIR lowering - __println__ builtin（書式付き出力の展開）
// ============================================================

#include "expr.hpp"
#include "internal/base/debug.hpp"
#include "internal/hir/lowering/fwd.hpp"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cm::mir {

// __println__ を処理した場合はローカルIDを、対象外ならnulloptを返す
std::optional<LocalId> ExprLowering::try_lower_println(const hir::HirCall& call,
                                                       const hir::TypePtr& result_type,
                                                       LoweringContext& ctx) {
    (void)result_type;
    // __println__ builtin特別処理
    if (call.func_name == "__println__") {
        // 引数がない場合は空行を出力
        if (call.args.empty()) {
            BlockId success_block = ctx.new_block();

            // cm_println_string("") を呼び出す
            std::vector<MirOperandPtr> args;
            MirConstant str_const;
            str_const.type = hir::make_string();
            str_const.value = std::string("");
            args.push_back(MirOperand::constant(str_const));

            auto func_operand = MirOperand::function_ref("cm_println_string");
            auto call_term = std::make_unique<MirTerminator>();
            call_term->kind = MirTerminator::Call;
            call_term->data = MirTerminator::CallData{
                std::move(func_operand),
                std::move(args),
                std::nullopt,  // 戻り値なし
                success_block,
                std::nullopt,  // unwind無し
                "",
                "",
                false  // 通常の関数呼び出し
            };
            ctx.set_terminator(std::move(call_term));
            ctx.switch_to_block(success_block);
            return ctx.new_temp(hir::make_void());
        }

        // 最初の引数を取得
        const auto& first_arg = call.args[0];

        // 引数の型に基づいて適切なランタイム関数を選択
        std::string runtime_func;
        std::vector<MirOperandPtr> args;

        // 複数引数がある場合は常にフォーマット関数を使う
        // または、文字列リテラルでフォーマット指定子がある場合
        bool use_format = false;
        bool has_escaped_braces = false;

        // 文字列リテラルかチェック（フォーマット文字列の可能性）
        if (auto lit = std::get_if<std::unique_ptr<hir::HirLiteral>>(&first_arg->kind)) {
            if ((*lit) && (*lit)->value.index() == 5) {  // string値
                std::string str_val = std::get<std::string>((*lit)->value);

                // フォーマット文字列かチェック（{...}パターンを含むか、またはエスケープされた中括弧を含むか）
                size_t pos = 0;
                while ((pos = str_val.find('{', pos)) != std::string::npos) {
                    if (pos + 1 < str_val.length() && str_val[pos + 1] == '{') {
                        // エスケープされた {{ を発見
                        has_escaped_braces = true;
                        pos += 2;
                        continue;
                    }
                    // { の後に } があるか確認
                    size_t end_pos = str_val.find('}', pos + 1);
                    if (end_pos != std::string::npos) {
                        use_format = true;
                        break;
                    }
                    pos++;
                }
                // エスケープされた }} もチェック
                if (!has_escaped_braces) {
                    pos = 0;
                    while ((pos = str_val.find('}', pos)) != std::string::npos) {
                        if (pos + 1 < str_val.length() && str_val[pos + 1] == '}') {
                            has_escaped_braces = true;
                            break;
                        }
                        pos++;
                    }
                }

                if ((use_format && call.args.size() > 1) || has_escaped_braces || use_format) {
                    // フォーマット文字列として処理
                    runtime_func = "cm_println_format";

                    // 名前付きプレースホルダを抽出
                    auto [var_names, converted_format] = extract_named_placeholders(str_val, ctx);

                    // フォーマット文字列を最初の引数として追加（変換後のフォーマット）
                    MirConstant str_const;
                    str_const.type = first_arg->type;
                    str_const.value = converted_format;
                    args.push_back(MirOperand::constant(str_const));

                    // 引数を収集（名前付き変数を先に、明示的引数を後に）
                    std::vector<LocalId> arg_locals;

                    // 名前付き変数を解決して追加（プレースホルダの順番通り）
                    // プレースホルダは唯一の解決経路resolve_interp_placeholder（識別子直接参照＋式パイプライン）で値へ降下する（type-resolution-simplification 領域1第4段）。
                    // 従来ここにあった約2,000行のテキストパターン照合（メンバ・添字・アロー・enum・否定・アドレス等の個別分岐）は、部分一致で誤った場所を構築する再発源（B7/N1/V1〜V4/W5）だったため撤去した。
                    // 表示変換はコード生成のformat置換が値ローカルの型で行うため、ここでは型が正しいローカルを渡すことのみ保証する
                    for (const auto& var_name : var_names) {
                        arg_locals.push_back(resolve_interp_placeholder(var_name, ctx));
                    }

                    // 明示的な引数は無視（単一の文字列リテラルのみ許可）
                    // 将来的にはエラーを報告する
                    if (call.args.size() > 1) {
                        // TODO: エラーを報告: println accepts only a single string literal. Use variable interpolation instead: println("{var}") 現在は追加引数を無視
                    }

                    // 引数の数を追加
                    MirConstant argc_const;
                    argc_const.type = hir::make_int();
                    argc_const.value = static_cast<int64_t>(arg_locals.size());
                    args.push_back(MirOperand::constant(argc_const));

                    // 実際の引数を追加
                    for (LocalId arg_local : arg_locals) {
                        args.push_back(MirOperand::copy(MirPlace{arg_local}));
                    }
                } else {
                    // 通常の文字列出力
                    runtime_func = "cm_println_string";
                    MirConstant str_const;
                    str_const.type = first_arg->type;
                    str_const.value = str_val;
                    args.push_back(MirOperand::constant(str_const));
                }
            } else {
                // その他のリテラル（整数など）- 型に応じてランタイム関数を選択
                LocalId arg_local = lower_expression(*first_arg, ctx);
                // MIRローカルの型情報を確認
                hir::TypePtr lit_type = first_arg->type;
                if (arg_local < ctx.func->locals.size() && ctx.func->locals[arg_local].type) {
                    lit_type = ctx.func->locals[arg_local].type;
                }
                if (lit_type && (lit_type->kind == hir::TypeKind::Float ||
                                 lit_type->kind == hir::TypeKind::Double)) {
                    // 浮動小数リテラル（println(1.0)等）はcm_println_doubleへ。
                    // これを見落とすとdoubleがcm_println_int（i32）に渡りLLVM検証エラーになる（C15）。
                    runtime_func = "cm_println_double";
                } else if (lit_type && (lit_type->kind == hir::TypeKind::Long ||
                                        lit_type->kind == hir::TypeKind::ISize)) {
                    runtime_func = "cm_println_long";
                } else if (lit_type && (lit_type->kind == hir::TypeKind::ULong ||
                                        lit_type->kind == hir::TypeKind::USize)) {
                    runtime_func = "cm_println_ulong";
                } else if (lit_type && (lit_type->kind == hir::TypeKind::UInt ||
                                        lit_type->kind == hir::TypeKind::UShort ||
                                        lit_type->kind == hir::TypeKind::UTiny)) {
                    runtime_func = "cm_println_uint";
                } else {
                    runtime_func = "cm_println_int";
                }
                args.push_back(MirOperand::copy(MirPlace{arg_local}));
            }
        } else {
            // 式の場合、評価して型に基づいて選択
            LocalId arg_local = lower_expression(*first_arg, ctx);

            // 型チェック: MIRローカルの型情報を優先し、フォールバックとしてHIR式の型を使用
            // match armのペイロード変数など、AST型チェッカーが正しい型を設定できないケースでは
            // MIRローカルの型（HirLet経由で正しく設定される）を使用する必要がある
            hir::TypePtr arg_type = first_arg->type;
            if (arg_local < ctx.func->locals.size() && ctx.func->locals[arg_local].type) {
                // MIRローカルに型情報がある場合、それを優先
                arg_type = ctx.func->locals[arg_local].type;
            }
            if (arg_type) {
                switch (arg_type->kind) {
                    case hir::TypeKind::String:
                        // 文字列変数で複数引数がある場合はフォーマット関数を使う
                        if (call.args.size() > 1) {
                            runtime_func = "cm_println_format";

                            // 文字列変数を最初の引数として追加
                            args.push_back(MirOperand::copy(MirPlace{arg_local}));

                            // 引数の数を追加
                            MirConstant argc_const;
                            argc_const.type = hir::make_int();
                            argc_const.value = static_cast<int64_t>(call.args.size() - 1);
                            args.push_back(MirOperand::constant(argc_const));

                            // 残りの引数を処理
                            for (size_t i = 1; i < call.args.size(); ++i) {
                                LocalId arg = lower_expression(*call.args[i], ctx);
                                args.push_back(MirOperand::copy(MirPlace{arg}));
                            }

                            // Call終端命令を作成
                            BlockId success_block = ctx.new_block();
                            auto func_operand = MirOperand::function_ref(runtime_func);
                            auto call_term = std::make_unique<MirTerminator>();
                            call_term->kind = MirTerminator::Call;
                            call_term->data = MirTerminator::CallData{
                                std::move(func_operand),
                                std::move(args),
                                std::nullopt,  // printlnは戻り値なし
                                success_block,
                                std::nullopt,  // unwind無し
                                std::string(),
                                std::string(),
                                false  // 通常の関数呼び出し
                            };
                            ctx.set_terminator(std::move(call_term));
                            ctx.switch_to_block(success_block);
                            return ctx.new_temp(hir::make_void());
                        } else {
                            runtime_func = "cm_println_string";
                        }
                        break;
                    case hir::TypeKind::Float:
                    case hir::TypeKind::Double:
                        runtime_func = "cm_println_double";
                        break;
                    case hir::TypeKind::Bool:
                        runtime_func = "cm_println_bool";
                        break;
                    case hir::TypeKind::Char:
                        runtime_func = "cm_println_char";
                        break;
                    case hir::TypeKind::Long:
                    case hir::TypeKind::ISize:
                        runtime_func = "cm_println_long";
                        break;
                    case hir::TypeKind::ULong:
                    case hir::TypeKind::USize:
                        runtime_func = "cm_println_ulong";
                        break;
                    case hir::TypeKind::UInt:
                    case hir::TypeKind::UShort:
                    case hir::TypeKind::UTiny:
                        runtime_func = "cm_println_uint";
                        break;
                    default:
                        runtime_func = "cm_println_int";
                        break;
                }
            } else {
                runtime_func = "cm_println_int";
            }
            args.push_back(MirOperand::copy(MirPlace{arg_local}));
        }

        // Call終端命令（戻り値なし）
        BlockId success_block = ctx.new_block();

        // 関数名を関数参照オペランドとして作成
        auto func_operand = MirOperand::function_ref(runtime_func);

        // Call終端命令を手動で作成
        auto call_term = std::make_unique<MirTerminator>();
        call_term->kind = MirTerminator::Call;
        call_term->data = MirTerminator::CallData{
            std::move(func_operand),
            std::move(args),
            std::nullopt,  // printlnは戻り値なし
            success_block,
            std::nullopt,  // unwind無し
            std::string(),
            std::string(),
            false  // 通常の関数呼び出し
        };
        ctx.set_terminator(std::move(call_term));

        // 次のブロックへ移動
        ctx.switch_to_block(success_block);

        // ダミーの戻り値
        return ctx.new_temp(hir::make_void());
    }

    return std::nullopt;
}

}  // namespace cm::mir
