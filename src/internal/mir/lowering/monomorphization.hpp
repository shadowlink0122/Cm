#pragma once

#include "base.hpp"
#include "internal/base/debug.hpp"

#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

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

    // 単相化で型が確定したローカルのprintln/print系ディスパッチ補正（N2）
    void fixup_println_dispatch(MirFunction* caller, LocalId local_id);

    // ジェネリック関数呼び出しを特殊化関数呼び出しに書き換え
    void rewrite_generic_calls(
        MirProgram& program, const std::map<std::pair<std::string, std::vector<std::string>>,
                                            std::vector<std::tuple<std::string, size_t>>>& needed);

    // MIR内の全型を走査し、必要な構造体特殊化を収集（型引数はhir::Typeツリーで保持する）
    void collect_struct_specializations(
        MirProgram& program,
        std::map<std::string, std::pair<std::string, std::vector<hir::TypePtr>>>& needed);

    // 特殊化構造体を生成（型引数はhir::Typeツリー。ネストした特殊化も再帰的に生成する）
    void generate_specialized_struct(MirProgram& program, const std::string& base_name,
                                     const std::vector<hir::TypePtr>& type_args);

    // 型引数1個分のシンボルキーを生成（既存の__フラット規約を維持する）
    std::string arg_symbol_key(const hir::TypePtr& arg) const;

    // 特殊化構造体のシンボルキーを生成する。
    // 通常は関数特殊化経路と整合する従来のbase__arg連結を用い、フラット名がユーザー定義
    // 構造体と同名になる場合のみ'$'区切りの可逆エンコード名へ退避して縮退を排除する（C8）
    std::string struct_symbol_key(const std::string& base_name,
                                  const std::vector<hir::TypePtr>& type_args) const;

    // フラット特殊化名の残り部分（base__以降）を型引数ツリーへ復元する。
    // 基底の型パラメータが1個の場合は全セグメントを1引数として結合する（ネスト対応）
    std::vector<hir::TypePtr> parse_flat_type_args(const std::string& base_name,
                                                   const std::string& remainder) const;

    // 型パラメータを実引数ツリーで置換する（名前の平坦化を行わず構造を保つ）
    hir::TypePtr substitute_type_tree(
        const hir::TypePtr& type, const std::unordered_map<std::string, hir::TypePtr>& subst) const;

    // 置換済みツリー内のジェネリックインスタンスを特殊化生成し、シンボル名参照へ書き換える
    hir::TypePtr to_symbol_type(MirProgram& program, const hir::TypePtr& type);

    // 特殊化名（フラット名・エンコード名・基底名+型引数）から置換済みフィールド型を復元する
    std::optional<std::vector<hir::TypePtr>> resolve_struct_field_types(
        const hir::TypePtr& type) const;

    // 型ツリー内に未解決のジェネリック型パラメータが残っているか
    bool tree_has_generic_param(const hir::TypePtr& type) const;

    // 全ジェネリック型パラメータ名（構造体・関数から収集。tree_has_generic_paramで使用）
    std::unordered_set<std::string> all_generic_param_names;

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

    // 特殊化された型のサイズを計算（自然アライメントのCレイアウト）
    int64_t calculate_specialized_type_size(const hir::TypePtr& type) const;

    // 特殊化された型のアライメントを計算
    int64_t calculate_specialized_type_align(const hir::TypePtr& type) const;
};

}  // namespace cm::mir