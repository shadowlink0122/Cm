#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// ============================================================
// 単相化内部ヘルパー（mono/ 配下の各TUで共有）
// ============================================================

#include "internal/mir/lowering/mono/monomorphization.hpp"
#include "internal/mir/lowering/mono/utils.hpp"

namespace cm::mir {

// 型内の型パラメータを再帰的に置換するヘルパー関数（実装は internal.cpp）
hir::TypePtr substitute_type_in_type(
    const hir::TypePtr& type, const std::unordered_map<std::string, hir::TypePtr>& type_subst,
    Monomorphization* mono);

}  // namespace cm::mir
