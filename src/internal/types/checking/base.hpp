#pragma once

// ============================================================
// TypeChecker 基底定義
// 共通の型定義、メンバ変数、ユーティリティ関数
// ============================================================

#include "internal/base/debug/tc.hpp"
#include "internal/syntax/ast/decl.hpp"
#include "internal/syntax/ast/module.hpp"
#include "internal/syntax/parser/parser.hpp"
#include "internal/types/generic_context.hpp"
#include "internal/types/scope.hpp"

#include <cstdint>
#include <functional>
#include <regex>
#include <set>
#include <string>
#include <vector>

namespace cm {

// メソッド情報
struct MethodInfo {
    std::string name;
    std::vector<ast::TypePtr> param_types;
    ast::TypePtr return_type;
    ast::Visibility visibility = ast::Visibility::Export;  // デフォルトは公開
    bool is_static = false;                                // 静的メソッドかどうか
    // 必須引数の数（デフォルト値のない引数）。SIZE_MAXは全引数必須
    size_t required_params = SIZE_MAX;
};

// TypeCheckerの前方宣言
class TypeChecker;

}  // namespace cm
