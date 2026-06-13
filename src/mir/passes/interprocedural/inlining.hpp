#pragma once

#include "../../nodes.hpp"
#include "../core/base.hpp"

#include <algorithm>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace cm::mir::opt {

// ============================================================
// 関数インライン化
// ============================================================
class FunctionInlining : public OptimizationPass {
   public:
    std::string name() const override { return "Function Inlining"; }

    bool run(MirFunction& /*func*/) override { return false; }

    bool run_on_program(MirProgram& program) override;

   private:
    const size_t INLINE_THRESHOLD = 10;        // より小さい関数のみインライン化
    const size_t MAX_INLINE_PER_FUNCTION = 2;  // 同じ関数の最大インライン化回数を削減
    const size_t MAX_TOTAL_INLINES = 20;  // プログラム全体でのインライン化回数制限
    std::unordered_map<std::string, size_t> inline_counts;
    bool max_inlines_reached = false;

    bool process_function(MirFunction& caller,
                          const std::unordered_map<std::string, const MirFunction*>& func_map);
    bool process_block(MirFunction& caller, BlockId block_id,
                       const std::unordered_map<std::string, const MirFunction*>& func_map);
    bool should_inline(const MirFunction& callee);

    void perform_inlining(MirFunction& caller, BlockId call_block_id, const MirFunction& callee,
                          const MirTerminator::CallData& call_data);

    // クローン関数
    MirStatementPtr clone_statement(const MirStatement& src);
    MirTerminatorPtr clone_terminator(const MirTerminator& src);
    MirOperandPtr clone_operand(const MirOperand& src);
    MirPlace clone_place(const MirPlace& src);
    MirRvaluePtr clone_rvalue(const MirRvalue& src);

    // リマップ関数
    void remap_block(BasicBlock& block, LocalId local_offset, const std::vector<BlockId>& block_map,
                     const MirTerminator::CallData& call_data);
    void remap_statement(MirStatement& stmt, LocalId offset);
    void remap_terminator(MirTerminator& term, LocalId lo, const std::vector<BlockId>& bm);
    void remap_place(MirPlace& p, LocalId offset);
    void remap_operand(MirOperand& op, LocalId offset);
    void remap_rvalue(MirRvalue& rv, LocalId offset);
};

}  // namespace cm::mir::opt
