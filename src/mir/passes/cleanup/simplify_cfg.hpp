#pragma once

#include "../core/base.hpp"

#include <queue>
#include <unordered_set>
#include <vector>

namespace cm::mir::opt {

// ============================================================
// CFG簡約化 (Control Flow Graph Simplification)
// ============================================================
class SimplifyControlFlow : public OptimizationPass {
   public:
    std::string name() const override { return "Simplify Control Flow"; }

    bool run(MirFunction& func) override {
        bool changed = false;
        bool local_changed = true;

        // 収束するまで繰り返す（または一定回数）
        int iterations = 0;
        const int max_iterations = 100;  // 安全のため

        while (local_changed && iterations < max_iterations) {
            local_changed = false;
            iterations++;

            // CFG情報を最新にする
            func.build_cfg();

            // 1. 到達不能ブロックの削除
            local_changed |= remove_unreachable(func);
            if (local_changed) {
                changed = true;
                continue;  // 削除されたブロックがあるのでCFG再構築へ
            }

            // 2. 直線的なブロックの統合 (A -> B)
            local_changed |= merge_blocks(func);
            if (local_changed) {
                changed = true;
                continue;
            }

            // 3. 空ブロック（単一Goto）の削除とバイパス
            local_changed |= remove_empty_blocks(func);
            if (local_changed) {
                changed = true;
                continue;
            }
        }

        return changed;
    }

   private:
    bool remove_unreachable(MirFunction& func) {
        if (func.basic_blocks.empty())
            return false;

        std::vector<bool> reachable(func.basic_blocks.size(), false);
        std::queue<BlockId> worklist;

        // エントリーブロックは関数のentry_blockを使用（0とは限らない）
        BlockId entry = func.entry_block;
        if (entry < func.basic_blocks.size() && func.basic_blocks[entry]) {
            reachable[entry] = true;
            worklist.push(entry);
        }

        while (!worklist.empty()) {
            BlockId current = worklist.front();
            worklist.pop();

            if (current >= func.basic_blocks.size())
                continue;
            auto& block = func.basic_blocks[current];
            if (!block)
                continue;

            for (BlockId succ : block->successors) {
                if (succ < reachable.size() && !reachable[succ]) {
                    reachable[succ] = true;
                    worklist.push(succ);
                }
            }
        }

        bool changed = false;
        for (size_t i = 0; i < func.basic_blocks.size(); ++i) {
            if (func.basic_blocks[i] && !reachable[i]) {
                // Remove unreachable block
                func.basic_blocks[i] = nullptr;
                changed = true;
            }
        }
        return changed;
    }

    bool merge_blocks(MirFunction& func) {
        bool changed = false;

        // A -> B の結合を探す
        // AのSuccessorがBのみ、かつ、BのPredecessorがAのみ

        for (size_t i = 0; i < func.basic_blocks.size(); ++i) {
            auto& block_a = func.basic_blocks[i];
            if (!block_a)
                continue;

            // Aは無条件ジャンプ（Goto）で終わっている必要がある
            // もしくは、Fallthrough的な動作だが、MIRでは明示的Gotoが基本。
            // SwitchやCallで終わっている場合はMergeできない（Branchがあるので）。
            // ただし、AのSuccessorが1つだけなら、それはGotoのはず。
            // (Call can have unwind? If 1 succ, only success dest).
            // A should strictly end with Goto for simplicity.
            if (!block_a->terminator || block_a->terminator->kind != MirTerminator::Goto) {
                continue;
            }

            if (block_a->successors.size() != 1)
                continue;
            BlockId b_id = block_a->successors[0];

            if (b_id == i)
                continue;  // Self loop A->A
            if (b_id >= func.basic_blocks.size())
                continue;

            auto& block_b = func.basic_blocks[b_id];
            if (!block_b)
                continue;

            if (block_b->predecessors.size() != 1)
                continue;
            if (block_b->predecessors[0] != i)
                continue;  // Bの親はAのみ

            // Merge A and B
            // Move statements from B to A
            for (auto& stmt : block_b->statements) {
                block_a->statements.push_back(std::move(stmt));
            }

            // A Terminator becomes B Terminator
            block_a->terminator = std::move(block_b->terminator);

            // Mark B as removed
            func.basic_blocks[b_id] = nullptr;

            changed = true;
            // 一度変更したらCFGが壊れるので（BのSuccessorsのPrepsが更新されていないなど）、
            // 再構築が必要。すぐにreturnしてループさせる。
            // ただし、局所的に直すことも可能だが、safeにいく。
            return true;
        }

        return changed;
    }

    bool remove_empty_blocks(MirFunction& func) {
        bool changed = false;

        for (size_t i = 0; i < func.basic_blocks.size(); ++i) {
            auto& block = func.basic_blocks[i];
            if (!block)
                continue;

            // EmptyでGotoのみ
            // StatementsがすべてNopならEmptyとみなす
            bool has_code = false;
            for (const auto& s : block->statements) {
                if (s->kind != MirStatement::Nop) {
                    has_code = true;
                    break;
                }
            }
            if (has_code)
                continue;

            if (!block->terminator || block->terminator->kind != MirTerminator::Goto)
                continue;

            BlockId target = std::get<MirTerminator::GotoData>(block->terminator->data).target;
            if (target == i)
                continue;  // Check infinite loop empty block

            // Redirect predecessors to target
            // i is the block to remove.
            // preds of i are in block->predecessors

            if (block->predecessors.empty())
                continue;  // Unreachable?

            // 無限ループ防止: predecessorsの中にtargetがいたら、それはループを作るので注意？
            // A -> Empty -> A : A -> A となるだけ。OK。

            for (BlockId pred_id : block->predecessors) {
                if (pred_id >= func.basic_blocks.size() || !func.basic_blocks[pred_id])
                    continue;
                auto& pred_block = func.basic_blocks[pred_id];
                if (pred_block->terminator) {
                    redirect_jumps(*pred_block->terminator, i, target);
                }
            }

            // Mark as removed
            func.basic_blocks[i] = nullptr;
            changed = true;
            return true;  // Rebuild CFG
        }

        return changed;
    }

    void redirect_jumps(MirTerminator& term, BlockId from, BlockId to) {
        switch (term.kind) {
            case MirTerminator::Goto: {
                auto& d = std::get<MirTerminator::GotoData>(term.data);
                if (d.target == from)
                    d.target = to;
                break;
            }
            case MirTerminator::SwitchInt: {
                auto& d = std::get<MirTerminator::SwitchIntData>(term.data);
                for (auto& [val, target] : d.targets) {
                    if (target == from)
                        target = to;
                }
                if (d.otherwise == from)
                    d.otherwise = to;
                break;
            }
            case MirTerminator::Call: {
                auto& d = std::get<MirTerminator::CallData>(term.data);
                if (d.success == from)
                    d.success = to;
                if (d.unwind && *d.unwind == from)
                    d.unwind = to;
                break;
            }
            default:
                break;
        }
    }
};

}  // namespace cm::mir::opt
