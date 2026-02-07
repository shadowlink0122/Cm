#pragma once

#include "../../hir/nodes.hpp"
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

    // 共有のインターフェース実装情報へのポインタ（設定されている場合はこちらを使用）
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>>*
        shared_impl_info = nullptr;

    // インターフェース名のセット
    std::unordered_set<std::string> interface_names;

    // typedef定義 (エイリアス名 -> 実際の型)
    std::unordered_map<std::string, hir::TypePtr> typedef_defs;

    // enum定義 (enum名 -> (メンバー名 -> 値))
    std::unordered_map<std::string, std::unordered_map<std::string, int64_t>> enum_defs;

    // デストラクタを持つ型のセット
    std::unordered_set<std::string> types_with_destructor;

    // インターフェース定義 (インターフェース名 -> HirInterface)
    std::unordered_map<std::string, const hir::HirInterface*> interface_defs_;

    // グローバルconst変数の値 (変数名 -> 定数値)
    // 文字列補間で使用するため、モジュールレベルのconst変数の初期値を保持
    std::unordered_map<std::string, MirConstant> global_const_values;

    // モジュール関連
    std::string current_module_path;    // 現在のモジュールパス
    std::vector<MirImportPtr> imports;  // インポートリスト
    std::unordered_map<std::string, std::string> imported_modules;  // エイリアス -> モジュールパス

   public:
    MirLoweringBase() = default;
    virtual ~MirLoweringBase() = default;

    // 共有impl_infoを設定
    void set_shared_impl_info(
        std::unordered_map<std::string, std::unordered_map<std::string, std::string>>* info) {
        shared_impl_info = info;
    }

    // impl_infoを取得（共有が設定されていればそれを使用）
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
    void register_struct(const hir::HirStruct& st) {
        struct_defs[st.name] = &st;
        // 初期化メソッドがある場合は記録
        if (!st.has_explicit_constructor) {
            // デフォルトコンストラクタがある
            impl_info[st.name]["@init"] = st.name + "__init";
        }
    }

    // typedef定義を登録
    void register_typedef(const hir::HirTypedef& td) { typedef_defs[td.name] = td.type; }

    // インポートを処理
    void process_imports(const hir::HirProgram& hir_program) {
        for (const auto& decl : hir_program.declarations) {
            if (auto* imp = std::get_if<std::unique_ptr<hir::HirImport>>(&decl->kind)) {
                auto mir_import = std::make_unique<MirImport>();
                mir_import->path = (*imp)->path;
                mir_import->package_name = (*imp)->package_name;
                mir_import->alias = (*imp)->alias;

                // モジュール名を構築（例: ["std", "io"] -> "std::io"）
                std::string module_name;
                for (size_t i = 0; i < mir_import->path.size(); ++i) {
                    if (i > 0)
                        module_name += "::";
                    module_name += mir_import->path[i];
                }

                // インポートマップに追加
                if (!mir_import->alias.empty()) {
                    imported_modules[mir_import->alias] = module_name;
                } else {
                    // エイリアスがない場合は最後のセグメントを使用
                    if (!mir_import->path.empty()) {
                        imported_modules[mir_import->path.back()] = module_name;
                    }
                }

                mir_program.imports.push_back(std::move(mir_import));
            }
        }
    }

    // typedefとenumを解決（エイリアスを実際の型に展開）
    hir::TypePtr resolve_typedef(hir::TypePtr type) {
        if (!type)
            return type;

        // TypeAlias型の場合、typedefかチェック
        if (type->kind == hir::TypeKind::TypeAlias || type->kind == hir::TypeKind::Struct) {
            // まずtypedef定義を確認
            auto it = typedef_defs.find(type->name);
            if (it != typedef_defs.end()) {
                // 再帰的に解決（typedef chainがある場合）
                return resolve_typedef(it->second);
            }

            // enum定義を確認（enum型はintとして扱う）
            auto enum_it = enum_defs.find(type->name);
            if (enum_it != enum_defs.end()) {
                return hir::make_int();
            }
        }

        // ポインタ型の場合、要素型を再帰的に解決
        if (type->kind == hir::TypeKind::Pointer || type->kind == hir::TypeKind::Reference) {
            auto resolved_elem = resolve_typedef(type->element_type);
            if (resolved_elem != type->element_type) {
                auto result = std::make_shared<hir::Type>(*type);
                result->element_type = resolved_elem;
                return result;
            }
        }

        // 配列型の場合、要素型を再帰的に解決
        if (type->kind == hir::TypeKind::Array) {
            auto resolved_elem = resolve_typedef(type->element_type);
            if (resolved_elem != type->element_type) {
                auto result = std::make_shared<hir::Type>(*type);
                result->element_type = resolved_elem;
                return result;
            }
        }

        return type;
    }

    // enum定義を登録（MirEnumも作成してmir_programに追加）
    void register_enum(const hir::HirEnum& e) {
        // enum_defsに登録（値マッピング用）
        for (const auto& member : e.members) {
            enum_defs[e.name][member.name] = member.value;
        }

        // MirEnumを作成してmir_programに追加
        auto mir_enum = std::make_unique<MirEnum>();
        mir_enum->name = e.name;
        mir_enum->is_export = e.is_export;

        for (const auto& member : e.members) {
            MirEnumMember mir_member;
            mir_member.name = member.name;
            mir_member.tag_value = member.value;
            // Associated dataフィールドをコピー
            for (const auto& [field_name, field_type] : member.fields) {
                mir_member.fields.emplace_back(field_name, field_type);
            }
            mir_enum->members.push_back(std::move(mir_member));
        }

        mir_program.enums.push_back(std::move(mir_enum));
    }

    // グローバルconst変数を登録
    void register_global_var(const hir::HirGlobalVar& gv) {
        // const変数のみ登録（文字列補間で使用）
        if (gv.is_const && gv.init) {
            // 初期化式がリテラルの場合のみ値を保存
            if (auto* lit = std::get_if<std::unique_ptr<hir::HirLiteral>>(&gv.init->kind)) {
                if (*lit) {
                    MirConstant const_val;
                    const_val.type = gv.type;
                    const_val.value = (*lit)->value;
                    global_const_values[gv.name] = const_val;
                }
            }
        }
    }

    // 構造体のフィールドインデックスを取得
    std::optional<FieldId> get_field_index(const std::string& struct_name,
                                           const std::string& field_name) {
        auto it = struct_defs.find(struct_name);
        if (it == struct_defs.end()) {
            return std::nullopt;
        }

        const auto* st = it->second;
        for (size_t i = 0; i < st->fields.size(); ++i) {
            if (st->fields[i].name == field_name) {
                return i;
            }
        }
        return std::nullopt;
    }

    // MIR構造体を生成
    MirStruct create_mir_struct(const hir::HirStruct& st) {
        MirStruct mir_struct;
        mir_struct.name = st.name;
        mir_struct.is_css = st.is_css;

        // フィールドとレイアウトを計算
        uint32_t current_offset = 0;
        uint32_t max_align = 1;

        for (const auto& field : st.fields) {
            MirStructField mir_field;
            mir_field.name = field.name;
            // フィールドの型をtypedef/enum解決
            mir_field.type = resolve_typedef(field.type);

            // 型のサイズとアライメントを取得（簡易版）
            uint32_t size = 0, align = 1;
            if (mir_field.type) {
                switch (mir_field.type->kind) {
                    case hir::TypeKind::Bool:
                    case hir::TypeKind::Tiny:
                    case hir::TypeKind::UTiny:
                    case hir::TypeKind::Char:
                        size = 1;
                        align = 1;
                        break;
                    case hir::TypeKind::Short:
                    case hir::TypeKind::UShort:
                        size = 2;
                        align = 2;
                        break;
                    case hir::TypeKind::Int:
                    case hir::TypeKind::UInt:
                    case hir::TypeKind::Float:
                        size = 4;
                        align = 4;
                        break;
                    case hir::TypeKind::Long:
                    case hir::TypeKind::ULong:
                    case hir::TypeKind::Double:
                    case hir::TypeKind::Pointer:
                        size = 8;
                        align = 8;
                        break;
                    case hir::TypeKind::String:
                        // 文字列は参照として扱う（簡易実装）
                        size = 16;  // ptr + len
                        align = 8;
                        break;
                    default:
                        // その他の型はポインタサイズと仮定
                        size = 8;
                        align = 8;
                        break;
                }
            }

            // アライメント調整
            current_offset = (current_offset + align - 1) & ~(align - 1);
            mir_field.offset = current_offset;
            current_offset += size;
            max_align = std::max(max_align, align);

            mir_struct.fields.push_back(mir_field);
        }

        // 構造体全体のサイズ（最終アライメント調整）
        mir_struct.size = (current_offset + max_align - 1) & ~(max_align - 1);
        mir_struct.align = max_align;

        return mir_struct;
    }
};

}  // namespace cm::mir
