// パーサ - 型解析（ジェネリックパラメータ、型パース、配列サフィックス）
#include "../../common/debug/par.hpp"
#include "parser.hpp"

namespace cm {

// ジェネリックパラメータ（<T>, <T: Interface>, <T: I + J>, <T: I | J>, <T, U>）をパース
std::pair<std::vector<std::string>, std::vector<ast::GenericParam>>
Parser::parse_generic_params_v2() {
    std::vector<std::string> names;
    std::vector<ast::GenericParam> params;

    if (!check(TokenKind::Lt)) {
        return {names, params};
    }

    advance();  // '<' を消費

    // パラメータリストをパース
    do {
        if (check(TokenKind::Gt)) {
            break;
        }

        std::string param_name = expect_ident();

        // コロンがあれば制約を読む
        if (consume_if(TokenKind::Colon)) {
            // const キーワードがあれば定数パラメータ
            if (consume_if(TokenKind::KwConst)) {
                ast::TypePtr const_type = parse_type();

                names.push_back(param_name);
                params.emplace_back(param_name, std::move(const_type));

                debug::par::log(debug::par::Id::FuncDef,
                                "Const generic param: " + param_name + " : const " +
                                    (const_type ? ast::type_to_string(*const_type) : "unknown"),
                                debug::Level::Debug);
            } else {
                // 通常のインターフェース制約
                std::vector<std::string> interfaces;
                ast::ConstraintKind constraint_kind = ast::ConstraintKind::Single;

                interfaces.push_back(expect_ident());

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

                names.push_back(param_name);
                ast::TypeConstraint tc(constraint_kind, interfaces);
                params.emplace_back(param_name, std::move(tc));

                std::string constraint_str;
                std::string separator =
                    (constraint_kind == ast::ConstraintKind::Or) ? " | " : " + ";
                for (size_t i = 0; i < interfaces.size(); ++i) {
                    if (i > 0)
                        constraint_str += separator;
                    constraint_str += interfaces[i];
                }
                debug::par::log(debug::par::Id::FuncDef,
                                "Generic param: " + param_name + " : " + constraint_str,
                                debug::Level::Debug);
            }
        } else {
            // 制約なし: <T>
            names.push_back(param_name);
            params.emplace_back(param_name);

            debug::par::log(debug::par::Id::FuncDef, "Generic param: " + param_name,
                            debug::Level::Debug);
        }

    } while (consume_if(TokenKind::Comma));

    expect(TokenKind::Gt);

    return {names, params};
}

// 後方互換性のため維持
std::vector<std::string> Parser::parse_generic_params() {
    auto [names, _] = parse_generic_params_v2();
    return names;
}

// 型解析
ast::TypePtr Parser::parse_type() {
    ast::TypePtr type;

    // const修飾子をチェック
    bool has_const = consume_if(TokenKind::KwConst);

    // ポインタ/参照
    if (!in_operator_return_type_ && consume_if(TokenKind::Star)) {
        type = ast::make_pointer(parse_type());
        if (has_const && type->element_type) {
            type->element_type->qualifiers.is_const = true;
        }
        return type;
    }
    if (!in_operator_return_type_ && consume_if(TokenKind::Amp)) {
        type = ast::make_reference(parse_type());
        return type;
    }

    // 配列 [T] or [T; N]
    if (consume_if(TokenKind::LBracket)) {
        auto elem = parse_type();
        std::optional<uint32_t> size;
        if (consume_if(TokenKind::Semicolon)) {
            if (check(TokenKind::IntLiteral)) {
                size = static_cast<uint32_t>(current().get_int());
                advance();
            }
        }
        expect(TokenKind::RBracket);
        auto arr_type = ast::make_array(std::move(elem), size);
        return arr_type;
    }

    // プリミティブ型
    ast::TypePtr base_type;
    switch (current().kind) {
        case TokenKind::KwAuto:
            advance();
            base_type = std::make_shared<ast::Type>(ast::TypeKind::Inferred);
            break;
        case TokenKind::KwTypeof: {
            advance();
            expect(TokenKind::LParen);
            auto expr = parse_expr();
            expect(TokenKind::RParen);
            auto type = std::make_shared<ast::Type>(ast::TypeKind::Inferred);
            type->name = "__typeof__";
            base_type = type;
            break;
        }
        case TokenKind::KwVoid:
            advance();
            base_type = ast::make_void();
            break;
        case TokenKind::KwBool:
            advance();
            base_type = std::make_shared<ast::Type>(ast::TypeKind::Bool);
            break;
        case TokenKind::KwTiny:
            advance();
            base_type = std::make_shared<ast::Type>(ast::TypeKind::Tiny);
            break;
        case TokenKind::KwShort:
            advance();
            base_type = std::make_shared<ast::Type>(ast::TypeKind::Short);
            break;
        case TokenKind::KwInt:
            advance();
            base_type = ast::make_int();
            break;
        case TokenKind::KwLong:
            advance();
            base_type = ast::make_long();
            break;
        case TokenKind::KwUtiny:
            advance();
            base_type = std::make_shared<ast::Type>(ast::TypeKind::UTiny);
            break;
        case TokenKind::KwUshort:
            advance();
            base_type = std::make_shared<ast::Type>(ast::TypeKind::UShort);
            break;
        case TokenKind::KwUint:
            advance();
            base_type = std::make_shared<ast::Type>(ast::TypeKind::UInt);
            break;
        case TokenKind::KwUlong:
            advance();
            base_type = std::make_shared<ast::Type>(ast::TypeKind::ULong);
            break;
        case TokenKind::KwIsize:
            advance();
            base_type = ast::make_isize();
            break;
        case TokenKind::KwUsize:
            advance();
            base_type = ast::make_usize();
            break;
        case TokenKind::KwFloat:
            advance();
            base_type = ast::make_float();
            break;
        case TokenKind::KwDouble:
            advance();
            base_type = ast::make_double();
            break;
        case TokenKind::KwUfloat:
            advance();
            base_type = ast::make_ufloat();
            break;
        case TokenKind::KwUdouble:
            advance();
            base_type = ast::make_udouble();
            break;
        case TokenKind::KwChar:
            advance();
            base_type = ast::make_char();
            break;
        case TokenKind::KwString:
            advance();
            base_type = ast::make_string();
            break;
        case TokenKind::KwCstring:
            advance();
            base_type = ast::make_cstring();
            break;
        case TokenKind::KwNull:
            advance();
            base_type = ast::make_null();
            break;
        default:
            break;
    }

    // 関数ポインタ型: int*(int, int) または ポインタ型: void*
    if (base_type && check(TokenKind::Star)) {
        if (pos_ + 1 < tokens_.size() && tokens_[pos_ + 1].kind == TokenKind::LParen) {
            advance();  // consume *
            advance();  // consume (

            std::vector<ast::TypePtr> param_types;
            if (!check(TokenKind::RParen)) {
                do {
                    param_types.push_back(parse_type());
                } while (consume_if(TokenKind::Comma));
            }
            expect(TokenKind::RParen);

            return ast::make_function_ptr(std::move(base_type), std::move(param_types));
        } else {
            advance();  // consume *
            if (has_const) {
                base_type->qualifiers.is_const = true;
            }
            return ast::make_pointer(std::move(base_type));
        }
    }

    if (base_type) {
        return base_type;
    }

    // ユーザー定義型（ジェネリクス対応）
    if (check(TokenKind::Ident)) {
        std::string name = current_text();
        advance();

        // namespace::Type 形式をサポート
        while (check(TokenKind::ColonColon)) {
            advance();
            if (!check(TokenKind::Ident)) {
                error("Expected identifier after '::'");
                return ast::make_error();
            }
            name += "::" + current_text();
            advance();
        }

        // ジェネリック型引数をチェック
        if (check(TokenKind::Lt)) {
            advance();  // '<' を消費

            std::vector<ast::TypePtr> type_args;

            do {
                type_args.push_back(parse_type());
            } while (consume_if(TokenKind::Comma));

            consume_gt_in_type_context();

            auto type = ast::make_named(name);
            type->type_args = std::move(type_args);

            // 関数ポインタ型/ポインタ型
            if (check(TokenKind::Star) && !in_operator_return_type_) {
                if (pos_ + 1 < tokens_.size() && tokens_[pos_ + 1].kind == TokenKind::LParen) {
                    advance();  // consume *
                    advance();  // consume (

                    std::vector<ast::TypePtr> param_types;
                    if (!check(TokenKind::RParen)) {
                        do {
                            param_types.push_back(parse_type());
                        } while (consume_if(TokenKind::Comma));
                    }
                    expect(TokenKind::RParen);

                    return ast::make_function_ptr(std::move(type), std::move(param_types));
                } else {
                    advance();  // consume *
                    return ast::make_pointer(std::move(type));
                }
            }

            return type;
        }

        // 関数ポインタ型/ポインタ型
        if (check(TokenKind::Star) && !in_operator_return_type_) {
            auto named_type = ast::make_named(name);
            if (pos_ + 1 < tokens_.size() && tokens_[pos_ + 1].kind == TokenKind::LParen) {
                advance();  // consume *
                advance();  // consume (

                std::vector<ast::TypePtr> param_types;
                if (!check(TokenKind::RParen)) {
                    do {
                        param_types.push_back(parse_type());
                    } while (consume_if(TokenKind::Comma));
                }
                expect(TokenKind::RParen);

                return ast::make_function_ptr(std::move(named_type), std::move(param_types));
            } else {
                advance();  // consume *
                return ast::make_pointer(std::move(named_type));
            }
        }
        // 参照型
        if (check(TokenKind::Amp) && !in_operator_return_type_) {
            auto named_type = ast::make_named(name);
            advance();  // consume &
            return ast::make_reference(std::move(named_type));
        }

        auto result = ast::make_named(name);
        return result;
    }

    error("Expected type");
    return ast::make_error();
}

// インラインユニオン型をサポートする型解析
ast::TypePtr Parser::parse_type_with_union() {
    auto first = parse_type();
    first = check_array_suffix(std::move(first));

    if (!check(TokenKind::Pipe)) {
        return first;
    }

    // ユニオン型として収集
    std::vector<ast::TypePtr> types;
    types.push_back(std::move(first));

    while (consume_if(TokenKind::Pipe)) {
        auto next_type = parse_type();
        next_type = check_array_suffix(std::move(next_type));
        types.push_back(std::move(next_type));
    }

    // UnionType構築
    std::vector<ast::UnionVariant> variants;
    std::string union_name;
    for (size_t i = 0; i < types.size(); ++i) {
        std::string tag = ast::type_to_string(*types[i]);
        if (i > 0)
            union_name += " | ";
        union_name += tag;
        ast::UnionVariant v(tag);
        v.fields.push_back(std::move(types[i]));
        variants.push_back(std::move(v));
    }
    auto union_type = ast::make_union(std::move(variants));
    union_type->name = std::move(union_name);
    return union_type;
}

// C++スタイルの配列サイズ指定とポインタをチェック
ast::TypePtr Parser::check_array_suffix(ast::TypePtr base_type) {
    // T[N] 形式をチェック
    if (consume_if(TokenKind::LBracket)) {
        std::optional<uint32_t> size;
        std::string size_param_name;

        if (check(TokenKind::IntLiteral)) {
            size = static_cast<uint32_t>(current().get_int());
            advance();
        } else if (check(TokenKind::Ident)) {
            size_param_name = std::string(current().get_string());
            advance();
        }

        expect(TokenKind::RBracket);

        ast::TypePtr arr_type;
        if (!size_param_name.empty()) {
            arr_type = ast::make_array_with_param(std::move(base_type), size_param_name);
        } else {
            arr_type = ast::make_array(std::move(base_type), size);
        }

        return check_array_suffix(std::move(arr_type));
    }
    // T* 形式をチェック
    if (consume_if(TokenKind::Star)) {
        auto ptr_type = ast::make_pointer(std::move(base_type));
        return check_array_suffix(std::move(ptr_type));
    }
    return base_type;
}

// ネストジェネリクス対応: 型パース中に'>'を消費
void Parser::consume_gt_in_type_context() {
    if (pending_gt_count_ > 0) {
        pending_gt_count_--;
        return;
    }

    if (consume_if(TokenKind::Gt)) {
        return;
    }

    if (check(TokenKind::GtGt)) {
        advance();
        pending_gt_count_ = 1;
        return;
    }

    error("Expected '>'");
}

// 識別子を期待
std::string Parser::expect_ident() {
    if (check(TokenKind::Ident)) {
        std::string name(current().get_string());
        advance();
        return name;
    }
    error("Expected identifier, got '" + std::string(current().get_string()) + "'");
    advance();
    return "<error>";
}

// 現在のテキストを取得
std::string Parser::current_text() const {
    if (check(TokenKind::Ident)) {
        return std::string(current().get_string());
    }
    return "";
}

}  // namespace cm
