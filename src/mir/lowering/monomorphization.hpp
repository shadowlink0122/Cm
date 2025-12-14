#pragma once

#include "../../common/debug.hpp"
#include "lowering_base.hpp"

#include <map>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace cm::mir {

// ============================================================
// モノモーフィゼーション（単一化）
// ジェネリック型パラメータを具体的な型に特殊化
// ============================================================
class Monomorphization : public MirLoweringBase {
   public:
    // プログラム全体のモノモーフィゼーション
    void monomorphize(
        MirProgram& program,
        const std::unordered_map<std::string, const hir::HirFunction*>& hir_functions,
        const std::unordered_map<std::string, const hir::HirStruct*>& hir_structs = {}) {
        hir_funcs = &hir_functions;
        hir_struct_defs = &hir_structs;

        // 構造体のモノモーフィゼーション（関数より先に実行）
        monomorphize_structs(program);

        // ジェネリック関数を特定
        std::unordered_set<std::string> generic_funcs;
        for (const auto& [name, func] : hir_functions) {
            if (func && !func->generic_params.empty()) {
                generic_funcs.insert(name);
                debug_msg("MONO", "Found generic function: " + name + " with " +
                                      std::to_string(func->generic_params.size()) + " type params");
            }
        }

        if (generic_funcs.empty()) {
            debug_msg("MONO", "No generic functions found");
            return;  // ジェネリック関数がなければ何もしない
        }

        // 特殊化が必要な呼び出しを収集
        // (関数名, 型引数リスト) -> 呼び出し箇所のリスト
        std::map<std::pair<std::string, std::vector<std::string>>,
                 std::vector<std::tuple<std::string, size_t>>>
            needed;

        // 全関数の呼び出しをスキャンして特殊化が必要なものを見つける
        for (auto& func : program.functions) {
            if (func) {
                scan_generic_calls(func.get(), generic_funcs, hir_functions, needed);
            }
        }

        debug_msg("MONO", "Found " + std::to_string(needed.size()) + " specializations needed");

        // 特殊化関数を生成して呼び出しを書き換え
        generate_generic_specializations(program, hir_functions, needed);

        // 元のジェネリック関数を削除
        cleanup_generic_functions(program, generic_funcs);
    }

   private:
    const std::unordered_map<std::string, const hir::HirFunction*>* hir_funcs = nullptr;
    const std::unordered_map<std::string, const hir::HirStruct*>* hir_struct_defs = nullptr;

    // 生成済みの特殊化構造体名を追跡
    std::unordered_set<std::string> generated_struct_specializations;

    // ジェネリック構造体のモノモーフィゼーション
    void monomorphize_structs(MirProgram& program);

    // MIR内の全型を走査し、必要な構造体特殊化を収集
    void collect_struct_specializations(
        MirProgram& program,
        std::map<std::string, std::pair<std::string, std::vector<std::string>>>& needed);

    // 特殊化構造体を生成
    void generate_specialized_struct(MirProgram& program, const std::string& base_name,
                                     const std::vector<std::string>& type_args);

    // MIR内の型参照を更新（Pair → Pair__int など）
    void update_type_references(MirProgram& program);

    // 型名から特殊化構造体名を生成
    std::string make_specialized_struct_name(const std::string& base_name,
                                             const std::vector<std::string>& type_args) {
        std::string result = base_name;
        for (const auto& arg : type_args) {
            result += "__" + arg;
        }
        return result;
    }

    // 型からtype_argsを文字列として抽出
    std::vector<std::string> extract_type_args_strings(const hir::TypePtr& type);

    // ジェネリック関数呼び出しをスキャン
    void scan_generic_calls(
        MirFunction* func, const std::unordered_set<std::string>& generic_funcs,
        const std::unordered_map<std::string, const hir::HirFunction*>& hir_functions,
        std::map<std::pair<std::string, std::vector<std::string>>,
                 std::vector<std::tuple<std::string, size_t>>>& needed);

    // ジェネリック関数の特殊化を生成
    void generate_generic_specializations(
        MirProgram& program,
        const std::unordered_map<std::string, const hir::HirFunction*>& hir_functions,
        const std::map<std::pair<std::string, std::vector<std::string>>,
                       std::vector<std::tuple<std::string, size_t>>>& needed);

    // ジェネリック関数を削除
    void cleanup_generic_functions(MirProgram& program,
                                   const std::unordered_set<std::string>& generic_funcs);

    // 引数の型から型パラメータを推論
    std::vector<std::string> infer_type_args(const MirFunction* caller,
                                             const MirTerminator::CallData& call_data,
                                             const hir::HirFunction* callee);

    // 型名から特殊化関数名を生成
    // Container<T>__print + [int] -> Container<int>__print
    std::string make_specialized_name(const std::string& base_name,
                                      const std::vector<std::string>& type_args) {
        // base_name が "Container<T>__print" の形式かチェック
        auto pos = base_name.find("<");
        auto end_pos = base_name.find(">__");

        if (pos != std::string::npos && end_pos != std::string::npos && !type_args.empty()) {
            // Container<T>__print -> Container<int>__print
            std::string prefix = base_name.substr(0, pos + 1);  // "Container<"
            std::string suffix = base_name.substr(end_pos);     // ">__print"

            // 型引数を連結
            std::string args_str;
            for (size_t i = 0; i < type_args.size(); ++i) {
                if (i > 0)
                    args_str += ", ";
                args_str += type_args[i];
            }

            return prefix + args_str + suffix;  // "Container<int>__print"
        }

        // 通常の場合は末尾に追加
        std::string result = base_name;
        for (const auto& arg : type_args) {
            result += "__" + arg;
        }
        return result;
    }

    // インターフェース型かチェック（旧実装との互換性のため）
    bool is_interface_type(const std::string& type_name) const {
        return interface_names.count(type_name) > 0;
    }

    // 旧実装の互換性のため
    void scan_function_calls(
        MirFunction* func, const std::string& caller_name,
        const std::unordered_map<std::string, const hir::HirFunction*>& hir_functions,
        std::unordered_map<std::string, std::vector<std::tuple<std::string, size_t, std::string>>>&
            needed);

    void generate_specializations(
        MirProgram& program,
        const std::unordered_map<std::string, const hir::HirFunction*>& hir_functions,
        const std::unordered_map<
            std::string, std::vector<std::tuple<std::string, size_t, std::string>>>& needed);

    MirFunctionPtr generate_specialized_function(const hir::HirFunction& original,
                                                 const std::string& actual_type, size_t param_idx);

    void cleanup_generic_functions(
        MirProgram& program,
        const std::unordered_map<
            std::string, std::vector<std::tuple<std::string, size_t, std::string>>>& needed);
};

}  // namespace cm::mir