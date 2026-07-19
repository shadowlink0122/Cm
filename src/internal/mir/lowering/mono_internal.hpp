#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// ============================================================
// 単相化内部ヘルパー（monomorphization_impl / mono_structs で共有）
// ============================================================

#include "monomorphization.hpp"
#include "monomorphization_utils.hpp"

namespace cm::mir {

// 型内の型パラメータを再帰的に置換するヘルパー関数（実装は mono_internal.cpp）
hir::TypePtr substitute_type_in_type(
    const hir::TypePtr& type, const std::unordered_map<std::string, hir::TypePtr>& type_subst,
    Monomorphization* mono);

}  // namespace cm::mir
