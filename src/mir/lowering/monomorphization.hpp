#pragma once

#include "lowering_base.hpp"

#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace cm::mir {

// ============================================================
// モノモーフィゼーション（単一化）
// インターフェースパラメータを具体的な型に特殊化
// ============================================================
class Monomorphization : public MirLoweringBase {
   public:
    // プログラム全体のモノモーフィゼーション
    void monomorphize(
        MirProgram& program,
        const std::unordered_map<std::string, const hir::HirFunction*>& hir_functions) {
        // 単一化が必要な呼び出しを収集 (関数名 -> [(呼び出し元, パラメータIndex, 実際の型)])
        std::unordered_map<std::string, std::vector<std::tuple<std::string, size_t, std::string>>>
            needed_specializations;

        // 全関数の呼び出しをスキャンして単一化が必要なものを見つける
        for (auto& func : program.functions) {
            if (func) {
                scan_function_calls(func.get(), func->name, hir_functions, needed_specializations);
            }
        }

        // 特殊化関数を生成して呼び出しを書き換え
        generate_specializations(program, hir_functions, needed_specializations);

        // 不要になった元の関数を削除
        cleanup_generic_functions(program, needed_specializations);
    }

   private:
    // 関数内の呼び出しをスキャンして特殊化が必要なものを記録
    void scan_function_calls(
        MirFunction* func, const std::string& caller_name,
        const std::unordered_map<std::string, const hir::HirFunction*>& hir_functions,
        std::unordered_map<std::string, std::vector<std::tuple<std::string, size_t, std::string>>>&
            needed);

    // 特殊化関数を生成
    void generate_specializations(
        MirProgram& program,
        const std::unordered_map<std::string, const hir::HirFunction*>& hir_functions,
        const std::unordered_map<
            std::string, std::vector<std::tuple<std::string, size_t, std::string>>>& needed);

    // 特殊化関数を生成（単一の関数）
    MirFunctionPtr generate_specialized_function(const hir::HirFunction& original,
                                                 const std::string& actual_type, size_t param_idx);

    // 元のジェネリック関数を削除
    void cleanup_generic_functions(
        MirProgram& program,
        const std::unordered_map<
            std::string, std::vector<std::tuple<std::string, size_t, std::string>>>& needed);

    // 型名から特殊化関数名を生成
    std::string make_specialized_name(const std::string& base_name, const std::string& type_name) {
        // 例: print_value + Point -> print_value__Point
        return base_name + "__" + type_name;
    }

    // インターフェース型かチェック
    bool is_interface_type(const std::string& type_name) const {
        return interface_names.count(type_name) > 0;
    }

    // 型がインターフェースを実装しているかチェック
    bool implements_interface(const std::string& type_name,
                              const std::string& interface_name) const {
        auto it = impl_info.find(type_name);
        if (it != impl_info.end()) {
            return it->second.count(interface_name) > 0;
        }
        return false;
    }
};

}  // namespace cm::mir