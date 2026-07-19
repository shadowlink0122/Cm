// MIR lowering - 基本式（リテラル・変数参照・三項演算子）

#include "internal/base/debug.hpp"
#include "internal/mir/lowering/expr.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cm::mir {

LocalId ExprLowering::lower_literal(const hir::HirLiteral& lit, const hir::TypePtr& expr_type,
                                    LoweringContext& ctx) {
    // sizeof_for_T マーカー型の処理（ジェネリック型パラメータのsizeof）
    // MIR生成時にはtype_param_mapが空のため解決できない
    // マーカー型をオペランド型として保持し、モノモフィゼーション時に置換
    if (expr_type && expr_type->kind == hir::TypeKind::Generic &&
        expr_type->name.find("sizeof_for_") == 0) {
        // 型パラメータ解決を試みる（モノモフィ後の特殊化関数で有効）
        std::string type_param = expr_type->name.substr(11);  // "sizeof_for_" の長さ
        auto resolved_type = ctx.resolve_type_param(type_param);

        if (resolved_type) {
            // 解決できた場合は実際のサイズを計算
            int64_t actual_size = ctx.calculate_type_size(resolved_type);
            MirConstant constant;
            constant.type = hir::make_long();
            constant.value = actual_size;
            LocalId temp = ctx.new_temp(constant.type);
            ctx.push_statement(MirStatement::assign(
                MirPlace{temp}, MirRvalue::use(MirOperand::constant(constant))));
            return temp;
        }

        // 解決できない場合（ジェネリック関数のMIR生成時）
        // マーカー型を持つ定数を生成し、モノモフィゼーションで後で置換
        MirConstant constant;
        constant.type = expr_type;                      // sizeof_for_Tマーカー型を保持
        constant.value = std::get<int64_t>(lit.value);  // HIRで計算された暫定値

        LocalId temp = ctx.new_temp(hir::make_long());
        auto operand = MirOperand::constant(constant);
        operand->type = expr_type;  // オペランドの型にもマーカーを設定
        ctx.push_statement(
            MirStatement::assign(MirPlace{temp}, MirRvalue::use(std::move(operand))));
        return temp;
    }

    // 文字列リテラルの場合、補間が必要かチェック
    if (lit.value.index() == 5) {  // string型のインデックス
        std::string str_val = std::get<std::string>(lit.value);

        // プレースホルダを含むかチェック（エスケープされた中括弧も含む）
        bool has_placeholders = false;
        bool has_escaped_braces = false;
        size_t pos = 0;

        while (pos < str_val.length()) {
            if (pos + 1 < str_val.length() && str_val[pos] == '{' && str_val[pos + 1] == '{') {
                has_escaped_braces = true;
                pos += 2;
                continue;
            }
            if (pos + 1 < str_val.length() && str_val[pos] == '}' && str_val[pos + 1] == '}') {
                has_escaped_braces = true;
                pos += 2;
                continue;
            }
            if (str_val[pos] == '{') {
                size_t end_pos = str_val.find('}', pos + 1);
                if (end_pos != std::string::npos) {
                    // {と}の間に内容があるかチェック
                    std::string content = str_val.substr(pos + 1, end_pos - pos - 1);
                    if (!content.empty() &&
                        (std::isalpha(content[0]) || content[0] == '*' || content[0] == '&')) {
                        has_placeholders = true;
                        break;
                    }
                }
            }
            pos++;
        }

        // プレースホルダがある場合、フォーマット関数を呼び出す
        if (has_placeholders || has_escaped_braces) {
            // 名前付きプレースホルダを抽出
            auto [var_names, converted_format] = extract_named_placeholders(str_val, ctx);

            // cm_format_string 関数を呼び出す
            std::vector<MirOperandPtr> args;

            // フォーマット文字列を最初の引数として追加
            MirConstant str_const;
            str_const.type = hir::make_string();
            str_const.value = converted_format;
            args.push_back(MirOperand::constant(str_const));

            // 名前付き変数を解決して引数リストを作成
            std::vector<LocalId> arg_locals;
            for (const auto& var_name : var_names) {
                // まずconst変数をチェック
                auto const_value = ctx.get_const_value(var_name);
                if (const_value) {
                    // const変数の場合、その値を持つ一時変数を作成
                    LocalId temp = ctx.new_temp(const_value->type);
                    auto const_stmt = std::make_unique<MirStatement>();
                    const_stmt->kind = MirStatement::Assign;
                    const_stmt->data = MirStatement::AssignData{
                        MirPlace{temp}, MirRvalue::use(MirOperand::constant(*const_value))};
                    ctx.push_statement(std::move(const_stmt));
                    arg_locals.push_back(temp);
                } else {
                    // メンバーアクセスかどうかをチェック（例: c.get() または self.x）
                    size_t dot_pos = var_name.find('.');
                    size_t paren_pos = var_name.find('(');

                    if (dot_pos != std::string::npos && paren_pos != std::string::npos &&
                        paren_pos > dot_pos) {
                        // メソッド呼び出し: obj.method()
                        std::string obj_name = var_name.substr(0, dot_pos);
                        std::string method_part = var_name.substr(dot_pos + 1);
                        std::string method_name = method_part.substr(0, method_part.find('('));

                        auto obj_id = ctx.resolve_variable(obj_name);
                        if (obj_id) {
                            LocalId obj_local = *obj_id;
                            hir::TypePtr obj_type = ctx.func->locals[obj_local].type;

                            // ポインタ型の場合、デリファレンスして構造体型を取得
                            if (obj_type && obj_type->kind == hir::TypeKind::Pointer) {
                                obj_type = obj_type->element_type;
                            }

                            if (obj_type && obj_type->kind == hir::TypeKind::Struct) {
                                std::string struct_name = obj_type->name;
                                std::string full_method_name = struct_name + "__" + method_name;

                                // selfポインタを作成
                                LocalId ref_temp = ctx.new_temp(hir::make_pointer(obj_type));
                                ctx.push_statement(MirStatement::assign(
                                    MirPlace{ref_temp},
                                    MirRvalue::ref(MirPlace{obj_local}, false)));

                                // メソッド呼び出し
                                hir::TypePtr return_type = hir::make_int();
                                LocalId result = ctx.new_temp(return_type);
                                BlockId success_block = ctx.new_block();

                                std::vector<MirOperandPtr> method_args;
                                method_args.push_back(MirOperand::copy(MirPlace{ref_temp}));

                                auto call_term = std::make_unique<MirTerminator>();
                                call_term->kind = MirTerminator::Call;
                                call_term->data = MirTerminator::CallData{
                                    MirOperand::function_ref(full_method_name),
                                    std::move(method_args),
                                    MirPlace{result},
                                    success_block,
                                    std::nullopt,
                                    "",
                                    "",
                                    false};
                                ctx.set_terminator(std::move(call_term));
                                ctx.switch_to_block(success_block);

                                arg_locals.push_back(result);
                            } else {
                                arg_locals.push_back(ctx.new_temp(hir::make_error()));
                            }
                        } else {
                            arg_locals.push_back(ctx.new_temp(hir::make_error()));
                        }
                    } else if (dot_pos != std::string::npos) {
                        // フィールドアクセス: obj.field (例: self.x)
                        std::string obj_name = var_name.substr(0, dot_pos);
                        std::string field_name = var_name.substr(dot_pos + 1);

                        auto obj_id = ctx.resolve_variable(obj_name);
                        if (obj_id) {
                            LocalId obj_local = *obj_id;
                            hir::TypePtr obj_type = ctx.func->locals[obj_local].type;

                            // ポインタ型の場合、デリファレンスして構造体型を取得
                            bool needs_deref = false;
                            if (obj_type && obj_type->kind == hir::TypeKind::Pointer) {
                                needs_deref = true;
                                obj_type = obj_type->element_type;
                            }

                            if (obj_type && obj_type->kind == hir::TypeKind::Struct) {
                                auto field_idx = ctx.get_field_index(obj_type->name, field_name);
                                if (field_idx) {
                                    MirPlace place{obj_local};
                                    if (needs_deref) {
                                        place.projections.push_back(PlaceProjection::deref());
                                    }
                                    place.projections.push_back(PlaceProjection::field(*field_idx));

                                    hir::TypePtr field_type = hir::make_int();
                                    LocalId temp = ctx.new_temp(field_type);
                                    ctx.push_statement(MirStatement::assign(
                                        MirPlace{temp}, MirRvalue::use(MirOperand::copy(place))));
                                    arg_locals.push_back(temp);
                                } else {
                                    arg_locals.push_back(ctx.new_temp(hir::make_error()));
                                }
                            } else {
                                arg_locals.push_back(ctx.new_temp(hir::make_error()));
                            }
                        } else {
                            arg_locals.push_back(ctx.new_temp(hir::make_error()));
                        }
                    } else {
                        // 通常の変数を解決
                        auto var_id = ctx.resolve_variable(var_name);
                        if (var_id) {
                            arg_locals.push_back(*var_id);
                        } else {
                            // 変数が見つからない場合、エラー用のダミー値
                            auto err_type = hir::make_error();
                            arg_locals.push_back(ctx.new_temp(err_type));
                        }
                    }
                }
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

            // 戻り値用の一時変数
            LocalId result = ctx.new_temp(hir::make_string());

            // Call終端命令を作成
            BlockId success_block = ctx.new_block();
            auto func_operand = MirOperand::function_ref("cm_format_string");
            auto call_term = std::make_unique<MirTerminator>();
            call_term->kind = MirTerminator::Call;
            call_term->data = MirTerminator::CallData{
                std::move(func_operand),
                std::move(args),
                MirPlace{result},  // 戻り値を格納
                success_block,
                std::nullopt,  // unwind無し
                "",
                "",
                false  // 通常の関数呼び出し
            };
            ctx.set_terminator(std::move(call_term));
            ctx.switch_to_block(success_block);

            return result;
        }
    }

    // 通常のリテラル処理
    MirConstant constant;

    std::visit(
        [&](const auto& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, bool>) {
                constant.type = hir::make_bool();
                constant.value = val;
            } else if constexpr (std::is_same_v<T, int64_t>) {
                // リテラル値が32bit範囲に収まるかで型を決定
                // expr_typeが設定されている場合はそれを優先
                if (expr_type && (expr_type->kind == hir::TypeKind::Long ||
                                  expr_type->kind == hir::TypeKind::ULong ||
                                  expr_type->kind == hir::TypeKind::UInt ||
                                  expr_type->kind == hir::TypeKind::Short ||
                                  expr_type->kind == hir::TypeKind::UShort ||
                                  expr_type->kind == hir::TypeKind::Tiny ||
                                  expr_type->kind == hir::TypeKind::UTiny)) {
                    constant.type = expr_type;
                } else if (val > 2147483647LL || val < -2147483648LL) {
                    // i32範囲外 → long(i64)
                    // 符号なし領域（MSBが立つ場合）はulongとして扱う
                    if (val < 0) {
                        // ビットキャストされた巨大unsigned値（例: 0xFE6C6C...）
                        constant.type = hir::make_ulong();
                    } else {
                        constant.type = hir::make_long();
                    }
                } else {
                    constant.type = hir::make_int();
                }
                constant.value = val;
            } else if constexpr (std::is_same_v<T, double>) {
                constant.type = hir::make_double();
                constant.value = val;
            } else if constexpr (std::is_same_v<T, char>) {
                constant.type = hir::make_char();
                constant.value = static_cast<int64_t>(val);
            } else if constexpr (std::is_same_v<T, std::string>) {
                constant.type = hir::make_string();
                constant.value = val;
            } else {
                // デフォルト（monostate）
                constant.type = hir::make_void();
                constant.value = int64_t(0);
            }
        },
        lit.value);

    // SV幅付きリテラル情報を伝搬
    constant.bit_info = lit.bit_info;

    // 一時変数に代入
    LocalId temp = ctx.new_temp(constant.type);
    ctx.push_statement(
        MirStatement::assign(MirPlace{temp}, MirRvalue::use(MirOperand::constant(constant))));

    return temp;
}

