#pragma once

#include "../../nodes.hpp"
#include "../core/base.hpp"

#include <map>
#include <string>
#include <vector>

namespace cm::mir::opt {

// ============================================================
// 配列ベースオフセット抽出最適化
//
// 多次元配列アクセス a[i][j] を以下のように変換:
//   Before: 毎アクセスで i*stride を計算
//   After:  base = i*stride を一時変数として抽出し再利用
//
// これにより内側ループでLLVM LICMが base を外に移動可能
// ============================================================
class ArrayBaseExtraction : public OptimizationPass {
   public:
    std::string name() const override { return "ArrayBaseExtraction"; }

    bool run(MirFunction& func) override;

   private:
    // ブロック内の多次元配列アクセスを処理
    bool process_block(MirFunction& func, BasicBlock& block);

    // 多次元インデックスを持つか
    bool has_multi_dim_index(const MirPlace& place);

    // Place/Rvalue/Operandの変換
    std::vector<MirStatementPtr> transform_place(
        MirFunction& func, MirPlace& place,
        std::map<std::pair<LocalId, LocalId>, LocalId>& baseOffsetCache);
    std::vector<MirStatementPtr> transform_rvalue(
        MirFunction& func, MirRvalue& rvalue,
        std::map<std::pair<LocalId, LocalId>, LocalId>& baseOffsetCache);
    std::vector<MirStatementPtr> transform_operand(
        MirFunction& func, MirOperand& operand,
        std::map<std::pair<LocalId, LocalId>, LocalId>& baseOffsetCache);

    // 配列ストライド取得
    uint64_t get_array_stride(const hir::TypePtr& type, size_t numDimensions);
};

}  // namespace cm::mir::opt
