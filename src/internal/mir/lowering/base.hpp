#pragma once

#include "context.hpp"
#include "internal/base/diag/diagnostics.hpp"
#include "internal/base/source/module_range.hpp"
#include "internal/hir/nodes.hpp"
#include "internal/mir/nodes.hpp"
#include "internal/syntax/ast/typedef.hpp"

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

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

    // インターフェイスメソッドの戻り値型 (マングル名`Iface__method` -> 戻り値型。B7: 補間ミニパイプラインの戻り値型解決用にLoweringContextへシードする)
    std::unordered_map<std::string, hir::TypePtr> interface_method_returns_;

    // グローバルconst変数の値
    std::unordered_map<std::string, MirConstant> global_const_values;

    // モジュール関連
    std::string current_module_path;
    std::vector<MirImportPtr> imports;
    std::unordered_map<std::string, std::string> imported_modules;

    // グローバル変数名のセット
    std::unordered_set<std::string> global_var_names;

    // モジュール範囲情報（ソースファイルベースのモジュール分割用）
    const std::vector<cm::ModuleRange>* module_ranges_ = nullptr;

    // MIR段階の診断（ログでなく診断として収集しcodegen前に停止する。diagnostics-engine-unification 第2段）
    // MirLoweringが親としてexpr/stmt loweringへ自身のベクタを共有する
    std::vector<Diagnostic> mir_diagnostics_;
    std::vector<Diagnostic>* shared_diagnostics_ = nullptr;

   public:
    MirLoweringBase() = default;
    virtual ~MirLoweringBase() = default;

    // モジュール範囲情報を設定（ソースファイルベースの分割用）
    void set_module_ranges(const std::vector<cm::ModuleRange>* ranges) { module_ranges_ = ranges; }

    // 診断ベクタを共有（MirLoweringのctorでexpr/stmt loweringへ配線する）
    void set_shared_diagnostics(std::vector<Diagnostic>* diags) { shared_diagnostics_ = diags; }

    // MIR段階の診断（共有されていれば親のベクタ）
    std::vector<Diagnostic>& mir_diagnostics() {
        return shared_diagnostics_ ? *shared_diagnostics_ : mir_diagnostics_;
    }
    const std::vector<Diagnostic>& mir_diagnostics() const {
        return shared_diagnostics_ ? *shared_diagnostics_ : mir_diagnostics_;
    }

    // MIR段階の問題をエラー診断として報告する（黙殺・ログ出力での代替は禁止）
    void report_error(const Span& span, const std::string& message) {
        mir_diagnostics().emplace_back(Severity::Error, span, message);
    }

    bool has_diagnostic_errors() const {
        for (const auto& d : mir_diagnostics()) {
            if (d.severity == Severity::Error) {
                return true;
            }
        }
        return false;
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
