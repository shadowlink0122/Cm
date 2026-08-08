// ============================================================
// 式パーサ - 演算子優先順位に沿った代入・三項・二項・単項・キャスト式の解析
// ============================================================

#include "internal/base/debug/par.hpp"
#include "internal/base/i18n.hpp"
#include "internal/syntax/parser/parser.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

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
    int rel_iters = 0;

    while (true) {
        rel_iters++;
        if (rel_iters > 100) {
            // パーサーが無限ループに陥った場合の安全ガード
            break;
        }
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
    auto left = parse_cast_expr();

    while (true) {
        if (consume_if(TokenKind::Star)) {
            auto right = parse_cast_expr();
            left = ast::make_binary(ast::BinaryOp::Mul, std::move(left), std::move(right));
        } else if (consume_if(TokenKind::Slash)) {
            auto right = parse_cast_expr();
            left = ast::make_binary(ast::BinaryOp::Div, std::move(left), std::move(right));
        } else if (consume_if(TokenKind::Percent)) {
            auto right = parse_cast_expr();
            left = ast::make_binary(ast::BinaryOp::Mod, std::move(left), std::move(right));
        } else {
            break;
        }
    }

    return left;
}

// キャスト式: expr as Type / 型判別式: expr is Type
// 単項演算子より低い優先度で処理することで、&x as ulong が (&x) as ulong として解釈される
ast::ExprPtr Parser::parse_cast_expr() {
    auto expr = parse_unary();

    while (true) {
        // as/isの型右辺のスライスサフィックス: 空ブラケット[]のみ型として消費する（int[]等。[i]は従来通り(expr as T)[i]の添字式として残す）
        auto consume_slice_suffix = [&](ast::TypePtr ty) {
            while (check(TokenKind::LBracket) && peek_kind() == TokenKind::RBracket) {
                advance();
                advance();
                ty = ast::make_array(std::move(ty));
            }
            return ty;
        };
        if (consume_if(TokenKind::KwAs)) {
            debug::par::log(debug::par::Id::PrimaryExpr, "Detected 'as' cast expression",
                            debug::Level::Debug);
            auto target_type = consume_slice_suffix(parse_type());
            expr = ast::make_cast(std::move(expr), std::move(target_type));
        } else if (consume_if(TokenKind::KwIs)) {
            // ユニオン型の実行時型判別: expr is Type → bool
            debug::par::log(debug::par::Id::PrimaryExpr, "Detected 'is' type check expression",
                            debug::Level::Debug);
            auto target_type = consume_slice_suffix(parse_type());
            auto span = Span{expr->span.start, previous().end};
            auto cast = std::make_unique<ast::CastExpr>(std::move(expr), std::move(target_type));
            cast->type_check = true;
            expr = std::make_unique<ast::Expr>(std::move(cast), span);
        } else {
            break;
        }
    }

    return expr;
}

// 単項演算子
ast::ExprPtr Parser::parse_unary() {
    uint32_t start_pos = current().start;

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
    // move式: move expr → 所有権の移動を明示
    if (consume_if(TokenKind::KwMove)) {
        debug::par::log(debug::par::Id::PrimaryExpr, "Found 'move' expression",
                        debug::Level::Debug);
        auto operand = parse_unary();
        return ast::make_move(std::move(operand), Span{start_pos, previous().end});
    }
    // await式: await expr → Future<T>を待機してTを返す
    if (consume_if(TokenKind::KwAwait)) {
        debug::par::log(debug::par::Id::PrimaryExpr, "Found 'await' expression",
                        debug::Level::Debug);
        auto operand = parse_unary();
        return ast::make_await(std::move(operand), Span{start_pos, previous().end});
    }

    return parse_postfix();
}

}  // namespace cm
