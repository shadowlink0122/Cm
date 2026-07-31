// ============================================================
// モジュール関連パーサ - マクロ定義・ディレクティブ・アトリビュートの解析
// ============================================================

#include "internal/base/debug/par.hpp"
#include "internal/base/i18n.hpp"
#include "internal/syntax/ast/module.hpp"
#include "internal/syntax/parser/parser.hpp"

#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cm {

// ============================================================
// マクロ定義 (v0.13.0)
// ============================================================
// 構文: macro TYPE NAME = EXPR;
// 例: macro int VERSION = 13;
//     macro string NAME = "Cm";
//     macro int*(int, int) add = (a, b) => a + b;  <- 関数マクロ
ast::DeclPtr Parser::parse_macro(bool is_exported) {
    expect(TokenKind::KwMacro);
    uint32_t start_pos = previous().start;

    // 型をパース
    auto type = parse_type_with_union();

    // マクロ名
    std::string name = expect_ident();
    debug::par::log(debug::par::Id::MacroDef, "Parsing typed macro: " + name, debug::Level::Debug);

    // = を期待
    expect(TokenKind::Eq);

    // 値をパース（定数式）
    auto value = parse_expr();

    // セミコロン
    expect(TokenKind::Semicolon);

    // v0.13.0: ラムダ式マクロの場合は関数として変換
    if (auto* lambda = value->as<ast::LambdaExpr>()) {
        debug::par::log(debug::par::Id::MacroDef, "Converting lambda macro to function: " + name,
                        debug::Level::Debug);

        // 戻り値型を決定（関数ポインタ型から取得またはラムダから）
        ast::TypePtr return_type;
        if (type->kind == ast::TypeKind::Function) {
            return_type = type->return_type;
        } else if (lambda->return_type) {
            return_type = lambda->return_type;
        } else {
            return_type = ast::make_int();  // デフォルト
        }

        // パラメータをムーブ（ラムダからはconst参照なので直接ムーブは不可、新規作成）
        // lambda->params のベクトルを直接ムーブ
        std::vector<ast::Param> params = std::move(lambda->params);

        // ボディを変換
        std::vector<ast::StmtPtr> body;
        if (lambda->is_expr_body()) {
            // 式形式: => expr を return expr; に変換
            auto ret = std::make_unique<ast::ReturnStmt>();
            ret->value = std::move(std::get<ast::ExprPtr>(lambda->body));
            auto stmt =
                std::make_unique<ast::Stmt>(std::move(ret), Span{start_pos, previous().end});
            body.push_back(std::move(stmt));
        } else {
            // ブロック形式
            body = std::move(std::get<std::vector<ast::StmtPtr>>(lambda->body));
        }

        // FunctionDeclを作成（4引数コンストラクタを使用）
        auto func = std::make_unique<ast::FunctionDecl>(std::move(name), std::move(params),
                                                        std::move(return_type), std::move(body));
        func->visibility = is_exported ? ast::Visibility::Export : ast::Visibility::Private;

        return std::make_unique<ast::Decl>(std::move(func), Span{start_pos, previous().end});
    }

    // MacroDeclノードを作成（リテラル定数の場合）
    auto macro_decl =
        std::make_unique<ast::MacroDecl>(std::move(name), std::move(type), std::move(value));
    macro_decl->is_exported = is_exported;

    return std::make_unique<ast::Decl>(std::move(macro_decl), Span{start_pos, previous().end});
}

