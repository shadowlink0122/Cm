#pragma once

#include "../frontend/ast/types.hpp"

#include <memory>
#include <string>
#include <vector>

namespace cm::hir {

// ============================================================
// HIR型（AST型を再利用 + HIR固有の情報）
// ============================================================
using Type = ast::Type;
using TypePtr = ast::TypePtr;
using TypeKind = ast::TypeKind;

// 型ヘルパーの再エクスポート
using ast::make_array;
using ast::make_bool;
using ast::make_char;
using ast::make_double;
using ast::make_error;
using ast::make_float;
using ast::make_function_ptr;
using ast::make_int;
using ast::make_long;
using ast::make_named;
using ast::make_null;
using ast::make_pointer;
using ast::make_reference;
using ast::make_short;
using ast::make_string;
using ast::make_tiny;
using ast::make_udouble;
using ast::make_ufloat;
using ast::make_uint;
using ast::make_ulong;
using ast::make_ushort;
using ast::make_utiny;
using ast::make_void;
using ast::type_to_string;

}  // namespace cm::hir
