#pragma once

#include "../common/debug/hir.hpp"
#include "../frontend/ast/decl.hpp"
#include "nodes.hpp"

#include <set>
#include <unordered_map>
#include <unordered_set>

namespace cm::hir {

// ============================================================
// AST → HIR 変換
// ============================================================
class HirLowering {
   public:
    // メインエントリポイント
    HirProgram lower(ast::Program& program);

   private:
    // キャッシュ
    std::unordered_map<std::string, const ast::StructDecl*> struct_defs_;
    std::unordered_map<std::string, const ast::FunctionDecl*> func_defs_;
    std::unordered_map<std::string, int64_t> enum_values_;
    std::unordered_set<std::string> types_with_default_ctor_;

    // ヘルパー関数
    std::string get_default_member_name(const std::string& struct_name) const;
    void process_namespace(ast::ModuleDecl& mod, const std::string& parent_namespace,
                           HirProgram& hir);

    // 宣言のlowering
    HirDeclPtr lower_decl(ast::Decl& decl);
    HirDeclPtr lower_function(ast::FunctionDecl& func);
    HirDeclPtr lower_struct(ast::StructDecl& st);
    HirDeclPtr lower_interface(ast::InterfaceDecl& iface);
    HirDeclPtr lower_impl(ast::ImplDecl& impl);
    HirDeclPtr lower_import(ast::ImportDecl& imp);
    HirDeclPtr lower_enum(ast::EnumDecl& en);
    HirDeclPtr lower_typedef(ast::TypedefDecl& td);
    HirDeclPtr lower_global_var(ast::GlobalVarDecl& gv);
    HirDeclPtr lower_module(ast::ModuleDecl& mod);
    HirDeclPtr lower_extern_block(ast::ExternBlockDecl& extern_block);

    // 文のlowering
    HirStmtPtr lower_stmt(ast::Stmt& stmt);
    HirStmtPtr lower_let(ast::LetStmt& let);
    HirStmtPtr lower_return(ast::ReturnStmt& ret);
    HirStmtPtr lower_if(ast::IfStmt& if_stmt);
    HirStmtPtr lower_while(ast::WhileStmt& while_stmt);
    HirStmtPtr lower_for(ast::ForStmt& for_stmt);
    HirStmtPtr lower_for_in(ast::ForInStmt& for_in);
    HirStmtPtr lower_switch(ast::SwitchStmt& switch_stmt);
    HirStmtPtr lower_expr_stmt(ast::ExprStmt& expr_stmt);
    HirStmtPtr lower_block(ast::BlockStmt& block);
    HirStmtPtr lower_defer(ast::DeferStmt& defer);
    std::unique_ptr<HirSwitchPattern> lower_pattern(ast::Pattern& pattern);

    // 式のlowering
    HirExprPtr lower_expr(ast::Expr& expr);
    HirExprPtr lower_literal(ast::LiteralExpr& lit, TypePtr type);
    HirExprPtr lower_binary(ast::BinaryExpr& binary, TypePtr type);
    HirExprPtr lower_unary(ast::UnaryExpr& unary, TypePtr type);
    HirExprPtr lower_call(ast::CallExpr& call, TypePtr type);
    HirExprPtr lower_index(ast::IndexExpr& idx, TypePtr type);
    HirExprPtr lower_slice(ast::SliceExpr& slice, TypePtr type);
    HirExprPtr lower_member(ast::MemberExpr& mem, TypePtr type);
    HirExprPtr lower_ternary(ast::TernaryExpr& tern, TypePtr type);
    HirExprPtr lower_match(ast::MatchExpr& match, TypePtr type);
    HirExprPtr lower_struct_literal(ast::StructLiteralExpr& lit, TypePtr expected_type);
    HirExprPtr lower_array_literal(ast::ArrayLiteralExpr& lit, TypePtr expected_type);

    // 演算子変換
    HirOperatorKind convert_operator_kind(ast::OperatorKind kind);
    HirBinaryOp convert_binary_op(ast::BinaryOp op);
    HirUnaryOp convert_unary_op(ast::UnaryOp op);
    HirBinaryOp get_base_op(ast::BinaryOp op);
    bool is_compound_assign(ast::BinaryOp op);
    bool is_comparison_op(ast::BinaryOp op);

    // デバッグ文字列
    std::string hir_binary_op_to_string(HirBinaryOp op);
    std::string hir_unary_op_to_string(HirUnaryOp op);

    // match式のヘルパー
    HirExprPtr make_default_value(TypePtr type);
    HirExprPtr build_match_condition(const HirExprPtr& scrutinee, TypePtr scrutinee_type,
                                     const ast::MatchArm& arm);
    HirExprPtr clone_hir_expr(const HirExprPtr& expr);
    HirExprPtr lower_guard_with_binding(ast::Expr& guard, const std::string& var_name,
                                        const HirExprPtr& scrutinee, TypePtr scrutinee_type);
};

}  // namespace cm::hir
