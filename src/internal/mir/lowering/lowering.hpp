#pragma once

#include "base.hpp"
#include "expr.hpp"
#include "monomorphization.hpp"
#include "stmt.hpp"

#include <algorithm>
#include <memory>
#include <numeric>

namespace cm::mir {

// ============================================================
// MIR Lowering
// HIRからMIRへの変換のメインクラス
// ============================================================
class MirLowering : public MirLoweringBase {
   private:
    StmtLowering stmt_lowering;
    ExprLowering expr_lowering;
    Monomorphization monomorphizer;

    // インターフェース定義のキャッシュ
    std::unordered_map<std::string, const hir::HirInterface*> interface_defs_;

    // ジェネリック構造体のauto_implsを保存
    std::unordered_map<std::string, std::vector<std::string>> generic_struct_auto_impls_;

   public:
    MirLowering() {
        // 相互参照の設定
        stmt_lowering.set_expr_lowering(&expr_lowering);
        // impl_infoを共有
        expr_lowering.set_shared_impl_info(&impl_info);
        stmt_lowering.set_shared_impl_info(&impl_info);
    }

    // HIRプログラムをMIRに変換
    MirProgram lower(const hir::HirProgram& hir_program);

    // 高階関数呼び出し（map/filter）をクロージャ対応版に書き換え
    void rewrite_hof_calls_for_closures();

    // クロージャ情報を代入先変数に伝播（固定点まで繰り返し）
    void propagate_closure_info();

   private:
    // 宣言の登録
    void register_declarations(const hir::HirProgram& hir_program);

    // HirOperatorKindをMirOperatorKindに変換
    MirOperatorKind convert_hir_operator_kind(hir::HirOperatorKind kind);

    // インターフェース定義を登録
    void register_interface(const hir::HirInterface& iface);

    // vtable生成
    void generate_vtables();

    // impl定義を登録
    void register_impl(const hir::HirImpl& impl);

    // with キーワードによる自動実装を生成
    void generate_auto_impls(const hir::HirProgram& hir_program);

    // モノモーフィゼーション後のジェネリック構造体に対する自動実装を生成
    void generate_monomorphized_auto_impls();

    // 組み込み演算子/メソッドの自動生成（非ジェネリック構造体用）
    void generate_builtin_eq_operator(const hir::HirStruct& st);
    void generate_builtin_lt_operator(const hir::HirStruct& st);
    void generate_builtin_clone_method(const hir::HirStruct& st);
    void generate_builtin_hash_method(const hir::HirStruct& st);
    void generate_builtin_debug_method(const hir::HirStruct& st);
    void generate_builtin_display_method(const hir::HirStruct& st);
    void generate_builtin_css_method(const hir::HirStruct& st);
    void generate_builtin_to_css_method(const hir::HirStruct& st);
    void generate_builtin_is_css_method(const hir::HirStruct& st);

    // 組み込み演算子/メソッドの自動生成（モノモーフィゼーション版）
    void generate_builtin_eq_operator_for_monomorphized(const MirStruct& st);
    void generate_builtin_lt_operator_for_monomorphized(const MirStruct& st);
    void generate_builtin_clone_method_for_monomorphized(const MirStruct& st);
    void generate_builtin_hash_method_for_monomorphized(const MirStruct& st);
    void generate_builtin_debug_method_for_monomorphized(const MirStruct& st);
    void generate_builtin_display_method_for_monomorphized(const MirStruct& st);
    void generate_builtin_css_method_for_monomorphized(const MirStruct& st);
    void generate_builtin_to_css_method_for_monomorphized(const MirStruct& st);
    void generate_builtin_is_css_method_for_monomorphized(const MirStruct& st);

    // 構造体の比較演算子を関数呼び出しに変換するパス
    void rewrite_struct_comparison_operators();

    // 演算子の自動実装を生成（ユーザー定義インターフェース用）
    void generate_auto_operator_impl(const hir::HirStruct& st, const hir::HirInterface& iface,
                                     const hir::HirOperatorSig& op);

    // kebab-case変換ヘルパー
    std::string to_kebab_case(const std::string& name);

    // 通常の関数をlowering
    void lower_functions(const hir::HirProgram& hir_program);

    // impl内のメソッドをlowering
    void lower_impl_methods(const hir::HirProgram& hir_program);

    // モノモーフィゼーションを実行
    void perform_monomorphization();

    // 関数のlowering
    std::unique_ptr<MirFunction> lower_function(const hir::HirFunction& func);

    // 演算子実装のlowering
    std::unique_ptr<MirFunction> lower_operator(const hir::HirOperatorImpl& op_impl,
                                                const std::string& type_name);

    // impl内のメソッドをlowering
    void lower_impl(const hir::HirImpl& impl);

    // デストラクタ呼び出しを挿入
    void emit_destructors(LoweringContext& ctx);
};

}  // namespace cm::mir
