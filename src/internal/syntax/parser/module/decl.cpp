// ============================================================
// モジュール関連パーサ - module/namespace/import/export/use宣言の解析
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
// モジュール宣言
// ============================================================
ast::DeclPtr Parser::parse_module() {
    uint32_t start_pos = current().start;
    expect(TokenKind::KwModule);

    ast::ModulePath path;
    path.segments.push_back(expect_ident());

    // パス区切りは :: と .（ライブラリは module std.core; 形式を使用）の両方を許可
    while (consume_if(TokenKind::ColonColon) || consume_if(TokenKind::Dot)) {
        path.segments.push_back(expect_ident());
    }

    expect(TokenKind::Semicolon);

    auto decl = std::make_unique<ast::ModuleDecl>(std::move(path));
    return std::make_unique<ast::Decl>(std::move(decl), Span{start_pos, previous().end});
}

// ============================================================
// Namespace宣言
// ============================================================
ast::DeclPtr Parser::parse_namespace() {
    uint32_t start_pos = current().start;
    expect(TokenKind::KwNamespace);

    // BUG修正(v0.14.2): namespace名に型キーワード（string, int等）も許可
    // プリプロセッサがimportファイル名をnamespace名に使用するため、"string.cm" → "namespace string { ... }" が正しくパースできる必要がある
    std::string namespace_name;
    if (check(TokenKind::Ident)) {
        namespace_name = expect_ident();
    } else {
        // 型キーワードをnamespace名として受け入れる
        // BUG修正: キーワードトークンはget_string()が空文字を返すため
        // token_kind_to_string()を使用してキーワード名を取得
        auto kind = current().kind;
        if (kind == TokenKind::KwString || kind == TokenKind::KwInt || kind == TokenKind::KwUint ||
            kind == TokenKind::KwLong || kind == TokenKind::KwUlong || kind == TokenKind::KwShort ||
            kind == TokenKind::KwUshort || kind == TokenKind::KwTiny ||
            kind == TokenKind::KwUtiny || kind == TokenKind::KwFloat ||
            kind == TokenKind::KwDouble || kind == TokenKind::KwBool || kind == TokenKind::KwChar ||
            kind == TokenKind::KwVoid || kind == TokenKind::KwIsize || kind == TokenKind::KwUsize ||
            kind == TokenKind::KwCstring) {
            namespace_name = std::string(token_kind_to_string(kind));
            advance();
        } else {
            namespace_name = expect_ident();  // エラーメッセージ生成のフォールバック
        }
    }

    expect(TokenKind::LBrace);

    // namespace内の宣言をパース
    std::vector<ast::DeclPtr> declarations;
    size_t last_pos = pos_;
    int ns_iterations = 0;
    const int MAX_NS_ITERATIONS = 5000;

    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof) &&
           ns_iterations < MAX_NS_ITERATIONS) {
        // parse_top_level()がnullptrを返しトークンが進まない場合のスタック検出
        if (pos_ == last_pos && ns_iterations > 0) {
            // トークンを強制的に進めてスタックを解消
            if (!is_at_end() && !check(TokenKind::RBrace)) {
                advance();
            } else {
                break;
            }
        }
        last_pos = pos_;

        auto decl = parse_top_level();
        if (decl) {
            declarations.push_back(std::move(decl));
        }
        ns_iterations++;
    }

    expect(TokenKind::RBrace);

    // NamespaceDecl を ModuleDecl として扱う（内部表現を統一）
    ast::ModulePath path;
    path.segments.push_back(namespace_name);
    auto module_decl = std::make_unique<ast::ModuleDecl>(std::move(path));
    module_decl->declarations = std::move(declarations);

    return std::make_unique<ast::Decl>(std::move(module_decl), Span{start_pos, previous().end});
}

