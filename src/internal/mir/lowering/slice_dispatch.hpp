// ============================================================
// スライス要素型ディスパッチの一元化
// ============================================================
// スカラ要素型（整数・浮動小数）について、スライスのelem_size（確保幅）と
// ランタイム関数の幅サフィックス（push/pop/get）・ソート関数サフィックスを
// 唯一の情報源として定義する。
// 以前はこの対応表がexpr_slice.cpp・access.cpp・construct.cpp・let.cpp・
// expr_println.cppに個別複製されており、Short/UShortの取りこぼしでelem_size=2に対し
// 4バイト関数が選ばれヒープ破壊（C4）を招いた。ここへ集約することでelem_sizeと
// アクセス幅の不一致を構造的に防ぎ、新しいスカラ型の追加を1箇所の変更で済ませる。
// ポインタ・文字列・構造体・ユニオン・配列などの集約型はスカラではないためnulloptを返し、
// 各呼び出し側が従来通り個別に扱う（集約の格納表現はサイトごとに異なるため一元化しない）。

#pragma once

#include "internal/hir/types.hpp"

#include <cstdint>
#include <optional>

namespace cm::mir {

struct SliceScalarInfo {
    int64_t elem_size;  // 要素のバイト幅
    const char* width;  // ランタイム関数の幅サフィックス: "i8"/"i16"/"i32"/"i64"/"f32"/"f64"
};

// スカラ要素型ならelem_sizeと幅サフィックスを返す。集約型はnullopt。
inline std::optional<SliceScalarInfo> slice_scalar_info(hir::TypeKind kind) {
    switch (kind) {
        case hir::TypeKind::Bool:
        case hir::TypeKind::Char:
        case hir::TypeKind::Tiny:
        case hir::TypeKind::UTiny:
            return SliceScalarInfo{1, "i8"};
        case hir::TypeKind::Short:
        case hir::TypeKind::UShort:
            return SliceScalarInfo{2, "i16"};
        case hir::TypeKind::Int:
        case hir::TypeKind::UInt:
            return SliceScalarInfo{4, "i32"};
        case hir::TypeKind::Long:
        case hir::TypeKind::ULong:
        case hir::TypeKind::ISize:
        case hir::TypeKind::USize:
            return SliceScalarInfo{8, "i64"};
        case hir::TypeKind::Float:
        case hir::TypeKind::UFloat:
            return SliceScalarInfo{4, "f32"};
        case hir::TypeKind::Double:
        case hir::TypeKind::UDouble:
            return SliceScalarInfo{8, "f64"};
        default:
            return std::nullopt;
    }
}

// スカラ要素型のソート関数サフィックス（符号・浮動小数を区別）。集約はnullptr。
inline const char* slice_scalar_sort_suffix(hir::TypeKind kind) {
    switch (kind) {
        case hir::TypeKind::Bool:
        case hir::TypeKind::Char:
        case hir::TypeKind::UTiny:
            return "u8";
        case hir::TypeKind::Tiny:
            return "i8";
        case hir::TypeKind::Short:
            return "i16";
        case hir::TypeKind::UShort:
            return "u16";
        case hir::TypeKind::Int:
            return "i32";
        case hir::TypeKind::UInt:
            return "u32";
        case hir::TypeKind::Long:
        case hir::TypeKind::ISize:
            return "i64";
        case hir::TypeKind::ULong:
        case hir::TypeKind::USize:
            return "u64";
        case hir::TypeKind::Float:
        case hir::TypeKind::UFloat:
            return "f32";
        case hir::TypeKind::Double:
        case hir::TypeKind::UDouble:
            return "f64";
        default:
            return nullptr;
    }
}

}  // namespace cm::mir
