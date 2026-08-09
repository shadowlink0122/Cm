#pragma once

// ============================================================
// レイアウト問い合わせの一元化（layout-query-unification）
// スライス格納ストライド・固定長配列の実ストライド・スライスヘッダサイズの
// 決定をここへ集約し、各loweringサイトの手書きswitch（13箇所）を廃止する。
// サイズ選択ロジックの新規手書きは禁止し、必ず本APIを経由すること
// ============================================================

#include "context.hpp"
#include "internal/base/target.hpp"
#include "internal/hir/slice_dispatch.hpp"

#include <cstdint>

namespace cm::mir::layout {

// CmSliceヘッダサイズ（data/len/cap/elem_sizeの4フィールド。ランタイムのCmSlice構造体と一致）。
// 多次元スライスの内側ヘッダは外側dataバッファへこのサイズでインライン格納される
inline int64_t slice_header_size() {
    return static_cast<int64_t>(sizeof(void*) * 4);
}

template <typename AggregateSizeFn>
int64_t array_elem_stride_of(const hir::TypePtr& resolved_elem, AggregateSizeFn&& aggregate_size);

// スライス格納内の要素ストライド（cm_slice_newへ渡すelem_size）のコア選択。
// ポインタ・文字列はランタイムのスロット規約（8バイト固定。wasm32でもget/set/pushが
// 同じelem_sizeでオフセット計算するため一貫する）、集約はblobの実サイズ、
// 内側スライスはヘッダのインライン格納サイズ
template <typename AggregateSizeFn>
int64_t slice_elem_stride_of(const hir::TypePtr& resolved_elem, AggregateSizeFn&& aggregate_size) {
    if (!resolved_elem) {
        return 4;
    }
    if (auto info = hir::slice_scalar_info(resolved_elem->kind)) {
        return info->elem_size;
    }
    switch (resolved_elem->kind) {
        case hir::TypeKind::Pointer:
        case hir::TypeKind::String:
            return 8;
        case hir::TypeKind::Struct:
        case hir::TypeKind::Union:
            return aggregate_size(resolved_elem);
        case hir::TypeKind::Array:
            // 固定長配列要素はN×要素「実ストライド」のインラインblob（Y6）。
            // スロット規約（ポインタ8固定）ではなく配列レイアウト（array_elem_stride_of: wasm32ポインタ=4）を使う。
            // blobのmemcpy・codegenのGEPと同じ実レイアウトでヘッダelem_sizeを揃えるため
            if (resolved_elem->array_size.has_value()) {
                return static_cast<int64_t>(resolved_elem->array_size.value()) *
                       array_elem_stride_of(resolved_elem->element_type, aggregate_size);
            }
            return slice_header_size();
        default:
            return 4;
    }
}

// 固定長配列の実ストライド（cm_array_to_sliceのmemcpy基準）のコア選択。
// ポインタ・文字列はターゲットのポインタ幅（wasm32=4。8固定だと2要素目以降が範囲外読みになる）
template <typename AggregateSizeFn>
int64_t array_elem_stride_of(const hir::TypePtr& resolved_elem, AggregateSizeFn&& aggregate_size) {
    if (!resolved_elem) {
        return 4;
    }
    if (auto info = hir::slice_scalar_info(resolved_elem->kind)) {
        return info->elem_size;
    }
    switch (resolved_elem->kind) {
        case hir::TypeKind::Pointer:
        case hir::TypeKind::String:
            return static_cast<int64_t>(cm::target_pointer_size());
        case hir::TypeKind::Struct:
        case hir::TypeKind::Union:
            return aggregate_size(resolved_elem);
        case hir::TypeKind::Array:
            // 固定長配列要素はN×要素ストライドのインラインblob、スライス要素はヘッダのインライン格納（Y6）
            if (resolved_elem->array_size.has_value()) {
                return static_cast<int64_t>(resolved_elem->array_size.value()) *
                       array_elem_stride_of(resolved_elem->element_type, aggregate_size);
            }
            return slice_header_size();
        default:
            return 4;
    }
}

// LoweringContext版（typedefエイリアス解決と集約サイズ計算をctxへ委譲する便宜ラッパ）
inline int64_t slice_elem_stride(LoweringContext& ctx, const hir::TypePtr& elem_type_raw) {
    auto elem = ctx.resolve_typedef(elem_type_raw ? elem_type_raw : hir::make_int());
    return slice_elem_stride_of(elem ? elem : elem_type_raw,
                                [&](const hir::TypePtr& t) { return ctx.layout_size(t); });
}

inline int64_t array_elem_stride(LoweringContext& ctx, const hir::TypePtr& elem_type_raw) {
    auto elem = ctx.resolve_typedef(elem_type_raw ? elem_type_raw : hir::make_int());
    return array_elem_stride_of(elem ? elem : elem_type_raw,
                                [&](const hir::TypePtr& t) { return ctx.layout_size(t); });
}

}  // namespace cm::mir::layout