// 変数参照のlowering
LocalId ExprLowering::lower_var_ref(const hir::HirVarRef& var, const hir::TypePtr& expr_type,
                                    LoweringContext& ctx) {
    // クロージャ（キャプチャあり）の場合
    if (var.is_closure && !var.captured_vars.empty()) {
        hir::TypePtr func_ptr_type =
            expr_type ? expr_type : hir::make_function_ptr(hir::make_int(), {});
        LocalId temp = ctx.new_temp(func_ptr_type);

        // クロージャ関数への参照を格納
        ctx.push_statement(MirStatement::assign(
            MirPlace{temp}, MirRvalue::use(MirOperand::function_ref(var.name))));

        // キャプチャ情報をローカルに設定
        auto& local_decl = ctx.func->locals[temp];
        local_decl.is_closure = true;
        local_decl.closure_func_name = var.name;

        // キャプチャされた変数のローカルIDを解決して保存
        for (const auto& cap : var.captured_vars) {
            auto cap_local = ctx.resolve_variable(cap.name);
            if (cap_local) {
                local_decl.captured_locals.push_back(*cap_local);
            }
        }

        return temp;
    }

    // 関数参照の場合（関数ポインタ用）
    // ただし同名のローカル変数・引数があればそれを優先する（シャドーイング）。
    // HIR lowering はローカルスコープを持たず func_defs_ の有無だけで is_function_ref を立てるため、
    // 引数 title が import した title() 関数と同名だと関数参照とみなされてしまう。
    // ここでスコープを先に引き、ローカルが見つかれば関数参照ではなく変数読み出しへ倒す
    // （倒さないと native/jit で関数ポインタが値として渡り不正な結果になる。js/tsはJSのスコープで偶然正しくなる）
    if (var.is_function_ref && !ctx.resolve_variable(var.name)) {
        // 式の型（関数ポインタ型）を使用
        hir::TypePtr func_ptr_type =
            expr_type ? expr_type : hir::make_function_ptr(hir::make_int(), {});
        LocalId temp = ctx.new_temp(func_ptr_type);

        // 関数参照を一時変数に格納
        ctx.push_statement(MirStatement::assign(
            MirPlace{temp}, MirRvalue::use(MirOperand::function_ref(var.name))));

        return temp;
    }

    // 変数名からローカルIDを解決
    auto local_opt = ctx.resolve_variable(var.name);

    if (!local_opt) {
        // impl内での暗黙的selfフィールドアクセスをチェック
        auto self_opt = ctx.resolve_variable("self");
        if (self_opt) {
            // selfを通じてフィールドにアクセス
            LocalId self_local = *self_opt;
            hir::TypePtr self_type = ctx.func->locals[self_local].type;

            // selfがポインタ型の場合、デリファレンスして構造体型を取得
            bool self_is_pointer = false;
            if (self_type && self_type->kind == hir::TypeKind::Pointer) {
                self_is_pointer = true;
                self_type = self_type->element_type;  // ポインタの先の型を使用
            }

            // 構造体のフィールドインデックスを取得
            std::string struct_name;
            if (self_type && self_type->kind == hir::TypeKind::Struct) {
                struct_name = self_type->name;
            } else if (self_type && !self_type->name.empty()) {
                struct_name = self_type->name;
            }

            auto field_idx = ctx.get_field_index(struct_name, var.name);
            if (field_idx) {
                // self.fieldとしてアクセス
                MirPlace place{self_local};
                // selfがポインタの場合、まずデリファレンス
                if (self_is_pointer) {
                    place.projections.push_back(PlaceProjection::deref());
                }
                place.projections.push_back(PlaceProjection::field(*field_idx));

                // フィールドの値を一時変数にコピー
                hir::TypePtr field_type = expr_type ? expr_type : hir::make_int();
                LocalId temp = ctx.new_temp(field_type);
                ctx.push_statement(
                    MirStatement::assign(MirPlace{temp}, MirRvalue::use(MirOperand::copy(place))));
                return temp;
            }
        }

        // グローバルconst変数をチェック（COLOR_RED等のexport const定数）
        auto const_val = ctx.get_const_value(var.name);
        if (const_val) {
            LocalId temp = ctx.new_temp(const_val->type ? const_val->type : hir::make_int());
            ctx.push_statement(MirStatement::assign(
                MirPlace{temp}, MirRvalue::use(MirOperand::constant(*const_val))));
            return temp;
        }

        // 既知の関数名なら関数参照として解決する（補間ミニパイプライン経由ではis_function_refが立たないため）
        if (ctx.hir_func_defs && ctx.hir_func_defs->count(var.name)) {
            hir::TypePtr func_ptr_type =
                expr_type ? expr_type : hir::make_function_ptr(hir::make_int(), {});
            LocalId temp = ctx.new_temp(func_ptr_type);
            ctx.push_statement(MirStatement::assign(
                MirPlace{temp}, MirRvalue::use(MirOperand::function_ref(var.name))));
            return temp;
        }

        // 変数が見つからない場合は仮の値を返す
        LocalId temp = ctx.new_temp(hir::make_int());
        MirConstant zero_const;
        zero_const.value = int64_t(0);
        zero_const.type = hir::make_int();
        ctx.push_statement(
            MirStatement::assign(MirPlace{temp}, MirRvalue::use(MirOperand::constant(zero_const))));
        return temp;
    }

    LocalId local = *local_opt;

    // 変数の型を取得
    hir::TypePtr var_type = hir::make_int();  // デフォルト
    if (local < ctx.func->locals.size()) {
        var_type = ctx.func->locals[local].type;
    }

    // 変数をコピーして一時変数に
    LocalId temp = ctx.new_temp(var_type);
    ctx.push_statement(
        MirStatement::assign(MirPlace{temp}, MirRvalue::use(MirOperand::copy(MirPlace{local}))));

    return temp;
}

