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
    // 特殊化要求（typed-instantiation）: 総称シンボル名+型引数ツリー+呼び出しサイト表。
    // 特殊化の同定は型ノードで行い、シンボル名はエンコード終端（make_specialized_name）でのみ生成する
    struct SpecRequest {
        std::string generic_name;             // 総称シンボル名（HIR関数キー）
        std::vector<hir::TypePtr> type_args;  // 型引数ツリー
        std::vector<std::pair<std::string, size_t>> call_sites;  // (呼び出し元関数名, block index)
    };
    // キー=特殊化シンボル名（型引数はarg_symbol_keyで一意にエンコード済みのため重複判定を兼ねる）
    using SpecRequests = std::map<std::string, SpecRequest>;

    // 特殊化した総称演算子impl（impl<T> Foo<T> for Eq { operator ... }）の記録。
    // 演算子呼び出しは生のBinaryOpのため呼び出しサイト走査で拾えず、構造体特殊化集合を起点に特殊化する。
    // モノモーフ化後にMirLoweringがimpl_infoへ登録し、Pass 6の演算子→関数呼び出し書き換えが引き当てる
    struct SpecializedOperator {
        std::string struct_name;  // 特殊化構造体キー（Pass 6のローカル型名と一致）
        std::string op_kind;      // impl_infoキー（Eq/Ord/Add...）
        std::string func_name;  // 特殊化演算子関数名
    };
    const std::vector<SpecializedOperator>& specialized_operators() const {
        return specialized_operators_;
    }

    // プログラム全体のモノモーフィゼーション
    void monomorphize(
        MirProgram& program,
        const std::unordered_map<std::string, const hir::HirFunction*>& hir_functions,
        const std::unordered_map<std::string, const hir::HirStruct*>& hir_structs = {});

    // 型引数1個分のシンボルキーを生成する（正準キーAPI）
    std::string arg_symbol_key(const hir::TypePtr& arg) const;

    // 特殊化構造体のシンボルキーを生成する（正準キーAPI）。
    // フラット名（base__k1__k2）は本質的に曖昧なため産生を全廃し、常に'$'長さ接頭辞の可逆エンコード名を返す
    std::string struct_symbol_key(const std::string& base_name,
                                  const std::vector<hir::TypePtr>& type_args) const;

   private:
    const std::unordered_map<std::string, const hir::HirFunction*>* hir_funcs = nullptr;
    const std::unordered_map<std::string, const hir::HirStruct*>* hir_struct_defs = nullptr;

    // 生成済みの特殊化構造体名を追跡
    std::unordered_set<std::string> generated_struct_specializations;

    // 特殊化した総称演算子implの記録（MirLoweringがimpl_info登録に使用）
    std::vector<SpecializedOperator> specialized_operators_;
    // 種蒔き済みの演算子特殊化名（固定点反復での重複種蒔きを防ぐ）
    std::unordered_set<std::string> seeded_operator_specs_;

    // ジェネリック構造体のモノモーフィゼーション
    void monomorphize_structs(MirProgram& program);

    // 単相化で型が確定したローカルのprintln/print系ディスパッチ補正（N2）
    void fixup_println_dispatch(MirFunction* caller, LocalId local_id);

    // ジェネリック呼び出しを特殊化呼び出しへ書き換える（スキャンで記録した呼び出しサイト表の引き当てのみ。
    // 名前パターン照合・型パラメータ名の仮定（T/U/V/W）は行わない）
    void rewrite_generic_calls(MirProgram& program, const SpecRequests& needed);

    // MIR内の全型を走査し、必要な構造体特殊化を収集（型引数はhir::Typeツリーで保持する）
    void collect_struct_specializations(
        MirProgram& program,
        std::map<std::string, std::pair<std::string, std::vector<hir::TypePtr>>>& needed);

    // 特殊化構造体を生成（型引数はhir::Typeツリー。ネストした特殊化も再帰的に生成する）
    void generate_specialized_struct(MirProgram& program, const std::string& base_name,
                                     const std::vector<hir::TypePtr>& type_args);

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

    // 型からtype_argsを文字列として抽出
    std::vector<std::string> extract_type_args_strings(const hir::TypePtr& type);

    // ジェネリック関数呼び出しをスキャンし、特殊化要求（型引数ツリー+呼び出しサイト）を収集する
    void scan_generic_calls(
        MirFunction* func, const std::unordered_set<std::string>& generic_funcs,
        const std::unordered_map<std::string, const hir::HirFunction*>& hir_functions,
        SpecRequests& needed);

    // ジェネリック関数の特殊化を生成
    void generate_generic_specializations(
        MirProgram& program,
        const std::unordered_map<std::string, const hir::HirFunction*>& hir_functions,
        const SpecRequests& needed);

    // ジェネリック関数を削除
    void cleanup_generic_functions(MirProgram& program,
                                   const std::unordered_set<std::string>& generic_funcs);

    // 生成済み構造体特殊化を起点に総称演算子implの特殊化要求を種蒔きする。
    // 演算子の呼び出しサイトは生のBinaryOpでスキャンに現れないため、構造体特殊化集合から要求を作る
    void seed_operator_specializations(MirProgram& program, SpecRequests& needed);

    // 呼び出しサイトの実引数型・戻り値格納先型（MIRローカルの型ツリー）から型パラメータを構造的単一化で推論する
    std::vector<hir::TypePtr> infer_type_args(const MirFunction* caller,
                                              const MirTerminator::CallData& call_data,
                                              const hir::HirFunction* callee);

    // パラメータ型と実引数型の構造的単一化（Pointer/Array/Structのtype_argsを再帰照合し型パラメータを束縛する）
    void unify_type_param(const hir::TypePtr& param_type, const hir::TypePtr& arg_type,
                          const hir::HirFunction* callee,
                          std::unordered_map<std::string, hir::TypePtr>& inferred) const;

    // フラット名/表示名を型ツリーへ復元する単一の境界（Vector__int / Vector<int> / ptr_int / int）
    hir::TypePtr decode_type_name(const std::string& name) const;

    // 特殊化関数名を生成（型引数ツリーからarg_symbol_keyでエンコード。名前生成の終端）
    std::string make_specialized_name(const std::string& base_name,
                                      const std::vector<hir::TypePtr>& type_args) const;

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