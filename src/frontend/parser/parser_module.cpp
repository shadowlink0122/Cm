// モジュール関連のパーサー実装

#include "parser.hpp"
#include "../ast/module.hpp"
#include "../../common/debug/par.hpp"

namespace cm {

// ============================================================
// モジュール宣言
// ============================================================
ast::DeclPtr Parser::parse_module() {
    uint32_t start_pos = current().start;
    expect(TokenKind::KwModule);

    ast::ModulePath path;
    path.segments.push_back(expect_ident());

    while (consume_if(TokenKind::Dot)) {
        path.segments.push_back(expect_ident());
    }

    expect(TokenKind::Semicolon);

    auto decl = std::make_unique<ast::ModuleDecl>(std::move(path));
    return std::make_unique<ast::Decl>(std::move(decl), Span{start_pos, previous().end});
}

// ============================================================
// Import文
// ============================================================
ast::DeclPtr Parser::parse_import_stmt() {
    uint32_t start_pos = current().start;
    expect(TokenKind::KwImport);

    // モジュールパス
    ast::ModulePath path;
    path.segments.push_back(expect_ident());
    while (consume_if(TokenKind::Dot)) {
        path.segments.push_back(expect_ident());
    }

    ast::ImportDecl import_decl(std::move(path));

    // インポートアイテム
    if (consume_if(TokenKind::Dot)) {
        if (consume_if(TokenKind::Star)) {
            // ワイルドカードインポート: import std.io.*;
            import_decl.is_wildcard = true;
        } else if (consume_if(TokenKind::LBrace)) {
            // 複数アイテムインポート: import std.io.{print, println};
            do {
                std::string name = expect_ident();
                std::optional<std::string> alias;

                // エイリアス: print as p
                if (check(TokenKind::Ident) && current_text() == "as") {
                    advance();
                    alias = expect_ident();
                }

                import_decl.items.push_back(ast::ImportItem(std::move(name), std::move(alias)));
            } while (consume_if(TokenKind::Comma));

            expect(TokenKind::RBrace);
        } else {
            // 単一アイテムインポート: import std.io.print;
            std::string name = expect_ident();
            std::optional<std::string> alias;

            // エイリアス: import std.io.print as p;
            if (check(TokenKind::Ident) && current_text() == "as") {
                advance();
                alias = expect_ident();
            }

            import_decl.items.push_back(ast::ImportItem(std::move(name), std::move(alias)));
        }
    } else {
        // モジュール全体のインポート: import std.io;
        // またはエイリアス付き: import std.io as io;
        if (check(TokenKind::Ident) && current_text() == "as") {
            advance();
            std::string alias = expect_ident();
            import_decl.items.push_back(ast::ImportItem("", alias));
        }
    }

    expect(TokenKind::Semicolon);

    return std::make_unique<ast::Decl>(
        std::make_unique<ast::ImportDecl>(std::move(import_decl)),
        Span{start_pos, previous().end}
    );
}

// ============================================================
// Export文
// ============================================================
ast::DeclPtr Parser::parse_export() {
    uint32_t start_pos = current().start;
    bool has_export = consume_if(TokenKind::KwExport);

    if (!has_export) {
        return nullptr;
    }

    // export後の宣言を解析
    ast::DeclPtr inner_decl = nullptr;

    // export const int x = 10;
    if (check(TokenKind::KwConst)) {
        inner_decl = parse_const_decl();
    }
    // export struct Point { ... }
    else if (check(TokenKind::KwStruct)) {
        inner_decl = parse_struct(true);
    }
    // export interface Drawable { ... }
    else if (check(TokenKind::KwInterface)) {
        inner_decl = parse_interface(true);
    }
    // export template<...> ...
    else if (check(TokenKind::KwTemplate)) {
        inner_decl = parse_template_decl();
    }
    // export enum Direction { ... }
    else if (check(TokenKind::KwEnum)) {
        inner_decl = parse_enum_decl();
    }
    // export関数
    else {
        bool is_static = consume_if(TokenKind::KwStatic);
        bool is_inline = consume_if(TokenKind::KwInline);
        bool is_async = consume_if(TokenKind::KwAsync);
        inner_decl = parse_function(true, is_static, is_inline, is_async);
    }

    if (!inner_decl) {
        error("Expected declaration after 'export'");
        return nullptr;
    }

    // ExportDeclでラップ
    auto export_decl = std::make_unique<ast::ExportDecl>(std::move(inner_decl));
    return std::make_unique<ast::Decl>(
        std::move(export_decl),
        Span{start_pos, previous().end}
    );
}

// ============================================================
// Use文
// ============================================================
ast::DeclPtr Parser::parse_use() {
    uint32_t start_pos = current().start;
    expect(TokenKind::KwUse);

    // パス解析
    ast::ModulePath path;
    path.segments.push_back(expect_ident());
    while (consume_if(TokenKind::ColonColon)) {
        path.segments.push_back(expect_ident());
    }

    // エイリアス
    std::optional<std::string> alias;
    if (check(TokenKind::Ident) && current_text() == "as") {
        advance();
        alias = expect_ident();
    }

    ast::UseDecl use_decl(std::move(path), std::move(alias));

    expect(TokenKind::Semicolon);

    return std::make_unique<ast::Decl>(
        std::make_unique<ast::UseDecl>(std::move(use_decl)),
        Span{start_pos, previous().end}
    );
}

// ============================================================
// マクロ定義
// ============================================================
ast::DeclPtr Parser::parse_macro() {
    uint32_t start_pos = current().start;

    // 新しい構文: #macro keyword
    if (check(TokenKind::Hash)) {
        advance();  // consume '#'

        if (!check(TokenKind::KwMacro)) {
            // #が単独で出現した場合はnullptrを返す
            return nullptr;
        }
        advance();  // consume 'macro'

        // 戻り値の型（voidやautoなど）
        parse_type();

        // マクロ名（通常の関数名スタイル）
        std::string macro_name = expect_ident();

        std::vector<ast::MacroParam> params;

        // パラメータ
        expect(TokenKind::LParen);
        if (!check(TokenKind::RParen)) {
            do {
                // パラメータの型（auto, block, または具体的な型）
                std::string type_hint = expect_ident();

                // パラメータ名
                std::string param_name;

                // 可変長引数チェック（... が名前の後に来る）
                bool is_variadic = false;
                if (check(TokenKind::Ellipsis)) {
                    // 型... の形式（可変長引数）
                    advance();
                    param_name = expect_ident();
                    is_variadic = true;
                } else {
                    param_name = expect_ident();
                    // 名前の後の ... もチェック
                    if (consume_if(TokenKind::Ellipsis)) {
                        is_variadic = true;
                    }
                }

                params.push_back(ast::MacroParam(std::move(param_name), std::move(type_hint), is_variadic));
            } while (consume_if(TokenKind::Comma));
        }
        expect(TokenKind::RParen);

        // マクロボディ
        auto body = parse_block();

        // Function kind macroを作成
        ast::MacroDecl macro_decl(
            ast::MacroDecl::Function,
            std::move(macro_name),
            std::move(params),
            std::move(body)
        );

        return std::make_unique<ast::Decl>(
            std::make_unique<ast::MacroDecl>(std::move(macro_decl)),
            Span{start_pos, previous().end}
        );
    }

    // 旧構文のmacroキーワード（互換性のために残す - 推奨されない）
    if (check(TokenKind::KwMacro)) {
        advance();  // consume 'macro'

        // 戻り値の型（voidやautoなど）
        parse_type();

        // マクロ名
        std::string macro_name = expect_ident();

        std::vector<ast::MacroParam> params;
        expect(TokenKind::LParen);
        // パラメータ処理
        if (!check(TokenKind::RParen)) {
            do {
                // パラメータの型（auto, block, または具体的な型）
                std::string type_hint = expect_ident();

                // パラメータ名
                std::string param_name;

                // 可変長引数チェック
                bool is_variadic = false;
                if (check(TokenKind::Ellipsis)) {
                    advance();
                    param_name = expect_ident();
                    is_variadic = true;
                } else {
                    param_name = expect_ident();
                    if (consume_if(TokenKind::Ellipsis)) {
                        is_variadic = true;
                    }
                }

                params.push_back(ast::MacroParam(std::move(param_name), std::move(type_hint), is_variadic));
            } while (consume_if(TokenKind::Comma));
        }
        expect(TokenKind::RParen);

        auto body = parse_block();

        ast::MacroDecl macro_decl(
            ast::MacroDecl::Function,
            std::move(macro_name),
            std::move(params),
            std::move(body)
        );

        return std::make_unique<ast::Decl>(
            std::make_unique<ast::MacroDecl>(std::move(macro_decl)),
            Span{start_pos, previous().end}
        );
    }

    return nullptr;
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
            // 文字列リテラルまたは識別子
            if (check(TokenKind::StringLiteral)) {
                args.push_back(std::string(current().get_string()));
                advance();
            } else {
                args.push_back(expect_ident());
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
    expect(TokenKind::At);
    expect(TokenKind::LBracket);

    // アトリビュート名
    std::string attr_name = expect_ident();
    std::vector<std::string> args;

    // 引数がある場合
    if (consume_if(TokenKind::LParen)) {
        do {
            // 文字列リテラルまたは識別子
            if (check(TokenKind::StringLiteral)) {
                args.push_back(std::string(current().get_string()));
                advance();
            } else {
                args.push_back(expect_ident());
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
                    if (paren_count == 0) break;
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

// ============================================================
// 定数宣言（export const用）
// ============================================================
ast::DeclPtr Parser::parse_const_decl() {
    // uint32_t start_pos = current().start;  // TODO: use when ConstDecl is implemented
    expect(TokenKind::KwConst);

    // 型
    auto type = parse_type();

    // 変数名
    std::string name = expect_ident();

    // 初期化子
    expect(TokenKind::Eq);
    auto init = parse_expr();

    expect(TokenKind::Semicolon);

    // 定数宣言用のASTノード（簡易的にLetStmtを流用）
    auto let_stmt = std::make_unique<ast::LetStmt>(
        std::move(name), std::move(type), std::move(init), true
    );

    // TODO: 専用のConstDeclノードを作成する
    return nullptr;  // 一時的な実装
}

// ============================================================
// constexpr宣言
// ============================================================
ast::DeclPtr Parser::parse_constexpr() {
    // uint32_t start_pos = current().start;  // TODO: use when constexpr is implemented
    expect(TokenKind::KwConstexpr);

    // constexpr変数またはconstexpr関数
    auto type = parse_type();
    std::string name = expect_ident();

    if (check(TokenKind::LParen)) {
        // constexpr関数
        expect(TokenKind::LParen);
        auto params = parse_params();
        expect(TokenKind::RParen);
        auto body = parse_block();

        auto func = std::make_unique<ast::FunctionDecl>(
            std::move(name), std::move(params), std::move(type), std::move(body)
        );
        // TODO: constexprフラグを設定

        return std::make_unique<ast::Decl>(std::move(func));
    } else {
        // constexpr変数
        expect(TokenKind::Eq);
        auto init = parse_expr();
        expect(TokenKind::Semicolon);

        // TODO: ConstExprDeclノードを作成
        return nullptr;
    }
}

// ============================================================
// テンプレート宣言
// ============================================================
ast::DeclPtr Parser::parse_template_decl() {
    // uint32_t start_pos = current().start;  // TODO: use when template support is implemented
    expect(TokenKind::KwTemplate);

    // テンプレートパラメータ
    expect(TokenKind::Lt);
    std::vector<std::string> template_params;

    do {
        if (consume_if(TokenKind::KwTypename)) {
            template_params.push_back(expect_ident());
        } else {
            // その他のテンプレートパラメータ型
            auto type = parse_type();
            template_params.push_back(expect_ident());
        }
    } while (consume_if(TokenKind::Comma));

    expect(TokenKind::Gt);

    // テンプレート化される宣言
    // TODO: テンプレート対応の実装

    return nullptr;  // 一時的な実装
}

// ============================================================
// Enum宣言
// ============================================================
ast::DeclPtr Parser::parse_enum_decl() {
    // TODO: Enum実装
    return nullptr;
}

// ============================================================
// extern宣言
// ============================================================
ast::DeclPtr Parser::parse_extern() {
    // uint32_t start_pos = current().start;  // TODO: use when ExternBlock node is implemented
    expect(TokenKind::KwExtern);

    // extern "C" { ... } or extern "C" function
    if (check(TokenKind::StringLiteral)) {
        std::string lang = std::string(current().get_string());
        advance();

        if (consume_if(TokenKind::LBrace)) {
            // extern "C" { ... } ブロック
            std::vector<ast::DeclPtr> decls;
            while (!check(TokenKind::RBrace) && !is_at_end()) {
                if (auto decl = parse_extern_decl()) {
                    decls.push_back(std::move(decl));
                }
            }
            expect(TokenKind::RBrace);

            // TODO: ExternBlockノードを作成
            return nullptr;  // 一時的な実装
        } else {
            // 単一の extern "C" 宣言
            return parse_extern_decl();
        }
    }

    // extern だけの場合（C++スタイル）
    return parse_extern_decl();
}

// extern宣言の個別解析
ast::DeclPtr Parser::parse_extern_decl() {
    // 関数プロトタイプ
    auto return_type = parse_type();
    std::string name = expect_ident();

    expect(TokenKind::LParen);
    auto params = parse_params();
    expect(TokenKind::RParen);

    expect(TokenKind::Semicolon);

    // extern関数として作成（bodyなし）
    auto func = std::make_unique<ast::FunctionDecl>(
        std::move(name), std::move(params), std::move(return_type),
        std::vector<ast::StmtPtr>()  // 空のボディ
    );

    // externフラグを設定（TODO: FunctionDeclにis_externフラグを追加）
    return std::make_unique<ast::Decl>(std::move(func));
}

}  // namespace cm