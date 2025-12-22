// expr_lowering_call.cpp - 関数呼び出しのlowering
// extract_named_placeholders, lower_call

#include "../../common/debug.hpp"
#include "expr.hpp"

namespace cm::mir {

std::pair<std::vector<std::string>, std::string> ExprLowering::extract_named_placeholders(
    const std::string& format_str, [[maybe_unused]] LoweringContext& ctx) {
    std::vector<std::string> var_names;
    std::string converted_format;

    size_t pos = 0;
    while (pos < format_str.length()) {
        if (pos + 1 < format_str.length() && format_str[pos] == '{' && format_str[pos + 1] == '{') {
            // エスケープされた {{ を処理
            converted_format += "{{";
            pos += 2;
            continue;
        }
        if (pos + 1 < format_str.length() && format_str[pos] == '}' && format_str[pos + 1] == '}') {
            // エスケープされた }} を処理
            converted_format += "}}";
            pos += 2;
            continue;
        }

        if (format_str[pos] == '{') {
            size_t end = pos + 1;
            // :: は変数名の一部として扱う（enum値のため）
            while (end < format_str.length() && format_str[end] != '}') {
                // フォーマット指定子のコロンをチェック（:: ではない場合）
                if (format_str[end] == ':' &&
                    (end + 1 >= format_str.length() || format_str[end + 1] != ':')) {
                    break;  // フォーマット指定子の開始
                }
                if (format_str[end] == ':' && end + 1 < format_str.length() &&
                    format_str[end + 1] == ':') {
                    end += 2;  // :: をスキップ
                } else {
                    end++;
                }
            }

            if (end < format_str.length() || end == format_str.length()) {
                std::string content = format_str.substr(pos + 1, end - pos - 1);

                // フォーマット指定子を探す（:: はスキップ）
                size_t colon_pos = std::string::npos;
                for (size_t i = pos + 1; i < format_str.length(); ++i) {
                    if (format_str[i] == ':' &&
                        (i + 1 >= format_str.length() || format_str[i + 1] != ':')) {
                        colon_pos = i;
                        break;
                    }
                    if (format_str[i] == ':' && i + 1 < format_str.length() &&
                        format_str[i + 1] == ':') {
                        i++;  // :: をスキップ
                    }
                }
                size_t close_pos = format_str.find('}', pos + 1);

                if (close_pos != std::string::npos) {
                    if (colon_pos != std::string::npos && colon_pos < close_pos) {
                        // {name:format} の形式
                        std::string var_name = format_str.substr(pos + 1, colon_pos - pos - 1);
                        std::string format_spec =
                            format_str.substr(colon_pos, close_pos - colon_pos + 1);

                        // &variable パターンをチェック
                        if (!var_name.empty() && var_name[0] == '&') {
                            // &variable形式 - アドレス取得 (後でサポート)
                            std::string actual_var = var_name.substr(1);
                            if (!actual_var.empty() && std::isalpha(actual_var[0])) {
                                var_names.push_back("&" + actual_var);  // &付きで格納
                                // フォーマット指定子をそのまま維持
                                converted_format += "{" + format_spec;
                            } else {
                                // 無効な&フォーマット - そのまま処理
                                converted_format += format_str.substr(pos, close_pos - pos + 1);
                            }
                        } else if (!var_name.empty() && var_name[0] == '*') {
                            // *variable形式 - ポインタのデリファレンス
                            std::string ptr_var = var_name.substr(1);
                            if (!ptr_var.empty() && std::isalpha(ptr_var[0])) {
                                var_names.push_back("*" + ptr_var);  // *付きで格納
                                // フォーマット指定子をそのまま維持
                                converted_format += "{" + format_spec;
                            } else {
                                // 無効な*フォーマット - そのまま処理
                                converted_format += format_str.substr(pos, close_pos - pos + 1);
                            }
                        } else if (!var_name.empty() &&
                                   (std::isalpha(var_name[0]) || var_name[0] == '!' ||
                                    var_name[0] == '*' ||  // デリファレンスを許可
                                    var_name.substr(0, 5) == "self." ||
                                    var_name.find("::") != std::string::npos)) {
                            // 変数名、メンバーアクセス、メソッド呼び出し、enum値、または否定演算子として有効
                            // self.x, p.field, r.area(), Color::Red, !true のような形式も許可
                            var_names.push_back(var_name);
                            converted_format += "{" + format_spec;  // {:x} のような形式に変換
                        } else {
                            // 位置プレースホルダは無視（変数名ではないので処理しない）
                            // 空のままにしてエラーにする
                            return {var_names, format_str};  // エラー：変換せずに元の文字列を返す
                        }
                        pos = close_pos + 1;
                    } else {
                        // {name} の形式
                        std::string var_name = format_str.substr(pos + 1, close_pos - pos - 1);

                        // &variable パターンをチェック
                        if (!var_name.empty() && var_name[0] == '&') {
                            // &variable形式 - アドレス取得 (後でサポート)
                            std::string actual_var = var_name.substr(1);
                            if (!actual_var.empty() && std::isalpha(actual_var[0])) {
                                var_names.push_back("&" + actual_var);  // &付きで格納
                                // アドレス表示用: プレースホルダーのみ（プレフィックスなし）
                                converted_format += "{}";
                            } else {
                                // 無効な&フォーマット - そのまま処理
                                converted_format += format_str.substr(pos, close_pos - pos + 1);
                            }
                        } else if (!var_name.empty() && var_name[0] == '*') {
                            // *variable形式 - ポインタのデリファレンス
                            std::string ptr_var = var_name.substr(1);
                            // (*ptr).x 形式もサポート
                            if (!ptr_var.empty() &&
                                (std::isalpha(ptr_var[0]) || ptr_var[0] == '(')) {
                                var_names.push_back("*" + ptr_var);  // *付きで格納
                                converted_format += "{}";
                            } else {
                                // 無効な*フォーマット - そのまま処理
                                converted_format += format_str.substr(pos, close_pos - pos + 1);
                            }
                        } else if (!var_name.empty() &&
                                   (std::isalpha(var_name[0]) || var_name[0] == '!' ||
                                    var_name[0] == '*' ||  // デリファレンスを許可
                                    var_name[0] == '(' ||  // (*ptr).x 形式を許可
                                    var_name.substr(0, 5) == "self." ||
                                    var_name.find("::") != std::string::npos ||
                                    var_name.find("->") !=
                                        std::string::npos)) {  // ptr->x 形式を許可
                            // 変数名、メンバーアクセス、メソッド呼び出し、enum値、または否定演算子として有効
                            // self.x, p.field, r.area(), Color::Red, !true のような形式も許可
                            debug_msg("MIR", "Extracted placeholder: " + var_name);
                            var_names.push_back(var_name);
                            converted_format += "{}";  // 位置プレースホルダに変換
                        } else {
                            // 位置プレースホルダは無視（変数名ではないので処理しない）
                            // 空のままにしてエラーにする
                            return {var_names, format_str};  // エラー：変換せずに元の文字列を返す
                        }
                        pos = close_pos + 1;
                    }
                } else {
                    converted_format += format_str[pos];
                    pos++;
                }
            } else {
                converted_format += format_str[pos];
                pos++;
            }
        } else {
            converted_format += format_str[pos];
            pos++;
        }
    }

    return {var_names, converted_format};
}

// 関数呼び出しのlowering
LocalId ExprLowering::lower_call(const hir::HirCall& call, const hir::TypePtr& result_type,
                                 LoweringContext& ctx) {
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
                    for (const auto& var_name : var_names) {
                        if (!var_name.empty() && var_name[0] == '!') {
                            // 論理否定演算子の処理
                            std::string inner_expr = var_name.substr(1);

                            // 複数の否定演算子を処理（例: !!true）
                            int negation_count = 1;
                            while (!inner_expr.empty() && inner_expr[0] == '!') {
                                negation_count++;
                                inner_expr = inner_expr.substr(1);
                            }

                            LocalId expr_result;

                            // 内部式を評価
                            if (inner_expr == "true") {
                                // trueリテラル
                                MirConstant bool_const;
                                bool_const.type = hir::make_bool();
                                bool_const.value = true;
                                expr_result = ctx.new_temp(hir::make_bool());
                                ctx.push_statement(MirStatement::assign(
                                    MirPlace{expr_result},
                                    MirRvalue::use(MirOperand::constant(bool_const))));
                            } else if (inner_expr == "false") {
                                // falseリテラル
                                MirConstant bool_const;
                                bool_const.type = hir::make_bool();
                                bool_const.value = false;
                                expr_result = ctx.new_temp(hir::make_bool());
                                ctx.push_statement(MirStatement::assign(
                                    MirPlace{expr_result},
                                    MirRvalue::use(MirOperand::constant(bool_const))));
                            } else if (inner_expr.find(" && ") != std::string::npos ||
                                       inner_expr.find(" || ") != std::string::npos) {
                                // 論理式の処理（簡易実装）
                                // TODO: 完全な式パーサーが必要
                                // 現在は論理演算を含む式は評価できないため、falseを返す
                                MirConstant bool_const;
                                bool_const.type = hir::make_bool();
                                bool_const.value = false;
                                expr_result = ctx.new_temp(hir::make_bool());
                                ctx.push_statement(MirStatement::assign(
                                    MirPlace{expr_result},
                                    MirRvalue::use(MirOperand::constant(bool_const))));
                            } else {
                                // 変数を解決
                                auto var_id = ctx.resolve_variable(inner_expr);
                                if (var_id) {
                                    expr_result = *var_id;
                                } else {
                                    // 変数が見つからない場合、falseとして扱う
                                    MirConstant bool_const;
                                    bool_const.type = hir::make_bool();
                                    bool_const.value = false;
                                    expr_result = ctx.new_temp(hir::make_bool());
                                    ctx.push_statement(MirStatement::assign(
                                        MirPlace{expr_result},
                                        MirRvalue::use(MirOperand::constant(bool_const))));
                                }
                            }

                            // 否定演算を適用
                            for (int i = 0; i < negation_count; ++i) {
                                LocalId new_result = ctx.new_temp(hir::make_bool());
                                ctx.push_statement(MirStatement::assign(
                                    MirPlace{new_result},
                                    MirRvalue::unary(MirUnaryOp::Not,
                                                     MirOperand::copy(MirPlace{expr_result}))));
                                expr_result = new_result;
                            }

                            arg_locals.push_back(expr_result);
                        } else if (!var_name.empty() && var_name[0] == '&') {
                            // &variable形式 - アドレス取得を実装
                            std::string actual_var = var_name.substr(1);
                            auto var_id = ctx.resolve_variable(actual_var);
                            if (var_id) {
                                // アドレス取得 - ポインタ型の一時変数を作成
                                hir::TypePtr ptr_type = nullptr;

                                // 変数の型を取得してポインタ型を作成
                                if (*var_id < ctx.func->locals.size()) {
                                    auto base_type = ctx.func->locals[*var_id].type;
                                    ptr_type = hir::make_pointer(base_type);
                                }

                                if (!ptr_type) {
                                    ptr_type = hir::make_pointer(hir::make_int());  // デフォルト
                                }

                                LocalId result = ctx.new_temp(ptr_type);

                                // Ref演算でアドレスを取得（immutableポインタ）
                                ctx.push_statement(MirStatement::assign(
                                    MirPlace{result}, MirRvalue::ref(MirPlace{*var_id}, false)));

                                // 特殊マーカーを使って、これがアドレス表示であることを示す
                                // フォーマット文字列内で &変数名: プレフィックスを追加
                                debug_msg("MIR", "Address interpolation: adding pointer local " +
                                                     std::to_string(result) + " with type " +
                                                     ptr_type->name);
                                arg_locals.push_back(result);  // ポインタをそのまま渡す
                            } else {
                                // 変数が見つからない場合、エラー用のダミー値を追加
                                auto err_type = hir::make_error();
                                arg_locals.push_back(ctx.new_temp(err_type));
                            }
                        } else if (!var_name.empty() && var_name[0] == '*') {
                            // *variable形式 - ポインタのデリファレンス
                            std::string ptr_var = var_name.substr(1);

                            // (*ptr).member形式のチェック
                            bool is_paren_deref = (ptr_var.size() > 2 && ptr_var[0] == '(' &&
                                                   ptr_var.find(')') != std::string::npos);
                            if (is_paren_deref) {
                                size_t close_paren = ptr_var.find(')');
                                std::string actual_ptr = ptr_var.substr(1, close_paren - 1);
                                std::string member_part = ptr_var.substr(close_paren + 1);

                                // メンバーアクセス (.member)
                                if (!member_part.empty() && member_part[0] == '.') {
                                    std::string member_name = member_part.substr(1);
                                    auto var_id = ctx.resolve_variable(actual_ptr);
                                    if (var_id) {
                                        hir::TypePtr ptr_type = nullptr;
                                        hir::TypePtr struct_type = nullptr;

                                        if (*var_id < ctx.func->locals.size()) {
                                            ptr_type = ctx.func->locals[*var_id].type;
                                            if (ptr_type &&
                                                ptr_type->kind == hir::TypeKind::Pointer &&
                                                ptr_type->element_type) {
                                                struct_type = ptr_type->element_type;
                                            }
                                        }

                                        if (struct_type &&
                                            struct_type->kind == hir::TypeKind::Struct) {
                                            auto field_idx =
                                                ctx.get_field_index(struct_type->name, member_name);
                                            if (field_idx) {
                                                // フィールド型を取得
                                                hir::TypePtr field_type = hir::make_int();
                                                if (ctx.struct_defs &&
                                                    ctx.struct_defs->count(struct_type->name)) {
                                                    const auto* struct_def =
                                                        ctx.struct_defs->at(struct_type->name);
                                                    if (*field_idx < struct_def->fields.size()) {
                                                        field_type =
                                                            struct_def->fields[*field_idx].type;
                                                    }
                                                }

                                                LocalId result = ctx.new_temp(field_type);

                                                // Deref + Field プロジェクション
                                                MirPlace place{*var_id};
                                                place.projections.push_back(
                                                    PlaceProjection::deref());
                                                place.projections.push_back(
                                                    PlaceProjection::field(*field_idx));

                                                ctx.push_statement(MirStatement::assign(
                                                    MirPlace{result},
                                                    MirRvalue::use(MirOperand::copy(place))));

                                                arg_locals.push_back(result);
                                                continue;
                                            }
                                        }
                                    }
                                    // フォールバック: エラーとして処理
                                    auto err_type = hir::make_error();
                                    arg_locals.push_back(ctx.new_temp(err_type));
                                    continue;
                                }
                            }

                            auto var_id = ctx.resolve_variable(ptr_var);
                            if (var_id) {
                                // ポインタ変数の型を取得
                                hir::TypePtr ptr_type = nullptr;
                                hir::TypePtr deref_type = nullptr;

                                if (*var_id < ctx.func->locals.size()) {
                                    ptr_type = ctx.func->locals[*var_id].type;
                                    // ポインタ型から指している型を取得
                                    if (ptr_type && ptr_type->kind == hir::TypeKind::Pointer &&
                                        ptr_type->element_type) {
                                        deref_type = ptr_type->element_type;
                                    }
                                }

                                if (!deref_type) {
                                    deref_type = hir::make_int();  // デフォルト
                                }

                                LocalId result = ctx.new_temp(deref_type);

                                // Derefプロジェクションを使用してデリファレンス
                                MirPlace place{*var_id};
                                place.projections.push_back(PlaceProjection::deref());

                                // ポインタをデリファレンスして値を取得
                                ctx.push_statement(MirStatement::assign(
                                    MirPlace{result}, MirRvalue::use(MirOperand::copy(place))));

                                debug_msg("MIR",
                                          "Pointer dereference interpolation: dereferencing " +
                                              std::to_string(*var_id) + " to " +
                                              std::to_string(result));
                                arg_locals.push_back(result);
                            } else {
                                // 変数が見つからない場合、エラー用のダミー値を追加
                                auto err_type = hir::make_error();
                                arg_locals.push_back(ctx.new_temp(err_type));
                            }
                        } else if (!var_name.empty() && var_name[0] == '(' && var_name[1] == '*') {
                            // (*ptr).member 形式 - ポインタデリファレンス + メンバーアクセス
                            size_t close_paren = var_name.find(')');
                            if (close_paren != std::string::npos) {
                                std::string ptr_name = var_name.substr(2, close_paren - 2);
                                std::string member_part = var_name.substr(close_paren + 1);

                                if (!member_part.empty() && member_part[0] == '.') {
                                    std::string member_name = member_part.substr(1);
                                    auto var_id = ctx.resolve_variable(ptr_name);

                                    if (var_id) {
                                        hir::TypePtr ptr_type = nullptr;
                                        hir::TypePtr struct_type = nullptr;

                                        if (*var_id < ctx.func->locals.size()) {
                                            ptr_type = ctx.func->locals[*var_id].type;
                                            if (ptr_type &&
                                                ptr_type->kind == hir::TypeKind::Pointer &&
                                                ptr_type->element_type) {
                                                struct_type = ptr_type->element_type;
                                            }
                                        }

                                        if (struct_type &&
                                            struct_type->kind == hir::TypeKind::Struct) {
                                            auto field_idx =
                                                ctx.get_field_index(struct_type->name, member_name);
                                            if (field_idx) {
                                                // フィールド型を取得
                                                hir::TypePtr field_type = hir::make_int();
                                                if (ctx.struct_defs &&
                                                    ctx.struct_defs->count(struct_type->name)) {
                                                    const auto* struct_def =
                                                        ctx.struct_defs->at(struct_type->name);
                                                    if (*field_idx < struct_def->fields.size()) {
                                                        field_type =
                                                            struct_def->fields[*field_idx].type;
                                                    }
                                                }

                                                LocalId result = ctx.new_temp(field_type);

                                                // Deref + Field プロジェクション
                                                MirPlace place{*var_id};
                                                place.projections.push_back(
                                                    PlaceProjection::deref());
                                                place.projections.push_back(
                                                    PlaceProjection::field(*field_idx));

                                                ctx.push_statement(MirStatement::assign(
                                                    MirPlace{result},
                                                    MirRvalue::use(MirOperand::copy(place))));

                                                debug_msg("MIR", "(*ptr).member interpolation: " +
                                                                     ptr_name + "." + member_name);
                                                arg_locals.push_back(result);
                                                continue;
                                            }
                                        }
                                    }
                                }
                            }
                            // フォールバック: エラーとして処理
                            auto err_type = hir::make_error();
                            arg_locals.push_back(ctx.new_temp(err_type));
                        } else if (!var_name.empty() && var_name.find("->") != std::string::npos) {
                            // ptr->member 形式 - ポインタメンバアクセス
                            size_t arrow_pos = var_name.find("->");
                            std::string ptr_name = var_name.substr(0, arrow_pos);
                            std::string member_name = var_name.substr(arrow_pos + 2);

                            auto var_id = ctx.resolve_variable(ptr_name);
                            if (var_id) {
                                hir::TypePtr ptr_type = nullptr;
                                hir::TypePtr struct_type = nullptr;

                                if (*var_id < ctx.func->locals.size()) {
                                    ptr_type = ctx.func->locals[*var_id].type;
                                    if (ptr_type && ptr_type->kind == hir::TypeKind::Pointer &&
                                        ptr_type->element_type) {
                                        struct_type = ptr_type->element_type;
                                    }
                                }

                                if (struct_type && struct_type->kind == hir::TypeKind::Struct) {
                                    auto field_idx =
                                        ctx.get_field_index(struct_type->name, member_name);
                                    if (field_idx) {
                                        // フィールド型を取得
                                        hir::TypePtr field_type = hir::make_int();
                                        if (ctx.struct_defs &&
                                            ctx.struct_defs->count(struct_type->name)) {
                                            const auto* struct_def =
                                                ctx.struct_defs->at(struct_type->name);
                                            if (*field_idx < struct_def->fields.size()) {
                                                field_type = struct_def->fields[*field_idx].type;
                                            }
                                        }

                                        LocalId result = ctx.new_temp(field_type);

                                        // Deref + Field プロジェクション
                                        MirPlace place{*var_id};
                                        place.projections.push_back(PlaceProjection::deref());
                                        place.projections.push_back(
                                            PlaceProjection::field(*field_idx));

                                        ctx.push_statement(MirStatement::assign(
                                            MirPlace{result},
                                            MirRvalue::use(MirOperand::copy(place))));

                                        debug_msg("MIR", "ptr->member interpolation: " + ptr_name +
                                                             "->" + member_name);
                                        arg_locals.push_back(result);
                                        continue;
                                    }
                                }
                            }
                            // フォールバック: エラーとして処理
                            auto err_type = hir::make_error();
                            arg_locals.push_back(ctx.new_temp(err_type));
                        } else {
                            // メンバーアクセスかどうかチェック（"self.x" や "p.field" の形式）
                            // または配列要素のフィールドアクセス（"arr[0].x" の形式）
                            size_t dot_pos = var_name.find('.');
                            size_t bracket_pos = var_name.find('[');

                            // arr[0].x のような配列要素のフィールドアクセス
                            if (bracket_pos != std::string::npos &&
                                (dot_pos == std::string::npos || bracket_pos < dot_pos)) {
                                // 配列インデックスを含むパターン: arr[0] or arr[0].field
                                std::string arr_name = var_name.substr(0, bracket_pos);
                                size_t close_bracket = var_name.find(']', bracket_pos);

                                if (close_bracket != std::string::npos) {
                                    std::string index_str = var_name.substr(
                                        bracket_pos + 1, close_bracket - bracket_pos - 1);
                                    std::string remaining = var_name.substr(close_bracket + 1);
                                    if (!remaining.empty() && remaining[0] == '.') {
                                        remaining = remaining.substr(1);
                                    }

                                    auto arr_id = ctx.resolve_variable(arr_name);
                                    if (arr_id) {
                                        hir::TypePtr arr_type = nullptr;
                                        if (*arr_id < ctx.func->locals.size()) {
                                            arr_type = ctx.func->locals[*arr_id].type;
                                        }

                                        // スライス（動的配列）かどうか判定
                                        bool is_slice = arr_type &&
                                                        arr_type->kind == hir::TypeKind::Array &&
                                                        !arr_type->array_size.has_value();

                                        if (is_slice && remaining.empty()) {
                                            // スライスのインデックスアクセス - cm_slice_get_*
                                            // を呼び出す
                                            hir::TypePtr elem_type = arr_type->element_type
                                                                         ? arr_type->element_type
                                                                         : hir::make_int();

                                            std::string get_func = "cm_slice_get_i32";
                                            auto elem_kind = elem_type->kind;
                                            if (elem_kind == hir::TypeKind::Char ||
                                                elem_kind == hir::TypeKind::Bool ||
                                                elem_kind == hir::TypeKind::Tiny ||
                                                elem_kind == hir::TypeKind::UTiny) {
                                                get_func = "cm_slice_get_i8";
                                            } else if (elem_kind == hir::TypeKind::Long ||
                                                       elem_kind == hir::TypeKind::ULong) {
                                                get_func = "cm_slice_get_i64";
                                            } else if (elem_kind == hir::TypeKind::Double ||
                                                       elem_kind == hir::TypeKind::Float) {
                                                get_func = "cm_slice_get_f64";
                                            } else if (elem_kind == hir::TypeKind::Pointer ||
                                                       elem_kind == hir::TypeKind::String ||
                                                       elem_kind == hir::TypeKind::Struct) {
                                                get_func = "cm_slice_get_ptr";
                                            }

                                            LocalId result = ctx.new_temp(elem_type);
                                            BlockId success_block = ctx.new_block();

                                            // インデックス値を作成
                                            LocalId idx_local = ctx.new_temp(hir::make_int());
                                            try {
                                                int idx = std::stoi(index_str);
                                                MirConstant idx_const;
                                                idx_const.value = static_cast<int64_t>(idx);
                                                idx_const.type = hir::make_int();
                                                ctx.push_statement(MirStatement::assign(
                                                    MirPlace{idx_local},
                                                    MirRvalue::use(
                                                        MirOperand::constant(idx_const))));
                                            } catch (...) {
                                                MirConstant idx_const;
                                                idx_const.value = int64_t{0};
                                                idx_const.type = hir::make_int();
                                                ctx.push_statement(MirStatement::assign(
                                                    MirPlace{idx_local},
                                                    MirRvalue::use(
                                                        MirOperand::constant(idx_const))));
                                            }

                                            std::vector<MirOperandPtr> call_args;
                                            call_args.push_back(
                                                MirOperand::copy(MirPlace{*arr_id}));
                                            call_args.push_back(
                                                MirOperand::copy(MirPlace{idx_local}));

                                            auto call_term = std::make_unique<MirTerminator>();
                                            call_term->kind = MirTerminator::Call;
                                            call_term->data = MirTerminator::CallData{
                                                MirOperand::function_ref(get_func),
                                                std::move(call_args),
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
                                            // 静的配列またはスライス+フィールドアクセスの処理
                                            MirPlace place{*arr_id};
                                            hir::TypePtr current_type = arr_type;
                                            bool valid = true;

                                            // インデックスを処理
                                            try {
                                                int idx = std::stoi(index_str);
                                                LocalId idx_local = ctx.new_temp(hir::make_int());
                                                MirConstant idx_const;
                                                idx_const.value = static_cast<int64_t>(idx);
                                                idx_const.type = hir::make_int();
                                                ctx.push_statement(MirStatement::assign(
                                                    MirPlace{idx_local},
                                                    MirRvalue::use(
                                                        MirOperand::constant(idx_const))));

                                                place.projections.push_back(
                                                    PlaceProjection::index(idx_local));

                                                // 配列の要素型に更新
                                                if (current_type &&
                                                    current_type->kind == hir::TypeKind::Array &&
                                                    current_type->element_type) {
                                                    current_type = current_type->element_type;
                                                }
                                            } catch (...) {
                                                valid = false;
                                            }

                                            // 残りのフィールドアクセスを処理
                                            while (!remaining.empty() && valid) {
                                                size_t next_bracket = remaining.find('[');
                                                size_t next_dot = remaining.find('.');

                                                std::string field_part;
                                                std::string next_index_part;

                                                if (next_bracket != std::string::npos &&
                                                    (next_dot == std::string::npos ||
                                                     next_bracket < next_dot)) {
                                                    field_part = remaining.substr(0, next_bracket);
                                                    size_t next_close =
                                                        remaining.find(']', next_bracket);
                                                    if (next_close != std::string::npos) {
                                                        next_index_part = remaining.substr(
                                                            next_bracket + 1,
                                                            next_close - next_bracket - 1);
                                                        remaining =
                                                            remaining.substr(next_close + 1);
                                                        if (!remaining.empty() &&
                                                            remaining[0] == '.') {
                                                            remaining = remaining.substr(1);
                                                        }
                                                    } else {
                                                        valid = false;
                                                        break;
                                                    }
                                                } else if (next_dot != std::string::npos) {
                                                    field_part = remaining.substr(0, next_dot);
                                                    remaining = remaining.substr(next_dot + 1);
                                                } else {
                                                    field_part = remaining;
                                                    remaining.clear();
                                                }

                                                // フィールドアクセスを処理
                                                if (!field_part.empty()) {
                                                    if (!current_type ||
                                                        current_type->kind !=
                                                            hir::TypeKind::Struct) {
                                                        valid = false;
                                                        break;
                                                    }

                                                    auto field_idx = ctx.get_field_index(
                                                        current_type->name, field_part);
                                                    if (!field_idx) {
                                                        valid = false;
                                                        break;
                                                    }

                                                    place.projections.push_back(
                                                        PlaceProjection::field(*field_idx));

                                                    if (ctx.struct_defs &&
                                                        ctx.struct_defs->count(
                                                            current_type->name)) {
                                                        const auto* struct_def =
                                                            ctx.struct_defs->at(current_type->name);
                                                        if (*field_idx <
                                                            struct_def->fields.size()) {
                                                            current_type =
                                                                struct_def->fields[*field_idx].type;
                                                        } else {
                                                            current_type = hir::make_int();
                                                        }
                                                    } else {
                                                        current_type = hir::make_int();
                                                    }
                                                }

                                                // 追加のインデックスを処理
                                                if (!next_index_part.empty()) {
                                                    try {
                                                        int next_idx = std::stoi(next_index_part);
                                                        LocalId next_idx_local =
                                                            ctx.new_temp(hir::make_int());
                                                        MirConstant next_idx_const;
                                                        next_idx_const.value =
                                                            static_cast<int64_t>(next_idx);
                                                        next_idx_const.type = hir::make_int();
                                                        ctx.push_statement(MirStatement::assign(
                                                            MirPlace{next_idx_local},
                                                            MirRvalue::use(MirOperand::constant(
                                                                next_idx_const))));

                                                        place.projections.push_back(
                                                            PlaceProjection::index(next_idx_local));

                                                        if (current_type &&
                                                            current_type->kind ==
                                                                hir::TypeKind::Array &&
                                                            current_type->element_type) {
                                                            current_type =
                                                                current_type->element_type;
                                                        }
                                                    } catch (...) {
                                                        valid = false;
                                                    }
                                                }
                                            }

                                            if (valid) {
                                                LocalId result = ctx.new_temp(current_type);
                                                ctx.push_statement(MirStatement::assign(
                                                    MirPlace{result},
                                                    MirRvalue::use(MirOperand::copy(place))));
                                                arg_locals.push_back(result);
                                            } else {
                                                auto err_type = hir::make_error();
                                                arg_locals.push_back(ctx.new_temp(err_type));
                                            }
                                        }  // end of static array / slice+field block
                                    } else {
                                        auto err_type = hir::make_error();
                                        arg_locals.push_back(ctx.new_temp(err_type));
                                    }
                                } else {
                                    auto err_type = hir::make_error();
                                    arg_locals.push_back(ctx.new_temp(err_type));
                                }
                            } else if (dot_pos != std::string::npos) {
                                // メンバーアクセスの処理
                                std::string obj_name = var_name.substr(0, dot_pos);
                                std::string member_name = var_name.substr(dot_pos + 1);

                                // メソッド呼び出しかどうかチェック（末尾が "()" の場合）
                                bool is_method_call = false;
                                if (member_name.size() > 2 &&
                                    member_name.substr(member_name.size() - 2) == "()") {
                                    is_method_call = true;
                                    member_name = member_name.substr(
                                        0, member_name.size() - 2);  // "()" を除去
                                }

                                // オブジェクトを解決
                                auto obj_id = ctx.resolve_variable(obj_name);
                                if (obj_id) {
                                    // オブジェクトの型を取得
                                    hir::TypePtr obj_type = nullptr;
                                    if (*obj_id < ctx.func->locals.size()) {
                                        obj_type = ctx.func->locals[*obj_id].type;
                                    }

                                    if (is_method_call) {
                                        // メソッド呼び出しの処理

                                        // スライス（動的配列）のメソッドかどうかチェック
                                        if (obj_type && obj_type->kind == hir::TypeKind::Array &&
                                            !obj_type->array_size.has_value()) {
                                            // スライスのメソッド呼び出し
                                            if (member_name == "len" || member_name == "length" ||
                                                member_name == "size") {
                                                LocalId result = ctx.new_temp(hir::make_uint());
                                                BlockId success_block = ctx.new_block();

                                                std::vector<MirOperandPtr> call_args;
                                                call_args.push_back(
                                                    MirOperand::copy(MirPlace{*obj_id}));

                                                auto call_term = std::make_unique<MirTerminator>();
                                                call_term->kind = MirTerminator::Call;
                                                call_term->data = MirTerminator::CallData{
                                                    MirOperand::function_ref("cm_slice_len"),
                                                    std::move(call_args),
                                                    MirPlace{result},
                                                    success_block,
                                                    std::nullopt,
                                                    "",
                                                    "",
                                                    false};
                                                ctx.set_terminator(std::move(call_term));
                                                ctx.switch_to_block(success_block);

                                                arg_locals.push_back(result);
                                            } else if (member_name == "cap" ||
                                                       member_name == "capacity") {
                                                LocalId result = ctx.new_temp(hir::make_uint());
                                                BlockId success_block = ctx.new_block();

                                                std::vector<MirOperandPtr> call_args;
                                                call_args.push_back(
                                                    MirOperand::copy(MirPlace{*obj_id}));

                                                auto call_term = std::make_unique<MirTerminator>();
                                                call_term->kind = MirTerminator::Call;
                                                call_term->data = MirTerminator::CallData{
                                                    MirOperand::function_ref("cm_slice_cap"),
                                                    std::move(call_args),
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
                                                // 未知のスライスメソッド
                                                auto err_type = hir::make_error();
                                                arg_locals.push_back(ctx.new_temp(err_type));
                                            }
                                        } else {
                                            // 構造体のメソッド呼び出し
                                            std::string method_name_full =
                                                obj_type->name + "::" + member_name;

                                            // メソッド呼び出しのための新しいブロックを作成
                                            BlockId call_block = ctx.new_block();
                                            BlockId after_call_block = ctx.new_block();

                                            // 戻り値用の一時変数（とりあえずintとして扱う）
                                            hir::TypePtr return_type = hir::make_int();
                                            LocalId result = ctx.new_temp(return_type);

                                            // 引数リスト（selfパラメータ）
                                            std::vector<MirOperandPtr> method_args;
                                            method_args.push_back(
                                                MirOperand::copy(MirPlace{*obj_id}));

                                            // メソッド呼び出しのターミネータを作成
                                            // インターフェースメソッドとして処理（仮想メソッド呼び出し）
                                            auto call_term = std::make_unique<MirTerminator>();
                                            call_term->kind = MirTerminator::Call;
                                            call_term->data = MirTerminator::CallData{
                                                MirOperand::function_ref(method_name_full),
                                                std::move(method_args),
                                                std::make_optional(MirPlace{result}),
                                                after_call_block,
                                                std::nullopt,  // unwindブロックなし
                                                "",  // interface_name（後でコード生成時に解決）
                                                member_name,  // method_name
                                                true  // is_virtual（インターフェースメソッドの可能性）
                                            };

                                            // 現在のブロックから呼び出しブロックへジャンプ
                                            ctx.set_terminator(
                                                MirTerminator::goto_block(call_block));

                                            // 呼び出しブロックを設定
                                            ctx.switch_to_block(call_block);
                                            ctx.get_current_block()->terminator =
                                                std::move(call_term);

                                            // 呼び出し後のブロックに切り替え
                                            ctx.switch_to_block(after_call_block);

                                            arg_locals.push_back(result);
                                        }
                                    } else if (obj_type &&
                                               obj_type->kind == hir::TypeKind::Struct) {
                                        // フィールドアクセスの処理（ネストしたアクセス、配列インデックスもサポート）
                                        // member_name が "inner.x" や "arr[0]"
                                        // のような場合、分割して処理

                                        // プロジェクションを構築
                                        MirPlace place{*obj_id};
                                        hir::TypePtr current_type = obj_type;
                                        bool valid = true;
                                        std::string remaining = member_name;

                                        while (!remaining.empty() && valid) {
                                            // 配列インデックスをチェック
                                            size_t bracket_pos = remaining.find('[');
                                            size_t dot_pos = remaining.find('.');

                                            std::string field_part;
                                            std::string index_part;

                                            if (bracket_pos != std::string::npos &&
                                                (dot_pos == std::string::npos ||
                                                 bracket_pos < dot_pos)) {
                                                // arr[0] または arr[0].field のような形式
                                                field_part = remaining.substr(0, bracket_pos);
                                                size_t close_bracket =
                                                    remaining.find(']', bracket_pos);
                                                if (close_bracket != std::string::npos) {
                                                    index_part = remaining.substr(
                                                        bracket_pos + 1,
                                                        close_bracket - bracket_pos - 1);
                                                    remaining = remaining.substr(close_bracket + 1);
                                                    if (!remaining.empty() && remaining[0] == '.') {
                                                        remaining = remaining.substr(1);
                                                    }
                                                } else {
                                                    valid = false;
                                                    break;
                                                }
                                            } else if (dot_pos != std::string::npos) {
                                                // field.next のような形式
                                                field_part = remaining.substr(0, dot_pos);
                                                remaining = remaining.substr(dot_pos + 1);
                                            } else {
                                                // 最後のフィールド
                                                field_part = remaining;
                                                remaining.clear();
                                            }

                                            // フィールドアクセスを処理
                                            if (!field_part.empty()) {
                                                if (!current_type ||
                                                    current_type->kind != hir::TypeKind::Struct) {
                                                    valid = false;
                                                    break;
                                                }

                                                auto field_idx = ctx.get_field_index(
                                                    current_type->name, field_part);
                                                if (!field_idx) {
                                                    valid = false;
                                                    break;
                                                }

                                                place.projections.push_back(
                                                    PlaceProjection::field(*field_idx));

                                                // 次のフィールドの型を取得
                                                if (ctx.struct_defs &&
                                                    ctx.struct_defs->count(current_type->name)) {
                                                    const auto* struct_def =
                                                        ctx.struct_defs->at(current_type->name);
                                                    if (*field_idx < struct_def->fields.size()) {
                                                        current_type =
                                                            struct_def->fields[*field_idx].type;
                                                    } else {
                                                        current_type = hir::make_int();
                                                    }
                                                } else {
                                                    current_type = hir::make_int();
                                                }
                                            }

                                            // 配列インデックスを処理
                                            if (!index_part.empty()) {
                                                // インデックスを数値に変換
                                                try {
                                                    int idx = std::stoi(index_part);

                                                    // インデックス用の定数を作成
                                                    LocalId idx_local =
                                                        ctx.new_temp(hir::make_int());
                                                    MirConstant idx_const;
                                                    idx_const.value = static_cast<int64_t>(idx);
                                                    idx_const.type = hir::make_int();
                                                    ctx.push_statement(MirStatement::assign(
                                                        MirPlace{idx_local},
                                                        MirRvalue::use(
                                                            MirOperand::constant(idx_const))));

                                                    place.projections.push_back(
                                                        PlaceProjection::index(idx_local));

                                                    // 配列の要素型に更新
                                                    if (current_type &&
                                                        current_type->kind ==
                                                            hir::TypeKind::Array &&
                                                        current_type->element_type) {
                                                        current_type = current_type->element_type;
                                                    } else {
                                                        current_type = hir::make_int();
                                                    }
                                                } catch (...) {
                                                    valid = false;
                                                }
                                            }
                                        }

                                        if (valid) {
                                            // フィールドアクセスのための一時変数
                                            LocalId result = ctx.new_temp(current_type);

                                            ctx.push_statement(MirStatement::assign(
                                                MirPlace{result},
                                                MirRvalue::use(MirOperand::copy(place))));

                                            arg_locals.push_back(result);
                                        } else {
                                            // フィールドが見つからない
                                            auto err_type = hir::make_error();
                                            arg_locals.push_back(ctx.new_temp(err_type));
                                        }
                                    } else {
                                        // 構造体型でない
                                        auto err_type = hir::make_error();
                                        arg_locals.push_back(ctx.new_temp(err_type));
                                    }
                                } else {
                                    // オブジェクトが見つからない
                                    auto err_type = hir::make_error();
                                    arg_locals.push_back(ctx.new_temp(err_type));
                                }
                            } else {
                                // enum値かどうかチェック（"::" を含む場合）
                                size_t scope_pos = var_name.find("::");
                                if (scope_pos != std::string::npos) {
                                    // enum値の処理（例: Color::Red）
                                    std::string enum_name = var_name.substr(0, scope_pos);
                                    std::string enum_member = var_name.substr(scope_pos + 2);

                                    // enum定数を解決
                                    auto enum_value = ctx.get_enum_value(enum_name, enum_member);

                                    hir::TypePtr enum_type =
                                        hir::make_int();  // enum型はintとして扱う
                                    LocalId result = ctx.new_temp(enum_type);

                                    // enum値を定数として生成
                                    MirConstant enum_const;
                                    enum_const.type = enum_type;

                                    if (enum_value) {
                                        // enum値が見つかった場合
                                        enum_const.value = *enum_value;
                                    } else {
                                        // enum値が見つからない場合はエラー値
                                        enum_const.value = int64_t(0);
                                        debug_msg("MIR",
                                                  "Warning: Enum value not found: " + var_name);
                                    }

                                    ctx.push_statement(MirStatement::assign(
                                        MirPlace{result},
                                        MirRvalue::use(MirOperand::constant(enum_const))));

                                    arg_locals.push_back(result);
                                } else {
                                    debug_msg("MIR", "Processing placeholder: " + var_name);
                                    // メソッド呼び出しかどうかチェック（obj.method()形式）
                                    size_t dot_pos = var_name.find('.');
                                    size_t paren_pos = var_name.find('(');

                                    if (dot_pos != std::string::npos &&
                                        paren_pos != std::string::npos && dot_pos < paren_pos &&
                                        var_name.back() == ')') {
                                        // メソッド呼び出しパターン: obj.method(args)
                                        std::string obj_name = var_name.substr(0, dot_pos);
                                        std::string method_name =
                                            var_name.substr(dot_pos + 1, paren_pos - dot_pos - 1);
                                        std::string args_str = var_name.substr(
                                            paren_pos + 1, var_name.length() - paren_pos - 2);

                                        debug_msg("MIR", "Method call interpolation: obj=" +
                                                             obj_name + ", method=" + method_name);

                                        // オブジェクトを解決
                                        auto obj_id = ctx.resolve_variable(obj_name);
                                        if (obj_id) {
                                            // オブジェクトの型を取得
                                            hir::TypePtr obj_type = nullptr;
                                            if (*obj_id < ctx.func->locals.size()) {
                                                obj_type = ctx.func->locals[*obj_id].type;
                                            }

                                            if (obj_type &&
                                                obj_type->kind == hir::TypeKind::Struct) {
                                                debug_msg("MIR", "Object type: " + obj_type->name);

                                                // メソッド呼び出しのブロックを作成
                                                BlockId call_block = ctx.new_block();
                                                BlockId after_call_block = ctx.new_block();

                                                // 戻り値用の一時変数
                                                hir::TypePtr return_type = hir::make_int();
                                                LocalId result = ctx.new_temp(return_type);

                                                // 引数の準備（レシーバーが最初の引数）
                                                std::vector<MirOperandPtr> call_args;
                                                call_args.push_back(
                                                    MirOperand::copy(MirPlace{*obj_id}));

                                                // 追加引数を解析（簡易実装）
                                                if (!args_str.empty()) {
                                                    try {
                                                        int64_t value = std::stoll(args_str);
                                                        MirConstant arg_const;
                                                        arg_const.type = hir::make_int();
                                                        arg_const.value = value;
                                                        call_args.push_back(
                                                            MirOperand::constant(arg_const));
                                                    } catch (...) {
                                                        // 解析エラーの場合は引数なし
                                                    }
                                                }

                                                // インターフェースメソッド名を探す
                                                // 形式: StructName::InterfaceName::MethodName
                                                std::string full_method_name =
                                                    obj_type->name + "__" + method_name;
                                                debug_msg("MIR",
                                                          "Full method name: " + full_method_name);

                                                // 簡易実装：インターフェース名を仮定（実際は検索が必要）
                                                // TODO:
                                                // 実際のインターフェース名を検索する機能を実装
                                                std::string interface_name = "";
                                                bool is_virtual = false;

                                                // 暫定的にインターフェース名を検索
                                                // 実際にはimpl情報から検索する必要がある
                                                if (method_name == "sum") {
                                                    interface_name = "Summable";
                                                    is_virtual = true;
                                                } else if (method_name == "get_value") {
                                                    interface_name = "Valuable";
                                                    is_virtual = true;
                                                }

                                                // メソッド呼び出しのターミネータを作成
                                                auto call_term = std::make_unique<MirTerminator>();
                                                call_term->kind = MirTerminator::Call;
                                                call_term->data = MirTerminator::CallData{
                                                    MirOperand::function_ref(full_method_name),
                                                    std::move(call_args),
                                                    std::make_optional(MirPlace{result}),
                                                    after_call_block,
                                                    std::nullopt,
                                                    interface_name,  // インターフェース名
                                                    method_name,     // メソッド名
                                                    is_virtual       // 仮想メソッドフラグ
                                                };

                                                // 現在のブロックから呼び出しブロックへジャンプ
                                                ctx.set_terminator(
                                                    MirTerminator::goto_block(call_block));

                                                // 呼び出しブロックを設定
                                                ctx.switch_to_block(call_block);
                                                ctx.get_current_block()->terminator =
                                                    std::move(call_term);

                                                // 呼び出し後のブロックに切り替え
                                                ctx.switch_to_block(after_call_block);

                                                arg_locals.push_back(result);
                                            } else if (obj_type &&
                                                       obj_type->kind == hir::TypeKind::Array &&
                                                       !obj_type->array_size.has_value()) {
                                                // スライス（動的配列）のメソッド呼び出し
                                                debug_msg("MIR",
                                                          "Slice method call: " + method_name);

                                                if (method_name == "len" ||
                                                    method_name == "length" ||
                                                    method_name == "size") {
                                                    // cm_slice_len 呼び出し
                                                    LocalId result = ctx.new_temp(hir::make_uint());
                                                    BlockId success_block = ctx.new_block();

                                                    std::vector<MirOperandPtr> call_args;
                                                    call_args.push_back(
                                                        MirOperand::copy(MirPlace{*obj_id}));

                                                    auto call_term =
                                                        std::make_unique<MirTerminator>();
                                                    call_term->kind = MirTerminator::Call;
                                                    call_term->data = MirTerminator::CallData{
                                                        MirOperand::function_ref("cm_slice_len"),
                                                        std::move(call_args),
                                                        MirPlace{result},
                                                        success_block,
                                                        std::nullopt,
                                                        "",
                                                        "",
                                                        false};
                                                    ctx.set_terminator(std::move(call_term));
                                                    ctx.switch_to_block(success_block);

                                                    arg_locals.push_back(result);
                                                } else if (method_name == "cap" ||
                                                           method_name == "capacity") {
                                                    // cm_slice_cap 呼び出し
                                                    LocalId result = ctx.new_temp(hir::make_uint());
                                                    BlockId success_block = ctx.new_block();

                                                    std::vector<MirOperandPtr> call_args;
                                                    call_args.push_back(
                                                        MirOperand::copy(MirPlace{*obj_id}));

                                                    auto call_term =
                                                        std::make_unique<MirTerminator>();
                                                    call_term->kind = MirTerminator::Call;
                                                    call_term->data = MirTerminator::CallData{
                                                        MirOperand::function_ref("cm_slice_cap"),
                                                        std::move(call_args),
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
                                                    // 未知のメソッド
                                                    auto err_type = hir::make_error();
                                                    arg_locals.push_back(ctx.new_temp(err_type));
                                                }
                                            } else {
                                                // 構造体型でもスライスでもない場合はエラー
                                                auto err_type = hir::make_error();
                                                arg_locals.push_back(ctx.new_temp(err_type));
                                            }
                                        } else {
                                            // オブジェクトが見つからない場合はエラー
                                            auto err_type = hir::make_error();
                                            arg_locals.push_back(ctx.new_temp(err_type));
                                        }
                                    } else {
                                        // 通常の関数呼び出しかどうかチェック
                                        bool is_function_call = false;
                                        std::string func_name = var_name;
                                        std::vector<std::string> func_args;

                                        // 関数呼び出しパターンをチェック（"func()" or
                                        // "func(args)"）
                                        if (paren_pos != std::string::npos &&
                                            var_name.back() == ')') {
                                            is_function_call = true;
                                            func_name = var_name.substr(0, paren_pos);

                                            // 引数を解析（簡易実装：現在は単一の整数のみサポート）
                                            std::string args_str = var_name.substr(
                                                paren_pos + 1, var_name.length() - paren_pos - 2);
                                            if (!args_str.empty()) {
                                                func_args.push_back(args_str);
                                            }
                                        }

                                        if (is_function_call) {
                                            // まず変数として解決を試みる（関数ポインタの可能性）
                                            auto var_id = ctx.resolve_variable(func_name);

                                            if (var_id) {
                                                // 変数が見つかった - 関数ポインタ経由の呼び出し
                                                BlockId call_block = ctx.new_block();
                                                BlockId after_call_block = ctx.new_block();

                                                // 戻り値用の一時変数（とりあえずintとして扱う）
                                                hir::TypePtr return_type = hir::make_int();
                                                LocalId result = ctx.new_temp(return_type);

                                                // 引数の準備
                                                std::vector<MirOperandPtr> call_args;
                                                for (const auto& arg_str : func_args) {
                                                    // 簡易実装：整数リテラルのみサポート
                                                    try {
                                                        int64_t value = std::stoll(arg_str);
                                                        MirConstant arg_const;
                                                        arg_const.type = hir::make_int();
                                                        arg_const.value = value;
                                                        call_args.push_back(
                                                            MirOperand::constant(arg_const));
                                                    } catch (...) {
                                                        // 解析エラーの場合はダミー値
                                                        MirConstant arg_const;
                                                        arg_const.type = hir::make_int();
                                                        arg_const.value = 0;
                                                        call_args.push_back(
                                                            MirOperand::constant(arg_const));
                                                    }
                                                }

                                                // 関数ポインタ経由の呼び出し
                                                auto call_term = std::make_unique<MirTerminator>();
                                                call_term->kind = MirTerminator::Call;
                                                call_term->data = MirTerminator::CallData{
                                                    MirOperand::copy(MirPlace{
                                                        *var_id}),  // 関数ポインタ変数を使用
                                                    std::move(call_args),
                                                    std::make_optional(MirPlace{result}),
                                                    after_call_block,
                                                    std::nullopt,  // unwindブロックなし
                                                    "",            // interface_name
                                                    "",            // method_name
                                                    false          // is_virtual
                                                };

                                                // 現在のブロックから呼び出しブロックへジャンプ
                                                ctx.set_terminator(
                                                    MirTerminator::goto_block(call_block));

                                                // 呼び出しブロックを設定
                                                ctx.switch_to_block(call_block);
                                                ctx.get_current_block()->terminator =
                                                    std::move(call_term);

                                                // 呼び出し後のブロックに切り替え
                                                ctx.switch_to_block(after_call_block);

                                                arg_locals.push_back(result);
                                            } else {
                                                // 変数が見つからない - 通常の関数呼び出しを試みる
                                                BlockId call_block = ctx.new_block();
                                                BlockId after_call_block = ctx.new_block();

                                                // 戻り値用の一時変数（とりあえずintとして扱う）
                                                hir::TypePtr return_type = hir::make_int();
                                                LocalId result = ctx.new_temp(return_type);

                                                // 引数の準備
                                                std::vector<MirOperandPtr> call_args;
                                                for (const auto& arg_str : func_args) {
                                                    // 簡易実装：整数リテラルのみサポート
                                                    try {
                                                        int64_t value = std::stoll(arg_str);
                                                        MirConstant arg_const;
                                                        arg_const.type = hir::make_int();
                                                        arg_const.value = value;
                                                        call_args.push_back(
                                                            MirOperand::constant(arg_const));
                                                    } catch (...) {
                                                        // 解析エラーの場合はダミー値
                                                        MirConstant arg_const;
                                                        arg_const.type = hir::make_int();
                                                        arg_const.value = 0;
                                                        call_args.push_back(
                                                            MirOperand::constant(arg_const));
                                                    }
                                                }

                                                // 関数呼び出しのターミネータを作成
                                                auto call_term = std::make_unique<MirTerminator>();
                                                call_term->kind = MirTerminator::Call;
                                                call_term->data = MirTerminator::CallData{
                                                    MirOperand::function_ref(func_name),
                                                    std::move(call_args),
                                                    std::make_optional(MirPlace{result}),
                                                    after_call_block,
                                                    std::nullopt,  // unwindブロックなし
                                                    "",            // interface_name
                                                    "",            // method_name
                                                    false          // is_virtual
                                                };

                                                // 現在のブロックから呼び出しブロックへジャンプ
                                                ctx.set_terminator(
                                                    MirTerminator::goto_block(call_block));

                                                // 呼び出しブロックを設定
                                                ctx.switch_to_block(call_block);
                                                ctx.get_current_block()->terminator =
                                                    std::move(call_term);

                                                // 呼び出し後のブロックに切り替え
                                                ctx.switch_to_block(after_call_block);

                                                arg_locals.push_back(result);
                                            }
                                        } else {
                                            // 通常の変数
                                            // まずconst値をチェック（定数として登録されている場合）
                                            auto const_val = ctx.get_const_value(var_name);
                                            if (const_val) {
                                                // const変数の値を一時変数に格納
                                                LocalId const_temp = ctx.new_temp(const_val->type);
                                                ctx.push_statement(MirStatement::assign(
                                                    MirPlace{const_temp},
                                                    MirRvalue::use(
                                                        MirOperand::constant(*const_val))));
                                                arg_locals.push_back(const_temp);
                                            } else {
                                                auto var_id = ctx.resolve_variable(var_name);
                                                if (var_id) {
                                                    arg_locals.push_back(*var_id);
                                                } else {
                                                    // 変数が見つからない場合、エラー用のダミー値を追加
                                                    auto err_type = hir::make_error();
                                                    arg_locals.push_back(ctx.new_temp(err_type));
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // 明示的な引数は無視（単一の文字列リテラルのみ許可）
                    // 将来的にはエラーを報告する
                    if (call.args.size() > 1) {
                        // TODO: エラーを報告: println accepts only a single string literal. Use
                        // variable interpolation instead: println("{var}") 現在は追加引数を無視
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
                // その他のリテラル（整数など）
                runtime_func = "cm_println_int";
                LocalId arg_local = lower_expression(*first_arg, ctx);
                args.push_back(MirOperand::copy(MirPlace{arg_local}));
            }
        } else {
            // 式の場合、評価して型に基づいて選択
            LocalId arg_local = lower_expression(*first_arg, ctx);

            // 型チェック
            if (first_arg->type) {
                switch (first_arg->type->kind) {
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

    // スライスのlen/cap処理
    if (call.func_name == "__builtin_slice_len" || call.func_name == "__builtin_slice_cap") {
        if (!call.args.empty()) {
            auto slice_expr = call.args[0].get();

            // スライス変数を解決（変数参照またはメンバアクセス）
            MirPlace slice_place{0};
            hir::TypePtr slice_type = nullptr;
            bool resolved = false;

            if (auto* var = std::get_if<std::unique_ptr<hir::HirVarRef>>(&slice_expr->kind)) {
                auto slice_local_opt = ctx.resolve_variable((*var)->name);
                if (slice_local_opt.has_value()) {
                    slice_place = MirPlace{*slice_local_opt};
                    resolved = true;
                }
            } else if (auto* mem =
                           std::get_if<std::unique_ptr<hir::HirMember>>(&slice_expr->kind)) {
                // メンバアクセス（c.values のような形式）- 直接参照を取得
                if (get_member_place(**mem, ctx, slice_place, slice_type)) {
                    resolved = true;
                }
            }

            if (resolved) {
                std::string func_name =
                    (call.func_name == "__builtin_slice_len") ? "cm_slice_len" : "cm_slice_cap";

                LocalId result = ctx.new_temp(hir::make_uint());
                BlockId success_block = ctx.new_block();
                std::vector<MirOperandPtr> args;
                args.push_back(MirOperand::copy(slice_place));

                auto call_term = std::make_unique<MirTerminator>();
                call_term->kind = MirTerminator::Call;
                call_term->data = MirTerminator::CallData{MirOperand::function_ref(func_name),
                                                          std::move(args),
                                                          MirPlace{result},
                                                          success_block,
                                                          std::nullopt,
                                                          "",
                                                          "",
                                                          false};
                ctx.set_terminator(std::move(call_term));
                ctx.switch_to_block(success_block);
                return result;
            }
        }
        return ctx.new_temp(hir::make_uint());
    }

    // スライスビルトイン関数の処理 - 通常の関数呼び出しとして変換
    if (call.func_name == "__builtin_slice_push") {
        if (call.args.size() >= 2) {
            auto slice_expr = call.args[0].get();

            // スライス変数を解決（変数参照またはメンバアクセス）
            MirPlace slice_place{0};
            hir::TypePtr slice_type = nullptr;
            bool resolved = false;

            if (auto* var = std::get_if<std::unique_ptr<hir::HirVarRef>>(&slice_expr->kind)) {
                auto slice_local_opt = ctx.resolve_variable((*var)->name);
                if (slice_local_opt) {
                    slice_place = MirPlace{*slice_local_opt};
                    resolved = true;
                    if (*slice_local_opt < ctx.func->locals.size()) {
                        slice_type = ctx.func->locals[*slice_local_opt].type;
                    }
                }
            } else if (auto* mem =
                           std::get_if<std::unique_ptr<hir::HirMember>>(&slice_expr->kind)) {
                // メンバアクセス（c.values のような形式）- 直接参照を取得
                if (get_member_place(**mem, ctx, slice_place, slice_type)) {
                    resolved = true;
                }
            }

            if (resolved) {
                LocalId value_local = lower_expression(*call.args[1], ctx);

                // 要素型に基づいてランタイム関数を選択
                std::string push_func = "cm_slice_push_i32";
                if (slice_type && slice_type->element_type) {
                    auto elem_kind = slice_type->element_type->kind;
                    if (elem_kind == hir::TypeKind::Char || elem_kind == hir::TypeKind::Bool ||
                        elem_kind == hir::TypeKind::Tiny || elem_kind == hir::TypeKind::UTiny) {
                        push_func = "cm_slice_push_i8";
                    } else if (elem_kind == hir::TypeKind::Long ||
                               elem_kind == hir::TypeKind::ULong) {
                        push_func = "cm_slice_push_i64";
                    } else if (elem_kind == hir::TypeKind::Double ||
                               elem_kind == hir::TypeKind::Float) {
                        push_func = "cm_slice_push_f64";
                    } else if (elem_kind == hir::TypeKind::Pointer ||
                               elem_kind == hir::TypeKind::String ||
                               elem_kind == hir::TypeKind::Struct) {
                        push_func = "cm_slice_push_ptr";
                    }
                }

                BlockId success_block = ctx.new_block();
                std::vector<MirOperandPtr> args;
                args.push_back(MirOperand::copy(slice_place));
                args.push_back(MirOperand::copy(MirPlace{value_local}));

                auto call_term = std::make_unique<MirTerminator>();
                call_term->kind = MirTerminator::Call;
                call_term->data = MirTerminator::CallData{MirOperand::function_ref(push_func),
                                                          std::move(args),
                                                          std::nullopt,
                                                          success_block,
                                                          std::nullopt,
                                                          "",
                                                          "",
                                                          false};
                ctx.set_terminator(std::move(call_term));
                ctx.switch_to_block(success_block);
                return ctx.new_temp(hir::make_void());
            }
        }
        return ctx.new_temp(hir::make_void());
    }

    if (call.func_name == "__builtin_slice_pop") {
        if (!call.args.empty()) {
            auto slice_expr = call.args[0].get();
            if (auto* var = std::get_if<std::unique_ptr<hir::HirVarRef>>(&slice_expr->kind)) {
                auto slice_local_opt = ctx.resolve_variable((*var)->name);
                if (slice_local_opt.has_value()) {
                    LocalId slice_local = slice_local_opt.value();

                    std::string pop_func = "cm_slice_pop_i32";
                    hir::TypePtr elem_type = hir::make_int();
                    hir::TypePtr slice_type = nullptr;
                    if (slice_local < ctx.func->locals.size()) {
                        slice_type = ctx.func->locals[slice_local].type;
                    }
                    if (slice_type && slice_type->element_type) {
                        elem_type = slice_type->element_type;
                        auto elem_kind = elem_type->kind;
                        if (elem_kind == hir::TypeKind::Char || elem_kind == hir::TypeKind::Bool ||
                            elem_kind == hir::TypeKind::Tiny || elem_kind == hir::TypeKind::UTiny) {
                            pop_func = "cm_slice_pop_i8";
                        } else if (elem_kind == hir::TypeKind::Long ||
                                   elem_kind == hir::TypeKind::ULong) {
                            pop_func = "cm_slice_pop_i64";
                        } else if (elem_kind == hir::TypeKind::Double ||
                                   elem_kind == hir::TypeKind::Float) {
                            pop_func = "cm_slice_pop_f64";
                        } else if (elem_kind == hir::TypeKind::Pointer ||
                                   elem_kind == hir::TypeKind::String ||
                                   elem_kind == hir::TypeKind::Struct) {
                            pop_func = "cm_slice_pop_ptr";
                        }
                    }

                    LocalId result = ctx.new_temp(elem_type);
                    BlockId success_block = ctx.new_block();
                    std::vector<MirOperandPtr> args;
                    args.push_back(MirOperand::copy(MirPlace{slice_local}));

                    auto call_term = std::make_unique<MirTerminator>();
                    call_term->kind = MirTerminator::Call;
                    call_term->data = MirTerminator::CallData{MirOperand::function_ref(pop_func),
                                                              std::move(args),
                                                              MirPlace{result},
                                                              success_block,
                                                              std::nullopt,
                                                              "",
                                                              "",
                                                              false};
                    ctx.set_terminator(std::move(call_term));
                    ctx.switch_to_block(success_block);
                    return result;
                }
            }
        }
        return ctx.new_temp(result_type ? result_type : hir::make_int());
    }

    if (call.func_name == "__builtin_slice_delete") {
        if (call.args.size() >= 2) {
            auto slice_expr = call.args[0].get();
            if (auto* var = std::get_if<std::unique_ptr<hir::HirVarRef>>(&slice_expr->kind)) {
                auto slice_local_opt = ctx.resolve_variable((*var)->name);
                if (slice_local_opt.has_value()) {
                    LocalId slice_local = slice_local_opt.value();
                    LocalId index_local = lower_expression(*call.args[1], ctx);

                    BlockId success_block = ctx.new_block();
                    std::vector<MirOperandPtr> args;
                    args.push_back(MirOperand::copy(MirPlace{slice_local}));
                    args.push_back(MirOperand::copy(MirPlace{index_local}));

                    auto call_term = std::make_unique<MirTerminator>();
                    call_term->kind = MirTerminator::Call;
                    call_term->data =
                        MirTerminator::CallData{MirOperand::function_ref("cm_slice_delete"),
                                                std::move(args),
                                                std::nullopt,
                                                success_block,
                                                std::nullopt,
                                                "",
                                                "",
                                                false};
                    ctx.set_terminator(std::move(call_term));
                    ctx.switch_to_block(success_block);
                    return ctx.new_temp(hir::make_void());
                }
            }
        }
        return ctx.new_temp(hir::make_void());
    }

    if (call.func_name == "__builtin_slice_clear") {
        if (!call.args.empty()) {
            auto slice_expr = call.args[0].get();
            if (auto* var = std::get_if<std::unique_ptr<hir::HirVarRef>>(&slice_expr->kind)) {
                auto slice_local_opt = ctx.resolve_variable((*var)->name);
                if (slice_local_opt.has_value()) {
                    LocalId slice_local = slice_local_opt.value();

                    BlockId success_block = ctx.new_block();
                    std::vector<MirOperandPtr> args;
                    args.push_back(MirOperand::copy(MirPlace{slice_local}));

                    auto call_term = std::make_unique<MirTerminator>();
                    call_term->kind = MirTerminator::Call;
                    call_term->data =
                        MirTerminator::CallData{MirOperand::function_ref("cm_slice_clear"),
                                                std::move(args),
                                                std::nullopt,
                                                success_block,
                                                std::nullopt,
                                                "",
                                                "",
                                                false};
                    ctx.set_terminator(std::move(call_term));
                    ctx.switch_to_block(success_block);
                    return ctx.new_temp(hir::make_void());
                }
            }
        }
        return ctx.new_temp(hir::make_void());
    }

    // 通常の関数呼び出し
    std::vector<MirOperandPtr> args;
    for (const auto& arg : call.args) {
        LocalId arg_local = lower_expression(*arg, ctx);
        args.push_back(MirOperand::copy(MirPlace{arg_local}));
    }

    // 結果用の一時変数（型チェッカーが推論した型を使用）
    hir::TypePtr actual_result_type = result_type ? result_type : hir::make_int();
    LocalId result = ctx.new_temp(actual_result_type);

    // Call終端命令（現在のブロックを終端）
    BlockId success_block = ctx.new_block();

    // 関数オペランドを作成
    MirOperandPtr func_operand;
    if (call.is_indirect) {
        // 関数ポインタ経由の呼び出し: 変数から関数ポインタを取得
        auto var_id = ctx.resolve_variable(call.func_name);
        if (var_id) {
            func_operand = MirOperand::copy(MirPlace{*var_id});
        } else {
            // 変数が見つからない場合は直接関数参照として処理
            // extern関数などは変数ではなく関数として登録されている
            func_operand = MirOperand::function_ref(call.func_name);
        }
    } else {
        // 直接呼び出し: 関数参照を使用
        func_operand = MirOperand::function_ref(call.func_name);
    }

    // Call終端命令を手動で作成
    auto call_term = std::make_unique<MirTerminator>();
    call_term->kind = MirTerminator::Call;

    // インターフェースメソッド呼び出しかどうかを判定
    // 関数名が "TypeName__MethodName" の形式で、TypeNameがインターフェースの場合
    std::string interface_name;
    std::string method_name;
    bool is_virtual = false;

    auto underscore_pos = call.func_name.find("__");
    if (underscore_pos != std::string::npos) {
        std::string type_name = call.func_name.substr(0, underscore_pos);
        method_name = call.func_name.substr(underscore_pos + 2);

        // コンテキストのインターフェース名セットをチェック
        if (ctx.interface_names && ctx.interface_names->count(type_name) > 0) {
            interface_name = type_name;
            is_virtual = true;
        }
    }

    MirTerminator::CallData call_data{
        std::move(func_operand),
        std::move(args),
        MirPlace{result},  // 戻り値の格納先
        success_block,
        std::nullopt,  // unwind無し
        std::string(),
        std::string(),
        false  // 通常の関数呼び出し
    };

    if (is_virtual) {
        call_data.interface_name = interface_name;
        call_data.method_name = method_name;
        call_data.is_virtual = true;
    }

    call_term->data = std::move(call_data);
    ctx.set_terminator(std::move(call_term));

    // 次のブロックへ移動
    ctx.switch_to_block(success_block);

    return result;
}

// メンバーアクセスのlowering

}  // namespace cm::mir
