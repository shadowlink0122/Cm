#pragma once

#include "../../common/diagnostics.hpp"
#include "../ast/decl.hpp"
#include "../lexer/token.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cm {

// ============================================================
// 診断メッセージ（共通定義への互換性エイリアス）
// ============================================================
using DiagKind = Severity;

// ============================================================
// パーサ
// ============================================================
class Parser {
   public:
    Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)), pos_(0), last_error_line_(0) {}

    // プログラム全体を解析（parser_decl.cppで実装）
    ast::Program parse();

    const std::vector<Diagnostic>& diagnostics() const { return diagnostics_; }
    bool has_errors() const {
        for (const auto& d : diagnostics_) {
            if (d.severity == DiagKind::Error)
                return true;
        }
        return false;
    }

   private:
    // ============================================================
    // 宣言パース（parser_decl.cppで実装）
    // ============================================================
    ast::DeclPtr parse_top_level();
    bool is_global_var_start();
    ast::DeclPtr parse_function(bool is_export, bool is_static, bool is_inline,
                                std::vector<ast::AttributeNode> attributes = {},
                                bool is_async = false);
    std::vector<ast::Param> parse_params();
    ast::DeclPtr parse_struct(bool is_export, std::vector<ast::AttributeNode> attributes = {});
    std::optional<ast::OperatorKind> parse_operator_kind();
    ast::DeclPtr parse_interface(bool is_export, std::vector<ast::AttributeNode> attributes = {});
    ast::DeclPtr parse_impl(std::vector<ast::AttributeNode> attributes = {});
    ast::DeclPtr parse_impl_ctor(ast::TypePtr target, std::vector<ast::AttributeNode> attributes,
                                 std::vector<std::string> generic_params = {},
                                 std::vector<ast::GenericParam> generic_params_v2 = {});
    std::vector<ast::StmtPtr> parse_block();
    void error(const std::string& msg);
    void synchronize();

    // グローバル変数宣言のパース（parser_module.cppで実装）
    ast::DeclPtr parse_global_var_decl(bool is_export,
                                       std::vector<ast::AttributeNode> attributes = {});

    // ============================================================
    // 文パース（parser_stmt.cppで実装）
    // ============================================================
    ast::StmtPtr parse_stmt();

    // パターン（switch文用）
    std::unique_ptr<ast::Pattern> parse_pattern();
    std::unique_ptr<ast::Pattern> parse_pattern_element();

    // ============================================================
    // 式パース（parser_expr.cppで実装）
    // ============================================================
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
    ast::ExprPtr parse_lambda_body(std::vector<ast::Param> params, uint32_t start_pos);
    ast::ExprPtr parse_match_expr(uint32_t start_pos);
    std::unique_ptr<ast::MatchPattern> parse_match_pattern();
    std::unique_ptr<ast::MatchPattern> parse_match_pattern_element();

    // ============================================================
    // 型解析（parser_type.cppで実装）
    // ============================================================
    std::pair<std::vector<std::string>, std::vector<ast::GenericParam>> parse_generic_params_v2();
    std::vector<std::string> parse_generic_params();
    ast::TypePtr parse_type();
    ast::TypePtr parse_type_with_union();
    ast::TypePtr check_array_suffix(ast::TypePtr base_type);
    void consume_gt_in_type_context();
    std::string expect_ident();
    std::string current_text() const;
    bool is_type_start();

    // ============================================================
    // モジュール関連パーサ（parser_module.cppで実装）
    // ============================================================
    ast::DeclPtr parse_module();
    ast::DeclPtr parse_namespace();
    ast::DeclPtr parse_import_stmt(std::vector<ast::AttributeNode> attributes = {});
    ast::DeclPtr parse_export();
    ast::DeclPtr parse_use(std::vector<ast::AttributeNode> attributes = {});
    ast::DeclPtr parse_macro(bool is_exported = false);
    ast::DeclPtr parse_extern(std::vector<ast::AttributeNode> attributes = {});
    ast::DeclPtr parse_extern_decl(std::vector<ast::AttributeNode> attributes = {});
    std::unique_ptr<ast::FunctionDecl> parse_extern_func_decl();
    ast::TypePtr parse_extern_type();
    std::vector<ast::Param> parse_extern_params();
    ast::AttributeNode parse_directive();
    ast::AttributeNode parse_attribute();
    ast::DeclPtr parse_const_decl(bool is_export = false,
                                  std::vector<ast::AttributeNode> attributes = {});
    ast::DeclPtr parse_constexpr();
    ast::DeclPtr parse_template_decl();
    ast::DeclPtr parse_enum_decl(bool is_export = false,
                                 std::vector<ast::AttributeNode> attributes = {});
    ast::DeclPtr parse_typedef_decl(bool is_export = false,
                                    std::vector<ast::AttributeNode> attributes = {});
    ast::DeclPtr parse_impl_export(std::vector<ast::AttributeNode> attributes = {});

    // ============================================================
    // インラインユーティリティ（小型のためヘッダに残す）
    // ============================================================
    const Token& current() const { return tokens_[pos_]; }
    const Token& previous() const { return tokens_[pos_ > 0 ? pos_ - 1 : 0]; }
    bool is_at_end() const { return current().kind == TokenKind::Eof; }

    bool check(TokenKind kind) const { return current().kind == kind; }
    TokenKind peek_kind(size_t offset = 1) const {
        size_t idx = pos_ + offset;
        if (idx >= tokens_.size()) {
            return TokenKind::Eof;
        }
        return tokens_[idx].kind;
    }
    bool is_attribute_start() const {
        return check(TokenKind::At) ||
               (check(TokenKind::Hash) && peek_kind() == TokenKind::LBracket);
    }

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

    // ============================================================
    // メンバ変数
    // ============================================================
    std::vector<Token> tokens_;
    size_t pos_;
    std::vector<Diagnostic> diagnostics_;
    uint32_t last_error_line_ = 0;  // 連続エラー抑制用
    int pending_gt_count_ = 0;  // ネストジェネリクス用: GtGtから分割された残りの'>'カウント
    bool in_operator_return_type_ =
        false;  // 演算子戻り値型パース中フラグ（*&の型サフィックス抑制）
};

}  // namespace cm
