#pragma once

// ============================================================
// TypeChecker 基底定義
// 共通の型定義、メンバ変数、ユーティリティ関数
// ============================================================

#include "../../../common/debug/tc.hpp"
#include "../../ast/decl.hpp"
#include "../../ast/module.hpp"
#include "../../parser/parser.hpp"
#include "../generic_context.hpp"
#include "../scope.hpp"

#include <functional>
#include <regex>
#include <set>

namespace cm {

// メソッド情報
struct MethodInfo {
    std::string name;
    std::vector<ast::TypePtr> param_types;
    ast::TypePtr return_type;
    ast::Visibility visibility = ast::Visibility::Export;  // デフォルトは公開
    bool is_static = false;                                // 静的メソッドかどうか
};

// TypeCheckerの前方宣言
class TypeChecker;

}  // namespace cm
