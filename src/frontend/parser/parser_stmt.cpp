#include "parser.hpp"

namespace cm {

// ============================================================
// 文の解析
// ============================================================
ast::StmtPtr Parser::parse_stmt() {
    debug::par::log(debug::par::Id::Stmt, "", debug::Level::Trace);
    uint32_t start_pos = current().start;

    // ブロック
    if (check(TokenKind::LBrace)) {
        auto stmts = parse_block();
        return ast::make_block(std::move(stmts), Span{start_pos, previous().end});
    }

    // return
    if (consume_if(TokenKind::KwReturn)) {
        ast::ExprPtr value;
        if (!check(TokenKind::Semicolon)) {
            value = parse_expr();
        }
        expect(TokenKind::Semicolon);
        return ast::make_return(std::move(value), Span{start_pos, previous().end});
    }

    // if
    if (consume_if(TokenKind::KwIf)) {
        expect(TokenKind::LParen);
        auto cond = parse_expr();
        expect(TokenKind::RParen);
        auto then_block = parse_block();

        std::vector<ast::StmtPtr> else_block;
        if (consume_if(TokenKind::KwElse)) {
            if (check(TokenKind::KwIf)) {
                // else if
                auto elif = parse_stmt();
                else_block.push_back(std::move(elif));
            } else {
                else_block = parse_block();
            }
        }

        return ast::make_if(std::move(cond), std::move(then_block), std::move(else_block),
                            Span{start_pos, previous().end});
    }

    // while
    if (consume_if(TokenKind::KwWhile)) {
        expect(TokenKind::LParen);
        auto cond = parse_expr();
        expect(TokenKind::RParen);
        auto body = parse_block();
        return ast::make_while(std::move(cond), std::move(body), Span{start_pos, previous().end});
    }

    // switch
    if (consume_if(TokenKind::KwSwitch)) {
        expect(TokenKind::LParen);
        auto expr = parse_expr();
        expect(TokenKind::RParen);
        expect(TokenKind::LBrace);

        std::vector<ast::SwitchCase> cases;
        bool has_else = false;

        while (!check(TokenKind::RBrace) && !is_at_end()) {
            if (consume_if(TokenKind::KwCase)) {
                // case(pattern) { stmts }
                expect(TokenKind::LParen);
                auto pattern = parse_pattern();
                expect(TokenKind::RParen);

                auto stmts = parse_block();
                cases.emplace_back(std::move(pattern), std::move(stmts));
            } else if (consume_if(TokenKind::KwElse)) {
                if (has_else) {
                    error("重複するelse節");
                }
                has_else = true;

                // else { stmts }
                auto stmts = parse_block();
                cases.emplace_back(nullptr, std::move(stmts));
            } else {
                error("switch文内にはcaseまたはelseが必要です");
            }
        }

        expect(TokenKind::RBrace);
        return ast::make_switch(std::move(expr), std::move(cases),
                               Span{start_pos, previous().end});
    }

    // for
    if (consume_if(TokenKind::KwFor)) {
        expect(TokenKind::LParen);

        // 初期化
        ast::StmtPtr init;
        if (!check(TokenKind::Semicolon)) {
            if (check(TokenKind::KwConst) || is_type_start()) {
                init = parse_stmt();  // let文
            } else {
                auto expr = parse_expr();
                expect(TokenKind::Semicolon);
                init = ast::make_expr_stmt(std::move(expr));
            }
        } else {
            expect(TokenKind::Semicolon);
        }

        // 条件
        ast::ExprPtr cond;
        if (!check(TokenKind::Semicolon)) {
            cond = parse_expr();
        }
        expect(TokenKind::Semicolon);

        // 更新
        ast::ExprPtr update;
        if (!check(TokenKind::RParen)) {
            update = parse_expr();
        }
        expect(TokenKind::RParen);

        auto body = parse_block();

        auto stmt = std::make_unique<ast::ForStmt>(std::move(init), std::move(cond),
                                                   std::move(update), std::move(body));
        return std::make_unique<ast::Stmt>(std::move(stmt), Span{start_pos, previous().end});
    }

    // break
    if (consume_if(TokenKind::KwBreak)) {
        expect(TokenKind::Semicolon);
        return ast::make_break(Span{start_pos, previous().end});
    }

    // continue
    if (consume_if(TokenKind::KwContinue)) {
        expect(TokenKind::Semicolon);
        return ast::make_continue(Span{start_pos, previous().end});
    }

    // 変数宣言 (auto x = ... or type x ...)
    if (check(TokenKind::KwConst) || is_type_start()) {
        bool is_const = consume_if(TokenKind::KwConst);
        auto type = parse_type();
        std::string name = expect_ident();

        ast::ExprPtr init;
        if (consume_if(TokenKind::Eq)) {
            init = parse_expr();
        }

        expect(TokenKind::Semicolon);
        return ast::make_let(std::move(name), std::move(type), std::move(init), is_const,
                             Span{start_pos, previous().end});
    }

    // 式文
    auto expr = parse_expr();
    expect(TokenKind::Semicolon);
    return ast::make_expr_stmt(std::move(expr), Span{start_pos, previous().end});
}

// 型の開始かどうか
bool Parser::is_type_start() {
    switch (current().kind) {
        case TokenKind::KwVoid:
        case TokenKind::KwBool:
        case TokenKind::KwTiny:
        case TokenKind::KwShort:
        case TokenKind::KwInt:
        case TokenKind::KwLong:
        case TokenKind::KwUtiny:
        case TokenKind::KwUshort:
        case TokenKind::KwUint:
        case TokenKind::KwUlong:
        case TokenKind::KwFloat:
        case TokenKind::KwDouble:
        case TokenKind::KwChar:
        case TokenKind::KwString:
        case TokenKind::Star:
        case TokenKind::Amp:
        case TokenKind::LBracket:
            return true;
        case TokenKind::Ident:
            // 識別子の後に識別子が来たら変数宣言
            if (pos_ + 1 < tokens_.size()) {
                return tokens_[pos_ + 1].kind == TokenKind::Ident;
            }
            return false;
        default:
            return false;
    }
}

// パターンの解析（switch文用）
std::unique_ptr<ast::Pattern> Parser::parse_pattern() {
    std::vector<std::unique_ptr<ast::Pattern>> or_patterns;

    // 最初のパターン要素を解析
    auto first_pattern = parse_pattern_element();
    or_patterns.push_back(std::move(first_pattern));

    // ORパターン（|）をチェック
    while (consume_if(TokenKind::Pipe)) {
        auto next_pattern = parse_pattern_element();
        or_patterns.push_back(std::move(next_pattern));
    }

    // 単一パターンの場合はそのまま返す
    if (or_patterns.size() == 1) {
        return std::move(or_patterns[0]);
    }

    // 複数パターンの場合はORパターンとして返す
    return ast::Pattern::make_or(std::move(or_patterns));
}

// パターン要素の解析（単一値または範囲）
std::unique_ptr<ast::Pattern> Parser::parse_pattern_element() {
    // 値を解析
    auto first_value = parse_primary();  // リテラルまたは識別子

    // 範囲パターン（...）をチェック
    if (consume_if(TokenKind::Ellipsis)) {
        auto end_value = parse_primary();
        return ast::Pattern::make_range(std::move(first_value), std::move(end_value));
    }

    // 単一値パターン
    return ast::Pattern::make_value(std::move(first_value));
}

}  // namespace cm
