#pragma once

// 定数畳み込み共通ユーティリティ
//
// MIRの整数定数は int64_t で保持されるが、Cmの整数型は 8/16/32/64bit の幅と符号を持つ。畳み込み結果を型の幅に正規化（ラップ）しないと、「int w = INT_MAX + 1 が 2147483648 のまま伝播し、表示値(-2147483648)と比較結果(w < 0 が false)が食い違う」等の不整合が起きる。
// SCCP と ConstantFolding の両方から使用する。

#include "../../nodes.hpp"

#include <cstdint>

namespace cm::mir::const_eval {

// 整数型のビット幅を返す（整数型以外は0）
inline int integer_bit_width(const hir::TypePtr& type) {
    if (!type) {
        return 0;
    }
    switch (type->kind) {
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
        default:
            return 0;
    }
}

// 符号なし整数型か
inline bool is_unsigned_type(const hir::TypePtr& type) {
    if (!type) {
        return false;
    }
    switch (type->kind) {
        case hir::TypeKind::UTiny:
        case hir::TypeKind::UShort:
        case hir::TypeKind::UInt:
        case hir::TypeKind::ULong:
        case hir::TypeKind::USize:
            return true;
        default:
            return false;
    }
}

// int64値を型の幅・符号に正規化する（オーバーフローは2の補数でラップ）。
// 符号付き型は幅内の値へ符号拡張、符号なし型は幅でマスクする
inline int64_t normalize_int(int64_t value, const hir::TypePtr& type) {
    int width = integer_bit_width(type);
    if (width == 0 || width >= 64) {
        return value;
    }
    uint64_t mask = (1ULL << width) - 1;
    uint64_t v = static_cast<uint64_t>(value) & mask;
    if (!is_unsigned_type(type)) {
        uint64_t sign_bit = 1ULL << (width - 1);
        if (v & sign_bit) {
            v |= ~mask;
        }
    }
    return static_cast<int64_t>(v);
}

// 比較・除算・剰余・右シフトを符号なしで実行すべきか（どちらかのオペランドが符号なし型なら符号なし演算）
inline bool use_unsigned_op(const hir::TypePtr& lhs_type, const hir::TypePtr& rhs_type) {
    return is_unsigned_type(lhs_type) || is_unsigned_type(rhs_type);
}

// 二項演算の結果型を求める（幅の広い方へ昇格。同幅なら符号なし優先）。
// MIRのBinaryOpDataにresult_typeがある場合はそちらを優先すること
inline hir::TypePtr promote_types(const hir::TypePtr& a, const hir::TypePtr& b) {
    int wa = integer_bit_width(a);
    int wb = integer_bit_width(b);
    if (wa == 0) {
        return b;
    }
    if (wb == 0) {
        return a;
    }
    if (wa > wb) {
        return a;
    }
    if (wb > wa) {
        return b;
    }
    return is_unsigned_type(a) ? a : b;
}

}  // namespace cm::mir::const_eval
