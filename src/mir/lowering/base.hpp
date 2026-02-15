#pragma once

#include "../../frontend/ast/typedef.hpp"
#include "../../hir/nodes.hpp"
#include "../../preprocessor/import.hpp"
#include "../nodes.hpp"
#include "context.hpp"

#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <variant>

namespace cm::mir {

// ============================================================
// MIR Lowering 基底クラス
// HIRからMIRへの変換の基本機能を提供
// ============================================================
class MirLoweringBase {
   protected:
    // 現在のMIRプログラム
    MirProgram mir_program;

    // HIR関数のキャッシュ (名前 -> HirFunction)
    std::unordered_map<std::string, const hir::HirFunction*> hir_functions;

    // 構造体定義 (名前 -> HirStruct)
    std::unordered_map<std::string, const hir::HirStruct*> struct_defs;

    // インターフェース実装情報 (型名 -> (インターフェース名 -> メソッドマップ))
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> impl_info;

    // 共有のインターフェース実装情報へのポインタ
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>>*
        shared_impl_info = nullptr;

    // インターフェース名のセット
    std::unordered_set<std::string> interface_names;

    // Tagged Union（ペイロード付きenum）名のセット
    std::unordered_set<std::string> tagged_union_names;

    // typedef定義 (エイリアス名 -> 実際の型)
    std::unordered_map<std::string, hir::TypePtr> typedef_defs;

    // enum定義 (enum名 -> (メンバー名 -> 値))
    std::unordered_map<std::string, std::unordered_map<std::string, int64_t>> enum_defs;

    // デストラクタを持つ型のセット
    std::unordered_set<std::string> types_with_destructor;

    // インターフェース定義 (インターフェース名 -> HirInterface)
    std::unordered_map<std::string, const hir::HirInterface*> interface_defs_;

    // グローバルconst変数の値
    std::unordered_map<std::string, MirConstant> global_const_values;

    // モジュール関連
    std::string current_module_path;
    std::vector<MirImportPtr> imports;
    std::unordered_map<std::string, std::string> imported_modules;

    // グローバル変数名のセット
    std::unordered_set<std::string> global_var_names;

    // モジュール範囲情報（ソースファイルベースのモジュール分割用）
    const std::vector<preprocessor::ImportPreprocessor::ModuleRange>* module_ranges_ = nullptr;

   public:
    MirLoweringBase() = default;
    virtual ~MirLoweringBase() = default;

    // モジュール範囲情報を設定（ソースファイルベースの分割用）
    void set_module_ranges(
        const std::vector<preprocessor::ImportPreprocessor::ModuleRange>* ranges) {
        module_ranges_ = ranges;
    }

    // 共有impl_infoを設定
    void set_shared_impl_info(
        std::unordered_map<std::string, std::unordered_map<std::string, std::string>>* info) {
        shared_impl_info = info;
    }

    // impl_infoを取得
    auto& get_impl_info() { return shared_impl_info ? *shared_impl_info : impl_info; }
    const auto& get_impl_info() const { return shared_impl_info ? *shared_impl_info : impl_info; }

    // MIRプログラムを取得
    MirProgram& get_program() { return mir_program; }
    const MirProgram& get_program() const { return mir_program; }

    // 型がデストラクタを持つか確認
    bool has_destructor(const std::string& type_name) const {
        return types_with_destructor.count(type_name) > 0;
    }

   protected:
    // 構造体を登録
    void register_struct(const hir::HirStruct& st);

    // typedef定義を登録
    void register_typedef(const hir::HirTypedef& td) { typedef_defs[td.name] = td.type; }

    // インポートを処理
    void process_imports(const hir::HirProgram& hir_program);

    // typedefとenumを解決（エイリアスを実際の型に展開）
    hir::TypePtr resolve_typedef(hir::TypePtr type);

    // enum定義を登録
    void register_enum(const hir::HirEnum& e);

    // グローバル変数を登録
    void register_global_var(const hir::HirGlobalVar& gv);

    // 構造体のフィールドインデックスを取得
    std::optional<FieldId> get_field_index(const std::string& struct_name,
                                           const std::string& field_name);

    // MIR構造体を生成
    MirStruct create_mir_struct(const hir::HirStruct& st);

    // ソースファイルパスの解決（module_rangesから）
    std::string resolve_source_file(uint32_t offset) const;

   private:
    // グローバルconst用のコンパイル時定数評価
    std::optional<MirConstant> try_global_const_eval(const hir::HirExpr& expr);
};

}  // namespace cm::mir
