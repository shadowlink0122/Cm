#include "simplify_cfg.hpp"

namespace cm::mir::opt {

bool SimplifyControlFlow::run(MirFunction& func) {
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

bool SimplifyControlFlow::remove_unreachable(MirFunction& func) {
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
            func.basic_blocks[i] = nullptr;
            changed = true;
        }
    }
    return changed;
}

bool SimplifyControlFlow::merge_blocks(MirFunction& func) {
    bool changed = false;

    // A -> B の結合を探す
    // AのSuccessorがBのみ、かつ、BのPredecessorがAのみ
    for (size_t i = 0; i < func.basic_blocks.size(); ++i) {
        auto& block_a = func.basic_blocks[i];
        if (!block_a)
            continue;

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
        for (auto& stmt : block_b->statements) {
            block_a->statements.push_back(std::move(stmt));
        }

        block_a->terminator = std::move(block_b->terminator);
        func.basic_blocks[b_id] = nullptr;

        changed = true;
        return true;
    }

    return changed;
}

bool SimplifyControlFlow::remove_empty_blocks(MirFunction& func) {
    bool changed = false;

    for (size_t i = 0; i < func.basic_blocks.size(); ++i) {
        auto& block = func.basic_blocks[i];
        if (!block)
            continue;

        // EmptyでGotoのみ
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

        if (block->predecessors.empty())
            continue;

        for (BlockId pred_id : block->predecessors) {
            if (pred_id >= func.basic_blocks.size() || !func.basic_blocks[pred_id])
                continue;
            auto& pred_block = func.basic_blocks[pred_id];
            if (pred_block->terminator) {
                redirect_jumps(*pred_block->terminator, i, target);
            }
        }

        func.basic_blocks[i] = nullptr;
        changed = true;
        return true;  // Rebuild CFG
    }

    return changed;
}

void SimplifyControlFlow::redirect_jumps(MirTerminator& term, BlockId from, BlockId to) {
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

}  // namespace cm::mir::opt