// 三項演算子のlowering
LocalId ExprLowering::lower_ternary(const hir::HirTernary& ternary, LoweringContext& ctx) {
    // 条件をlowering
    LocalId cond = lower_expression(*ternary.condition, ctx);

    // ブロックを作成
    BlockId then_block = ctx.new_block();
    BlockId else_block = ctx.new_block();
    BlockId merge_block = ctx.new_block();

    // 結果用の変数（then_exprの型を使用）
    hir::TypePtr result_type = ternary.then_expr ? ternary.then_expr->type : hir::make_int();
    LocalId result = ctx.new_temp(result_type);

    // 条件分岐
    ctx.set_terminator(
        MirTerminator::switch_int(MirOperand::copy(MirPlace{cond}), {{1, then_block}}, else_block));

    // then部
    ctx.switch_to_block(then_block);
    LocalId then_value = lower_expression(*ternary.then_expr, ctx);
    ctx.push_statement(MirStatement::assign(
        MirPlace{result}, MirRvalue::use(MirOperand::copy(MirPlace{then_value}))));
    ctx.set_terminator(MirTerminator::goto_block(merge_block));

    // else部
    ctx.switch_to_block(else_block);
    LocalId else_value = lower_expression(*ternary.else_expr, ctx);
    ctx.push_statement(MirStatement::assign(
        MirPlace{result}, MirRvalue::use(MirOperand::copy(MirPlace{else_value}))));
    ctx.set_terminator(MirTerminator::goto_block(merge_block));

    // マージポイント
    ctx.switch_to_block(merge_block);

    return result;
}

}  // namespace cm::mir
