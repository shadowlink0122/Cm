#pragma once

#include "../../hir/nodes.hpp"
#include "../nodes.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace cm::mir {

// ============================================================
// 自動実装生成器
// with キーワードによる組み込みトレイト（Eq, Ord, Clone, Hash, Debug, Display, Css）
// の自動実装を生成するクラス
// ============================================================
class AutoImplGenerator {
   public:
    // 必要な参照を保持
    struct Context {
        MirProgram& program;
        std::unordered_map<std::string, std::unordered_map<std::string, std::string>>& impl_info;
        const std::unordered_map<std::string, const hir::HirInterface*>& interface_defs;
    };

   private:
    Context ctx_;
    // ジェネリック構造体のauto_implsを保存
    std::unordered_map<std::string, std::vector<std::string>> generic_struct_auto_impls_;

   public:
    explicit AutoImplGenerator(Context ctx) : ctx_(ctx) {}

    // ジェネリック構造体のauto_implsを登録
    void register_generic_auto_impls(const std::string& name,
                                     const std::vector<std::string>& impls) {
        generic_struct_auto_impls_[name] = impls;
    }

    // 非ジェネリック構造体の自動実装を生成
    void generate_for_struct(const hir::HirStruct& st);

    // モノモーフィゼーション後の自動実装を生成
    void generate_monomorphized_auto_impls();

    // ============================================================
    // 組み込みトレイト実装（非ジェネリック構造体用）
    // ============================================================
    void generate_builtin_eq_operator(const hir::HirStruct& st);
    void generate_builtin_lt_operator(const hir::HirStruct& st);
    void generate_builtin_clone_method(const hir::HirStruct& st);
    void generate_builtin_hash_method(const hir::HirStruct& st);
    void generate_builtin_debug_method(const hir::HirStruct& st);
    void generate_builtin_display_method(const hir::HirStruct& st);
    void generate_builtin_css_method(const hir::HirStruct& st);
    void generate_builtin_is_css_method(const hir::HirStruct& st);

    // ============================================================
    // 組み込みトレイト実装（モノモーフィゼーション済み構造体用）
    // ============================================================
    void generate_builtin_eq_operator_for_monomorphized(const MirStruct& st);
    void generate_builtin_lt_operator_for_monomorphized(const MirStruct& st);
    void generate_builtin_clone_method_for_monomorphized(const MirStruct& st);
    void generate_builtin_hash_method_for_monomorphized(const MirStruct& st);
    void generate_builtin_debug_method_for_monomorphized(const MirStruct& st);
    void generate_builtin_display_method_for_monomorphized(const MirStruct& st);
    void generate_builtin_css_method_for_monomorphized(const MirStruct& st);
    void generate_builtin_is_css_method_for_monomorphized(const MirStruct& st);

    // ユーザー定義演算子の自動実装
    void generate_auto_operator_impl(const hir::HirStruct& st, const hir::HirInterface& iface,
                                     const hir::HirOperatorSig& op);

   private:
    // ヘルパー: kebab-case変換
    std::string to_kebab_case(const std::string& name);
};

}  // namespace cm::mir
