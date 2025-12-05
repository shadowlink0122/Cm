#pragma once

#include "../../common/debug/par.hpp"
#include "../ast/decl.hpp"
#include "../lexer/token.hpp"

#include <functional>
#include <string>
#include <vector>

namespace cm {

// ============================================================
// 診断メッセージ
// ============================================================
enum class DiagKind { Error, Warning, Note };

struct Diagnostic {
    DiagKind kind;
    Span span;
    std::string message;

    Diagnostic(DiagKind k, Span s, std::string m) : kind(k), span(s), message(std::move(m)) {}
};

// ============================================================
// パーサ
// ============================================================
class Parser {
   public:
    Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)), pos_(0) {}

    // プログラム全体を解析
    ast::Program parse() {
        debug::par::log(debug::par::Id::Start);

        ast::Program program;
        while (!is_at_end()) {
            if (auto decl = parse_top_level()) {
                program.declarations.push_back(std::move(decl));
            } else {
                synchronize();
            }
        }

        debug::par::log(debug::par::Id::End,
                        std::to_string(program.declarations.size()) + " declarations");
        return program;
    }

    const std::vector<Diagnostic>& diagnostics() const { return diagnostics_; }
    bool has_errors() const {
        for (const auto& d : diagnostics_) {
            if (d.kind == DiagKind::Error)
                return true;
        }
        return false;
    }

   private:
    // トップレベル宣言
    ast::DeclPtr parse_top_level() {
        // import
        if (check(TokenKind::KwImport)) {
            return parse_import();
        }

        // 修飾子を収集
        bool is_export = consume_if(TokenKind::KwExport);
        bool is_static = consume_if(TokenKind::KwStatic);
        bool is_inline = consume_if(TokenKind::KwInline);
        bool is_async = consume_if(TokenKind::KwAsync);

        // struct
        if (check(TokenKind::KwStruct)) {
            return parse_struct(is_export);
        }

        // interface
        if (check(TokenKind::KwInterface)) {
            return parse_interface(is_export);
        }

        // impl
        if (check(TokenKind::KwImpl)) {
            return parse_impl();
        }

        // 関数 (型 名前 ...)
        return parse_function(is_export, is_static, is_inline, is_async);
    }

    // import文
    ast::DeclPtr parse_import() {
        uint32_t start_pos = current().start;
        expect(TokenKind::KwImport);

        std::vector<std::string> path;
        path.push_back(expect_ident());

        while (consume_if(TokenKind::ColonColon)) {
            path.push_back(expect_ident());
        }

        std::string alias;
        // "as" は識別子としてチェック
        if (check(TokenKind::Ident) && current_text() == "as") {
            advance();
            alias = expect_ident();
        }

        expect(TokenKind::Semicolon);
        return ast::make_import(std::move(path), std::move(alias), Span{start_pos, previous().end});
    }

    // 関数定義
    ast::DeclPtr parse_function(bool is_export, bool is_static, bool is_inline, bool is_async) {
        uint32_t start_pos = current().start;
        debug::par::log(debug::par::Id::FuncDef, "", debug::Level::Trace);

        auto return_type = parse_type();
        std::string name = expect_ident();

        expect(TokenKind::LParen);
        auto params = parse_params();
        expect(TokenKind::RParen);

        auto body = parse_block();

        auto func = std::make_unique<ast::FunctionDecl>(std::move(name), std::move(params),
                                                        std::move(return_type), std::move(body));

        func->visibility = is_export ? ast::Visibility::Export : ast::Visibility::Private;
        func->is_static = is_static;
        func->is_inline = is_inline;
        func->is_async = is_async;

        return std::make_unique<ast::Decl>(std::move(func), Span{start_pos, previous().end});
    }

    // パラメータリスト
    std::vector<ast::Param> parse_params() {
        std::vector<ast::Param> params;

        if (!check(TokenKind::RParen)) {
            do {
                ast::Param param;
                param.qualifiers.is_const = consume_if(TokenKind::KwConst);
                param.type = parse_type();
                param.name = expect_ident();
                params.push_back(std::move(param));
            } while (consume_if(TokenKind::Comma));
        }

        return params;
    }

    // 構造体
    ast::DeclPtr parse_struct(bool is_export) {
        uint32_t start_pos = current().start;
        debug::par::log(debug::par::Id::StructDef, "", debug::Level::Trace);

        expect(TokenKind::KwStruct);
        std::string name = expect_ident();

        // with キーワード
        std::vector<std::string> auto_impls;
        if (consume_if(TokenKind::KwWith)) {
            do {
                auto_impls.push_back(expect_ident());
            } while (consume_if(TokenKind::Comma));
        }

        expect(TokenKind::LBrace);

        std::vector<ast::Field> fields;
        while (!check(TokenKind::RBrace) && !is_at_end()) {
            ast::Field field;
            field.visibility = consume_if(TokenKind::KwPrivate) ? ast::Visibility::Private
                                                                : ast::Visibility::Export;
            field.qualifiers.is_const = consume_if(TokenKind::KwConst);
            field.type = parse_type();
            field.name = expect_ident();
            expect(TokenKind::Semicolon);
            fields.push_back(std::move(field));
        }

        expect(TokenKind::RBrace);

        auto decl = std::make_unique<ast::StructDecl>(std::move(name), std::move(fields));
        decl->visibility = is_export ? ast::Visibility::Export : ast::Visibility::Private;
        decl->auto_impls = std::move(auto_impls);

        return std::make_unique<ast::Decl>(std::move(decl), Span{start_pos, previous().end});
    }

    // インターフェース
    ast::DeclPtr parse_interface(bool is_export) {
        expect(TokenKind::KwInterface);
        std::string name = expect_ident();
        expect(TokenKind::LBrace);

        std::vector<ast::MethodSig> methods;
        while (!check(TokenKind::RBrace) && !is_at_end()) {
            ast::MethodSig sig;
            sig.return_type = parse_type();
            sig.name = expect_ident();
            expect(TokenKind::LParen);
            sig.params = parse_params();
            expect(TokenKind::RParen);
            expect(TokenKind::Semicolon);
            methods.push_back(std::move(sig));
        }

        expect(TokenKind::RBrace);

        auto decl = std::make_unique<ast::InterfaceDecl>(std::move(name), std::move(methods));
        decl->visibility = is_export ? ast::Visibility::Export : ast::Visibility::Private;

        return std::make_unique<ast::Decl>(std::move(decl));
    }

    // impl
    ast::DeclPtr parse_impl() {
        expect(TokenKind::KwImpl);
        std::string iface = expect_ident();
        expect(TokenKind::KwFor);
        auto target = parse_type();
        expect(TokenKind::LBrace);

        auto decl = std::make_unique<ast::ImplDecl>(std::move(iface), std::move(target));

        while (!check(TokenKind::RBrace) && !is_at_end()) {
            auto func = parse_function(false, false, false, false);
            if (auto* f = func->as<ast::FunctionDecl>()) {
                decl->methods.push_back(
                    std::unique_ptr<ast::FunctionDecl>(static_cast<ast::FunctionDecl*>(
                        std::get<std::unique_ptr<ast::FunctionDecl>>(func->kind).release())));
            }
        }

        expect(TokenKind::RBrace);
        return std::make_unique<ast::Decl>(std::move(decl));
    }

    // ブロック
    std::vector<ast::StmtPtr> parse_block() {
        debug::par::log(debug::par::Id::Block, "", debug::Level::Trace);
        expect(TokenKind::LBrace);

        std::vector<ast::StmtPtr> stmts;
        while (!check(TokenKind::RBrace) && !is_at_end()) {
            if (auto stmt = parse_stmt()) {
                stmts.push_back(std::move(stmt));
            }
        }

        expect(TokenKind::RBrace);
        return stmts;
    }

    // 文
    ast::StmtPtr parse_stmt();

    // 式
    ast::ExprPtr parse_expr();
    ast::ExprPtr parse_assignment();
    ast::ExprPtr parse_ternary();
    ast::ExprPtr parse_logical_or();
    ast::ExprPtr parse_logical_and();
    ast::ExprPtr parse_bitwise_or();
    ast::ExprPtr parse_bitwise_xor();
    ast::ExprPtr parse_bitwise_and();
    ast::ExprPtr parse_equality();
    ast::ExprPtr parse_relational();
    ast::ExprPtr parse_shift();
    ast::ExprPtr parse_additive();
    ast::ExprPtr parse_multiplicative();
    ast::ExprPtr parse_unary();
    ast::ExprPtr parse_postfix();
    ast::ExprPtr parse_primary();

    // 型解析
    ast::TypePtr parse_type() {
        ast::TypePtr type;

        // ポインタ/参照
        if (consume_if(TokenKind::Star)) {
            type = ast::make_pointer(parse_type());
            return type;
        }
        if (consume_if(TokenKind::Amp)) {
            type = ast::make_reference(parse_type());
            return type;
        }

        // 配列 [T] or [T; N]
        if (consume_if(TokenKind::LBracket)) {
            auto elem = parse_type();
            std::optional<uint32_t> size;
            if (consume_if(TokenKind::Semicolon)) {
                if (check(TokenKind::IntLiteral)) {
                    size = static_cast<uint32_t>(current().get_int());
                    advance();
                }
            }
            expect(TokenKind::RBracket);
            return ast::make_array(std::move(elem), size);
        }

        // プリミティブ型
        switch (current().kind) {
            case TokenKind::KwVoid:
                advance();
                return ast::make_void();
            case TokenKind::KwBool:
                advance();
                return std::make_shared<ast::Type>(ast::TypeKind::Bool);
            case TokenKind::KwTiny:
                advance();
                return std::make_shared<ast::Type>(ast::TypeKind::Tiny);
            case TokenKind::KwShort:
                advance();
                return std::make_shared<ast::Type>(ast::TypeKind::Short);
            case TokenKind::KwInt:
                advance();
                return ast::make_int();
            case TokenKind::KwLong:
                advance();
                return ast::make_long();
            case TokenKind::KwUtiny:
                advance();
                return std::make_shared<ast::Type>(ast::TypeKind::UTiny);
            case TokenKind::KwUshort:
                advance();
                return std::make_shared<ast::Type>(ast::TypeKind::UShort);
            case TokenKind::KwUint:
                advance();
                return std::make_shared<ast::Type>(ast::TypeKind::UInt);
            case TokenKind::KwUlong:
                advance();
                return std::make_shared<ast::Type>(ast::TypeKind::ULong);
            case TokenKind::KwFloat:
                advance();
                return ast::make_float();
            case TokenKind::KwDouble:
                advance();
                return ast::make_double();
            case TokenKind::KwChar:
                advance();
                return ast::make_char();
            case TokenKind::KwString:
                advance();
                return ast::make_string();
            default:
                break;
        }

        // ユーザー定義型
        if (check(TokenKind::Ident)) {
            std::string name = current_text();
            advance();
            return ast::make_named(name);
        }

        error("Expected type");
        return ast::make_error();
    }

    // ユーティリティ
    const Token& current() const { return tokens_[pos_]; }
    const Token& previous() const { return tokens_[pos_ > 0 ? pos_ - 1 : 0]; }
    bool is_at_end() const { return current().kind == TokenKind::Eof; }

    bool check(TokenKind kind) const { return current().kind == kind; }

    Token advance() {
        if (!is_at_end())
            ++pos_;
        return previous();
    }

    bool consume_if(TokenKind kind) {
        if (check(kind)) {
            advance();
            return true;
        }
        return false;
    }

    void expect(TokenKind kind) {
        if (!consume_if(kind)) {
            error(std::string("Expected '") + token_kind_to_string(kind) + "'");
        }
    }

    std::string expect_ident() {
        if (check(TokenKind::Ident)) {
            std::string name(current().get_string());
            advance();
            return name;
        }
        error("Expected identifier");
        return "<error>";
    }

    std::string current_text() const {
        if (check(TokenKind::Ident)) {
            return std::string(current().get_string());
        }
        return "";
    }

    void error(const std::string& msg) {
        debug::par::log(debug::par::Id::Error, msg, debug::Level::Error);
        diagnostics_.emplace_back(DiagKind::Error, Span{current().start, current().end}, msg);
    }

    void synchronize() {
        advance();
        while (!is_at_end()) {
            if (previous().kind == TokenKind::Semicolon)
                return;
            switch (current().kind) {
                case TokenKind::KwStruct:
                case TokenKind::KwInterface:
                case TokenKind::KwImpl:
                case TokenKind::KwImport:
                case TokenKind::KwExport:
                    return;
                default:
                    advance();
            }
        }
    }

    bool is_type_start();

    std::vector<Token> tokens_;
    size_t pos_;
    std::vector<Diagnostic> diagnostics_;
};

}  // namespace cm
