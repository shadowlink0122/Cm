#pragma once

// ============================================================
// TypeChecker メインクラス定義
// ============================================================

#include "base.hpp"

namespace cm {

class TypeChecker {
   public:
    TypeChecker();

    // プログラム全体をチェック
    bool check(ast::Program& program);

    // 構造体定義を登録
    void register_struct(const std::string& name, const ast::StructDecl& decl);

    // 構造体定義を取得
    const ast::StructDecl* get_struct(const std::string& name) const;

    // 構造体のdefaultメンバの型を取得（なければnullptr）
    ast::TypePtr get_default_member_type(const std::string& struct_name) const;

    // 構造体のdefaultメンバの名前を取得（なければ空文字列）
    std::string get_default_member_name(const std::string& struct_name) const;

    // 診断情報
    const std::vector<Diagnostic>& diagnostics() const { return diagnostics_; }
    bool has_errors() const;

    // 自動実装情報を取得（HIR/MIR生成で使用）
    bool has_auto_impl(const std::string& struct_name, const std::string& iface_name) const;

    // 構造体定義を取得（外部から使用）
    const std::unordered_map<std::string, const ast::StructDecl*>& get_struct_defs() const {
        return struct_defs_;
    }

    // Lint警告の有効/無効を設定
    void set_enable_lint_warnings(bool enable) { enable_lint_warnings_ = enable; }

   private:
    // ============================================================
    // 宣言の登録・チェック (decl.cpp)
    // ============================================================
    void register_declaration(ast::Decl& decl);
    void check_declaration(ast::Decl& decl);
    void register_namespace(ast::ModuleDecl& mod, const std::string& parent_namespace);
    void check_namespace(ast::ModuleDecl& mod, const std::string& parent_namespace);
    void register_impl(ast::ImplDecl& impl);
    void check_impl(ast::ImplDecl& impl);
    void register_enum(ast::EnumDecl& en);
    void register_typedef(ast::TypedefDecl& td);
    void check_import(ast::ImportDecl& import);
    void check_function(ast::FunctionDecl& func);
    void register_println();
    void register_print();

    // ============================================================
    // 文のチェック (stmt.cpp)
    // ============================================================
    void check_statement(ast::Stmt& stmt);
    void check_let(ast::LetStmt& let);
    void check_return(ast::ReturnStmt& ret);
    void check_if(ast::IfStmt& if_stmt);
    void check_while(ast::WhileStmt& while_stmt);
    void check_for(ast::ForStmt& for_stmt);
    void check_for_in(ast::ForInStmt& for_in);

    // ============================================================
    // 式の型推論 (expr.cpp)
    // ============================================================
    ast::TypePtr infer_type(ast::Expr& expr);
    ast::TypePtr infer_literal(ast::LiteralExpr& lit);
    ast::TypePtr infer_array_literal(ast::ArrayLiteralExpr& lit);
    ast::TypePtr infer_struct_literal(ast::StructLiteralExpr& lit);
    ast::TypePtr infer_ident(ast::IdentExpr& ident);
    ast::TypePtr infer_binary(ast::BinaryExpr& binary);
    ast::TypePtr infer_unary(ast::UnaryExpr& unary);
    ast::TypePtr infer_ternary(ast::TernaryExpr& ternary);
    ast::TypePtr infer_index(ast::IndexExpr& idx);
    ast::TypePtr infer_slice(ast::SliceExpr& slice);
    ast::TypePtr infer_match(ast::MatchExpr& match_expr);
    ast::TypePtr infer_lambda(ast::LambdaExpr& lambda);

    // ============================================================
    // 関数/メソッド呼び出し (call.cpp)
    // ============================================================
    ast::TypePtr infer_call(ast::CallExpr& call);
    ast::TypePtr infer_member(ast::MemberExpr& member);
    ast::TypePtr infer_member_field(ast::MemberExpr& member, ast::TypePtr obj_type);
    ast::TypePtr infer_member_method(ast::MemberExpr& member, ast::TypePtr obj_type);
    ast::TypePtr infer_array_method(ast::MemberExpr& member, ast::TypePtr obj_type);
    ast::TypePtr infer_string_method(ast::MemberExpr& member, ast::TypePtr obj_type);

    // ============================================================
    // ジェネリクス処理 (generic.cpp)
    // ============================================================
    ast::TypePtr infer_generic_call(ast::CallExpr& call, const std::string& func_name,
                                    const std::vector<std::string>& type_params);
    ast::TypePtr substitute_generic_type(ast::TypePtr type,
                                         const std::vector<std::string>& type_params,
                                         const std::vector<ast::TypePtr>& type_args);
    bool check_constraint(const std::string& type_param, const ast::TypePtr& arg_type,
                          const ast::GenericParam& constraint);

    // ============================================================
    // 自動実装 (auto_impl.cpp)
    // ============================================================
    void register_auto_impl(const ast::StructDecl& st, const std::string& iface_name);
    void register_auto_eq_impl(const ast::StructDecl& st);
    void register_auto_ord_impl(const ast::StructDecl& st);
    void register_auto_clone_impl(const ast::StructDecl& st);
    void register_auto_hash_impl(const ast::StructDecl& st);
    void register_auto_debug_impl(const ast::StructDecl& st);
    void register_auto_display_impl(const ast::StructDecl& st);
    void register_auto_css_impl(const ast::StructDecl& st);
    void register_builtin_interfaces();

