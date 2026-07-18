#pragma once

// ============================================================
// HIR式lowering内部ヘルパー（expr / expr_member / expr_match で共有）
// ============================================================

#include "fwd.hpp"

namespace cm::hir {

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
