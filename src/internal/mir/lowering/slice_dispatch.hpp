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

// 要素の格納クラス（type-resolution-simplification 領域4）。
// push/pop/get/set系ランタイム関数の選択と呼び出し規約（値渡し/アドレス渡し・値戻し/要素ポインタ戻し）を各ビルトインloweringが個別に持たず、この分類から一律に導出する。
enum class SliceElemClass {
    Scalar,  // 整数・浮動小数: cm_slice_*_<width> を値渡し・値戻しで呼ぶ
    Ptr,     // ポインタ・文字列: cm_slice_*_ptr を値渡し・値戻しで呼ぶ
    InnerSlice,  // 内側スライス（多次元）: cm_slice_*_slice。ヘッダはdataへインライン格納
    Blob,  // 構造体・ユニオン: cm_slice_*_blob。アドレス渡し・要素ポインタ経由の受け取り
};

struct SliceElemDispatch {
    SliceElemClass cls;
    const char* suffix;  // ランタイム関数サフィックス: width/"ptr"/"slice"/"blob"
};

// 要素型kindから格納クラスとサフィックスを引く。
// 未知のkind（enum等の整数表現）は従来既定のScalar/i32に落とす
inline SliceElemDispatch slice_elem_dispatch(hir::TypeKind kind) {
    if (auto info = slice_scalar_info(kind)) {
        return {SliceElemClass::Scalar, info->width};
    }
    switch (kind) {
        case hir::TypeKind::Array:
            return {SliceElemClass::InnerSlice, "slice"};
        case hir::TypeKind::Struct:
        case hir::TypeKind::Union:
            return {SliceElemClass::Blob, "blob"};
        case hir::TypeKind::Pointer:
        case hir::TypeKind::String:
            return {SliceElemClass::Ptr, "ptr"};
        default:
            return {SliceElemClass::Scalar, "i32"};
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
