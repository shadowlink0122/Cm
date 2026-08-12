// ============================================================
// HIR lowering - リテラル式（基本・構造体・配列・ラムダ）
// ============================================================

#include "internal/hir/lowering/fwd.hpp"

#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace cm::hir {

// リテラル
HirExprPtr HirLowering::lower_literal(ast::LiteralExpr& lit, TypePtr type) {
    debug::hir::log(debug::hir::Id::LiteralLower, "", debug::Level::Trace);

    std::visit(
        [](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, int64_t>) {
                debug::hir::log(debug::hir::Id::IntLiteral, std::to_string(arg),
                                debug::Level::Trace);
            } else if constexpr (std::is_same_v<T, double>) {
                debug::hir::log(debug::hir::Id::FloatLiteral, std::to_string(arg),
                                debug::Level::Trace);
            } else if constexpr (std::is_same_v<T, std::string>) {
                debug::hir::log(debug::hir::Id::StringLiteral, "\"" + arg + "\"",
                                debug::Level::Trace);
            } else if constexpr (std::is_same_v<T, bool>) {
                debug::hir::log(debug::hir::Id::BoolLiteral, arg ? "true" : "false",
                                debug::Level::Trace);
            } else if constexpr (std::is_same_v<T, char>) {
                debug::hir::log(debug::hir::Id::CharLiteral, std::string(1, arg),
                                debug::Level::Trace);
            } else if constexpr (std::is_same_v<T, std::monostate>) {
                debug::hir::log(debug::hir::Id::NullLiteral, "null", debug::Level::Trace);
            }
        },
        lit.value);

    auto hir_lit = std::make_unique<HirLiteral>();
    hir_lit->value = lit.value;
    hir_lit->bit_info = lit.bit_info;  // SV幅付きリテラル伝搬
    // 型検査で脱糖済みの補間部分式をHIRへ下ろす（第4段b）。MIRはテキスト再パースせずこの式を消費する
    for (auto& [content, part_expr] : lit.interp_parts) {
        if (part_expr) {
            hir_lit->interp_parts.emplace_back(content,
                                               std::shared_ptr<HirExpr>(lower_expr(*part_expr)));
        }
    }
    return std::make_unique<HirExpr>(std::move(hir_lit), type);
}

// 構造体リテラル
HirExprPtr HirLowering::lower_struct_literal(ast::StructLiteralExpr& lit, TypePtr expected_type) {
    std::string type_name = lit.type_name;

    if (type_name.empty() && expected_type) {
        if (expected_type->kind == ast::TypeKind::Struct && !expected_type->name.empty()) {
            type_name = expected_type->name;
            debug::hir::log(debug::hir::Id::LiteralLower,
                            "Inferred struct type from context: " + type_name, debug::Level::Debug);
        }
    }

    debug::hir::log(debug::hir::Id::LiteralLower,
                    "Lowering struct literal: " + type_name + " expected " +
                        (expected_type ? ast::type_to_string(*expected_type) : std::string("null")),
                    debug::Level::Debug);

    auto hir_lit = std::make_unique<HirStructLiteral>();
    hir_lit->type_name = type_name;

    TypePtr struct_type = std::make_shared<ast::Type>(ast::TypeKind::Struct);
    struct_type->name = type_name;

    // ジェネリック特殊化別名（typedef IntPair = Pair<int,int>;）のリテラルは型検査が型引数付きの基底型を返すため、型引数を保持したままMIRへ伝播する（B8）
    if (expected_type && expected_type->kind == ast::TypeKind::Struct &&
        expected_type->name == type_name && !expected_type->type_args.empty()) {
        struct_type = expected_type;
    }

    const ast::StructDecl* struct_def = nullptr;
    if (!type_name.empty()) {
        auto struct_it = struct_defs_.find(type_name);
        if (struct_it != struct_defs_.end()) {
            struct_def = struct_it->second;
        }
    }

    for (auto& field : lit.fields) {
        HirStructLiteralField hir_field;
        hir_field.name = field.name;

        if (struct_def) {
            for (auto& def_field : struct_def->fields) {
                if (def_field.name == field.name) {
                    if (auto* nested_lit = field.value->as<ast::StructLiteralExpr>()) {
                        if (nested_lit->type_name.empty() && def_field.type &&
                            def_field.type->kind == ast::TypeKind::Struct) {
                            nested_lit->type_name = def_field.type->name;
                            debug::hir::log(
                                debug::hir::Id::LiteralLower,
                                "Propagated type to nested struct: " + def_field.type->name,
                                debug::Level::Debug);
                        }
                    }
                    break;
                }
            }
        }

        hir_field.value = lower_expr(*field.value);
        hir_lit->fields.push_back(std::move(hir_field));
    }

    return std::make_unique<HirExpr>(std::move(hir_lit), struct_type);
}

