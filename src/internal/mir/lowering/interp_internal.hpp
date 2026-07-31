#pragma once

// ============================================================
// 文字列補間lowering内部ヘルパー（expr_interpで使用）
// ============================================================
// 補間プレースホルダの解決は式パイプライン（resolve_interp_placeholder）へ一本化したため、旧テキストパターン分類器（interp_content_is_*）と旧呼び出し引数ヘルパーは撤去した（type-resolution-simplification 領域1第4段）

#include "expr.hpp"
#include "internal/hir/lowering/fwd.hpp"

#include <functional>
#include <string>

namespace cm::mir {

// ジェネリック構造体の特殊化名（Box<int> → Box__int）を構成する。
// 補間内メソッド呼び出しの関数名解決で、型引数を落とすと
// 未定義シンボル参照（Box__get）になるため必ず型引数を反映する
inline std::string interp_specialized_struct_name(const hir::TypePtr& t) {
    if (!t) {
        return "";
    }
    std::function<std::string(const hir::TypePtr&)> piece =
        [&](const hir::TypePtr& p) -> std::string {
        if (!p) {
            return "unknown";
        }
        switch (p->kind) {
            case hir::TypeKind::Int:
                return "int";
            case hir::TypeKind::UInt:
                return "uint";
            case hir::TypeKind::Long:
                return "long";
            case hir::TypeKind::ULong:
                return "ulong";
            case hir::TypeKind::Short:
                return "short";
            case hir::TypeKind::UShort:
                return "ushort";
            case hir::TypeKind::Tiny:
                return "tiny";
            case hir::TypeKind::UTiny:
                return "utiny";
            case hir::TypeKind::Float:
                return "float";
            case hir::TypeKind::Double:
                return "double";
            case hir::TypeKind::Bool:
                return "bool";
            case hir::TypeKind::Char:
                return "char";
            case hir::TypeKind::String:
                return "string";
            case hir::TypeKind::Pointer:
                return "ptr_" + piece(p->element_type);
            default:
                return p->name.empty() ? "unknown" : p->name;
        }
    };
    std::string name = t->name;
    for (const auto& ta : t->type_args) {
        name += "__" + piece(ta);
    }
    return name;
}

}  // namespace cm::mir
