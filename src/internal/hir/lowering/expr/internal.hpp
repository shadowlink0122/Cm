#pragma once

#include <memory>
#include <optional>
#include <utility>
#include <variant>

// ============================================================
// HIR式lowering内部ヘルパー（expr/ 配下の各TUで共有）
// ============================================================

#include "internal/hir/lowering/fwd.hpp"

namespace cm::hir {

// 整数リテラル値の取り出し（checkerで検証済みの前提）
inline std::optional<int64_t> slice_lit(const ast::ExprPtr& e) {
    if (!e) {
        return std::nullopt;
    }
    if (auto* lit = e->as<ast::LiteralExpr>()) {
        if (auto* iv = std::get_if<int64_t>(&lit->value)) {
            return *iv;
        }
    }
    return std::nullopt;
}

inline  // ビットスライス判定: bit[N] / bit / 整数型
    bool
    is_bits_type(const ast::TypePtr& t) {
    if (!t) {
        return false;
    }
    if (t->kind == ast::TypeKind::Array && t->element_type &&
        t->element_type->kind == ast::TypeKind::Bit) {
        return true;
    }
    return t->is_integer() || t->kind == ast::TypeKind::Bit;
}

inline  // int64リテラルのHIR式を作る
    HirExprPtr
    make_int_lit(int64_t v, ast::TypePtr t) {
    auto lit = std::make_unique<HirLiteral>();
    lit->value = v;
    return std::make_unique<HirExpr>(std::move(lit), std::move(t));
}

}  // namespace cm::hir