// 配列リテラル
HirExprPtr HirLowering::lower_array_literal(ast::ArrayLiteralExpr& lit, TypePtr expected_type) {
    debug::hir::log(
        debug::hir::Id::LiteralLower,
        "Lowering array literal with " + std::to_string(lit.elements.size()) + " elements",
        debug::Level::Debug);

    auto hir_lit = std::make_unique<HirArrayLiteral>();

    TypePtr expected_elem_type = nullptr;
    if (expected_type && expected_type->kind == ast::TypeKind::Array &&
        expected_type->element_type) {
        expected_elem_type = expected_type->element_type;
        debug::hir::log(debug::hir::Id::LiteralLower,
                        "Using expected element type: " + expected_elem_type->name,
                        debug::Level::Debug);
    }

    TypePtr elem_type = expected_elem_type;
    for (auto& elem : lit.elements) {
        if (expected_elem_type && expected_elem_type->kind == ast::TypeKind::Struct) {
            if (auto* nested_lit = elem->as<ast::StructLiteralExpr>()) {
                if (nested_lit->type_name.empty()) {
                    nested_lit->type_name = expected_elem_type->name;
                    debug::hir::log(
                        debug::hir::Id::LiteralLower,
                        "Propagated type to array element struct: " + expected_elem_type->name,
                        debug::Level::Debug);
                }
            }
        }

        auto lowered_elem = lower_expr(*elem);
        if (!elem_type) {
            elem_type = lowered_elem->type;
        }
        hir_lit->elements.push_back(std::move(lowered_elem));
    }

    if (!elem_type) {
        elem_type = hir::make_int();
    }

    TypePtr array_type = hir::make_array(elem_type, lit.elements.size());

    return std::make_unique<HirExpr>(std::move(hir_lit), array_type);
}

// ラムダ式
HirExprPtr HirLowering::lower_lambda(ast::LambdaExpr& lambda, TypePtr expected_type) {
    debug::hir::log(debug::hir::Id::ExprLower,
                    "Lowering lambda with " + std::to_string(lambda.params.size()) + " params" +
                        ", captures: " + std::to_string(lambda.captures.size()),
                    debug::Level::Debug);

    // パラメータを変換
    // 型が指定されていない場合、expected_typeから推論
    TypePtr return_type = nullptr;
    std::vector<TypePtr> param_types;

    if (expected_type && expected_type->kind == ast::TypeKind::Function) {
        return_type = expected_type->return_type;
        param_types = expected_type->param_types;
    }

    // 一意な名前を生成
    static int lambda_counter = 0;
    std::string lambda_name = "__lambda_" + std::to_string(lambda_counter++);

    // ラムダを関数として生成
    auto hir_func = std::make_unique<HirFunction>();
    hir_func->name = lambda_name;

    // キャプチャされた変数を最初のパラメータとして追加
    for (const auto& cap : lambda.captures) {
        HirParam cap_param;
        cap_param.name = cap.name;
        cap_param.type = cap.type;
        hir_func->params.push_back(std::move(cap_param));

        debug::hir::log(debug::hir::Id::ExprLower, "Lambda capture param: " + cap.name,
                        debug::Level::Debug);
    }

    for (size_t i = 0; i < lambda.params.size(); ++i) {
        HirParam param;
        param.name = lambda.params[i].name;

        // パラメータの型を決定
        if (lambda.params[i].type) {
            param.type = lambda.params[i].type;
        } else if (i < param_types.size()) {
            param.type = param_types[i];
        } else {
            param.type = hir::make_int();  // デフォルトはint
        }

        hir_func->params.push_back(std::move(param));
    }

    // 戻り値型
    hir_func->return_type = lambda.return_type ? lambda.return_type : return_type;
    if (!hir_func->return_type) {
        hir_func->return_type = hir::make_int();  // デフォルトはint
    }

    // ボディを変換
    if (lambda.is_expr_body()) {
        // 式本体の場合、returnに変換
        auto& body_expr = std::get<ast::ExprPtr>(lambda.body);
        auto hir_expr = lower_expr(*body_expr);

        auto ret = std::make_unique<HirReturn>();
        ret->value = std::move(hir_expr);
        auto ret_stmt = std::make_unique<HirStmt>(std::move(ret));
        hir_func->body.push_back(std::move(ret_stmt));
    } else {
        // ブロック本体の場合
        auto& body_stmts = std::get<std::vector<ast::StmtPtr>>(lambda.body);
        for (auto& stmt : body_stmts) {
            auto hir_stmt = lower_stmt(*stmt);
            if (hir_stmt) {
                hir_func->body.push_back(std::move(hir_stmt));
            }
        }
    }

    // ラムダ関数をリストに追加（後でプログラムに追加される）
    lambda_functions_.push_back(std::move(hir_func));

    // 関数ポインタ型を作成（キャプチャを含まない元の型）
    std::vector<TypePtr> hir_param_types;
    for (size_t i = lambda.captures.size(); i < lambda_functions_.back()->params.size(); ++i) {
        hir_param_types.push_back(lambda_functions_.back()->params[i].type);
    }
    TypePtr lambda_type =
        hir::make_function_ptr(lambda_functions_.back()->return_type, hir_param_types);

    debug::hir::log(debug::hir::Id::ExprLower, "Lambda lowered as function: " + lambda_name,
                    debug::Level::Debug);

    // キャプチャがある場合はクロージャ情報を持つ関数参照を生成
    if (!lambda.captures.empty()) {
        // クロージャ呼び出し用の特殊な参照を生成
        auto var_ref = std::make_unique<HirVarRef>();
        var_ref->name = lambda_name;
        var_ref->is_function_ref = true;
        var_ref->is_closure = true;

        // キャプチャ変数をコピー
        for (const auto& cap : lambda.captures) {
            HirVarRef::CapturedVar cv;
            cv.name = cap.name;
            cv.type = cap.type;
            var_ref->captured_vars.push_back(cv);
        }

        return std::make_unique<HirExpr>(std::move(var_ref), lambda_type);
    }

    // 関数参照を返す
    auto var_ref = std::make_unique<HirVarRef>();
    var_ref->name = lambda_name;
    var_ref->is_function_ref = true;

    return std::make_unique<HirExpr>(std::move(var_ref), lambda_type);
}

}  // namespace cm::hir