    // ============================================================
    // ユーティリティ (utils.cpp)
    // ============================================================
    ast::TypePtr resolve_typedef(ast::TypePtr type);
    bool types_compatible(ast::TypePtr expected, ast::TypePtr actual);
    ast::TypePtr common_type(ast::TypePtr a, ast::TypePtr b);
    std::vector<std::string> extract_format_variables(const std::string& format_str);
    void error(Span span, const std::string& msg);
    void warning(Span span, const std::string& msg);
    bool type_implements_interface(const std::string& type_name, const std::string& interface_name);
    bool check_type_constraints(const std::string& type_name,
                                const std::vector<std::string>& constraints);

    // 変数変更追跡（const推奨警告用）
    void mark_variable_modified(const std::string& name);
    void check_const_recommendations();

    // 未使用変数チェック (W001)
    void check_unused_variables();

    // 命名規則チェック (L100-L103)
    static bool is_snake_case(const std::string& name);
    static bool is_pascal_case(const std::string& name);
    static bool is_upper_snake_case(const std::string& name);
    void check_naming_conventions();

    // match式のヘルパー
    void check_match_pattern(ast::MatchPattern* pattern, ast::TypePtr expected_type);
    void check_match_exhaustiveness(ast::MatchExpr& match, ast::TypePtr scrutinee_type);

    // ============================================================
    // メンバ変数
    // ============================================================
    ScopeStack scopes_;
    ast::TypePtr current_return_type_;
    std::vector<Diagnostic> diagnostics_;
    std::unordered_map<std::string, const ast::StructDecl*> struct_defs_;

    // Lint警告の有効/無効（デフォルト: false = 警告なし）
    bool enable_lint_warnings_ = false;

    // 現在チェック中の文/式のSpan（エラー表示用）
    Span current_span_;

    // 型ごとのメソッド情報 (型名 → メソッド名 → メソッド情報)
    std::unordered_map<std::string, std::unordered_map<std::string, MethodInfo>> type_methods_;

    // 現在チェック中のimplのターゲット型（privateメソッド呼び出しチェック用）
    std::string current_impl_target_type_;

    // インターフェース実装情報 (型名 → 実装しているインターフェース名のセット)
    std::unordered_map<std::string, std::unordered_set<std::string>> impl_interfaces_;

    // インターフェース名のセット
    std::unordered_set<std::string> interface_names_;

    // インターフェースのメソッド情報 (インターフェース名 → メソッド名 → メソッド情報)
    std::unordered_map<std::string, std::unordered_map<std::string, MethodInfo>> interface_methods_;

    // enum値のキャッシュ (EnumName::MemberName -> value)
    std::unordered_map<std::string, int64_t> enum_values_;

    // enum名のセット
    std::unordered_set<std::string> enum_names_;

    // typedef定義のキャッシュ (エイリアス名 -> 実際の型)
    std::unordered_map<std::string, ast::TypePtr> typedef_defs_;

    // ジェネリックコンテキスト（現在処理中のジェネリック関数/構造体用）
    GenericContext generic_context_;

    // ジェネリック関数の登録情報（関数名 → ジェネリックパラメータリスト）
    std::unordered_map<std::string, std::vector<std::string>> generic_functions_;

    // ジェネリック関数の制約情報（関数名 → GenericParamリスト）
    std::unordered_map<std::string, std::vector<ast::GenericParam>> generic_function_constraints_;

    // ジェネリック構造体の登録情報（構造体名 → ジェネリックパラメータリスト）
    std::unordered_map<std::string, std::vector<std::string>> generic_structs_;

    // 組み込みインターフェースのジェネリックパラメータ
    std::unordered_map<std::string, std::vector<std::string>> builtin_interface_generic_params_;

    // 自動導出される演算子のマッピング (インターフェース名 → 導出演算子 → 基本演算子)
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>>
        builtin_derived_operators_;

    // 自動実装情報 (構造体名 → インターフェース名 → 実装済みフラグ)
    std::unordered_map<std::string, std::unordered_map<std::string, bool>> auto_impl_info_;

    // 変更されたことのある変数（const推奨警告用）
    std::unordered_set<std::string> modified_variables_;

    // 宣言された非const変数の情報（名前 → Span）
    std::unordered_map<std::string, Span> non_const_variable_spans_;

    // 初期化済み変数の追跡（初期化前使用チェック用）
    std::unordered_set<std::string> initialized_variables_;

    // 変数を初期化済みとしてマーク
    void mark_variable_initialized(const std::string& name);

    // 初期化前使用をチェック
    void check_uninitialized_use(const std::string& name, Span span);

    // ============================================================
    // Move Semantics - 移動済み変数の追跡（Scopeベース）
    // ============================================================
    // 変数を移動済みとしてマーク（Scope経由）
    void mark_variable_moved(const std::string& name);

    // 移動後の使用をチェック（Scope経由）
    void check_use_after_move(const std::string& name, Span span);
};

}  // namespace cm
