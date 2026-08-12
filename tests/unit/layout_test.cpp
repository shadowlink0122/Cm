// ============================================================
// レイアウト問い合わせAPI（layout-query-unification）の単体テスト
// スライス格納ストライドと固定長配列の実ストライドの2つの意味論が型kindごとに期待値どおりであること、
// およびwasm32ターゲットでポインタ系だけが分岐することを検証する
// ============================================================

#include "internal/mir/lowering/layout.hpp"

#include "internal/base/target.hpp"

#include <gtest/gtest.h>

namespace cm::mir::layout {
namespace {

// 集約サイズ計算のスタブ（Struct/Unionで呼ばれたことを固定値で確認する）
constexpr int64_t kAggregateSize = 24;

int64_t stub_aggregate(const hir::TypePtr&) {
    return kAggregateSize;
}

hir::TypePtr t(hir::TypeKind k) {
    return std::make_shared<hir::Type>(k);
}

// ネイティブ（64bit）ターゲットへ戻すRAIIガード（他テストへの汚染防止）
struct NativeTargetGuard {
    ~NativeTargetGuard() { cm::set_target_pointer_size("native"); }
};

TEST(LayoutTest, SliceHeaderSizeIsFourPointerSlots) {
    // CmSliceヘッダ（data/len/cap/elem_size）はホストポインタ幅×4
    EXPECT_EQ(slice_header_size(), static_cast<int64_t>(sizeof(void*) * 4));
}

TEST(LayoutTest, SliceElemStrideScalars) {
    // スカラはslice_dispatchの幅と一致する（アクセス関数のサフィックスと整合）
    EXPECT_EQ(slice_elem_stride_of(t(hir::TypeKind::Bool), stub_aggregate), 1);
    EXPECT_EQ(slice_elem_stride_of(t(hir::TypeKind::Char), stub_aggregate), 1);
    EXPECT_EQ(slice_elem_stride_of(t(hir::TypeKind::Tiny), stub_aggregate), 1);
    EXPECT_EQ(slice_elem_stride_of(t(hir::TypeKind::UTiny), stub_aggregate), 1);
    EXPECT_EQ(slice_elem_stride_of(t(hir::TypeKind::Short), stub_aggregate), 2);
    EXPECT_EQ(slice_elem_stride_of(t(hir::TypeKind::UShort), stub_aggregate), 2);
    EXPECT_EQ(slice_elem_stride_of(t(hir::TypeKind::Int), stub_aggregate), 4);
    EXPECT_EQ(slice_elem_stride_of(t(hir::TypeKind::UInt), stub_aggregate), 4);
    EXPECT_EQ(slice_elem_stride_of(t(hir::TypeKind::Float), stub_aggregate), 4);
    EXPECT_EQ(slice_elem_stride_of(t(hir::TypeKind::Long), stub_aggregate), 8);
    EXPECT_EQ(slice_elem_stride_of(t(hir::TypeKind::ULong), stub_aggregate), 8);
    EXPECT_EQ(slice_elem_stride_of(t(hir::TypeKind::ISize), stub_aggregate), 8);
    EXPECT_EQ(slice_elem_stride_of(t(hir::TypeKind::USize), stub_aggregate), 8);
    EXPECT_EQ(slice_elem_stride_of(t(hir::TypeKind::Double), stub_aggregate), 8);
}

TEST(LayoutTest, SliceElemStridePointerAndStringAreFixedEightEvenOnWasm32) {
    // スライス格納のポインタ・文字列スロットはランタイム規約の8バイト固定
    // （get/set/pushが同じelem_sizeでオフセット計算するためターゲット幅に依存しない）
    NativeTargetGuard guard;
    cm::set_target_pointer_size("wasm");
    EXPECT_EQ(slice_elem_stride_of(t(hir::TypeKind::Pointer), stub_aggregate), 8);
    EXPECT_EQ(slice_elem_stride_of(t(hir::TypeKind::String), stub_aggregate), 8);
}

TEST(LayoutTest, SliceElemStrideAggregatesAndInnerSlices) {
    // 構造体・ユニオンはblob実サイズ（集約サイズ計算へ委譲）、内側スライスはヘッダのインライン格納
    EXPECT_EQ(slice_elem_stride_of(t(hir::TypeKind::Struct), stub_aggregate), kAggregateSize);
    EXPECT_EQ(slice_elem_stride_of(t(hir::TypeKind::Union), stub_aggregate), kAggregateSize);
    EXPECT_EQ(slice_elem_stride_of(t(hir::TypeKind::Array), stub_aggregate), slice_header_size());
    EXPECT_EQ(slice_elem_stride_of(nullptr, stub_aggregate), 4);
}

TEST(LayoutTest, ArrayElemStrideScalarsMatchSliceStride) {
    // 固定長配列の実ストライドはスカラではスライス格納と同一
    EXPECT_EQ(array_elem_stride_of(t(hir::TypeKind::Bool), stub_aggregate), 1);
    EXPECT_EQ(array_elem_stride_of(t(hir::TypeKind::Short), stub_aggregate), 2);
    EXPECT_EQ(array_elem_stride_of(t(hir::TypeKind::Int), stub_aggregate), 4);
    EXPECT_EQ(array_elem_stride_of(t(hir::TypeKind::Float), stub_aggregate), 4);
    EXPECT_EQ(array_elem_stride_of(t(hir::TypeKind::Long), stub_aggregate), 8);
    EXPECT_EQ(array_elem_stride_of(t(hir::TypeKind::Double), stub_aggregate), 8);
    EXPECT_EQ(array_elem_stride_of(t(hir::TypeKind::Struct), stub_aggregate), kAggregateSize);
    EXPECT_EQ(array_elem_stride_of(t(hir::TypeKind::Array), stub_aggregate), slice_header_size());
    EXPECT_EQ(array_elem_stride_of(nullptr, stub_aggregate), 4);
}

TEST(LayoutTest, ArrayElemStridePointerFollowsTargetWidth) {
    // 配列の実ストライドはmemcpy基準なのでポインタ・文字列はターゲットのポインタ幅に追従する
    // （8固定だとwasm32で2要素目以降が範囲外読みになる既知バグの再導入防止）
    NativeTargetGuard guard;
    cm::set_target_pointer_size("native");
    EXPECT_EQ(array_elem_stride_of(t(hir::TypeKind::Pointer), stub_aggregate), 8);
    EXPECT_EQ(array_elem_stride_of(t(hir::TypeKind::String), stub_aggregate), 8);
    cm::set_target_pointer_size("wasm");
    EXPECT_EQ(array_elem_stride_of(t(hir::TypeKind::Pointer), stub_aggregate), 4);
    EXPECT_EQ(array_elem_stride_of(t(hir::TypeKind::String), stub_aggregate), 4);
}

}  // namespace
}  // namespace cm::mir::layout
