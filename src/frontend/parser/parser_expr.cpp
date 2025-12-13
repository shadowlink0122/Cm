#include "parser.hpp"

namespace cm {

// ============================================================
// 式の解析（演算子優先順位順）
// ============================================================

ast::ExprPtr Parser::parse_expr() {
    debug::par::log(debug::par::Id::Expr, "", debug::Level::Trace);
    debug::par::log(debug::par::Id::ExprStart, "Starting expression parse", debug::Level::Trace);
    auto result = parse_assignment();
    debug::par::log(debug::par::Id::ExprEnd, "Expression parsed", debug::Level::Trace);
    return result;
}

// 代入式 (右結合)
ast::ExprPtr Parser::parse_assignment() {
    debug::par::log(debug::par::Id::AssignmentCheck, "Checking for assignment operators",
                    debug::Level::Trace);
    auto left = parse_ternary();

    if (check(TokenKind::Eq)) {
        debug::par::log(debug::par::Id::AssignmentOp, "Found '=' (simple assignment)",
                        debug::Level::Debug);
        advance();
        auto right = parse_assignment();
        debug::par::log(debug::par::Id::AssignmentCreate, "Creating assignment expression",
                        debug::Level::Debug);
        return ast::make_binary(ast::BinaryOp::Assign, std::move(left), std::move(right));
    }
    if (check(TokenKind::PlusEq)) {
        debug::par::log(debug::par::Id::AssignmentOp, "Found '+=' (add-assign)",
                        debug::Level::Debug);
        advance();
        auto right = parse_assignment();
        debug::par::log(debug::par::Id::CompoundAssignment, "Creating compound assignment (+=)",
                        debug::Level::Debug);
        return ast::make_binary(ast::BinaryOp::AddAssign, std::move(left), std::move(right));
    }
    if (check(TokenKind::MinusEq)) {
        debug::par::log(debug::par::Id::AssignmentOp, "Found '-=' (sub-assign)",
                        debug::Level::Debug);
        advance();
        auto right = parse_assignment();
        debug::par::log(debug::par::Id::CompoundAssignment, "Creating compound assignment (-=)",
                        debug::Level::Debug);
        return ast::make_binary(ast::BinaryOp::SubAssign, std::move(left), std::move(right));
    }
    if (check(TokenKind::StarEq)) {
        debug::par::log(debug::par::Id::AssignmentOp, "Found '*=' (mul-assign)",
                        debug::Level::Debug);
        advance();
        auto right = parse_assignment();
        debug::par::log(debug::par::Id::CompoundAssignment, "Creating compound assignment (*=)",
                        debug::Level::Debug);
        return ast::make_binary(ast::BinaryOp::MulAssign, std::move(left), std::move(right));
    }
    if (check(TokenKind::SlashEq)) {
        debug::par::log(debug::par::Id::AssignmentOp, "Found '/=' (div-assign)",
                        debug::Level::Debug);
        advance();
        auto right = parse_assignment();
        debug::par::log(debug::par::Id::CompoundAssignment, "Creating compound assignment (/=)",
                        debug::Level::Debug);
        return ast::make_binary(ast::BinaryOp::DivAssign, std::move(left), std::move(right));
    }
    if (check(TokenKind::PercentEq)) {
        debug::par::log(debug::par::Id::AssignmentOp, "Found '%=' (mod-assign)",
                        debug::Level::Debug);
        advance();
        auto right = parse_assignment();
        debug::par::log(debug::par::Id::CompoundAssignment, "Creating compound assignment (%=)",
                        debug::Level::Debug);
        return ast::make_binary(ast::BinaryOp::ModAssign, std::move(left), std::move(right));
    }
    if (check(TokenKind::AmpEq)) {
        debug::par::log(debug::par::Id::AssignmentOp, "Found '&=' (bitand-assign)",
                        debug::Level::Debug);
        advance();
        auto right = parse_assignment();
        debug::par::log(debug::par::Id::CompoundAssignment, "Creating compound assignment (&=)",
                        debug::Level::Debug);
        return ast::make_binary(ast::BinaryOp::BitAndAssign, std::move(left), std::move(right));
    }
    if (check(TokenKind::PipeEq)) {
        debug::par::log(debug::par::Id::AssignmentOp, "Found '|=' (bitor-assign)",
                        debug::Level::Debug);
        advance();
        auto right = parse_assignment();
        debug::par::log(debug::par::Id::CompoundAssignment, "Creating compound assignment (|=)",
                        debug::Level::Debug);
        return ast::make_binary(ast::BinaryOp::BitOrAssign, std::move(left), std::move(right));
    }
    if (check(TokenKind::CaretEq)) {
        debug::par::log(debug::par::Id::AssignmentOp, "Found '^=' (bitxor-assign)",
                        debug::Level::Debug);
        advance();
        auto right = parse_assignment();
        debug::par::log(debug::par::Id::CompoundAssignment, "Creating compound assignment (^=)",
                        debug::Level::Debug);
        return ast::make_binary(ast::BinaryOp::BitXorAssign, std::move(left), std::move(right));
    }
    if (check(TokenKind::LtLtEq)) {
        debug::par::log(debug::par::Id::AssignmentOp, "Found '<<=' (shl-assign)",
                        debug::Level::Debug);
        advance();
        auto right = parse_assignment();
        debug::par::log(debug::par::Id::CompoundAssignment, "Creating compound assignment (<<=)",
                        debug::Level::Debug);
        return ast::make_binary(ast::BinaryOp::ShlAssign, std::move(left), std::move(right));
    }
    if (check(TokenKind::GtGtEq)) {
        debug::par::log(debug::par::Id::AssignmentOp, "Found '>>=' (shr-assign)",
                        debug::Level::Debug);
        advance();
        auto right = parse_assignment();
        debug::par::log(debug::par::Id::CompoundAssignment, "Creating compound assignment (>>=)",
                        debug::Level::Debug);
        return ast::make_binary(ast::BinaryOp::ShrAssign, std::move(left), std::move(right));
    }

    debug::par::log(debug::par::Id::NoAssignment, "No assignment operator found",
                    debug::Level::Trace);
    return left;
}

// 三項演算子
ast::ExprPtr Parser::parse_ternary() {
    auto cond = parse_logical_or();

    if (consume_if(TokenKind::Question)) {
        auto then_expr = parse_expr();
        expect(TokenKind::Colon);
        auto else_expr = parse_ternary();

        auto ternary = std::make_unique<ast::TernaryExpr>(std::move(cond), std::move(then_expr),
                                                          std::move(else_expr));
        return std::make_unique<ast::Expr>(std::move(ternary));
    }

    return cond;
}

// 論理OR
ast::ExprPtr Parser::parse_logical_or() {
    auto left = parse_logical_and();

    while (consume_if(TokenKind::PipePipe)) {
        auto right = parse_logical_and();
        left = ast::make_binary(ast::BinaryOp::Or, std::move(left), std::move(right));
    }

    return left;
}

// 論理AND
ast::ExprPtr Parser::parse_logical_and() {
    auto left = parse_bitwise_or();

    while (consume_if(TokenKind::AmpAmp)) {
        auto right = parse_bitwise_or();
        left = ast::make_binary(ast::BinaryOp::And, std::move(left), std::move(right));
    }

    return left;
}

// ビットOR
ast::ExprPtr Parser::parse_bitwise_or() {
    auto left = parse_bitwise_xor();

    while (consume_if(TokenKind::Pipe)) {
        auto right = parse_bitwise_xor();
        left = ast::make_binary(ast::BinaryOp::BitOr, std::move(left), std::move(right));
    }

    return left;
}

// ビットXOR
ast::ExprPtr Parser::parse_bitwise_xor() {
    auto left = parse_bitwise_and();

    while (consume_if(TokenKind::Caret)) {
        auto right = parse_bitwise_and();
        left = ast::make_binary(ast::BinaryOp::BitXor, std::move(left), std::move(right));
    }

    return left;
}

// ビットAND
ast::ExprPtr Parser::parse_bitwise_and() {
    auto left = parse_equality();

    while (consume_if(TokenKind::Amp)) {
        auto right = parse_equality();
        left = ast::make_binary(ast::BinaryOp::BitAnd, std::move(left), std::move(right));
    }

    return left;
}

// 等価比較
ast::ExprPtr Parser::parse_equality() {
    auto left = parse_relational();

    while (true) {
        if (consume_if(TokenKind::EqEq)) {
            auto right = parse_relational();
            left = ast::make_binary(ast::BinaryOp::Eq, std::move(left), std::move(right));
        } else if (consume_if(TokenKind::BangEq)) {
            auto right = parse_relational();
            left = ast::make_binary(ast::BinaryOp::Ne, std::move(left), std::move(right));
        } else {
            break;
        }
    }

    return left;
}

// 関係比較
ast::ExprPtr Parser::parse_relational() {
    auto left = parse_shift();

    while (true) {
        if (consume_if(TokenKind::Lt)) {
            auto right = parse_shift();
            left = ast::make_binary(ast::BinaryOp::Lt, std::move(left), std::move(right));
        } else if (consume_if(TokenKind::Gt)) {
            auto right = parse_shift();
            left = ast::make_binary(ast::BinaryOp::Gt, std::move(left), std::move(right));
        } else if (consume_if(TokenKind::LtEq)) {
            auto right = parse_shift();
            left = ast::make_binary(ast::BinaryOp::Le, std::move(left), std::move(right));
        } else if (consume_if(TokenKind::GtEq)) {
            auto right = parse_shift();
            left = ast::make_binary(ast::BinaryOp::Ge, std::move(left), std::move(right));
        } else {
            break;
        }
    }

    return left;
}

// シフト
ast::ExprPtr Parser::parse_shift() {
    auto left = parse_additive();

    while (true) {
        if (consume_if(TokenKind::LtLt)) {
            auto right = parse_additive();
            left = ast::make_binary(ast::BinaryOp::Shl, std::move(left), std::move(right));
        } else if (consume_if(TokenKind::GtGt)) {
            auto right = parse_additive();
            left = ast::make_binary(ast::BinaryOp::Shr, std::move(left), std::move(right));
        } else {
            break;
        }
    }

    return left;
}

// 加減算
ast::ExprPtr Parser::parse_additive() {
    auto left = parse_multiplicative();

    while (true) {
        if (consume_if(TokenKind::Plus)) {
            auto right = parse_multiplicative();
            left = ast::make_binary(ast::BinaryOp::Add, std::move(left), std::move(right));
        } else if (consume_if(TokenKind::Minus)) {
            auto right = parse_multiplicative();
            left = ast::make_binary(ast::BinaryOp::Sub, std::move(left), std::move(right));
        } else {
            break;
        }
    }

    return left;
}

// 乗除算
ast::ExprPtr Parser::parse_multiplicative() {
    auto left = parse_unary();

    while (true) {
        if (consume_if(TokenKind::Star)) {
            auto right = parse_unary();
            left = ast::make_binary(ast::BinaryOp::Mul, std::move(left), std::move(right));
        } else if (consume_if(TokenKind::Slash)) {
            auto right = parse_unary();
            left = ast::make_binary(ast::BinaryOp::Div, std::move(left), std::move(right));
        } else if (consume_if(TokenKind::Percent)) {
            auto right = parse_unary();
            left = ast::make_binary(ast::BinaryOp::Mod, std::move(left), std::move(right));
        } else {
            break;
        }
    }

    return left;
}

// 単項演算子
ast::ExprPtr Parser::parse_unary() {
    if (consume_if(TokenKind::Minus)) {
        auto operand = parse_unary();
        return ast::make_unary(ast::UnaryOp::Neg, std::move(operand));
    }
    if (consume_if(TokenKind::Bang)) {
        auto operand = parse_unary();
        return ast::make_unary(ast::UnaryOp::Not, std::move(operand));
    }
    if (consume_if(TokenKind::Tilde)) {
        auto operand = parse_unary();
        return ast::make_unary(ast::UnaryOp::BitNot, std::move(operand));
    }
    if (consume_if(TokenKind::Amp)) {
        auto operand = parse_unary();
        return ast::make_unary(ast::UnaryOp::AddrOf, std::move(operand));
    }
    if (consume_if(TokenKind::Star)) {
        auto operand = parse_unary();
        return ast::make_unary(ast::UnaryOp::Deref, std::move(operand));
    }
    if (consume_if(TokenKind::PlusPlus)) {
        auto operand = parse_unary();
        return ast::make_unary(ast::UnaryOp::PreInc, std::move(operand));
    }
    if (consume_if(TokenKind::MinusMinus)) {
        auto operand = parse_unary();
        return ast::make_unary(ast::UnaryOp::PreDec, std::move(operand));
    }

    return parse_postfix();
}

// 後置演算子
ast::ExprPtr Parser::parse_postfix() {
    debug::par::log(debug::par::Id::PostfixStart, "Parsing postfix expressions",
                    debug::Level::Trace);
    auto expr = parse_primary();

    while (true) {
        // 関数呼び出し
        if (consume_if(TokenKind::LParen)) {
            debug::par::log(debug::par::Id::FunctionCall, "Detected function call",
                            debug::Level::Debug);
            std::vector<ast::ExprPtr> args;
            int arg_count = 0;
            if (!check(TokenKind::RParen)) {
                do {
                    debug::par::log(debug::par::Id::CallArg,
                                    "Parsing argument " + std::to_string(arg_count + 1),
                                    debug::Level::Trace);
                    args.push_back(parse_expr());
                    arg_count++;
                } while (consume_if(TokenKind::Comma));
            }
            expect(TokenKind::RParen);
            debug::par::log(
                debug::par::Id::CallCreate,
                "Creating function call with " + std::to_string(arg_count) + " arguments",
                debug::Level::Debug);
            expr = ast::make_call(std::move(expr), std::move(args));
            continue;
        }

        // 配列アクセス
        if (consume_if(TokenKind::LBracket)) {
            debug::par::log(debug::par::Id::ArrayAccess, "Detected array access",
                            debug::Level::Debug);
            auto index = parse_expr();
            expect(TokenKind::RBracket);
            debug::par::log(debug::par::Id::IndexCreate, "Creating array index expression",
                            debug::Level::Debug);
            auto idx_expr = std::make_unique<ast::IndexExpr>(std::move(expr), std::move(index));
            expr = std::make_unique<ast::Expr>(std::move(idx_expr));
            continue;
        }

        // メンバアクセス
        if (consume_if(TokenKind::Dot)) {
            std::string member = expect_ident();
            debug::par::log(debug::par::Id::MemberAccess, "Accessing member: " + member,
                            debug::Level::Debug);

            // メソッド呼び出し
            if (consume_if(TokenKind::LParen)) {
                debug::par::log(debug::par::Id::MethodCall, "Detected method call: " + member,
                                debug::Level::Debug);
                auto mem_expr = std::make_unique<ast::MemberExpr>(std::move(expr), member);
                mem_expr->is_method_call = true;

                int arg_count = 0;
                if (!check(TokenKind::RParen)) {
                    do {
                        debug::par::log(debug::par::Id::CallArg,
                                        "Parsing method argument " + std::to_string(arg_count + 1),
                                        debug::Level::Trace);
                        mem_expr->args.push_back(parse_expr());
                        arg_count++;
                    } while (consume_if(TokenKind::Comma));
                }
                expect(TokenKind::RParen);
                debug::par::log(
                    debug::par::Id::MethodCreate,
                    "Creating method call with " + std::to_string(arg_count) + " arguments",
                    debug::Level::Debug);
                expr = std::make_unique<ast::Expr>(std::move(mem_expr));
            } else {
                debug::par::log(debug::par::Id::MemberCreate, "Creating member access",
                                debug::Level::Debug);
                auto mem_expr = std::make_unique<ast::MemberExpr>(std::move(expr), member);
                expr = std::make_unique<ast::Expr>(std::move(mem_expr));
            }
            continue;
        }

        // 後置インクリメント/デクリメント
        if (consume_if(TokenKind::PlusPlus)) {
            debug::par::log(debug::par::Id::PostIncrement, "Detected post-increment",
                            debug::Level::Debug);
            expr = ast::make_unary(ast::UnaryOp::PostInc, std::move(expr));
            continue;
        }
        if (consume_if(TokenKind::MinusMinus)) {
            debug::par::log(debug::par::Id::PostDecrement, "Detected post-decrement",
                            debug::Level::Debug);
            expr = ast::make_unary(ast::UnaryOp::PostDec, std::move(expr));
            continue;
        }

        break;
    }

    debug::par::log(debug::par::Id::PostfixEnd, "Finished parsing postfix", debug::Level::Trace);
    return expr;
}

// 一次式
ast::ExprPtr Parser::parse_primary() {
    debug::par::log(debug::par::Id::PrimaryExpr, "Parsing primary expression", debug::Level::Trace);
    uint32_t start_pos = current().start;

    // 数値リテラル
    if (check(TokenKind::IntLiteral)) {
        int64_t val = current().get_int();
        debug::par::log(debug::par::Id::IntLiteral, "Found integer literal: " + std::to_string(val),
                        debug::Level::Debug);
        advance();
        return ast::make_int_literal(val, Span{start_pos, previous().end});
    }

    if (check(TokenKind::FloatLiteral)) {
        double val = current().get_float();
        debug::par::log(debug::par::Id::FloatLiteral, "Found float literal: " + std::to_string(val),
                        debug::Level::Debug);
        advance();
        return ast::make_float_literal(val, Span{start_pos, previous().end});
    }

    // 文字列リテラル
    if (check(TokenKind::StringLiteral)) {
        std::string val(current().get_string());
        debug::par::log(debug::par::Id::StringLiteral, "Found string literal: \"" + val + "\"",
                        debug::Level::Debug);
        advance();
        return ast::make_string_literal(std::move(val), Span{start_pos, previous().end});
    }

    // 文字リテラル
    if (check(TokenKind::CharLiteral)) {
        std::string s(current().get_string());
        char val = s.empty() ? '\0' : s[0];
        debug::par::log(debug::par::Id::CharLiteral,
                        "Found char literal: '" + std::string(1, val) + "'", debug::Level::Debug);
        advance();
        auto lit = std::make_unique<ast::LiteralExpr>(val);
        return std::make_unique<ast::Expr>(std::move(lit), Span{start_pos, previous().end});
    }

    // true/false
    if (consume_if(TokenKind::KwTrue)) {
        debug::par::log(debug::par::Id::BoolLiteral, "Found boolean literal: true",
                        debug::Level::Debug);
        return ast::make_bool_literal(true, Span{start_pos, previous().end});
    }
    if (consume_if(TokenKind::KwFalse)) {
        debug::par::log(debug::par::Id::BoolLiteral, "Found boolean literal: false",
                        debug::Level::Debug);
        return ast::make_bool_literal(false, Span{start_pos, previous().end});
    }

    // null
    if (consume_if(TokenKind::KwNull)) {
        debug::par::log(debug::par::Id::NullLiteral, "Found null literal", debug::Level::Debug);
        return ast::make_null_literal(Span{start_pos, previous().end});
    }

    // new式
    if (consume_if(TokenKind::KwNew)) {
        debug::par::log(debug::par::Id::NewExpr, "Found 'new' expression", debug::Level::Debug);
        auto type = parse_type();
        std::vector<ast::ExprPtr> args;

        if (consume_if(TokenKind::LParen)) {
            debug::par::log(debug::par::Id::NewArgs, "Parsing new expression arguments",
                            debug::Level::Trace);
            if (!check(TokenKind::RParen)) {
                do {
                    args.push_back(parse_expr());
                } while (consume_if(TokenKind::Comma));
            }
            expect(TokenKind::RParen);
        }

        debug::par::log(debug::par::Id::NewCreate, "Creating new expression", debug::Level::Debug);
        auto new_expr = std::make_unique<ast::NewExpr>(std::move(type), std::move(args));
        return std::make_unique<ast::Expr>(std::move(new_expr), Span{start_pos, previous().end});
    }

    // 識別子（enum値アクセスを含む）
    if (check(TokenKind::Ident)) {
        std::string name(current().get_string());
        debug::par::log(debug::par::Id::IdentifierRef, "Found identifier: " + name,
                        debug::Level::Debug);
        advance();

        // enum値アクセス: EnumName::MemberName
        if (consume_if(TokenKind::ColonColon)) {
            std::string member = expect_ident();
            debug::par::log(debug::par::Id::IdentifierRef,
                            "Found enum access: " + name + "::" + member, debug::Level::Debug);
            // EnumName::Memberを "EnumName::Member" という識別子として扱う
            std::string enum_access = name + "::" + member;
            return ast::make_ident(std::move(enum_access), Span{start_pos, previous().end});
        }

        debug::par::log(debug::par::Id::VariableDetected, "Variable/Function reference: " + name,
                        debug::Level::Debug);
        return ast::make_ident(std::move(name), Span{start_pos, previous().end});
    }

    // 括弧式
    if (consume_if(TokenKind::LParen)) {
        debug::par::log(debug::par::Id::ParenExpr, "Found parenthesized expression",
                        debug::Level::Trace);
        auto expr = parse_expr();
        expect(TokenKind::RParen);
        debug::par::log(debug::par::Id::ParenClose, "Closed parenthesized expression",
                        debug::Level::Trace);
        return expr;
    }

    std::string error_msg = "Expected expression but found: ";
    error_msg += token_kind_to_string(current().kind);
    debug::par::log(debug::par::Id::ExprError, error_msg, debug::Level::Error);
    error("Expected expression");
    return ast::make_null_literal();
}

}  // namespace cm
