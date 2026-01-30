// expr_lowering_basic.cpp - 基本式のlowering
// lower_literal, lower_var_ref, lower_member, lower_index,
// lower_ternary, lower_struct_literal, lower_array_literal, convert_to_string

#include "../../common/debug.hpp"
#include "expr.hpp"

namespace cm::mir {

LocalId ExprLowering::lower_literal(const hir::HirLiteral& lit, LoweringContext& ctx) {
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
                constant.type = hir::make_int();
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
    if (var.is_function_ref) {
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

// 二項演算のlowering
LocalId ExprLowering::lower_member(const hir::HirMember& member, LoweringContext& ctx) {
    // ネストしたメンバーアクセスを検出して、プロジェクションを連結する
    std::vector<std::pair<std::string, std::string>> field_chain;  // (struct_name, field_name)
    const hir::HirExpr* current = member.object.get();

    // 最初のメンバー（自身）を追加
    auto initial_type = member.object->type;
    field_chain.push_back({initial_type ? initial_type->name : "", member.member});

    // フィールドチェーンを構築（最も内側から外側へ）
    while (auto* inner_member = std::get_if<std::unique_ptr<hir::HirMember>>(&current->kind)) {
        auto obj_type = (*inner_member)->object->type;
        field_chain.push_back({obj_type ? obj_type->name : "", (*inner_member)->member});
        current = (*inner_member)->object.get();
    }

    // ベースオブジェクトを取得
    // 変数参照の場合は直接その変数のLocalIdを使用してコピーを避ける
    // lower_expressionだと一時変数にコピーされてしまい、プロジェクションが失われる
    LocalId object;
    if (auto* var_ref = std::get_if<std::unique_ptr<hir::HirVarRef>>(&current->kind)) {
        auto var_id = ctx.resolve_variable((*var_ref)->name);
        if (var_id) {
            object = *var_id;
        } else {
            // 変数が見つからない場合はlower_expressionにフォールバック
            object = lower_expression(*current, ctx);
        }
    } else {
        object = lower_expression(*current, ctx);
    }
    hir::TypePtr obj_type = current->type;

    // MIRのローカル変数の型を取得（selfの場合はポインタ型）
    hir::TypePtr mir_type = nullptr;
    if (object < ctx.func->locals.size()) {
        mir_type = ctx.func->locals[object].type;
    }

    // ポインタ型の場合、デリファレンスが必要
    // HIRの型ではなくMIRの型でポインタかどうかを判定（selfは暗黙的にポインタ）
    bool needs_deref = false;
    if (mir_type && mir_type->kind == hir::TypeKind::Pointer) {
        needs_deref = true;
        obj_type = mir_type->element_type;  // ポインタの先の型を使用
    } else if (!obj_type || obj_type->kind != hir::TypeKind::Struct) {
        // HIRの型が未設定または構造体でない場合、MIRの型から推論
        if (mir_type) {
            obj_type = mir_type;
        }
    }

    if (!obj_type || obj_type->kind != hir::TypeKind::Struct) {
        debug_msg("MIR",
                  "Error: Member access on non-struct type for member '" + member.member + "'");
        return ctx.new_temp(hir::make_error());
    }

    // フィールドチェーンを逆順にしてプロジェクションを構築
    MirPlace place{object};

    // ポインタの場合、最初にデリファレンス
    if (needs_deref) {
        place.projections.push_back(PlaceProjection::deref());
    }

    hir::TypePtr current_type = obj_type;

    for (auto it = field_chain.rbegin(); it != field_chain.rend(); ++it) {
        const std::string& field_name = it->second;

        if (!current_type || current_type->kind != hir::TypeKind::Struct) {
            debug_msg("MIR", "Error: Non-struct type in member chain");
            return ctx.new_temp(hir::make_error());
        }

        // ジェネリック構造体の場合、ベース名で検索（例: Node<Item> -> Node, Pair__int -> Pair）
        std::string base_name = current_type->name;

        // マングリング済み名前（__を含む）の場合、ベース名を抽出
        size_t mangled_pos = base_name.find("__");
        std::string original_base = base_name;
        if (mangled_pos != std::string::npos) {
            base_name = base_name.substr(0, mangled_pos);
        }

        auto field_idx = ctx.get_field_index(base_name, field_name);
        if (!field_idx) {
            // マングリング前の名前でも見つからない場合、元の名前で再試行
            field_idx = ctx.get_field_index(original_base, field_name);
            if (!field_idx) {
                debug_msg("MIR", "Error: Field '" + field_name + "' not found in struct '" +
                                     base_name + "'");
                return ctx.new_temp(hir::make_error());
            }
        }

        place.projections.push_back(PlaceProjection::field(*field_idx));

        // 次のフィールドの型を取得
        // ジェネリック構造体の場合type_argsを保持し、フィールド型置換に使用
        if (ctx.struct_defs && ctx.struct_defs->count(base_name)) {
            const auto* struct_def = ctx.struct_defs->at(base_name);
            if (*field_idx < struct_def->fields.size()) {
                hir::TypePtr field_type = struct_def->fields[*field_idx].type;

                // フィールド型がジェネリックパラメータの場合、type_argsから置換
                // 注: HIRでTがStruct扱いになる場合があるため、generic_params名で照合
                if (field_type && !current_type->type_args.empty()) {
                    for (size_t j = 0; j < struct_def->generic_params.size() &&
                                       j < current_type->type_args.size();
                         ++j) {
                        // field_typeの名前がgeneric_paramsに一致する場合、置換
                        if (struct_def->generic_params[j].name == field_type->name) {
                            field_type = current_type->type_args[j];
                            break;
                        }
                    }
                }
                // type_argsが空でもマングリング済み名前（Pair__int等）から型引数を抽出
                else if (field_type && current_type->type_args.empty() &&
                         current_type->name.find("__") != std::string::npos) {
                    // マングリング名から型引数を抽出
                    std::vector<std::string> extracted_args;
                    std::string name = current_type->name;
                    size_t pos = name.find("__");
                    while (pos != std::string::npos) {
                        size_t next = name.find("__", pos + 2);
                        if (next == std::string::npos) {
                            extracted_args.push_back(name.substr(pos + 2));
                        } else {
                            extracted_args.push_back(name.substr(pos + 2, next - pos - 2));
                        }
                        pos = next;
                    }
                    // generic_paramsと照合して置換
                    for (size_t j = 0;
                         j < struct_def->generic_params.size() && j < extracted_args.size(); ++j) {
                        if (struct_def->generic_params[j].name == field_type->name) {
                            // 抽出した型名から具体型を作成
                            const std::string& type_name = extracted_args[j];
                            hir::TypePtr concrete_type;
                            if (type_name == "int") {
                                concrete_type = hir::make_int();
                            } else if (type_name == "uint") {
                                concrete_type = hir::make_uint();
                            } else if (type_name == "long") {
                                concrete_type = hir::make_long();
                            } else if (type_name == "ulong") {
                                concrete_type = hir::make_ulong();
                            } else if (type_name == "double") {
                                concrete_type = hir::make_double();
                            } else if (type_name == "float") {
                                concrete_type = hir::make_float();
                            } else if (type_name == "bool") {
                                concrete_type = hir::make_bool();
                            } else if (type_name == "string") {
                                concrete_type = hir::make_string();
                            } else {
                                // その他は構造体として扱う
                                concrete_type = std::make_shared<hir::Type>(hir::TypeKind::Struct);
                                concrete_type->name = type_name;
                            }
                            field_type = concrete_type;
                            break;
                        }
                    }
                }
                current_type = field_type;
            } else {
                current_type = hir::make_int();
            }
        } else {
            current_type = hir::make_int();
        }
    }

    // 最終的なフィールドの型で一時変数を作成
    // 重要: shared_ptrの参照共有で後から型が変更される問題を回避するため、
    // 型をディープコピーする
    hir::TypePtr final_type = current_type;
    if (current_type &&
        (current_type->kind == hir::TypeKind::Int || current_type->kind == hir::TypeKind::UInt ||
         current_type->kind == hir::TypeKind::Long || current_type->kind == hir::TypeKind::Float ||
         current_type->kind == hir::TypeKind::Double ||
         current_type->kind == hir::TypeKind::Bool)) {
        // プリミティブ型は新しいインスタンスを作成
        final_type = std::make_shared<hir::Type>(*current_type);
    } else if (current_type && current_type->kind == hir::TypeKind::Struct) {
        // 構造体型も新しいインスタンスを作成
        final_type = std::make_shared<hir::Type>(*current_type);
    }

    LocalId result = ctx.new_temp(final_type);

    ctx.push_statement(
        MirStatement::assign(MirPlace{result}, MirRvalue::use(MirOperand::copy(place))));

    return result;
}

// メンバアクセスからMirPlaceを取得（コピーせずに参照を取得）
bool ExprLowering::get_member_place(const hir::HirMember& member, LoweringContext& ctx,
                                    MirPlace& out_place, hir::TypePtr& out_type) {
    // ネストしたメンバーアクセスを検出して、プロジェクションを連結する
    std::vector<std::pair<std::string, std::string>> field_chain;
    const hir::HirExpr* current = member.object.get();

    // 最初のメンバー（自身）を追加
    auto initial_type = member.object->type;
    field_chain.push_back({initial_type ? initial_type->name : "", member.member});

    // フィールドチェーンを構築
    while (auto* inner_member = std::get_if<std::unique_ptr<hir::HirMember>>(&current->kind)) {
        auto obj_type = (*inner_member)->object->type;
        field_chain.push_back({obj_type ? obj_type->name : "", (*inner_member)->member});
        current = (*inner_member)->object.get();
    }

    // ベースオブジェクトを取得（変数参照のみサポート）
    if (auto* var_ref = std::get_if<std::unique_ptr<hir::HirVarRef>>(&current->kind)) {
        auto local_opt = ctx.resolve_variable((*var_ref)->name);
        if (!local_opt)
            return false;

        LocalId object = *local_opt;
        hir::TypePtr obj_type = nullptr;
        if (object < ctx.func->locals.size()) {
            obj_type = ctx.func->locals[object].type;
        }

        if (!obj_type || obj_type->kind != hir::TypeKind::Struct) {
            return false;
        }

        // フィールドチェーンを逆順にしてプロジェクションを構築
        out_place = MirPlace{object};
        hir::TypePtr current_type = obj_type;

        for (auto it = field_chain.rbegin(); it != field_chain.rend(); ++it) {
            const std::string& field_name = it->second;

            if (!current_type || current_type->kind != hir::TypeKind::Struct) {
                return false;
            }

            auto field_idx = ctx.get_field_index(current_type->name, field_name);
            if (!field_idx) {
                return false;
            }

            out_place.projections.push_back(PlaceProjection::field(*field_idx));

            // 次のフィールドの型を取得
            if (ctx.struct_defs && ctx.struct_defs->count(current_type->name)) {
                const auto* struct_def = ctx.struct_defs->at(current_type->name);
                if (*field_idx < struct_def->fields.size()) {
                    current_type = struct_def->fields[*field_idx].type;
                } else {
                    current_type = hir::make_int();
                }
            } else {
                current_type = hir::make_int();
            }
        }

        out_type = current_type;
        return true;
    }

    return false;
}

// 配列インデックスのlowering
LocalId ExprLowering::lower_index(const hir::HirIndex& index_expr, LoweringContext& ctx) {
    // オブジェクトをlowering
    LocalId array;

    // objectが変数参照の場合は直接その変数を使用（配列のコピーを防ぐ）
    if (auto* var_ref = std::get_if<std::unique_ptr<hir::HirVarRef>>(&index_expr.object->kind)) {
        auto var_id = ctx.resolve_variable((*var_ref)->name);
        if (var_id) {
            array = *var_id;
        } else {
            array = lower_expression(*index_expr.object, ctx);
        }
    } else {
        array = lower_expression(*index_expr.object, ctx);
    }

    // 多次元配列最適化: indices が設定されている場合、複数のIndex projectionを生成
    // これにより一時変数（行コピー）を回避し、LLVMのベクトル化が可能になる
    bool is_multi_dim = !index_expr.indices.empty();

    // インデックスをlowering
    std::vector<LocalId> index_locals;
    if (is_multi_dim) {
        // 多次元: 全インデックスを収集
        for (const auto& idx_expr : index_expr.indices) {
            index_locals.push_back(lower_expression(*idx_expr, ctx));
        }
    } else {
        // 単一インデックス（後方互換性）
        index_locals.push_back(lower_expression(*index_expr.index, ctx));
    }

    // 要素型を取得（最内側の要素型まで辿る）
    hir::TypePtr elem_type = hir::make_int();  // デフォルト
    bool is_slice = false;
    hir::TypePtr current_type = nullptr;

    if (index_expr.object && index_expr.object->type) {
        current_type = index_expr.object->type;
        // 多次元配列またはポインタの場合、インデックス数の深さまで要素型を辿る
        for (size_t i = 0; i < index_locals.size() && current_type; ++i) {
            if (current_type->kind == hir::TypeKind::Array) {
                is_slice = !current_type->array_size.has_value();
                if (current_type->element_type) {
                    current_type = current_type->element_type;
                } else {
                    break;
                }
            } else if (current_type->kind == hir::TypeKind::Pointer) {
                // ポインタ型の場合も要素型を取得（ptr[i]アクセス）
                if (current_type->element_type) {
                    current_type = current_type->element_type;
                } else {
                    break;
                }
            } else {
                break;
            }
        }
        elem_type = current_type ? current_type : hir::make_int();
    }

    // フォールバック: HIR型情報がnullの場合、MIRローカル変数の型から判定
    if (!is_slice && array < ctx.func->locals.size()) {
        hir::TypePtr array_type = ctx.func->locals[array].type;
        if (array_type && (array_type->kind == hir::TypeKind::Array ||
                           array_type->kind == hir::TypeKind::Pointer)) {
            current_type = array_type;
            for (size_t i = 0; i < index_locals.size() && current_type; ++i) {
                if (current_type->kind == hir::TypeKind::Array) {
                    is_slice = !current_type->array_size.has_value();
                    if (current_type->element_type) {
                        current_type = current_type->element_type;
                    } else {
                        break;
                    }
                } else if (current_type->kind == hir::TypeKind::Pointer) {
                    // ポインタ型の場合も要素型を取得
                    if (current_type->element_type) {
                        current_type = current_type->element_type;
                    } else {
                        break;
                    }
                } else {
                    break;
                }
            }
            elem_type = current_type ? current_type : hir::make_int();
        }
    }

    LocalId result = ctx.new_temp(elem_type);

    // スライスの場合は関数呼び出しを生成（多次元は非対応）
    if (is_slice && index_locals.size() == 1) {
        // 要素型が配列の場合（多次元スライス）はサブスライスを取得
        bool is_multidim = elem_type && elem_type->kind == hir::TypeKind::Array;

        std::string get_func = "cm_slice_get_i32";
        if (is_multidim) {
            get_func = "cm_slice_get_subslice";
        } else if (elem_type) {
            auto elem_kind = elem_type->kind;
            if (elem_kind == hir::TypeKind::Char || elem_kind == hir::TypeKind::Bool ||
                elem_kind == hir::TypeKind::Tiny || elem_kind == hir::TypeKind::UTiny) {
                get_func = "cm_slice_get_i8";
            } else if (elem_kind == hir::TypeKind::Long || elem_kind == hir::TypeKind::ULong) {
                get_func = "cm_slice_get_i64";
            } else if (elem_kind == hir::TypeKind::Double) {
                get_func = "cm_slice_get_f64";
            } else if (elem_kind == hir::TypeKind::Float) {
                get_func = "cm_slice_get_f32";
            } else if (elem_kind == hir::TypeKind::Pointer || elem_kind == hir::TypeKind::String ||
                       elem_kind == hir::TypeKind::Struct) {
                get_func = "cm_slice_get_ptr";
            }
        }

        BlockId success_block = ctx.new_block();
        std::vector<MirOperandPtr> args;
        args.push_back(MirOperand::copy(MirPlace{array}));
        args.push_back(MirOperand::copy(MirPlace{index_locals[0]}));

        auto call_term = std::make_unique<MirTerminator>();
        call_term->kind = MirTerminator::Call;
        call_term->data = MirTerminator::CallData{MirOperand::function_ref(get_func),
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

    // 通常の配列インデックス（単一または多次元）
    // 多次元配列最適化: 連続するIndex projectionを生成
    // a[i][j][k] → place.projections = [Index(i), Index(j), Index(k)]
    MirPlace place{array};
    for (LocalId idx_local : index_locals) {
        place.projections.push_back(PlaceProjection::index(idx_local));
    }

    ctx.push_statement(
        MirStatement::assign(MirPlace{result}, MirRvalue::use(MirOperand::copy(place))));

    return result;
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

// 構造体リテラルのlowering
LocalId ExprLowering::lower_struct_literal(const hir::HirStructLiteral& lit, LoweringContext& ctx) {
    debug_msg("MIR", "Lowering struct literal: " + lit.type_name);

    // 構造体の型を作成
    hir::TypePtr struct_type = std::make_shared<hir::Type>(hir::TypeKind::Struct);
    struct_type->name = lit.type_name;

    // 結果用の変数を作成
    LocalId result = ctx.new_temp(struct_type);

    // 構造体定義を取得
    const hir::HirStruct* struct_def = nullptr;
    if (ctx.struct_defs && ctx.struct_defs->count(lit.type_name)) {
        struct_def = ctx.struct_defs->at(lit.type_name);
    }

    // 各フィールドを初期化（名前付き初期化のみ）
    for (const auto& field : lit.fields) {
        // フィールドインデックスを名前から検索
        size_t field_idx = 0;
        hir::TypePtr field_type = nullptr;
        if (struct_def) {
            auto idx = ctx.get_field_index(lit.type_name, field.name);
            if (idx) {
                field_idx = *idx;
                if (*idx < struct_def->fields.size()) {
                    field_type = struct_def->fields[*idx].type;
                }
            }
        }

        // スライスフィールドへの配列リテラル代入をチェック
        bool is_slice_field = field_type && field_type->kind == hir::TypeKind::Array &&
                              !field_type->array_size.has_value();

        bool is_array_literal = false;
        const hir::HirArrayLiteral* arr_lit = nullptr;
        if (auto* arr_lit_ptr =
                std::get_if<std::unique_ptr<hir::HirArrayLiteral>>(&field.value->kind)) {
            if (*arr_lit_ptr) {
                is_array_literal = true;
                arr_lit = arr_lit_ptr->get();
            }
        }

        LocalId field_value;

        if (is_slice_field && is_array_literal && arr_lit) {
            // 配列リテラルからスライスを作成
            hir::TypePtr elem_type =
                field_type->element_type ? field_type->element_type : hir::make_int();

            // 要素サイズを取得
            int64_t elem_size = 4;  // デフォルトはint
            auto elem_kind = elem_type->kind;
            if (elem_kind == hir::TypeKind::Char || elem_kind == hir::TypeKind::Bool ||
                elem_kind == hir::TypeKind::Tiny || elem_kind == hir::TypeKind::UTiny) {
                elem_size = 1;
            } else if (elem_kind == hir::TypeKind::Short || elem_kind == hir::TypeKind::UShort) {
                elem_size = 2;
            } else if (elem_kind == hir::TypeKind::Long || elem_kind == hir::TypeKind::ULong ||
                       elem_kind == hir::TypeKind::Double) {
                elem_size = 8;
            } else if (elem_kind == hir::TypeKind::Float) {
                elem_size = 4;
            } else if (elem_kind == hir::TypeKind::Pointer || elem_kind == hir::TypeKind::String ||
                       elem_kind == hir::TypeKind::Struct) {
                elem_size = 8;
            }

            // スライス用の一時変数を作成
            field_value = ctx.new_temp(field_type);

            // cm_slice_new(elem_size, initial_capacity) を呼び出し
            LocalId elem_size_local = ctx.new_temp(hir::make_long());
            MirConstant elem_size_const;
            elem_size_const.value = static_cast<int64_t>(elem_size);
            elem_size_const.type = hir::make_long();
            ctx.push_statement(MirStatement::assign(
                MirPlace{elem_size_local}, MirRvalue::use(MirOperand::constant(elem_size_const))));

            LocalId init_cap_local = ctx.new_temp(hir::make_long());
            MirConstant init_cap_const;
            init_cap_const.value = static_cast<int64_t>(arr_lit->elements.size());
            init_cap_const.type = hir::make_long();
            ctx.push_statement(MirStatement::assign(
                MirPlace{init_cap_local}, MirRvalue::use(MirOperand::constant(init_cap_const))));

            // cm_slice_new呼び出し
            BlockId new_block = ctx.new_block();
            std::vector<MirOperandPtr> new_args;
            new_args.push_back(MirOperand::copy(MirPlace{elem_size_local}));
            new_args.push_back(MirOperand::copy(MirPlace{init_cap_local}));

            auto new_term = std::make_unique<MirTerminator>();
            new_term->kind = MirTerminator::Call;
            new_term->data = MirTerminator::CallData{MirOperand::function_ref("cm_slice_new"),
                                                     std::move(new_args),
                                                     MirPlace{field_value},
                                                     new_block,
                                                     std::nullopt,
                                                     "",
                                                     "",
                                                     false};
            ctx.set_terminator(std::move(new_term));
            ctx.switch_to_block(new_block);

            // push関数名を決定
            std::string push_func = "cm_slice_push_i32";
            if (elem_kind == hir::TypeKind::Char || elem_kind == hir::TypeKind::Bool ||
                elem_kind == hir::TypeKind::Tiny || elem_kind == hir::TypeKind::UTiny) {
                push_func = "cm_slice_push_i8";
            } else if (elem_kind == hir::TypeKind::Long || elem_kind == hir::TypeKind::ULong) {
                push_func = "cm_slice_push_i64";
            } else if (elem_kind == hir::TypeKind::Double) {
                push_func = "cm_slice_push_f64";
            } else if (elem_kind == hir::TypeKind::Float) {
                push_func = "cm_slice_push_f32";
            } else if (elem_kind == hir::TypeKind::Pointer || elem_kind == hir::TypeKind::String ||
                       elem_kind == hir::TypeKind::Struct) {
                push_func = "cm_slice_push_ptr";
            }

            // 各要素をpushで追加
            for (const auto& elem : arr_lit->elements) {
                LocalId elem_value = lower_expression(*elem, ctx);

                BlockId success_block = ctx.new_block();
                std::vector<MirOperandPtr> push_args;
                push_args.push_back(MirOperand::copy(MirPlace{field_value}));
                push_args.push_back(MirOperand::copy(MirPlace{elem_value}));

                auto call_term = std::make_unique<MirTerminator>();
                call_term->kind = MirTerminator::Call;
                call_term->data = MirTerminator::CallData{MirOperand::function_ref(push_func),
                                                          std::move(push_args),
                                                          std::nullopt,
                                                          success_block,
                                                          std::nullopt,
                                                          "",
                                                          "",
                                                          false};
                ctx.set_terminator(std::move(call_term));
                ctx.switch_to_block(success_block);
            }
        } else {
            // 通常の処理
            field_value = lower_expression(*field.value, ctx);
        }

        // フィールドへの代入を生成
        MirPlace place{result};
        place.projections.push_back(PlaceProjection::field(field_idx));

        ctx.push_statement(
            MirStatement::assign(place, MirRvalue::use(MirOperand::copy(MirPlace{field_value}))));
    }

    return result;
}

// 配列リテラル
LocalId ExprLowering::lower_array_literal(const hir::HirArrayLiteral& lit,
                                          const hir::TypePtr& expected_type, LoweringContext& ctx) {
    debug_msg("MIR",
              "Lowering array literal with " + std::to_string(lit.elements.size()) + " elements");

    // 要素の型を推論（期待される型があればそれを使用）
    hir::TypePtr elem_type = hir::make_int();  // デフォルト
    if (expected_type && expected_type->kind == hir::TypeKind::Array &&
        expected_type->element_type) {
        // 期待される配列型から要素型を取得
        elem_type = expected_type->element_type;
    } else if (!lit.elements.empty() && lit.elements[0]->type) {
        // フォールバック: 最初の要素の型を使用
        elem_type = lit.elements[0]->type;
    }

    // 配列型を作成
    hir::TypePtr array_type = hir::make_array(elem_type, lit.elements.size());

    // 結果用の変数を作成
    LocalId result = ctx.new_temp(array_type);

    // 各要素を初期化
    for (size_t i = 0; i < lit.elements.size(); ++i) {
        LocalId elem_value = lower_expression(*lit.elements[i], ctx);

        // 要素の型が期待される型と異なる場合、型変換が必要
        hir::TypePtr actual_elem_type = nullptr;
        if (elem_value < ctx.func->locals.size()) {
            actual_elem_type = ctx.func->locals[elem_value].type;
        }

        // 型変換が必要かチェック
        bool needs_cast = false;
        if (actual_elem_type && elem_type) {
            // floatとdoubleの変換
            if ((elem_type->kind == hir::TypeKind::Float &&
                 actual_elem_type->kind == hir::TypeKind::Double) ||
                (elem_type->kind == hir::TypeKind::Double &&
                 actual_elem_type->kind == hir::TypeKind::Float)) {
                needs_cast = true;
            }
            // intとlongの変換など
            else if (elem_type->kind != actual_elem_type->kind) {
                needs_cast = true;
            }
        }

        // 型変換が必要な場合はcastを挿入
        if (needs_cast) {
            LocalId casted = ctx.new_temp(elem_type);
            ctx.push_statement(MirStatement::assign(
                MirPlace{casted},
                MirRvalue::cast(MirOperand::copy(MirPlace{elem_value}), elem_type)));
            elem_value = casted;
        }

        // インデックス用の定数を変数に格納
        LocalId idx_local = ctx.new_temp(hir::make_int());
        MirConstant idx_const;
        idx_const.value = static_cast<int64_t>(i);
        idx_const.type = hir::make_int();
        ctx.push_statement(MirStatement::assign(MirPlace{idx_local},
                                                MirRvalue::use(MirOperand::constant(idx_const))));

        // 配列要素への代入を生成
        MirPlace place{result};
        place.projections.push_back(PlaceProjection::index(idx_local));

        ctx.push_statement(
            MirStatement::assign(place, MirRvalue::use(MirOperand::copy(MirPlace{elem_value}))));
    }

    return result;
}

// 値を文字列に変換するヘルパー（文字列連結用）
LocalId ExprLowering::convert_to_string(LocalId value, const hir::TypePtr& type,
                                        LoweringContext& ctx) {
    // 変換関数名を型に基づいて決定
    std::string conv_func;
    if (!type) {
        conv_func = "cm_int_to_string";  // デフォルト
    } else {
        switch (type->kind) {
            case hir::TypeKind::Int:
            case hir::TypeKind::Short:
            case hir::TypeKind::Long:
            case hir::TypeKind::Tiny:
                conv_func = "cm_int_to_string";
                break;
            case hir::TypeKind::UInt:
            case hir::TypeKind::UShort:
            case hir::TypeKind::ULong:
            case hir::TypeKind::UTiny:
                conv_func = "cm_uint_to_string";
                break;
            case hir::TypeKind::Float:
            case hir::TypeKind::Double:
                conv_func = "cm_double_to_string";
                break;
            case hir::TypeKind::Bool:
                conv_func = "cm_bool_to_string";
                break;
            case hir::TypeKind::Char:
                conv_func = "cm_char_to_string";
                break;
            case hir::TypeKind::String:
                // 既に文字列なので変換不要
                return value;
            default:
                conv_func = "cm_int_to_string";  // フォールバック
                break;
        }
    }

    // 変換関数を呼び出す
    LocalId str_result = ctx.new_temp(hir::make_string());
    std::vector<MirOperandPtr> conv_args;
    conv_args.push_back(MirOperand::copy(MirPlace{value}));

    BlockId conv_success = ctx.new_block();

    auto conv_func_operand = MirOperand::function_ref(conv_func);

    auto conv_call_term = std::make_unique<MirTerminator>();
    conv_call_term->kind = MirTerminator::Call;
    conv_call_term->data = MirTerminator::CallData{std::move(conv_func_operand),
                                                   std::move(conv_args),
                                                   MirPlace{str_result},
                                                   conv_success,
                                                   std::nullopt,
                                                   "",
                                                   "",
                                                   false};  // 通常の関数呼び出し
    ctx.set_terminator(std::move(conv_call_term));
    ctx.switch_to_block(conv_success);

    return str_result;
}

// キャスト式のlowering
LocalId ExprLowering::lower_cast(const hir::HirCast& cast, LoweringContext& ctx) {
    debug_msg("MIR", "Lowering cast expression");

    // オペランドをlowering
    LocalId operand = lower_expression(*cast.operand, ctx);

    // ターゲット型で結果変数を作成
    LocalId result = ctx.new_temp(cast.target_type);

    // キャスト命令を生成
    ctx.push_statement(MirStatement::assign(
        MirPlace{result}, MirRvalue::cast(MirOperand::copy(MirPlace{operand}), cast.target_type)));

    return result;
}

// enum バリアントコンストラクタのlowering（v0.13.0）
LocalId ExprLowering::lower_enum_construct(const hir::HirEnumConstruct& ec, LoweringContext& ctx) {
    debug_msg("MIR", "Lowering enum construct: " + ec.enum_name + "::" + ec.variant_name);

    // enum型を作成
    hir::TypePtr enum_type = std::make_shared<hir::Type>(hir::TypeKind::Struct);
    enum_type->name = ec.enum_name;

    // 結果用の変数を作成
    LocalId result = ctx.new_temp(enum_type);

    // 引数を lowering
    std::vector<MirOperandPtr> operands;
    for (const auto& arg : ec.args) {
        LocalId arg_local = lower_expression(*arg, ctx);
        operands.push_back(MirOperand::copy(MirPlace{arg_local}));
    }

    // Aggregate (Enum) Rvalue を生成
    AggregateKind kind;
    kind.type = AggregateKind::Enum;
    kind.name = ec.enum_name;
    kind.variant_name = ec.variant_name;
    kind.tag = ec.tag;
    kind.ty = enum_type;

    auto rv = std::make_unique<MirRvalue>();
    rv->kind = MirRvalue::Aggregate;
    rv->data = MirRvalue::AggregateData{kind, std::move(operands)};

    ctx.push_statement(MirStatement::assign(MirPlace{result}, std::move(rv)));

    return result;
}

}  // namespace cm::mir
