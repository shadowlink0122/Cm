// 単相化 - プログラム全体の単相化を駆動するエントリポイント

#include "internal/base/debug.hpp"
#include "internal/base/target.hpp"
#include "internal/mir/lowering/mono_internal.hpp"
#include "internal/mir/lowering/monomorphization.hpp"
#include "internal/mir/lowering/monomorphization_utils.hpp"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cm::mir {

// プログラム全体のモノモーフィゼーション（1パス実行）
void Monomorphization::monomorphize(
    MirProgram& program,
    const std::unordered_map<std::string, const hir::HirFunction*>& hir_functions,
    const std::unordered_map<std::string, const hir::HirStruct*>& hir_structs) {
    hir_funcs = &hir_functions;
    hir_struct_defs = &hir_structs;

    // 構造体のモノモーフィゼーション（関数より先に実行、1回のみ）
    monomorphize_structs(program);

    // ジェネリック関数を特定
    std::unordered_set<std::string> generic_funcs;
    for (const auto& [name, func] : hir_functions) {
        if (!func)
            continue;
        bool is_generic = !func->generic_params.empty() || name.find('<') != std::string::npos;
        if (is_generic) {
            generic_funcs.insert(name);
            debug_msg("MONO", "Found generic function: " + name + " with " +
                                  std::to_string(func->generic_params.size()) + " type params" +
                                  (name.find('<') != std::string::npos ? " (impl method)" : ""));
        }
    }

    if (generic_funcs.empty()) {
        debug_msg("MONO", "No generic functions found");
        fix_struct_method_self_args(program);
        return;
    }

    for (const auto& gf : generic_funcs) {
        debug_msg("MONO", "Generic func in set: " + gf);
    }

    // モノモーフィゼーションを不動点まで反復する。
    // 1パス目: 全関数をスキャン。以降のパス: 直前に新規生成された特殊化関数のみスキャンし、
    // その本体から芋づる式に必要になる特殊化を生成する（メソッドチェーン A->B->C... の深さに依らず全て生成）。
    // 新規特殊化が無くなれば下の new_needed.empty() で抜ける。上限は暴走防止の安全弁（実コードのネスト深度を大きく超える）。
    std::unordered_set<std::string> all_generated;
    const int MAX_PASSES = 64;

    for (int pass = 0; pass < MAX_PASSES; ++pass) {
        std::map<std::pair<std::string, std::vector<std::string>>,
                 std::vector<std::tuple<std::string, size_t>>>
            needed;

        for (auto& func : program.functions) {
            if (!func)
                continue;
            // 2パス目: 新規生成された特殊化関数のみスキャン
            if (pass > 0 && all_generated.count(func->name) == 0)
                continue;
            scan_generic_calls(func.get(), generic_funcs, hir_functions, needed);
        }

        // 既に生成済みの特殊化を除外
        std::map<std::pair<std::string, std::vector<std::string>>,
                 std::vector<std::tuple<std::string, size_t>>>
            new_needed;
        for (const auto& [key, call_sites] : needed) {
            std::string specialized_name = make_specialized_name(key.first, key.second);
            if (all_generated.count(specialized_name) == 0) {
                new_needed[key] = call_sites;
            }
        }

        if (new_needed.empty()) {
            debug_msg("MONO", "Pass " + std::to_string(pass) + ": No new specializations needed");
            break;
        }

        debug_msg("MONO", "Pass " + std::to_string(pass) + ": " +
                              std::to_string(new_needed.size()) + " specializations");

        generate_generic_specializations(program, hir_functions, new_needed);

        for (const auto& [key, _] : new_needed) {
            std::string specialized_name = make_specialized_name(key.first, key.second);
            all_generated.insert(specialized_name);
        }

        rewrite_generic_calls(program, new_needed);
    }

    fix_struct_method_self_args(program);
    cleanup_generic_functions(program, generic_funcs);
}

}  // namespace cm::mir
