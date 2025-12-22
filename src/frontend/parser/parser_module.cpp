// モジュール関連のパーサー実装

#include "../../common/debug/par.hpp"
#include "../ast/module.hpp"
#include "parser.hpp"

#include <unordered_set>

namespace cm {

// ============================================================
// モジュール宣言
// ============================================================
ast::DeclPtr Parser::parse_module() {
    uint32_t start_pos = current().start;
    expect(TokenKind::KwModule);

    ast::ModulePath path;
    path.segments.push_back(expect_ident());

    while (consume_if(TokenKind::ColonColon)) {
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

    std::string namespace_name = expect_ident();

    expect(TokenKind::LBrace);

    // namespace内の宣言をパース
    std::vector<ast::DeclPtr> declarations;
    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
        auto decl = parse_top_level();
        if (decl) {
            declarations.push_back(std::move(decl));
        }
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
ast::DeclPtr Parser::parse_import_stmt() {
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
                error("Expected '/' after '..'");
                return nullptr;
            }
        } else {
            error("Expected '/' after '.'");
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
        error("Expected 'from' after 'export *'");
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
ast::DeclPtr Parser::parse_impl_export() {
    uint32_t start_pos = current().start;
    expect(TokenKind::KwImpl);

    // impl Type または impl<T> Type<T> または impl Type for Interface
    // これらのメソッドを全てエクスポート対象としてマーク
    auto impl_decl = parse_impl();

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

        auto use_decl = std::make_unique<ast::UseDecl>(std::move(path), std::move(ffi_funcs), std::move(alias));
        return std::make_unique<ast::Decl>(std::move(use_decl), Span{start_pos, previous().end});
    }

    // 通常のモジュールuse
    ast::UseDecl use_decl(std::move(path), std::move(alias));
    expect(TokenKind::Semicolon);

    return std::make_unique<ast::Decl>(std::make_unique<ast::UseDecl>(std::move(use_decl)),
                                       Span{start_pos, previous().end});
}

// ============================================================
// マクロ定義
// ============================================================
ast::DeclPtr Parser::parse_macro() {
    // この関数は現在使用されていません
    // parse_top_level() で直接エラー処理とスキップを行っています
    error("Internal error: parse_macro() should not be called");
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

// ============================================================
// 定数宣言（export const用）
// ============================================================
ast::DeclPtr Parser::parse_const_decl(bool is_export) {
    uint32_t start_pos = current().start;
    expect(TokenKind::KwConst);

    // 型
    auto type = parse_type();

    // 変数名
    std::string name = expect_ident();

    // 初期化子
    expect(TokenKind::Eq);
    auto init = parse_expr();

    expect(TokenKind::Semicolon);

    // v4: GlobalVarDeclを使用してconst宣言を表現
    auto global_var = std::make_unique<ast::GlobalVarDecl>(
        std::move(name), std::move(type), std::move(init), true  // is_const = true
    );
    global_var->visibility = is_export ? ast::Visibility::Export : ast::Visibility::Private;

    return std::make_unique<ast::Decl>(std::move(global_var), Span{start_pos, previous().end});
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

        auto func = std::make_unique<ast::FunctionDecl>(std::move(name), std::move(params),
                                                        std::move(type), std::move(body));
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
ast::DeclPtr Parser::parse_enum_decl(bool is_export) {
    uint32_t start_pos = current().start;
    expect(TokenKind::KwEnum);

    std::string name = expect_ident();
    expect(TokenKind::LBrace);

    std::vector<ast::EnumMember> members;
    int64_t next_value = 0;                   // オートインクリメント用
    std::unordered_set<int64_t> used_values;  // 重複チェック用

    while (!check(TokenKind::RBrace) && !is_at_end()) {
        std::string member_name = expect_ident();
        std::optional<int64_t> explicit_value;

        // 明示的な値指定
        if (consume_if(TokenKind::Eq)) {
            // 負の数をサポート
            bool is_negative = consume_if(TokenKind::Minus);

            if (!check(TokenKind::IntLiteral)) {
                error("enum値には整数リテラルが必要です");
                return nullptr;
            }

            int64_t value = static_cast<int64_t>(current().get_int());
            advance();

            if (is_negative) {
                value = -value;
            }

            explicit_value = value;
            next_value = value + 1;
        } else {
            explicit_value = next_value;
            next_value++;
        }

        // 重複チェック
        if (used_values.count(*explicit_value)) {
            error("enum値 " + std::to_string(*explicit_value) + " は既に使用されています");
            return nullptr;
        }
        used_values.insert(*explicit_value);

        members.emplace_back(std::move(member_name), explicit_value);

        // カンマは省略可能（最後の要素の後も許可）
        consume_if(TokenKind::Comma);
    }

    expect(TokenKind::RBrace);

    auto enum_decl = std::make_unique<ast::EnumDecl>(std::move(name), std::move(members));
    enum_decl->visibility = is_export ? ast::Visibility::Export : ast::Visibility::Private;
    return std::make_unique<ast::Decl>(std::move(enum_decl), Span{start_pos, previous().end});
}

// ============================================================
// typedef宣言
// typedef T = Type1 | Type2 | ...;
// typedef T = "literal1" | "literal2" | ...;
// ============================================================
ast::DeclPtr Parser::parse_typedef_decl(bool is_export) {
    uint32_t start_pos = current().start;
    expect(TokenKind::KwTypedef);

    std::string name = expect_ident();
    expect(TokenKind::Eq);

    // ユニオン型 or リテラル型をパース
    // 最初の要素を見て判断

    // リテラル型かどうかをチェック
    bool is_literal_type = check(TokenKind::StringLiteral) || check(TokenKind::IntLiteral) ||
                           check(TokenKind::FloatLiteral);

    if (is_literal_type) {
        // リテラル型: typedef T = "a" | "b" | 1 | 2;
        std::vector<ast::LiteralType> literals;

        do {
            if (check(TokenKind::StringLiteral)) {
                literals.emplace_back(std::string(current().get_string()));
                advance();
            } else if (check(TokenKind::IntLiteral)) {
                literals.emplace_back(static_cast<int64_t>(current().get_int()));
                advance();
            } else if (check(TokenKind::FloatLiteral)) {
                literals.emplace_back(current().get_float());
                advance();
            } else {
                error("リテラル型には文字列、整数、または浮動小数点リテラルが必要です");
                return nullptr;
            }
        } while (consume_if(TokenKind::Pipe));

        expect(TokenKind::Semicolon);

        auto lit_union = ast::make_literal_union(std::move(literals));
        auto typedef_decl =
            std::make_unique<ast::TypedefDecl>(std::move(name), std::move(lit_union));
        typedef_decl->visibility = is_export ? ast::Visibility::Export : ast::Visibility::Private;
        return std::make_unique<ast::Decl>(std::move(typedef_decl),
                                           Span{start_pos, previous().end});
    } else {
        // ユニオン型: typedef T = Type1 | Type2;
        std::vector<ast::TypePtr> types;

        do {
            auto type = parse_type();
            if (!type) {
                error("typedefには有効な型が必要です");
                return nullptr;
            }
            // C++スタイルの配列・ポインタサフィックスをチェック (T*, T[N])
            type = check_array_suffix(std::move(type));
            types.push_back(std::move(type));
        } while (consume_if(TokenKind::Pipe));

        expect(TokenKind::Semicolon);

        // 単一の型の場合はエイリアス、複数の場合はユニオン型
        ast::TypePtr result_type;
        if (types.size() == 1) {
            result_type = std::move(types[0]);
        } else {
            // ユニオン型を作成
            std::vector<ast::UnionVariant> variants;
            for (auto& t : types) {
                // 型名をタグとして使用
                std::string tag = ast::type_to_string(*t);
                ast::UnionVariant v(tag);
                v.fields.push_back(std::move(t));
                variants.push_back(std::move(v));
            }
            result_type = ast::make_union(std::move(variants));
        }

        auto typedef_decl =
            std::make_unique<ast::TypedefDecl>(std::move(name), std::move(result_type));
        typedef_decl->visibility = is_export ? ast::Visibility::Export : ast::Visibility::Private;
        return std::make_unique<ast::Decl>(std::move(typedef_decl),
                                           Span{start_pos, previous().end});
    }
}

// ============================================================
// extern宣言
// ============================================================
ast::DeclPtr Parser::parse_extern() {
    uint32_t start_pos = current().start;
    expect(TokenKind::KwExtern);

    // extern "C" { ... } or extern "C" function
    if (check(TokenKind::StringLiteral)) {
        std::string lang = std::string(current().get_string());
        advance();

        if (consume_if(TokenKind::LBrace)) {
            // extern "C" { ... } ブロック
            auto extern_block = std::make_unique<ast::ExternBlockDecl>(lang);
            while (!check(TokenKind::RBrace) && !is_at_end()) {
                if (auto func = parse_extern_func_decl()) {
                    extern_block->declarations.push_back(std::move(func));
                }
            }
            expect(TokenKind::RBrace);

            return std::make_unique<ast::Decl>(std::move(extern_block),
                                               Span{start_pos, previous().end});
        } else {
            // 単一の extern "C" 宣言
            return parse_extern_decl();
        }
    }

    // extern だけの場合（C++スタイル）
    return parse_extern_decl();
}

// extern宣言の個別解析（FunctionDecl版）
std::unique_ptr<ast::FunctionDecl> Parser::parse_extern_func_decl() {
    // 関数プロトタイプ - C言語スタイルの型をサポート
    auto return_type = parse_extern_type();
    std::string name = expect_ident();

    expect(TokenKind::LParen);
    auto params = parse_extern_params();
    expect(TokenKind::RParen);

    expect(TokenKind::Semicolon);

    // extern関数として作成（bodyなし）
    auto func = std::make_unique<ast::FunctionDecl>(std::move(name), std::move(params),
                                                    std::move(return_type),
                                                    std::vector<ast::StmtPtr>()  // 空のボディ
    );
    func->is_extern = true;

    return func;
}

// C言語スタイルの型をパース（後置ポインタ T* をサポート）
ast::TypePtr Parser::parse_extern_type() {
    // const修飾子をスキップ（C言語互換）
    bool is_const = consume_if(TokenKind::KwConst);
    (void)is_const;  // 現時点では使用しない

    // 基本型をパース
    ast::TypePtr base_type = parse_type();

    // 後置ポインタをチェック（C言語スタイル: char*, int* など）
    while (check(TokenKind::Star)) {
        advance();  // consume *
        base_type = ast::make_pointer(std::move(base_type));
    }

    return base_type;
}

// extern関数用のパラメータパース
std::vector<ast::Param> Parser::parse_extern_params() {
    std::vector<ast::Param> params;

    if (check(TokenKind::RParen)) {
        return params;
    }

    do {
        ast::Param param;

        // const修飾子をスキップ
        param.qualifiers.is_const = consume_if(TokenKind::KwConst);

        // 型をパース（C言語スタイル）
        param.type = parse_extern_type();

        // パラメータ名（オプション）
        if (check(TokenKind::Ident)) {
            param.name = current_text();
            advance();
        }

        params.push_back(std::move(param));
    } while (consume_if(TokenKind::Comma));

    return params;
}

// extern宣言の個別解析（DeclPtr版）
ast::DeclPtr Parser::parse_extern_decl() {
    auto func = parse_extern_func_decl();
    return std::make_unique<ast::Decl>(std::move(func));
}

}  // namespace cm