// ============================================================
// Import文
// ============================================================
ast::DeclPtr Parser::parse_import_stmt(std::vector<ast::AttributeNode> attributes) {
    uint32_t start_pos = current().start;
    expect(TokenKind::KwImport);

    // 相対パスのチェック
    bool is_relative = false;
    std::string path_prefix;

    if (consume_if(TokenKind::Dot)) {
        is_relative = true;
        if (consume_if(TokenKind::Slash)) {
            path_prefix = "./";
        } else if (consume_if(TokenKind::Dot)) {
            // ..
            if (consume_if(TokenKind::Slash)) {
                path_prefix = "../";
            } else {
                error(i18n::msg(i18n::MsgId::PsExpected));
                return nullptr;
            }
        } else {
            error(i18n::msg(i18n::MsgId::PsExpected2));
            return nullptr;
        }
    }

    // モジュールパス
    ast::ModulePath path;

    // 相対パスの場合、プレフィックスを最初のセグメントとして追加
    if (is_relative) {
        path.segments.push_back(path_prefix);
    }

    path.segments.push_back(expect_ident());

    // スラッシュで区切られた深い階層パス: import ./io/file
    while (consume_if(TokenKind::Slash)) {
        path.segments.push_back(expect_ident());
    }

    // または :: で区切られた階層パス: import std::io
    while (consume_if(TokenKind::ColonColon)) {
        path.segments.push_back(expect_ident());
    }

    ast::ImportDecl import_decl(std::move(path));

    // インポートアイテム
    if (consume_if(TokenKind::ColonColon)) {
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

    import_decl.attributes = std::move(attributes);

    return std::make_unique<ast::Decl>(std::make_unique<ast::ImportDecl>(std::move(import_decl)),
                                       Span{start_pos, previous().end});
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

    // v4: エクスポートは名前のリストまたは再エクスポートのみ

    // export * from module; (ワイルドカード再エクスポート)
    if (consume_if(TokenKind::Star)) {
        if (check(TokenKind::Ident) && current_text() == "from") {
            advance();
            ast::ModulePath from_path;
            from_path.segments.push_back(expect_ident());
            while (consume_if(TokenKind::ColonColon)) {
                from_path.segments.push_back(expect_ident());
            }
            expect(TokenKind::Semicolon);

            auto export_decl = std::make_unique<ast::ExportDecl>(
                ast::ExportDecl::wildcard_from(std::move(from_path)));
            return std::make_unique<ast::Decl>(std::move(export_decl),
                                               Span{start_pos, previous().end});
        }
        error(i18n::msg(i18n::MsgId::PsExpectedFromExport));
        return nullptr;
    }

    // export { ... } from module; (ブレース付き再エクスポート)
    if (check(TokenKind::LBrace)) {
        advance();  // consume {

        std::vector<ast::ExportItem> items;
        do {
            // 階層的再エクスポートのチェック: io::{file, stream}
            std::vector<std::string> namespace_parts;
            std::string name = expect_ident();

            // io:: のような階層パスをパース
            while (consume_if(TokenKind::ColonColon)) {
                namespace_parts.push_back(name);

                // 次が { の場合は、階層的再エクスポート
                if (check(TokenKind::LBrace)) {
                    advance();  // consume {

                    // { の中のアイテムをパース
                    do {
                        std::string item_name = expect_ident();

                        // 階層パスを作成
                        ast::ModulePath ns_path;
                        ns_path.segments = namespace_parts;

                        items.push_back(ast::ExportItem(item_name, ns_path));
                    } while (consume_if(TokenKind::Comma));

                    expect(TokenKind::RBrace);
                    break;  // 階層的再エクスポートの処理完了
                }

                name = expect_ident();
            }

            // 通常のエクスポート項目（階層なし）
            if (namespace_parts.empty()) {
                std::optional<std::string> alias;

                // as エイリアス
                if (check(TokenKind::Ident) && current_text() == "as") {
                    advance();
                    alias = expect_ident();
                }

                items.push_back(ast::ExportItem(std::move(name)));
            }
        } while (consume_if(TokenKind::Comma));

        expect(TokenKind::RBrace);

        // from句があるか確認
        if (check(TokenKind::Ident) && current_text() == "from") {
            advance();
            ast::ModulePath from_path;
            from_path.segments.push_back(expect_ident());
            while (consume_if(TokenKind::ColonColon)) {
                from_path.segments.push_back(expect_ident());
            }
            expect(TokenKind::Semicolon);

            auto export_decl =
                std::make_unique<ast::ExportDecl>(std::move(items), std::move(from_path));
            return std::make_unique<ast::Decl>(std::move(export_decl),
                                               Span{start_pos, previous().end});
        } else {
            expect(TokenKind::Semicolon);
            auto export_decl = std::make_unique<ast::ExportDecl>(std::move(items));
            return std::make_unique<ast::Decl>(std::move(export_decl),
                                               Span{start_pos, previous().end});
        }
    }

    // export NAME1, NAME2, ...; (名前リスト)
    // export NAME from module; (単一再エクスポート)
    std::vector<ast::ExportItem> items;
    do {
        std::string name = expect_ident();
        std::optional<std::string> alias;

        // as エイリアス（v4では export NAME as ALIAS; をサポート）
        if (check(TokenKind::Ident) && current_text() == "as") {
            advance();
            alias = expect_ident();
        }

        items.push_back(ast::ExportItem(std::move(name)));
    } while (consume_if(TokenKind::Comma));

    // from句があるか確認
    if (check(TokenKind::Ident) && current_text() == "from") {
        advance();
        ast::ModulePath from_path;
        from_path.segments.push_back(expect_ident());
        while (consume_if(TokenKind::ColonColon)) {
            from_path.segments.push_back(expect_ident());
        }
        expect(TokenKind::Semicolon);

        auto export_decl =
            std::make_unique<ast::ExportDecl>(std::move(items), std::move(from_path));
        return std::make_unique<ast::Decl>(std::move(export_decl), Span{start_pos, previous().end});
    } else {
        expect(TokenKind::Semicolon);
        auto export_decl = std::make_unique<ast::ExportDecl>(std::move(items));
        return std::make_unique<ast::Decl>(std::move(export_decl), Span{start_pos, previous().end});
    }
}

// ============================================================
// Export impl (v4: impl全体のエクスポート)
// ============================================================
ast::DeclPtr Parser::parse_impl_export(std::vector<ast::AttributeNode> attributes) {
    // impl Type または impl<T> Type<T> または impl Type for Interface
    // これらのメソッドを全てエクスポート対象としてマーク
    // 注意: parse_impl()がimplキーワードを消費する
    auto impl_decl = parse_impl(std::move(attributes));

    if (impl_decl && impl_decl->as<ast::ImplDecl>()) {
        impl_decl->as<ast::ImplDecl>()->is_export = true;
    }

    return impl_decl;
}

// ============================================================
// Use文
// use std::io;              -- モジュールエイリアス
// use libc { ... };         -- FFI宣言
// use libc as c { ... };    -- 名前空間付きFFI宣言
// ============================================================
ast::DeclPtr Parser::parse_use(std::vector<ast::AttributeNode> attributes) {
    uint32_t start_pos = current().start;
    expect(TokenKind::KwUse);

    // パッケージインポート (use "pkg")
    if (check(TokenKind::StringLiteral)) {
        std::string pkg_name = std::string(current().get_string());
        advance();

        // エイリアス
        std::optional<std::string> alias;
        if (check(TokenKind::Ident) && current_text() == "as") {
            advance();
            alias = expect_ident();
        }

        // FFI宣言ブロック: use "pkg" { ... }
        if (check(TokenKind::LBrace)) {
            advance();  // consume '{'

            std::vector<ast::FFIFunctionDecl> ffi_funcs;

            while (!check(TokenKind::RBrace) && !is_at_end()) {
                ast::FFIFunctionDecl ffi_func;
                ffi_func.return_type = parse_extern_type();
                ffi_func.name = expect_ident();
                expect(TokenKind::LParen);
                if (!check(TokenKind::RParen)) {
                    do {
                        if (check(TokenKind::Ellipsis)) {
                            advance();
                            ffi_func.is_variadic = true;
                            break;
                        }
                        auto param_type = parse_extern_type();
                        std::string param_name;
                        if (check(TokenKind::Ident)) {
                            param_name = current_text();
                            advance();
                        }
                        ffi_func.params.push_back({param_name, param_type});
                    } while (consume_if(TokenKind::Comma));
                }
                expect(TokenKind::RParen);
                expect(TokenKind::Semicolon);
                ffi_funcs.push_back(std::move(ffi_func));
            }
            expect(TokenKind::RBrace);

            auto use_decl = std::make_unique<ast::UseDecl>(std::move(pkg_name),
                                                           std::move(ffi_funcs), std::move(alias));
            use_decl->attributes = std::move(attributes);
            return std::make_unique<ast::Decl>(std::move(use_decl),
                                               Span{start_pos, previous().end});
        }

        // 単なる外部モジュール参照 use "pkg";
        // (実際にはFFI宣言がないとJSでは使いにくいが、requireだけするケースもありうる) あるいは use
        // "pkg" as p;
        expect(TokenKind::Semicolon);
        auto use_decl = std::make_unique<ast::UseDecl>(std::move(pkg_name), std::move(alias));
        use_decl->attributes = std::move(attributes);
        return std::make_unique<ast::Decl>(std::move(use_decl), Span{start_pos, previous().end});
    }

    // パス解析 (従来の use std::io)
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

    // FFI宣言ブロック: use libc { ... }
    if (check(TokenKind::LBrace)) {
        advance();  // consume '{'

        std::vector<ast::FFIFunctionDecl> ffi_funcs;

        while (!check(TokenKind::RBrace) && !is_at_end()) {
            ast::FFIFunctionDecl ffi_func;

            // 戻り値型をパース
            ffi_func.return_type = parse_extern_type();

            // 関数名
            ffi_func.name = expect_ident();

            // パラメータリスト
            expect(TokenKind::LParen);
            if (!check(TokenKind::RParen)) {
                do {
                    // 可変引数チェック
                    if (check(TokenKind::Ellipsis)) {
                        advance();
                        ffi_func.is_variadic = true;
                        break;  // 可変引数は最後のパラメータ
                    }

                    // パラメータの型をパース
                    auto param_type = parse_extern_type();

                    // パラメータ名（オプション）
                    std::string param_name;
                    if (check(TokenKind::Ident)) {
                        param_name = current_text();
                        advance();
                    }

                    ffi_func.params.push_back({param_name, param_type});
                } while (consume_if(TokenKind::Comma));
            }
            expect(TokenKind::RParen);
            expect(TokenKind::Semicolon);

            ffi_funcs.push_back(std::move(ffi_func));
        }

        expect(TokenKind::RBrace);

        auto use_decl =
            std::make_unique<ast::UseDecl>(std::move(path), std::move(ffi_funcs), std::move(alias));
        use_decl->attributes = std::move(attributes);
        return std::make_unique<ast::Decl>(std::move(use_decl), Span{start_pos, previous().end});
    }

    // 通常のモジュールuse
    ast::UseDecl use_decl(std::move(path), std::move(alias));
    use_decl.attributes = std::move(attributes);
    expect(TokenKind::Semicolon);

    return std::make_unique<ast::Decl>(std::make_unique<ast::UseDecl>(std::move(use_decl)),
                                       Span{start_pos, previous().end});
}

}  // namespace cm