// ============================================================
// 関数ディレクティブ（#test, #bench, #deprecated, #inline, #optimize）
// ============================================================
ast::AttributeNode Parser::parse_directive() {
    expect(TokenKind::Hash);

    // ディレクティブ名
    std::string directive_name = expect_ident();
    std::vector<std::string> args;

    // 引数がある場合
    if (consume_if(TokenKind::LParen)) {
        do {
            if (consume_if(TokenKind::Bang)) {
                args.push_back("!" + expect_ident());
            } else if (check(TokenKind::StringLiteral)) {
                args.push_back(std::string(current().get_string()));
                advance();
            } else if (check(TokenKind::IntLiteral)) {
                args.push_back(std::to_string(current().get_int()));
                advance();
            } else {
                // 識別子 または 名前付き引数（key: value）
                std::string ident = expect_ident();
                if (consume_if(TokenKind::Colon)) {
                    // #[sv::pin("U12", io_type: "LVCMOS33", drive: 8)] 形式。
                    // "key:value" の1引数として保持する
                    std::string value;
                    if (check(TokenKind::StringLiteral)) {
                        value = std::string(current().get_string());
                        advance();
                    } else if (check(TokenKind::IntLiteral)) {
                        value = std::to_string(current().get_int());
                        advance();
                    } else {
                        value = expect_ident();
                    }
                    args.push_back(ident + ":" + value);
                } else {
                    args.push_back(ident);
                }
            }
        } while (consume_if(TokenKind::Comma));

        expect(TokenKind::RParen);
    }

    if (args.empty()) {
        return ast::AttributeNode(std::move(directive_name));
    } else {
        return ast::AttributeNode(std::move(directive_name), std::move(args));
    }
}

// ============================================================
// アトリビュート
// ============================================================
ast::AttributeNode Parser::parse_attribute() {
    if (consume_if(TokenKind::At)) {
        // @[...] 形式
    } else if (consume_if(TokenKind::Hash)) {
        // #[...] 形式
    } else {
        error(i18n::msg(i18n::MsgId::PsExpectedAttributeStart));
    }
    expect(TokenKind::LBracket);

    // アトリビュート名(名前空間付き: sv::pin等)
    std::string attr_name = expect_ident();
    while (consume_if(TokenKind::ColonColon)) {
        attr_name += "::" + expect_ident();
    }
    std::vector<std::string> args;

    // 引数がある場合
    if (consume_if(TokenKind::LParen)) {
        do {
            if (consume_if(TokenKind::Bang)) {
                args.push_back("!" + expect_ident());
            } else if (check(TokenKind::StringLiteral)) {
                args.push_back(std::string(current().get_string()));
                advance();
            } else if (check(TokenKind::IntLiteral)) {
                args.push_back(std::to_string(current().get_int()));
                advance();
            } else {
                // 識別子 または 名前付き引数（key: value）
                std::string ident = expect_ident();
                if (consume_if(TokenKind::Colon)) {
                    // #[sv::pin("U12", io_type: "LVCMOS33", drive: 8)] 形式。
                    // "key:value" の1引数として保持する
                    std::string value;
                    if (check(TokenKind::StringLiteral)) {
                        value = std::string(current().get_string());
                        advance();
                    } else if (check(TokenKind::IntLiteral)) {
                        value = std::to_string(current().get_int());
                        advance();
                    } else {
                        value = expect_ident();
                    }
                    args.push_back(ident + ":" + value);
                } else {
                    args.push_back(ident);
                }
            }
        } while (consume_if(TokenKind::Comma));

        expect(TokenKind::RParen);
    }

    // cfg属性の場合、条件式を解析
    if (attr_name == "cfg") {
        if (consume_if(TokenKind::LParen)) {
            // 条件式を文字列として保存（簡易実装）
            int paren_count = 1;
            std::string condition;

            while (paren_count > 0 && !is_at_end()) {
                if (check(TokenKind::LParen)) {
                    paren_count++;
                } else if (check(TokenKind::RParen)) {
                    paren_count--;
                    if (paren_count == 0)
                        break;
                }
                condition += current_text() + " ";
                advance();
            }

            args.push_back(condition);
            expect(TokenKind::RParen);
        }
    }

    expect(TokenKind::RBracket);

    if (args.empty()) {
        return ast::AttributeNode(std::move(attr_name));
    } else {
        return ast::AttributeNode(std::move(attr_name), std::move(args));
    }
}

}  // namespace cm
