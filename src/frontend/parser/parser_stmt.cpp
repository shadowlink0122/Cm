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
        debug::par::log(debug::par::Id::ReturnStmt, "", debug::Level::Trace);
        ast::ExprPtr value;
        if (!check(TokenKind::Semicolon)) {
            value = parse_expr();
        }
        expect(TokenKind::Semicolon);
        return ast::make_return(std::move(value), Span{start_pos, previous().end});
    }

    // if
    if (consume_if(TokenKind::KwIf)) {
        debug::par::log(debug::par::Id::IfStmt, "", debug::Level::Trace);
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
        return ast::make_switch(std::move(expr), std::move(cases), Span{start_pos, previous().end});
    }

    // for
    if (consume_if(TokenKind::KwFor)) {
        expect(TokenKind::LParen);

        // for-in構文かどうかを先読みで判定
        // for (Type var in iterable) または for (var in iterable)
        bool is_for_in = false;
        size_t lookahead = pos_;

        // 先読みで 'in' キーワードを探す
        // パターン1: Type var in ... (型指定あり)
        // パターン2: var in ... (型推論)

        // まず単純に識別子の後に'in'があるかチェック (型推論パターン)
        if (check(TokenKind::Ident)) {
            if (lookahead + 1 < tokens_.size() && tokens_[lookahead + 1].kind == TokenKind::KwIn) {
                is_for_in = true;
            }
        }

        // 型指定パターンをチェック
        if (!is_for_in && is_type_start()) {
            // 型をスキップして変数名と'in'を探す
            lookahead = pos_;

            // プリミティブ型をスキップ
            auto kind = tokens_[lookahead].kind;
            if (kind == TokenKind::KwInt || kind == TokenKind::KwUint ||
                kind == TokenKind::KwTiny || kind == TokenKind::KwUtiny ||
                kind == TokenKind::KwShort || kind == TokenKind::KwUshort ||
                kind == TokenKind::KwLong || kind == TokenKind::KwUlong ||
                kind == TokenKind::KwIsize || kind == TokenKind::KwUsize ||
                kind == TokenKind::KwFloat || kind == TokenKind::KwDouble ||
                kind == TokenKind::KwUfloat || kind == TokenKind::KwUdouble ||
                kind == TokenKind::KwBool || kind == TokenKind::KwChar ||
                kind == TokenKind::KwString || kind == TokenKind::KwCstring ||
                kind == TokenKind::KwVoid || kind == TokenKind::KwAuto) {
                lookahead++;  // 型キーワードをスキップ
            } else if (kind == TokenKind::Ident) {
                lookahead++;  // カスタム型名をスキップ
                // ジェネリック <...> をスキップ
                if (lookahead < tokens_.size() && tokens_[lookahead].kind == TokenKind::Lt) {
                    int depth = 1;
                    lookahead++;
                    while (lookahead < tokens_.size() && depth > 0) {
                        if (tokens_[lookahead].kind == TokenKind::Lt)
                            depth++;
                        else if (tokens_[lookahead].kind == TokenKind::Gt)
                            depth--;
                        lookahead++;
                    }
                }
            }

            // 配列 [N] をスキップ（多次元配列対応）
            while (lookahead < tokens_.size() && tokens_[lookahead].kind == TokenKind::LBracket) {
                lookahead++;
                if (lookahead < tokens_.size() &&
                    tokens_[lookahead].kind == TokenKind::IntLiteral) {
                    lookahead++;
                }
                if (lookahead < tokens_.size() && tokens_[lookahead].kind == TokenKind::RBracket) {
                    lookahead++;
                }
            }

            // ポインタ * をスキップ
            while (lookahead < tokens_.size() && tokens_[lookahead].kind == TokenKind::Star) {
                lookahead++;
            }

            // 変数名
            if (lookahead < tokens_.size() && tokens_[lookahead].kind == TokenKind::Ident) {
                lookahead++;
                // 'in' キーワード
                if (lookahead < tokens_.size() && tokens_[lookahead].kind == TokenKind::KwIn) {
                    is_for_in = true;
                }
            }
        }

        if (is_for_in) {
            // for-in構文をパース
            ast::TypePtr var_type;
            // 識別子の後に直接'in'があれば型推論
            bool has_explicit_type = !(check(TokenKind::Ident) && pos_ + 1 < tokens_.size() &&
                                       tokens_[pos_ + 1].kind == TokenKind::KwIn);
            if (has_explicit_type) {
                var_type = parse_type();
                // C++スタイルの配列サフィックスをチェック (int[3] など)
                var_type = check_array_suffix(std::move(var_type));
            }
            std::string var_name = expect_ident();
            expect(TokenKind::KwIn);
            auto iterable = parse_expr();
            expect(TokenKind::RParen);
            auto body = parse_block();

            auto stmt = std::make_unique<ast::ForInStmt>(std::move(var_name), std::move(var_type),
                                                         std::move(iterable), std::move(body));
            return std::make_unique<ast::Stmt>(std::move(stmt), Span{start_pos, previous().end});
        }

        // 通常のfor文
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

    // defer
    if (consume_if(TokenKind::KwDefer)) {
        // defer後の文を取得（セミコロンはその文で処理）
        auto body = parse_stmt();
        return ast::make_defer(std::move(body), Span{start_pos, previous().end});
    }

    // 変数宣言 (auto x = ... or type x ... or type x(args) or static type x = ...)
    // staticキーワードの後に型が来るかをチェック（static関数ではなくstatic変数）
    bool is_static_var = false;
    if (check(TokenKind::KwStatic)) {
        // 次のトークンを見て型開始かどうか判定
        size_t next_idx = pos_ + 1;
        if (next_idx < tokens_.size()) {
            TokenKind next_kind = tokens_[next_idx].kind;
            // 型開始トークンかどうか
            is_static_var = (next_kind == TokenKind::KwInt || next_kind == TokenKind::KwFloat ||
                             next_kind == TokenKind::KwDouble || next_kind == TokenKind::KwChar ||
                             next_kind == TokenKind::KwBool || next_kind == TokenKind::KwString ||
                             next_kind == TokenKind::KwCstring || next_kind == TokenKind::KwVoid ||
                             next_kind == TokenKind::KwTiny || next_kind == TokenKind::KwShort ||
                             next_kind == TokenKind::KwLong || next_kind == TokenKind::KwUint ||
                             next_kind == TokenKind::KwUtiny || next_kind == TokenKind::KwUshort ||
                             next_kind == TokenKind::KwUlong || next_kind == TokenKind::KwIsize ||
                             next_kind == TokenKind::KwUsize || next_kind == TokenKind::KwUfloat ||
                             next_kind == TokenKind::KwUdouble || next_kind == TokenKind::Ident);
        }
    }
    if (is_static_var) {
        advance();  // consume 'static'
    }

    if (is_static_var || check(TokenKind::KwConst) || is_type_start()) {
        bool is_const = consume_if(TokenKind::KwConst);
        if (is_const) {
            debug::par::log(debug::par::Id::ConstDecl, "Found const variable declaration",
                            debug::Level::Debug);
        } else if (is_static_var) {
            debug::par::log(debug::par::Id::VarDecl, "Found static variable declaration",
                            debug::Level::Debug);
        } else {
            debug::par::log(debug::par::Id::VarDecl, "Found variable declaration",
                            debug::Level::Debug);
        }

        auto type = parse_type();

        // C++スタイルの配列宣言をチェック: T[N] name;
        type = check_array_suffix(std::move(type));

        std::string name = expect_ident();
        debug::par::log(debug::par::Id::VarName, "Variable name: " + name, debug::Level::Debug);

        ast::ExprPtr init;
        std::vector<ast::ExprPtr> ctor_args;
        bool has_ctor_call = false;

        if (consume_if(TokenKind::Eq)) {
            debug::par::log(debug::par::Id::VarInit, "Variable has initializer",
                            debug::Level::Debug);
            init = parse_expr();
            debug::par::log(debug::par::Id::VarInitComplete,
                            "Variable initialization expression parsed", debug::Level::Debug);
        } else if (consume_if(TokenKind::LParen)) {
            // コンストラクタ呼び出し: Type name(args)
            debug::par::log(debug::par::Id::VarInit, "Variable has constructor call",
                            debug::Level::Debug);
            has_ctor_call = true;
            if (!check(TokenKind::RParen)) {
                do {
                    ctor_args.push_back(parse_expr());
                } while (consume_if(TokenKind::Comma));
            }
            expect(TokenKind::RParen);
        } else {
            debug::par::log(debug::par::Id::VarNoInit, "Variable declared without initializer",
                            debug::Level::Debug);
        }

        expect(TokenKind::Semicolon);
        std::string decl_msg = "Variable declaration complete: ";
        if (is_static_var) {
            decl_msg += "static ";
        }
        if (is_const) {
            decl_msg += "const ";
        }
        decl_msg += name;
        debug::par::log(debug::par::Id::VarDeclComplete, decl_msg, debug::Level::Debug);

        auto let_stmt = ast::make_let(std::move(name), std::move(type), std::move(init), is_const,
                                      Span{start_pos, previous().end}, is_static_var);

        // コンストラクタ引数を設定
        if (has_ctor_call) {
            if (auto* let = let_stmt->as<ast::LetStmt>()) {
                let->has_ctor_call = true;
                let->ctor_args = std::move(ctor_args);
            }
        }

        return let_stmt;
    }

    // 式文
    auto expr = parse_expr();
    expect(TokenKind::Semicolon);
    return ast::make_expr_stmt(std::move(expr), Span{start_pos, previous().end});
}

