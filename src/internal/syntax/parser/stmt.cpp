#include "internal/base/debug/par.hpp"
#include "internal/base/i18n.hpp"
#include "parser.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace cm {

// ============================================================
// 文の解析
// ============================================================
ast::StmtPtr Parser::parse_stmt() {
    // RAIIガード: スコープ終了時にdepthを自動デクリメント
    parse_depth_++;
    if (parse_depth_ > max_parse_depth_)
        max_parse_depth_ = parse_depth_;
    struct DepthGuard {
        int& d;
        ~DepthGuard() { d--; }
    } _dg{parse_depth_};

    // 再帰深度制限
    if (parse_depth_ > 500) {
        error(i18n::msg(i18n::MsgId::ParseRecursionDepthExceededTheLimit));
        return nullptr;
    }
    debug::par::log(debug::par::Id::Stmt, "", debug::Level::Trace);
    uint32_t start_pos = current().start;

    // R11: volatile/constexprはローカル宣言でも未対応。専用診断を出しつつ後続を通常宣言として解析続行する（従来はExpected expression等の無関係なエラーに化けていた）
    if (consume_if(TokenKind::KwVolatile)) {
        error(i18n::msg(i18n::MsgId::PsVolatileUnsupported));
    }
    if (consume_if(TokenKind::KwConstexpr)) {
        error(i18n::msg(i18n::MsgId::PsConstexprVarUnsupported));
    }

    // SVのcase修飾属性（SV-N3）: switch/match文の直前の #[sv::priority] / #[sv::unique0] のみ受理する
    uint8_t sv_case_modifier = 0;
    while (check(TokenKind::Hash) && pos_ + 1 < tokens_.size() &&
           tokens_[pos_ + 1].kind == TokenKind::LBracket) {
        auto attr = parse_attribute();
        if (attr.name == "sv::priority") {
            sv_case_modifier = 1;
        } else if (attr.name == "sv::unique0") {
            sv_case_modifier = 2;
        } else {
            error(i18n::msgf(i18n::MsgId::PsStmtAttributeOnlyCaseModifier, attr.name));
        }
    }
    if (sv_case_modifier != 0 && !check(TokenKind::KwSwitch) && !check(TokenKind::KwMatch)) {
        error(i18n::msgf(i18n::MsgId::PsStmtAttributeOnlyCaseModifier,
                         sv_case_modifier == 1 ? "sv::priority" : "sv::unique0"));
        sv_case_modifier = 0;
    }

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

    // must {} ブロック（最適化禁止）
    if (consume_if(TokenKind::KwMust)) {
        debug::par::log(debug::par::Id::Stmt, "must block", debug::Level::Trace);
        auto body = parse_block();
        return ast::make_must(std::move(body), Span{start_pos, previous().end});
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
                    error(i18n::msg(i18n::MsgId::ParseDuplicateElseClause));
                }
                has_else = true;

                // else { stmts }
                auto stmts = parse_block();
                cases.emplace_back(nullptr, std::move(stmts));
            } else {
                error(i18n::msg(i18n::MsgId::ParseASwitchStatementRequiresCase));
                // エラー回復: 次のcase/else/}まで進める
                while (!check(TokenKind::KwCase) && !check(TokenKind::KwElse) &&
                       !check(TokenKind::RBrace) && !is_at_end()) {
                    advance();
                }
            }
        }

        expect(TokenKind::RBrace);
        auto switch_stmt =
            ast::make_switch(std::move(expr), std::move(cases), Span{start_pos, previous().end});
        // SVのcase修飾属性を反映（SV-N3）
        if (sv_case_modifier != 0) {
            if (auto* sw = switch_stmt->as<ast::SwitchStmt>()) {
                sw->sv_case_modifier = sv_case_modifier;
            }
        }
        return switch_stmt;
    }

    // v0.13.0: match文（セミコロン不要のブロックベース構文）
    if (consume_if(TokenKind::KwMatch)) {
        debug::par::log(debug::par::Id::Stmt, "match statement", debug::Level::Trace);
        auto match_expr = parse_match_expr(start_pos);
        // SVのcase修飾属性を反映（SV-N3）
        if (sv_case_modifier != 0 && match_expr) {
            if (auto* me = match_expr->as<ast::MatchExpr>()) {
                me->sv_case_modifier = sv_case_modifier;
            }
        }
        // ExprStmtとしてラップ（セミコロンは不要）
        return ast::make_expr_stmt(std::move(match_expr), Span{start_pos, previous().end});
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
                        else if (tokens_[lookahead].kind == TokenKind::GtGt)
                            depth -= 2;  // ネストジェネリクス対応
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
                var_type = parse_type_with_union();
            }
            std::string var_name = expect_ident();
            reject_reserved_ident(var_name);
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

        auto type = parse_type_with_union();

        // 名前のスパンを記録（Lint警告用）
        uint32_t name_start = current().start;
        std::string name = expect_ident();
        reject_reserved_ident(name);
        uint32_t name_end = previous().end;
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

        // 名前のスパンを設定
        if (auto* let = let_stmt->as<ast::LetStmt>()) {
            let->name_span = Span{name_start, name_end};
        }

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
        // SV固有キーワード（SVモード時のキーワードトークン対応）
        case TokenKind::KwPosedge:
        case TokenKind::KwNegedge:
        case TokenKind::KwWire:
        case TokenKind::KwReg:
        case TokenKind::KwBit:
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
            // &type name の形式かチェック（&x のようなアドレス取得式と区別する。局所処理調査B2）。
            // 従来は先頭 & を無条件に参照型扱いにしていたため typeof(&x) が参照型 "&x" になっていた（*には同じ先読みガードがある）
            if (pos_ + 1 < tokens_.size()) {
                auto next_kind = tokens_[pos_ + 1].kind;
                if (next_kind == TokenKind::KwInt || next_kind == TokenKind::KwFloat ||
                    next_kind == TokenKind::KwDouble || next_kind == TokenKind::KwUfloat ||
                    next_kind == TokenKind::KwUdouble || next_kind == TokenKind::KwChar ||
                    next_kind == TokenKind::KwBool || next_kind == TokenKind::KwString ||
                    next_kind == TokenKind::KwCstring || next_kind == TokenKind::KwIsize ||
                    next_kind == TokenKind::KwUsize || next_kind == TokenKind::KwVoid ||
                    next_kind == TokenKind::Ident) {
                    // &int name or &Type name の形式（その後に識別子が続けば宣言）
                    if (pos_ + 2 < tokens_.size() && tokens_[pos_ + 2].kind == TokenKind::Ident) {
                        return true;
                    }
                }
            }
            return false;  // &x のようなアドレス取得式
        case TokenKind::LBracket:
            return true;
        case TokenKind::KwTypeof: {
            // typeof(...) の直後（ポインタ/配列サフィックスを挟んで）に識別子が来れば宣言（typeof(x) name）。
            // 式文 typeof(x); とは区別する（局所処理調査A1のtypeof分。paren宣言(int) kはB系のtypeof解決に依存しないため別課題）
            if (peek_kind() != TokenKind::LParen) {
                return false;
            }
            size_t i = pos_ + 2;  // 'typeof' '(' の次
            int depth = 1;
            while (i < tokens_.size() && depth > 0) {
                if (tokens_[i].kind == TokenKind::LParen) {
                    depth++;
                } else if (tokens_[i].kind == TokenKind::RParen) {
                    depth--;
                } else if (tokens_[i].kind == TokenKind::Eof) {
                    break;
                }
                i++;
            }
            if (depth != 0) {
                return false;
            }
            // 閉じ括弧後のポインタ/配列サフィックスをスキップ
            while (i < tokens_.size() &&
                   (tokens_[i].kind == TokenKind::Star || tokens_[i].kind == TokenKind::LBracket ||
                    tokens_[i].kind == TokenKind::RBracket)) {
                i++;
            }
            return i < tokens_.size() && tokens_[i].kind == TokenKind::Ident;
        }
        case TokenKind::LParen: {
            // 括弧付き型の宣言（局所処理調査A1のparen分）: (int) k = 5; ・ (int | string) u = 1; を宣言と認識する。
            // 括弧を閉じた直後（ポインタ/配列サフィックスを挟んで）に識別子が来る形は式として不成立（(expr) ident は無効）のため宣言と確定できる。
            // 型として無効な中身（(a+b) c 等）は宣言経路のparse_typeが診断する
            size_t i = pos_ + 1;  // '(' の次
            int depth = 1;
            while (i < tokens_.size() && depth > 0) {
                if (tokens_[i].kind == TokenKind::LParen) {
                    depth++;
                } else if (tokens_[i].kind == TokenKind::RParen) {
                    depth--;
                } else if (tokens_[i].kind == TokenKind::Eof) {
                    break;
                }
                i++;
            }
            if (depth != 0) {
                return false;
            }
            // 閉じ括弧後のポインタ/配列サフィックスをスキップ（typeof宣言の先読みと同型）
            while (i < tokens_.size() &&
                   (tokens_[i].kind == TokenKind::Star || tokens_[i].kind == TokenKind::LBracket ||
                    tokens_[i].kind == TokenKind::RBracket)) {
                i++;
            }
            return i < tokens_.size() && tokens_[i].kind == TokenKind::Ident;
        }
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
                                else if (tokens_[i].kind == TokenKind::GtGt)
                                    depth -= 2;  // ネストジェネリクス対応
                                i++;
                            }
                            if (depth <= 0 && i < tokens_.size() &&
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
                // 配列型: Type[N] name / Type[N][M] name / Type[] name（多次元・スライス連鎖に対応）。
                // 添字が整数リテラルまたは空のブラケット連鎖に限って型サフィックスとみなすため、
                // 変数への添字式（arr[i]・arr[2][j]）と衝突しない
                if (next_kind == TokenKind::LBracket) {
                    size_t i = pos_ + 1;
                    bool suffix_ok = true;
                    while (i < tokens_.size() && tokens_[i].kind == TokenKind::LBracket) {
                        i++;
                        if (i < tokens_.size() && tokens_[i].kind == TokenKind::IntLiteral) {
                            i++;
                        }
                        if (i < tokens_.size() && tokens_[i].kind == TokenKind::RBracket) {
                            i++;
                        } else {
                            suffix_ok = false;
                            break;
                        }
                    }
                    if (suffix_ok && i < tokens_.size() && tokens_[i].kind == TokenKind::Ident) {
                        return true;
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
                        } else if (tokens_[i].kind == TokenKind::GtGt) {
                            // ネストジェネリクス対応: >> は2つの > として処理
                            depth -= 2;
                        }
                        i++;
                    }
                    // depth == 0 なら閉じている（depth < 0 は >> で過剰消費した場合）
                    if (depth <= 0 && i < tokens_.size()) {
                        // ジェネリック型の後の[N]/[]サフィックス連鎖（多次元・スライス）をスキップ
                        while (i < tokens_.size() && tokens_[i].kind == TokenKind::LBracket) {
                            i++;
                            if (i < tokens_.size() && tokens_[i].kind == TokenKind::IntLiteral) {
                                i++;
                            }
                            if (i < tokens_.size() && tokens_[i].kind == TokenKind::RBracket) {
                                i++;
                            } else {
                                break;
                            }
                        }
                        // ポインタ型をスキップ: Type<T>* name や Type<T>** name
                        while (i < tokens_.size() && tokens_[i].kind == TokenKind::Star) {
                            i++;
                        }
                        if (i < tokens_.size() && tokens_[i].kind == TokenKind::Ident) {
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
