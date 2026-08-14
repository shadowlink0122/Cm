// ============================================================
// SVコード生成: 型写像（Cm型 → SV型文字列・ビット幅・配列サフィックス）
// ============================================================
#include "internal/codegen/sv/codegen.hpp"
#include "internal/codegen/sv/internal.hpp"

#include <string>

namespace cm::codegen::sv {

// === 型マッピング ===

std::string SVCodeGen::mapType(const hir::TypePtr& type) const {
    if (!type)
        return "logic";

    switch (type->kind) {
        case hir::TypeKind::Bool:
            return "logic";
        case hir::TypeKind::Tiny:
            return "logic signed [7:0]";
        case hir::TypeKind::UTiny:
            return "logic [7:0]";
        case hir::TypeKind::Short:
            return "logic signed [15:0]";
        case hir::TypeKind::UShort:
            return "logic [15:0]";
        case hir::TypeKind::Int:
            return "logic signed [31:0]";
        case hir::TypeKind::UInt:
            return "logic [31:0]";
        case hir::TypeKind::Long:
            return "logic signed [63:0]";
        case hir::TypeKind::ULong:
            return "logic [63:0]";
        case hir::TypeKind::ISize:
            return "logic signed [63:0]";
        case hir::TypeKind::USize:
            return "logic [63:0]";
        // SV固有型
        case hir::TypeKind::Posedge:
        case hir::TypeKind::Negedge:
            return "logic";  // クロック/リセット信号は1bit
        case hir::TypeKind::Wire:
        case hir::TypeKind::Reg:
            // element_typeがあればそれを使用
            if (type->element_type)
                return mapType(type->element_type);
            return "logic [31:0]";
        case hir::TypeKind::Bit:
            return "logic";  // bit単体は1bit、bit[N]はArray処理で幅変換
        case hir::TypeKind::Array:
            // bit[N] → logic [N-1:0] に変換
            if (type->element_type && type->element_type->kind == hir::TypeKind::Bit) {
                // #[sv::parameter] によるサイズ指定は記号のまま出力（bit[WIDTH]）
                if (!type->size_param_name.empty() &&
                    sv_param_names_.count(type->size_param_name) > 0) {
                    return "logic [" + type->size_param_name + "-1:0]";
                }
                if (type->array_size && *type->array_size > 1) {
                    return "logic [" + std::to_string(*type->array_size - 1) + ":0]";
                }
                return "logic";
            }
            // 通常の配列: element_type name [0:N-1] → element_typeだけ返す
            if (type->element_type) {
                return mapType(type->element_type);
            }
            return "logic [31:0]";
        case hir::TypeKind::Struct:
            // ネスト型・名前空間型のOuter::Inner名は、typedef宣言側と同じ規約で::を__へ写像した一意名として出力する
            return sv_type_name(type->name);
        case hir::TypeKind::String:
            return "logic [23:0]";
        default:
            return "logic [31:0]";  // デフォルトは32bit
    }
}

int SVCodeGen::getBitWidth(const hir::TypePtr& type) const {
    if (!type)
        return 32;

    switch (type->kind) {
        case hir::TypeKind::Bool:
            return 1;
        case hir::TypeKind::Tiny:
        case hir::TypeKind::UTiny:
            return 8;
        case hir::TypeKind::Short:
        case hir::TypeKind::UShort:
            return 16;
        case hir::TypeKind::Int:
        case hir::TypeKind::UInt:
            return 32;
        case hir::TypeKind::Long:
        case hir::TypeKind::ULong:
        case hir::TypeKind::ISize:
        case hir::TypeKind::USize:
            return 64;
        // SV固有型
        case hir::TypeKind::Posedge:
        case hir::TypeKind::Negedge:
            return 1;  // クロック/リセット信号は1bit
        case hir::TypeKind::Wire:
        case hir::TypeKind::Reg:
            if (type->element_type)
                return getBitWidth(type->element_type);
            return 32;
        case hir::TypeKind::Bit:
            return 1;  // bit単体は1bit
        case hir::TypeKind::Array:
            // bit[N] → Nビット
            if (type->element_type && type->element_type->kind == hir::TypeKind::Bit) {
                return type->array_size.value_or(1);
            }
            if (type->element_type)
                return getBitWidth(type->element_type);
            return 32;
        case hir::TypeKind::String:
            return 24;
        default:
            return 32;
    }
}

// === 配列サフィックス生成 ===

std::string SVCodeGen::getArraySuffix(const hir::TypePtr& type) const {
    if (!type)
        return "";
    // 通常の配列型（非bit配列）の場合、アンパックドディメンションを生成
    // （#[sv::parameter]の記号深度はarray_sizeが無くてもsize_param_nameで出力する）
    if (type->kind == hir::TypeKind::Array &&
        ((type->array_size && *type->array_size > 0) ||
         (!type->size_param_name.empty() && sv_param_names_.count(type->size_param_name) > 0))) {
        // bit[N] は packed dimension として mapType で処理済みなのでスキップ
        if (type->element_type && type->element_type->kind == hir::TypeKind::Bit) {
            return "";
        }
        // #[sv::parameter] によるサイズ指定は記号のまま出力
        if (!type->size_param_name.empty() && sv_param_names_.count(type->size_param_name) > 0) {
            return " [0:" + type->size_param_name + "-1]" + getArraySuffix(type->element_type);
        }
        return " [0:" + std::to_string(*type->array_size - 1) + "]" +
               getArraySuffix(type->element_type);
    }
    return "";
}

}  // namespace cm::codegen::sv