// 型の開始かどうか
bool Parser::is_type_start() {
    switch (current().kind) {
        case TokenKind::KwAuto:
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
        case TokenKind::KwIsize:
        case TokenKind::KwUsize:
        case TokenKind::KwFloat:
        case TokenKind::KwDouble:
        case TokenKind::KwUfloat:
        case TokenKind::KwUdouble:
        case TokenKind::KwChar:
        case TokenKind::KwString:
        case TokenKind::KwCstring:
            return true;
        case TokenKind::Star:
            // *type name の形式かチェック（*p = x のような式と区別）
            if (pos_ + 1 < tokens_.size()) {
                auto next_kind = tokens_[pos_ + 1].kind;
                // *の後に型キーワードまたは識別子が来て、
                // さらにその後に識別子が来れば型宣言
                if (next_kind == TokenKind::KwInt || next_kind == TokenKind::KwFloat ||
                    next_kind == TokenKind::KwDouble || next_kind == TokenKind::KwUfloat ||
                    next_kind == TokenKind::KwUdouble || next_kind == TokenKind::KwChar ||
                    next_kind == TokenKind::KwBool || next_kind == TokenKind::KwString ||
                    next_kind == TokenKind::KwCstring || next_kind == TokenKind::KwIsize ||
                    next_kind == TokenKind::KwUsize || next_kind == TokenKind::KwVoid ||
                    next_kind == TokenKind::Ident) {
                    // *int name or *Type name の形式
                    if (pos_ + 2 < tokens_.size() && tokens_[pos_ + 2].kind == TokenKind::Ident) {
                        return true;
                    }
                }
            }
            return false;  // *p = ... のような式
        case TokenKind::Amp:
        case TokenKind::LBracket:
            return true;
        case TokenKind::Ident:
            // 識別子の後に識別子が来たら変数宣言 (Type name)
            // 識別子の後に::が来たら名前空間修飾型の可能性 (ns::Type name)
            // 識別子の後に<が来たらジェネリック型の可能性 (Type<T> name)
            // 識別子の後に[が来たら配列型の可能性 (Type[N] name)
            // 識別子の後に*が来たらポインタ型の可能性 (Type* name)
            if (pos_ + 1 < tokens_.size()) {
                auto next_kind = tokens_[pos_ + 1].kind;
                if (next_kind == TokenKind::Ident) {
                    return true;
                }
                // 名前空間修飾型: ns::Type name
                if (next_kind == TokenKind::ColonColon) {
                    // :: の後をスキップして変数名があるかチェック
                    size_t i = pos_ + 2;
                    // ns::ns2::...::Type パターンをスキップ
                    while (i + 1 < tokens_.size() && tokens_[i].kind == TokenKind::Ident &&
                           tokens_[i + 1].kind == TokenKind::ColonColon) {
                        i += 2;  // Ident:: をスキップ
                    }
                    // 最後の型名をチェック
                    if (i < tokens_.size() && tokens_[i].kind == TokenKind::Ident) {
                        i++;
                        // 型名の後に変数名があるかチェック
                        if (i < tokens_.size() && tokens_[i].kind == TokenKind::Ident) {
                            return true;
                        }
                        // ジェネリック型: ns::Type<T> name
                        if (i < tokens_.size() && tokens_[i].kind == TokenKind::Lt) {
                            // <...> をスキップ
                            i++;
                            int depth = 1;
                            while (i < tokens_.size() && depth > 0) {
                                if (tokens_[i].kind == TokenKind::Lt)
                                    depth++;
                                else if (tokens_[i].kind == TokenKind::Gt)
                                    depth--;
                                i++;
                            }
                            if (depth == 0 && i < tokens_.size() &&
                                tokens_[i].kind == TokenKind::Ident) {
                                return true;
                            }
                        }
                    }
                }
                // ポインタ型: Type* name
                if (next_kind == TokenKind::Star) {
                    // * の後に識別子があれば変数宣言
                    if (pos_ + 2 < tokens_.size() && tokens_[pos_ + 2].kind == TokenKind::Ident) {
                        return true;
                    }
                }
                // 配列型: Type[N] name
                if (next_kind == TokenKind::LBracket) {
                    // [N] の後に識別子があるかチェック
                    size_t i = pos_ + 2;
                    // 配列サイズをスキップ
                    if (i < tokens_.size() && tokens_[i].kind == TokenKind::IntLiteral) {
                        i++;
                    }
                    // ] を期待
                    if (i < tokens_.size() && tokens_[i].kind == TokenKind::RBracket) {
                        i++;
                        // ] の後に識別子があれば変数宣言
                        if (i < tokens_.size() && tokens_[i].kind == TokenKind::Ident) {
                            return true;
                        }
                    }
                }
                // ジェネリック型: Type<...> name
                if (next_kind == TokenKind::Lt) {
                    // <...> の後に識別子があるかチェック
                    // 簡易チェック: <>のネストを追跡して、閉じた後に識別子があるか
                    size_t i = pos_ + 2;
                    int depth = 1;
                    while (i < tokens_.size() && depth > 0) {
                        if (tokens_[i].kind == TokenKind::Lt) {
                            depth++;
                        } else if (tokens_[i].kind == TokenKind::Gt) {
                            depth--;
                        }
                        i++;
                    }
                    // depth == 0 なら閉じている
                    if (depth == 0 && i < tokens_.size()) {
                        // ジェネリック型の後に[N]が来る可能性もチェック
                        if (tokens_[i].kind == TokenKind::LBracket) {
                            // [N]をスキップ
                            i++;
                            if (i < tokens_.size() && tokens_[i].kind == TokenKind::IntLiteral) {
                                i++;
                            }
                            if (i < tokens_.size() && tokens_[i].kind == TokenKind::RBracket) {
                                i++;
                            }
                        }
                        if (tokens_[i].kind == TokenKind::Ident) {
                            return true;
                        }
                    }
                }
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
