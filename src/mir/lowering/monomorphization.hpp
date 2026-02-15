#pragma once

#include "../../common/debug.hpp"
#include "base.hpp"

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
        const std::unordered_map<std::string, const hir::HirStruct*>& hir_structs = {});

   private:
    const std::unordered_map<std::string, const hir::HirFunction*>* hir_funcs = nullptr;
    const std::unordered_map<std::string, const hir::HirStruct*>* hir_struct_defs = nullptr;

    // 生成済みの特殊化構造体名を追跡
    std::unordered_set<std::string> generated_struct_specializations;

    // ジェネリック構造体のモノモーフィゼーション
    void monomorphize_structs(MirProgram& program);

    // ジェネリック関数呼び出しを特殊化関数呼び出しに書き換え
    void rewrite_generic_calls(
        MirProgram& program, const std::map<std::pair<std::string, std::vector<std::string>>,
                                            std::vector<std::tuple<std::string, size_t>>>& needed);

    // MIR内の全型を走査し、必要な構造体特殊化を収集
    void collect_struct_specializations(
        MirProgram& program,
        std::map<std::string, std::pair<std::string, std::vector<std::string>>>& needed);

    // 特殊化構造体を生成
    void generate_specialized_struct(MirProgram& program, const std::string& base_name,
                                     const std::vector<std::string>& type_args);

    // MIR内の型参照を更新
    void update_type_references(MirProgram& program);

    // 構造体メソッドのself引数を参照に修正
    void fix_struct_method_self_args(MirProgram& program);

    // ポインタ型名を正規化
    std::string normalize_type_arg(const std::string& type_arg);

    // 型名から特殊化構造体名を生成
    std::string make_specialized_struct_name(const std::string& base_name,
                                             const std::vector<std::string>& type_args);

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
    std::string make_specialized_name(const std::string& base_name,
                                      const std::vector<std::string>& type_args);

    // インターフェース型かチェック
    bool is_interface_type(const std::string& type_name) const;

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

    // 特殊化された型のサイズを計算
    int64_t calculate_specialized_type_size(const hir::TypePtr& type) const;
};

}  // namespace cm::mir