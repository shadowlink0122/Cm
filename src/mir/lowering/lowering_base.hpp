#pragma once

#include "../../hir/hir_nodes.hpp"
#include "../mir_nodes.hpp"
#include "lowering_context.hpp"

#include <unordered_map>
#include <unordered_set>

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

    // インターフェース名のセット
    std::unordered_set<std::string> interface_names;

    // typedef定義 (エイリアス名 -> 実際の型)
    std::unordered_map<std::string, hir::TypePtr> typedef_defs;

    // enum定義 (enum名 -> (メンバー名 -> 値))
    std::unordered_map<std::string, std::unordered_map<std::string, int64_t>> enum_defs;

    // デストラクタを持つ型のセット
    std::unordered_set<std::string> types_with_destructor;

   public:
    MirLoweringBase() = default;
    virtual ~MirLoweringBase() = default;

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

        return type;
    }

    // enum定義を登録
    void register_enum(const hir::HirEnum& e) {
        for (const auto& member : e.members) {
            enum_defs[e.name][member.name] = member.value;
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