// expr_lowering_basic.cpp - 基本式のlowering
// lower_literal, lower_var_ref, lower_member, lower_index,
// lower_ternary, lower_struct_literal, lower_array_literal, convert_to_string

#include "../../common/debug.hpp"
#include "expr_lowering.hpp"

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
    LocalId object = lower_expression(*current, ctx);
    hir::TypePtr obj_type = current->type;

    // 型が未設定の場合、ローカル変数から推論
    if (!obj_type || obj_type->kind != hir::TypeKind::Struct) {
        if (object < ctx.func->locals.size()) {
            obj_type = ctx.func->locals[object].type;
        }
    }

    if (!obj_type || obj_type->kind != hir::TypeKind::Struct) {
        debug_msg("MIR",
                  "Error: Member access on non-struct type for member '" + member.member + "'");
        return ctx.new_temp(hir::make_error());
    }

    // フィールドチェーンを逆順にしてプロジェクションを構築
    MirPlace place{object};
    hir::TypePtr current_type = obj_type;

    for (auto it = field_chain.rbegin(); it != field_chain.rend(); ++it) {
        const std::string& field_name = it->second;

        if (!current_type || current_type->kind != hir::TypeKind::Struct) {
            debug_msg("MIR", "Error: Non-struct type in member chain");
            return ctx.new_temp(hir::make_error());
        }

        auto field_idx = ctx.get_field_index(current_type->name, field_name);
        if (!field_idx) {
            debug_msg("MIR", "Error: Field '" + field_name + "' not found in struct '" +
                                 current_type->name + "'");
            return ctx.new_temp(hir::make_error());
        }

        place.projections.push_back(PlaceProjection::field(*field_idx));

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

    // 最終的なフィールドの型で一時変数を作成
    LocalId result = ctx.new_temp(current_type);

    ctx.push_statement(
        MirStatement::assign(MirPlace{result}, MirRvalue::use(MirOperand::copy(place))));

    return result;
}

// 配列インデックスのlowering
LocalId ExprLowering::lower_index(const hir::HirIndex& index_expr, LoweringContext& ctx) {
    // オブジェクトとインデックスをlowering
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

    LocalId index = lower_expression(*index_expr.index, ctx);

    // 要素型を取得
    hir::TypePtr elem_type = hir::make_int();  // デフォルト
    if (index_expr.object && index_expr.object->type &&
        index_expr.object->type->kind == hir::TypeKind::Array &&
        index_expr.object->type->element_type) {
        elem_type = index_expr.object->type->element_type;
    }

    LocalId result = ctx.new_temp(elem_type);

    MirPlace place{array};
    place.projections.push_back(PlaceProjection::index(index));

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
        // フィールド値をlower
        LocalId field_value = lower_expression(*field.value, ctx);

        // フィールドインデックスを名前から検索
        size_t field_idx = 0;
        if (struct_def) {
            auto idx = ctx.get_field_index(lit.type_name, field.name);
            if (idx) {
                field_idx = *idx;
            }
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

}  // namespace cm::mir
