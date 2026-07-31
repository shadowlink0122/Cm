// ============================================================
// 式パーサ - 後置演算子（関数呼び出し・添字/スライス・メンバアクセス・構造体リテラル等）の解析
// ============================================================

#include "internal/base/debug/par.hpp"
#include "internal/base/i18n.hpp"
#include "internal/syntax/parser/parser.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace cm {

// 後置演算子
ast::ExprPtr Parser::parse_postfix() {
    debug::par::log(debug::par::Id::PostfixStart, "Parsing postfix expressions",
                    debug::Level::Trace);
    auto expr = parse_primary();

    while (true) {
        // 構造体リテラル: TypeName{field1: val1, ...}
        // 名前付き初期化のみ対応（位置ベースは禁止）
        // 識別子の後に { が来た場合
        if (check(TokenKind::LBrace)) {
            // 式が識別子の場合のみ構造体リテラルとして解析
            if (auto* ident = expr->as<ast::IdentExpr>()) {
                std::string type_name = ident->name;
                uint32_t start_pos = expr->span.start;
                advance();  // {

                debug::par::log(debug::par::Id::PrimaryExpr, "Parsing struct literal: " + type_name,
                                debug::Level::Debug);

                std::vector<ast::StructLiteralField> fields;

                if (!check(TokenKind::RBrace)) {
                    do {
                        // フィールド名:値 形式のみ（名前付き初期化必須）
                        if (!check(TokenKind::Ident)) {
                            error(i18n::msg(i18n::MsgId::PsExpectedFieldNameStructLiteral));
                        }

                        std::string field_name(current().get_string());
                        advance();  // フィールド名を消費

                        if (!check(TokenKind::Colon)) {
                            error(i18n::msgf(i18n::MsgId::PsExpectedFieldNameStructLiteral2,
                                             field_name));
                        }
                        advance();  // : を消費

                        auto value = parse_expr();
                        fields.emplace_back(std::move(field_name), std::move(value));
                    } while (consume_if(TokenKind::Comma));
                }

                expect(TokenKind::RBrace);

                debug::par::log(
                    debug::par::Id::PrimaryExpr,
                    "Created struct literal with " + std::to_string(fields.size()) + " fields",
                    debug::Level::Debug);

                expr = ast::make_struct_literal(std::move(type_name), std::move(fields),
                                                Span{start_pos, previous().end});
                continue;
            }
        }

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

        // 配列アクセスまたはスライス
        if (consume_if(TokenKind::LBracket)) {
            debug::par::log(debug::par::Id::ArrayAccess, "Detected array access or slice",
                            debug::Level::Debug);

            // スライス構文: arr[start:end:step]
            // 空の start, end, step を許可: arr[:], arr[::], arr[1:], arr[:5], arr[1:5:2]
            ast::ExprPtr start_expr = nullptr;
            ast::ExprPtr end_expr = nullptr;
            ast::ExprPtr step_expr = nullptr;
            bool is_slice = false;

            // 開始インデックスをパース（:でなければ）
            if (!check(TokenKind::Colon)) {
                start_expr = parse_expr();
            }

            // インデックスドパートセレクト: x[base +: width]（ビットスライス）
            if (start_expr && consume_if(TokenKind::PlusColon)) {
                auto width_expr = parse_expr();
                expect(TokenKind::RBracket);
                auto slice = std::make_unique<ast::SliceExpr>(
                    std::move(expr), std::move(start_expr), std::move(width_expr));
                slice->is_part_select = true;
                expr = std::make_unique<ast::Expr>(std::move(slice));
                continue;
            }

            // コロンがあればスライス
            if (consume_if(TokenKind::Colon)) {
                is_slice = true;

                // 終了インデックス（:や]でなければ）
                if (!check(TokenKind::Colon) && !check(TokenKind::RBracket)) {
                    end_expr = parse_expr();
                }

                // 2つ目のコロンがあればステップ
                if (consume_if(TokenKind::Colon)) {
                    if (!check(TokenKind::RBracket)) {
                        step_expr = parse_expr();
                    }
                }
            }

            expect(TokenKind::RBracket);

            if (is_slice) {
                debug::par::log(debug::par::Id::IndexCreate, "Creating slice expression",
                                debug::Level::Debug);
                auto slice_expr =
                    std::make_unique<ast::SliceExpr>(std::move(expr), std::move(start_expr),
                                                     std::move(end_expr), std::move(step_expr));
                expr = std::make_unique<ast::Expr>(std::move(slice_expr));
            } else {
                debug::par::log(debug::par::Id::IndexCreate, "Creating array index expression",
                                debug::Level::Debug);
                auto idx_expr =
                    std::make_unique<ast::IndexExpr>(std::move(expr), std::move(start_expr));
                expr = std::make_unique<ast::Expr>(std::move(idx_expr));
            }
            continue;
        }

        // メンバアクセス (. または ->)
        if (check(TokenKind::Dot) || check(TokenKind::ThinArrow)) {
            bool is_arrow = consume_if(TokenKind::ThinArrow);
            if (!is_arrow) {
                consume_if(TokenKind::Dot);
            }

            std::string member = expect_ident();
            debug::par::log(
                debug::par::Id::MemberAccess,
                std::string(is_arrow ? "Arrow" : "Dot") + " accessing member: " + member,
                debug::Level::Debug);

            // -> の場合は暗黙のデリファレンスを追加
            if (is_arrow) {
                expr = ast::make_unary(ast::UnaryOp::Deref, std::move(expr));
            }

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

        // ?演算子（Result/Optionのエラー伝播）: expr?
        // 三項演算子（cond ? a : b）と区別するため、? の次のトークンが式を開始し得る場合は三項演算子として上位に委ねる
        if (check(TokenKind::Question)) {
            bool next_starts_expr = false;
            if (pos_ + 1 < tokens_.size()) {
                switch (tokens_[pos_ + 1].kind) {
                    case TokenKind::Ident:
                    case TokenKind::IntLiteral:
                    case TokenKind::FloatLiteral:
                    case TokenKind::StringLiteral:
                    case TokenKind::CharLiteral:
                    case TokenKind::KwTrue:
                    case TokenKind::KwFalse:
                    case TokenKind::KwNull:
                    case TokenKind::LParen:
                    case TokenKind::Bang:
                    case TokenKind::Minus:
                    case TokenKind::Tilde:
                    case TokenKind::KwMatch:
                        next_starts_expr = true;
                        break;
                    default:
                        break;
                }
            }
            if (!next_starts_expr) {
                advance();  // ?
                debug::par::log(debug::par::Id::PostfixStart, "Detected try operator (?)",
                                debug::Level::Debug);
                expr = ast::make_unary(ast::UnaryOp::Try, std::move(expr));
                continue;
            }
        }

        // キャスト式はparse_cast_expr()で処理（&x as ulongを(&x) as ulongとして解釈するため）

        break;
    }

    debug::par::log(debug::par::Id::PostfixEnd, "Finished parsing postfix", debug::Level::Trace);
    return expr;
}

}  // namespace cm
