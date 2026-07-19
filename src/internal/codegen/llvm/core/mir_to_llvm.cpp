/// @file mir_to_llvm.cpp
/// @brief MIR → LLVM IR 変換器の共通ヘルパー（typedef解決・小型構造体判定）
/// プログラム/関数変換の本体はtranslate/配下に分離

#include "mir_to_llvm.hpp"

#include "internal/base/debug/codegen.hpp"
#include "internal/codegen/llvm/monitoring/compilation_guard.hpp"

#include <iostream>
#include <llvm/IR/InlineAsm.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cm::codegen::llvm_backend {

// TypeAlias（typedef）を基底型に再帰的に解決するヘルパー
// MemAddr → ulong, FsError → ulong のように typedef チェーンを展開する
hir::TypePtr MIRToLLVM::resolveTypeAlias(const hir::TypePtr& type) const {
    if (!type)
        return nullptr;
    auto current = type;
    // TypeAlias チェーンを辿って基底型まで解決
    while (current && current->kind == hir::TypeKind::TypeAlias) {
        if (current->element_type) {
            current = current->element_type;
        } else {
            // element_type が未設定の場合、typedefDefsで名前ベースの解決を試行
            auto it = typedefDefs.find(current->name);
            if (it != typedefDefs.end()) {
                current = it->second;
            } else {
                break;
            }
        }
    }
    // Struct kindだがtypedefの場合も解決（MIRでStruct名としてtypedef名が残る場合）
    if (current && current->kind == hir::TypeKind::Struct) {
        auto it = typedefDefs.find(current->name);
        if (it != typedefDefs.end()) {
            return resolveTypeAlias(it->second);
        }
    }
    return current;
}

// 構造体がABI上「小さい」かどうかをチェック（値渡し可能かどうか）
// System V ABI: 16バイト以下の構造体はレジスタで値渡し
bool MIRToLLVM::isSmallStruct(const hir::TypePtr& type) const {
    if (!type || type->kind != hir::TypeKind::Struct) {
        return false;
    }

    // 構造体定義を取得
    auto it = structDefs.find(type->name);
    if (it == structDefs.end()) {
        return false;  // 定義が見つからない場合は安全のためポインタ渡し
    }

    const mir::MirStruct* structDef = it->second;

    // フィールドのサイズを合計
    size_t totalSize = 0;
    for (const auto& field : structDef->fields) {
        if (!field.type)
            continue;

        // TypeAlias（typedef）を基底型に解決
        auto resolvedFieldType = resolveTypeAlias(field.type);
        switch (resolvedFieldType->kind) {
            case hir::TypeKind::Bool:
            case hir::TypeKind::Char:
            case hir::TypeKind::Tiny:
            case hir::TypeKind::UTiny:
                totalSize += 1;
                break;
            case hir::TypeKind::Short:
            case hir::TypeKind::UShort:
                totalSize += 2;
                break;
            case hir::TypeKind::Int:
            case hir::TypeKind::UInt:
            case hir::TypeKind::Float:
                totalSize += 4;
                break;
            case hir::TypeKind::Long:
            case hir::TypeKind::ULong:
            case hir::TypeKind::Double:
            case hir::TypeKind::Pointer:
            case hir::TypeKind::String:
                totalSize += 8;
                break;
            case hir::TypeKind::Struct:
                // ネストした構造体は安全のためポインタ渡し
                return false;
            default:
                totalSize += 8;  // デフォルトはポインタサイズ
                break;
        }

        // 16バイトを超えたら即座にfalse
        if (totalSize > 16) {
            return false;
        }
    }

    return totalSize <= 16;
}

}  // namespace cm::codegen::llvm_backend
