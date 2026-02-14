// パーサ - 宣言パース（トップレベル、関数、構造体、インターフェース、impl）
#include "../../common/debug/par.hpp"
#include "parser.hpp"

namespace cm {

// プログラム全体を解析
ast::Program Parser::parse() {
    debug::par::log(debug::par::Id::Start);

    ast::Program program;
    int iterations = 0;
    const int MAX_ITERATIONS = 10000;
    size_t last_pos = pos_;

    while (!is_at_end() && iterations < MAX_ITERATIONS) {
        // 無限ループ検出
        if (pos_ == last_pos && iterations > 0) {
            error("Parser stuck - no progress made");
            if (!is_at_end()) {
                advance();  // 強制的に進める
            }
        }
        last_pos = pos_;

        if (auto decl = parse_top_level()) {
            program.declarations.push_back(std::move(decl));
        } else {
            synchronize();
        }
        iterations++;
    }

    if (iterations >= MAX_ITERATIONS) {
        error("Parser exceeded maximum iteration limit");
    }

    debug::par::log(debug::par::Id::End,
                    std::to_string(program.declarations.size()) + " declarations");
    return program;
}

// トップレベル宣言
ast::DeclPtr Parser::parse_top_level() {
    // アトリビュート（#[...]) を収集
    std::vector<ast::AttributeNode> attrs;
    while (is_attribute_start()) {
        attrs.push_back(parse_attribute());
    }

    // @[macro]の場合 (廃止予定 - #macroを使用してください)
    for (const auto& attr : attrs) {
        if (attr.name == "macro") {
            return nullptr;  // @[macro]はサポートしない
        }
    }

    // module宣言
    if (check(TokenKind::KwModule)) {
        return parse_module();
    }

    // namespace宣言
    if (check(TokenKind::KwNamespace)) {
        return parse_namespace();
    }

    // import
    if (check(TokenKind::KwImport)) {
        return parse_import_stmt(std::move(attrs));
    }

    // use
    if (check(TokenKind::KwUse)) {
        return parse_use(std::move(attrs));
    }

    // export (v4: 宣言時エクスポートと分離エクスポートの両方をサポート)
    if (check(TokenKind::KwExport)) {
        // 次のトークンを先読み
        auto saved_pos = pos_;
        advance();  // consume 'export'

        // export struct, export interface, export enum, export typedef, export const
        if (check(TokenKind::KwStruct)) {
            return parse_struct(true, std::move(attrs));
        }
        if (check(TokenKind::KwInterface)) {
            return parse_interface(true, std::move(attrs));
        }
        if (check(TokenKind::KwEnum)) {
            return parse_enum_decl(true, std::move(attrs));
        }
        if (check(TokenKind::KwTypedef)) {
            return parse_typedef_decl(true, std::move(attrs));
        }
        if (check(TokenKind::KwConst)) {
            return parse_const_decl(true, std::move(attrs));
        }
        if (check(TokenKind::KwImpl)) {
            return parse_impl_export(std::move(attrs));
        }
        // v0.13.0: export macro
        if (check(TokenKind::KwMacro)) {
            return parse_macro(true);
        }

        // export function (型から始まる関数、または修飾子から始まる関数の場合)
        // 修飾子: static, inline, async
        if (is_type_start() || check(TokenKind::KwStatic) || check(TokenKind::KwInline) ||
            check(TokenKind::KwAsync)) {
            // 修飾子を収集
            bool is_static = consume_if(TokenKind::KwStatic);
            bool is_inline = consume_if(TokenKind::KwInline);
            bool is_async = consume_if(TokenKind::KwAsync);

            // グローバル変数判定（型 名前 = ... のパターン）
            if (!is_static && !is_inline && !is_async && is_global_var_start()) {
                return parse_global_var_decl(true, std::move(attrs));
            }

            return parse_function(true, is_static, is_inline, std::move(attrs), is_async);
        }

        // それ以外は分離エクスポート (export NAME1, NAME2;)
        if (!attrs.empty()) {
            error("Attributes are not supported on export lists");
        }
        pos_ = saved_pos;
        return parse_export();
    }

    // extern
    if (check(TokenKind::KwExtern)) {
        return parse_extern(std::move(attrs));
    }

    // 修飾子を収集
    bool is_static = consume_if(TokenKind::KwStatic);
    bool is_inline = consume_if(TokenKind::KwInline);
    bool is_async = consume_if(TokenKind::KwAsync);

    // struct
    if (check(TokenKind::KwStruct)) {
        return parse_struct(false, std::move(attrs));
    }

    // interface
    if (check(TokenKind::KwInterface)) {
        return parse_interface(false, std::move(attrs));
    }

    // impl
    if (check(TokenKind::KwImpl)) {
        return parse_impl(std::move(attrs));
    }

    // template
    if (check(TokenKind::KwTemplate)) {
        return parse_template_decl();
    }

    // enum
    if (check(TokenKind::KwEnum)) {
        return parse_enum_decl(false, std::move(attrs));
    }

    // typedef
    if (check(TokenKind::KwTypedef)) {
        return parse_typedef_decl(false, std::move(attrs));
    }

    // const (v4: トップレベルconst宣言のサポート)
    if (check(TokenKind::KwConst)) {
        return parse_const_decl(false, std::move(attrs));
    }

    // #macro (新しいC++風マクロ構文)
    if (check(TokenKind::Hash)) {
        // #macroか他のディレクティブか確認
        auto saved_pos = pos_;
        advance();  // consume '#'

        if (check(TokenKind::KwMacro)) {
            return parse_macro(false);
        }

        // その他のディレクティブ（#test, #bench, #deprecated等）
        if (check(TokenKind::Ident)) {
            std::string directive_name = std::string(current().get_string());
            if (directive_name == "test" || directive_name == "bench" ||
                directive_name == "deprecated" || directive_name == "inline" ||
                directive_name == "optimize") {
                error("Directive '#" + directive_name + "' is not yet implemented");
                while (!is_at_end() && current().kind != TokenKind::Semicolon &&
                       current().kind != TokenKind::LBrace) {
                    advance();
                }
                return nullptr;
            }
        }

        pos_ = saved_pos;
        error("Unknown or invalid directive after '#'");
        return nullptr;
    }

    // macro (v0.13.0: 型付きマクロ)
    if (check(TokenKind::KwMacro)) {
        return parse_macro(false);
    }

    // constexpr
    if (check(TokenKind::KwConstexpr)) {
        return parse_constexpr();
    }

    // グローバル変数判定（型 名前 = ... のパターン）
    if (!is_static && !is_inline && !is_async && is_global_var_start()) {
        return parse_global_var_decl(false, std::move(attrs));
    }

    // 関数 (型 名前 ...)
    return parse_function(false, is_static, is_inline, std::move(attrs), is_async);
}

// グローバル変数宣言かどうかを先読みで判定
bool Parser::is_global_var_start() {
    if (!is_type_start())
        return false;

    auto saved_pos = pos_;
    advance();

    while (!is_at_end() && check(TokenKind::Star)) {
        advance();
    }

    bool result = false;
    if (!is_at_end() && check(TokenKind::Ident)) {
        advance();
        if (!is_at_end() && check(TokenKind::Eq)) {
            result = true;
        }
    }

    pos_ = saved_pos;
    return result;
}

// 関数定義
ast::DeclPtr Parser::parse_function(bool is_export, bool is_static, bool is_inline,
                                    std::vector<ast::AttributeNode> attributes, bool is_async) {
    uint32_t start_pos = current().start;
    debug::par::log(debug::par::Id::FuncDef, "", debug::Level::Trace);

    // 明示的なジェネリックパラメータをチェック（例: <T> T max(T a, T b)）
    auto [generic_params, generic_params_v2] = parse_generic_params_v2();

    auto return_type = parse_type_with_union();

    // 名前のスパンを記録（Lint警告用）
    uint32_t name_start = current().start;
    std::string name = expect_ident();
    uint32_t name_end = previous().end;

    // main関数はエクスポート不可
    if (is_export && name == "main") {
        error("main関数はエクスポートできません");
    }

    expect(TokenKind::LParen);
    auto params = parse_params();
    expect(TokenKind::RParen);

    auto body = parse_block();

    auto func = std::make_unique<ast::FunctionDecl>(std::move(name), std::move(params),
                                                    std::move(return_type), std::move(body));

    // 名前のスパンを設定
    func->name_span = Span{name_start, name_end};

    // ジェネリックパラメータを設定（明示的に指定された場合）
    if (!generic_params.empty()) {
        func->generic_params = std::move(generic_params);
        func->generic_params_v2 = std::move(generic_params_v2);

        std::string params_str = "Function '" + func->name + "' has generic params: ";
        for (const auto& p : func->generic_params) {
            params_str += p + " ";
        }
        debug::par::log(debug::par::Id::FuncDef, params_str, debug::Level::Info);
    }

    func->visibility = is_export ? ast::Visibility::Export : ast::Visibility::Private;
    func->is_static = is_static;
    func->is_inline = is_inline;
    func->is_async = is_async;
    func->attributes = std::move(attributes);

    return std::make_unique<ast::Decl>(std::move(func), Span{start_pos, previous().end});
}

// パラメータリスト
std::vector<ast::Param> Parser::parse_params() {
    std::vector<ast::Param> params;
    bool has_default = false;

    if (!check(TokenKind::RParen)) {
        do {
            ast::Param param;
            param.qualifiers.is_const = consume_if(TokenKind::KwConst);
            param.type = parse_type_with_union();

            param.name = expect_ident();

            // デフォルト引数をパース
            if (consume_if(TokenKind::Eq)) {
                param.default_value = parse_expr();
                has_default = true;
            } else if (has_default) {
                error("Default argument required after parameter with default value");
            }

            params.push_back(std::move(param));
        } while (consume_if(TokenKind::Comma));
    }

    return params;
}

// 構造体
ast::DeclPtr Parser::parse_struct(bool is_export, std::vector<ast::AttributeNode> attributes) {
    uint32_t start_pos = current().start;
    debug::par::log(debug::par::Id::StructDef, "", debug::Level::Trace);

    expect(TokenKind::KwStruct);

    uint32_t name_start = current().start;
    std::string name = expect_ident();
    uint32_t name_end = previous().end;

    auto [generic_params, generic_params_v2] = parse_generic_params_v2();

    // with キーワード
    std::vector<std::string> auto_impls;
    if (consume_if(TokenKind::KwWith)) {
        do {
            auto_impls.push_back(expect_ident());
        } while (consume_if(TokenKind::Comma));
    }

    // where句をパース
    if (consume_if(TokenKind::KwWhere)) {
        do {
            std::string type_param = expect_ident();
            expect(TokenKind::Colon);

            std::vector<std::string> interfaces;
            interfaces.push_back(expect_ident());
            ast::ConstraintKind constraint_kind = ast::ConstraintKind::Single;

            if (check(TokenKind::Pipe)) {
                constraint_kind = ast::ConstraintKind::Or;
                while (consume_if(TokenKind::Pipe)) {
                    interfaces.push_back(expect_ident());
                }
            } else if (check(TokenKind::Plus)) {
                constraint_kind = ast::ConstraintKind::And;
                while (consume_if(TokenKind::Plus)) {
                    interfaces.push_back(expect_ident());
                }
            }

            for (auto& gp : generic_params_v2) {
                if (gp.name == type_param) {
                    gp.type_constraint = ast::TypeConstraint(constraint_kind, interfaces);
                    gp.constraints = interfaces;
                    break;
                }
            }
        } while (consume_if(TokenKind::Comma));
    }

    expect(TokenKind::LBrace);

    std::vector<ast::Field> fields;
    bool has_default_field = false;
    while (!check(TokenKind::RBrace) && !is_at_end()) {
        ast::Field field;

        field.visibility =
            consume_if(TokenKind::KwPrivate) ? ast::Visibility::Private : ast::Visibility::Export;

        if (consume_if(TokenKind::KwDefault)) {
            if (has_default_field) {
                error("Only one default member allowed per struct");
            }
            field.is_default = true;
            has_default_field = true;
        }

        field.qualifiers.is_const = consume_if(TokenKind::KwConst);

        if (check(TokenKind::RBrace)) {
            break;
        }

        field.type = parse_type_with_union();

        field.name = expect_ident();
        expect(TokenKind::Semicolon);
        fields.push_back(std::move(field));
    }

    expect(TokenKind::RBrace);

    auto decl = std::make_unique<ast::StructDecl>(std::move(name), std::move(fields));
    decl->name_span = Span{name_start, name_end};
    decl->visibility = is_export ? ast::Visibility::Export : ast::Visibility::Private;
    decl->auto_impls = std::move(auto_impls);
    decl->attributes = std::move(attributes);

    if (!generic_params.empty()) {
        decl->generic_params = std::move(generic_params);
        decl->generic_params_v2 = std::move(generic_params_v2);

        std::string params_str = "Struct '" + decl->name + "' has generic params: ";
        for (const auto& p : decl->generic_params) {
            params_str += p + " ";
        }
        debug::par::log(debug::par::Id::StructDef, params_str, debug::Level::Info);
    }

    return std::make_unique<ast::Decl>(std::move(decl), Span{start_pos, previous().end});
}

// 演算子の種類をパース
std::optional<ast::OperatorKind> Parser::parse_operator_kind() {
    if (check(TokenKind::EqEq)) {
        advance();
        return ast::OperatorKind::Eq;
    }
    if (check(TokenKind::BangEq)) {
        advance();
        return ast::OperatorKind::Ne;
    }
    if (check(TokenKind::Lt)) {
        advance();
        return ast::OperatorKind::Lt;
    }
    if (check(TokenKind::Gt)) {
        advance();
        return ast::OperatorKind::Gt;
    }
    if (check(TokenKind::LtEq)) {
        advance();
        return ast::OperatorKind::Le;
    }
    if (check(TokenKind::GtEq)) {
        advance();
        return ast::OperatorKind::Ge;
    }
    if (check(TokenKind::Plus)) {
        advance();
        return ast::OperatorKind::Add;
    }
    if (check(TokenKind::Minus)) {
        advance();
        return ast::OperatorKind::Sub;
    }
    if (check(TokenKind::Star)) {
        advance();
        return ast::OperatorKind::Mul;
    }
    if (check(TokenKind::Slash)) {
        advance();
        return ast::OperatorKind::Div;
    }
    if (check(TokenKind::Percent)) {
        advance();
        return ast::OperatorKind::Mod;
    }
    if (check(TokenKind::Amp)) {
        advance();
        return ast::OperatorKind::BitAnd;
    }
    if (check(TokenKind::Pipe)) {
        advance();
        return ast::OperatorKind::BitOr;
    }
    if (check(TokenKind::Caret)) {
        advance();
        return ast::OperatorKind::BitXor;
    }
    if (check(TokenKind::LtLt)) {
        advance();
        return ast::OperatorKind::Shl;
    }
    if (check(TokenKind::GtGt)) {
        advance();
        return ast::OperatorKind::Shr;
    }
    if (check(TokenKind::Tilde)) {
        advance();
        return ast::OperatorKind::BitNot;
    }
    if (check(TokenKind::Bang)) {
        advance();
        return ast::OperatorKind::Not;
    }
    return std::nullopt;
}

// インターフェース
ast::DeclPtr Parser::parse_interface(bool is_export, std::vector<ast::AttributeNode> attributes) {
    expect(TokenKind::KwInterface);

    std::string name = expect_ident();

    auto [generic_params, generic_params_v2] = parse_generic_params_v2();

    expect(TokenKind::LBrace);

    std::vector<ast::MethodSig> methods;
    std::vector<ast::OperatorSig> operators;

    while (!check(TokenKind::RBrace) && !is_at_end()) {
        if (check(TokenKind::KwOperator)) {
            advance();
            ast::OperatorSig op_sig;
            in_operator_return_type_ = true;
            op_sig.return_type = parse_type();
            in_operator_return_type_ = false;
            op_sig.return_type = check_array_suffix(std::move(op_sig.return_type));

            auto op_kind = parse_operator_kind();
            if (!op_kind) {
                error("Expected operator symbol after 'operator'");
                continue;
            }
            op_sig.op = *op_kind;

            expect(TokenKind::LParen);
            op_sig.params = parse_params();
            expect(TokenKind::RParen);
            expect(TokenKind::Semicolon);
            operators.push_back(std::move(op_sig));
        } else {
            ast::MethodSig sig;
            sig.return_type = parse_type_with_union();
            sig.return_type = check_array_suffix(std::move(sig.return_type));
            sig.name = expect_ident();
            expect(TokenKind::LParen);
            sig.params = parse_params();
            expect(TokenKind::RParen);
            expect(TokenKind::Semicolon);
            methods.push_back(std::move(sig));
        }
    }

    expect(TokenKind::RBrace);

    auto decl = std::make_unique<ast::InterfaceDecl>(std::move(name), std::move(methods));
    decl->operators = std::move(operators);
    decl->visibility = is_export ? ast::Visibility::Export : ast::Visibility::Private;
    decl->attributes = std::move(attributes);

    if (!generic_params.empty()) {
        decl->generic_params = std::move(generic_params);
        decl->generic_params_v2 = std::move(generic_params_v2);
    }

    return std::make_unique<ast::Decl>(std::move(decl));
}

// impl
ast::DeclPtr Parser::parse_impl(std::vector<ast::AttributeNode> attributes) {
    expect(TokenKind::KwImpl);

    std::vector<std::string> generic_params;
    std::vector<ast::GenericParam> generic_params_v2;
    if (check(TokenKind::Lt)) {
        auto [params, params_v2] = parse_generic_params_v2();
        generic_params = std::move(params);
        generic_params_v2 = std::move(params_v2);
    }

    auto target = parse_type();
    target = check_array_suffix(std::move(target));

    if (consume_if(TokenKind::KwFor)) {
        std::string iface = expect_ident();

        std::vector<ast::TypePtr> iface_type_args;
        if (check(TokenKind::Lt)) {
            advance();
            do {
                iface_type_args.push_back(parse_type());
            } while (consume_if(TokenKind::Comma));
            expect(TokenKind::Gt);
        }

        // where句をパース
        std::vector<ast::WhereClause> where_clauses;
        if (consume_if(TokenKind::KwWhere)) {
            do {
                std::string type_param = expect_ident();
                expect(TokenKind::Colon);

                std::vector<std::string> interfaces;
                interfaces.push_back(expect_ident());
                ast::ConstraintKind constraint_kind = ast::ConstraintKind::Single;

                if (check(TokenKind::Pipe)) {
                    constraint_kind = ast::ConstraintKind::Or;
                    while (consume_if(TokenKind::Pipe)) {
                        interfaces.push_back(expect_ident());
                    }
                } else if (check(TokenKind::Plus)) {
                    constraint_kind = ast::ConstraintKind::And;
                    while (consume_if(TokenKind::Plus)) {
                        interfaces.push_back(expect_ident());
                    }
                }

                ast::TypeConstraint constraint(constraint_kind, std::move(interfaces));
                where_clauses.emplace_back(std::move(type_param), std::move(constraint));
            } while (consume_if(TokenKind::Comma));
        }

        expect(TokenKind::LBrace);

        auto decl = std::make_unique<ast::ImplDecl>(std::move(iface), std::move(target));
        decl->interface_type_args = std::move(iface_type_args);
        decl->where_clauses = std::move(where_clauses);
        decl->attributes = std::move(attributes);

        if (!generic_params.empty()) {
            decl->generic_params = std::move(generic_params);
            decl->generic_params_v2 = std::move(generic_params_v2);
        }

        while (!check(TokenKind::RBrace) && !is_at_end()) {
            try {
                std::vector<ast::AttributeNode> method_attrs;
                while (is_attribute_start()) {
                    method_attrs.push_back(parse_attribute());
                }

                if (check(TokenKind::KwOperator)) {
                    advance();
                    auto op_impl = std::make_unique<ast::OperatorImpl>();
                    in_operator_return_type_ = true;
                    op_impl->return_type = parse_type();
                    in_operator_return_type_ = false;

                    auto op_kind = parse_operator_kind();
                    if (!op_kind) {
                        error("Expected operator symbol after 'operator'");
                        continue;
                    }
                    op_impl->op = *op_kind;

                    expect(TokenKind::LParen);
                    op_impl->params = parse_params();
                    expect(TokenKind::RParen);
                    op_impl->body = parse_block();
                    decl->operators.push_back(std::move(op_impl));
                } else {
                    bool is_private = consume_if(TokenKind::KwPrivate);
                    bool is_static = consume_if(TokenKind::KwStatic);

                    auto func = parse_function(false, is_static, false, std::move(method_attrs));
                    if (auto* f = func->as<ast::FunctionDecl>()) {
                        if (is_private) {
                            f->visibility = ast::Visibility::Private;
                        } else {
                            f->visibility = ast::Visibility::Export;
                        }
                        decl->methods.push_back(
                            std::unique_ptr<ast::FunctionDecl>(static_cast<ast::FunctionDecl*>(
                                std::get<std::unique_ptr<ast::FunctionDecl>>(func->kind)
                                    .release())));
                    }
                }
            } catch (...) {
                synchronize();
            }
        }

        expect(TokenKind::RBrace);
        return std::make_unique<ast::Decl>(std::move(decl));
    } else {
        return parse_impl_ctor(std::move(target), std::move(attributes), std::move(generic_params),
                               std::move(generic_params_v2));
    }
}

// コンストラクタ/デストラクタ専用implの解析
ast::DeclPtr Parser::parse_impl_ctor(ast::TypePtr target,
                                     std::vector<ast::AttributeNode> attributes,
                                     std::vector<std::string> generic_params,
                                     std::vector<ast::GenericParam> generic_params_v2) {
    expect(TokenKind::LBrace);

    auto decl = std::make_unique<ast::ImplDecl>(std::move(target));
    decl->attributes = std::move(attributes);

    if (!generic_params.empty()) {
        decl->generic_params = std::move(generic_params);
        decl->generic_params_v2 = std::move(generic_params_v2);
    }

    while (!check(TokenKind::RBrace) && !is_at_end()) {
        try {
            bool is_overload = consume_if(TokenKind::KwOverload);

            // デストラクタ: ~self()
            if (check(TokenKind::Tilde)) {
                advance();
                if (current().kind == TokenKind::KwSelf ||
                    (current().kind == TokenKind::Ident && current().get_string() == "self")) {
                    advance();
                    expect(TokenKind::LParen);
                    expect(TokenKind::RParen);
                    auto body = parse_block();

                    auto dtor = std::make_unique<ast::FunctionDecl>(
                        "~self", std::vector<ast::Param>{}, ast::make_void(), std::move(body));
                    dtor->is_destructor = true;

                    if (decl->destructor) {
                        error("Only one destructor allowed per impl block");
                    }
                    decl->destructor = std::move(dtor);
                } else {
                    error("Expected 'self' after '~'");
                    synchronize();
                }
            }
            // コンストラクタ: self() or overload self(...)
            else if (current().kind == TokenKind::KwSelf ||
                     (current().kind == TokenKind::Ident && current().get_string() == "self")) {
                advance();
                expect(TokenKind::LParen);
                auto params = parse_params();
                expect(TokenKind::RParen);
                auto body = parse_block();

                auto ctor = std::make_unique<ast::FunctionDecl>("self", std::move(params),
                                                                ast::make_void(), std::move(body));
                ctor->is_constructor = true;
                ctor->is_overload = is_overload;

                decl->constructors.push_back(std::move(ctor));
            } else if (check(TokenKind::KwOperator)) {
                advance();
                auto op_impl = std::make_unique<ast::OperatorImpl>();
                in_operator_return_type_ = true;
                op_impl->return_type = parse_type();
                in_operator_return_type_ = false;

                auto op_kind = parse_operator_kind();
                if (!op_kind) {
                    error("Expected operator symbol after 'operator'");
                    continue;
                }
                op_impl->op = *op_kind;

                expect(TokenKind::LParen);
                op_impl->params = parse_params();
                expect(TokenKind::RParen);
                op_impl->body = parse_block();
                decl->operators.push_back(std::move(op_impl));
            } else {
                std::vector<ast::AttributeNode> method_attrs;
                while (is_attribute_start()) {
                    method_attrs.push_back(parse_attribute());
                }

                bool is_private = consume_if(TokenKind::KwPrivate);
                bool is_static = consume_if(TokenKind::KwStatic);

                auto func = parse_function(false, is_static, false, std::move(method_attrs));
                if (auto* f = func->as<ast::FunctionDecl>()) {
                    if (is_private) {
                        f->visibility = ast::Visibility::Private;
                    } else {
                        f->visibility = ast::Visibility::Export;
                    }
                    decl->methods.push_back(
                        std::unique_ptr<ast::FunctionDecl>(static_cast<ast::FunctionDecl*>(
                            std::get<std::unique_ptr<ast::FunctionDecl>>(func->kind).release())));
                }
            }
        } catch (...) {
            synchronize();
        }
    }

    expect(TokenKind::RBrace);
    return std::make_unique<ast::Decl>(std::move(decl));
}

// ブロック
std::vector<ast::StmtPtr> Parser::parse_block() {
    debug::par::log(debug::par::Id::Block, "", debug::Level::Trace);
    expect(TokenKind::LBrace);

    std::vector<ast::StmtPtr> stmts;
    int iterations = 0;
    const int MAX_BLOCK_ITERATIONS = 1000;
    size_t last_pos = pos_;

    while (!check(TokenKind::RBrace) && !is_at_end() && iterations < MAX_BLOCK_ITERATIONS) {
        if (pos_ == last_pos && iterations > 0) {
            error("Parser stuck in block - no progress made");
            while (!is_at_end() && current().kind != TokenKind::Semicolon &&
                   current().kind != TokenKind::RBrace) {
                advance();
            }
            if (current().kind == TokenKind::Semicolon) {
                advance();
            }
            if (is_at_end() || current().kind == TokenKind::RBrace) {
                break;
            }
        }
        last_pos = pos_;

        if (auto stmt = parse_stmt()) {
            stmts.push_back(std::move(stmt));
        } else {
            if (!is_at_end() && current().kind != TokenKind::RBrace) {
                advance();
            }
        }
        iterations++;
    }

    if (iterations >= MAX_BLOCK_ITERATIONS) {
        error("Block parsing exceeded maximum iteration limit");
    }

    expect(TokenKind::RBrace);
    return stmts;
}

// エラー報告
void Parser::error(const std::string& msg) {
    uint32_t current_line = current().start;
    if (current_line == last_error_line_ && !diagnostics_.empty()) {
        return;
    }
    last_error_line_ = current_line;

    debug::par::log(debug::par::Id::Error, msg, debug::Level::Error);
    diagnostics_.emplace_back(DiagKind::Error, Span{current().start, current().end}, msg);
}

// エラー復旧 - 同期ポイントまでスキップ
void Parser::synchronize() {
    const int MAX_SKIP = 1000;
    int skipped = 0;

    size_t last_pos = pos_;

    advance();
    while (!is_at_end() && skipped < MAX_SKIP) {
        if (pos_ == last_pos) {
            if (pos_ < tokens_.size() - 1) {
                pos_++;
            } else {
                break;
            }
        }
        last_pos = pos_;

        if (previous().kind == TokenKind::Semicolon)
            return;
        switch (current().kind) {
            case TokenKind::KwStruct:
            case TokenKind::KwInterface:
            case TokenKind::KwImpl:
            case TokenKind::KwImport:
            case TokenKind::KwExport:
            case TokenKind::Hash:
                return;
            case TokenKind::KwBool:
            case TokenKind::KwInt:
            case TokenKind::KwVoid:
            case TokenKind::KwString:
            case TokenKind::KwChar:
            case TokenKind::KwFloat:
            case TokenKind::KwDouble:
                return;
            default:
                advance();
                skipped++;
        }
    }

    if (skipped >= MAX_SKIP) {
        error("Parser stuck in synchronization - too many tokens skipped");
    }
}

}  // namespace cm
