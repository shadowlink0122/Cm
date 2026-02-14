#include "loop_analysis.hpp"

#include <queue>

namespace cm::mir {

LoopAnalysis::LoopAnalysis(const MirFunction& f, const DominatorTree& d) : func(f), dom_tree(d) {
    compute();
}

Loop* LoopAnalysis::get_inner_most_loop(BlockId b) const {
    auto it = block_to_loop.find(b);
    if (it != block_to_loop.end()) {
        return it->second;
    }
    return nullptr;
}

void LoopAnalysis::compute() {
    if (func.basic_blocks.empty())
        return;

    // バックエッジを検出してループを特定
    for (size_t i = 0; i < func.basic_blocks.size(); ++i) {
        if (!func.basic_blocks[i])
            continue;
        const auto& bb = *func.basic_blocks[i];

        // Successorsを取得
        std::vector<BlockId> succs;
        if (bb.terminator) {
            switch (bb.terminator->kind) {
                case MirTerminator::Goto:
                    succs.push_back(std::get<MirTerminator::GotoData>(bb.terminator->data).target);
                    break;
                case MirTerminator::SwitchInt: {
                    auto& data = std::get<MirTerminator::SwitchIntData>(bb.terminator->data);
                    succs.push_back(data.otherwise);
                    for (auto& pair : data.targets)
                        succs.push_back(pair.second);
                    break;
                }
                case MirTerminator::Call: {
                    auto& data = std::get<MirTerminator::CallData>(bb.terminator->data);
                    if (data.success != INVALID_BLOCK)
                        succs.push_back(data.success);
                    break;
                }
                default:
                    break;
            }
        }

        for (BlockId h : succs) {
            if (dom_tree.dominates(h, i)) {
                // Back-edge detected: i -> h
                Loop* loop = nullptr;
                for (auto& l : loops) {
                    if (l->header == h) {
                        loop = l.get();
                        break;
                    }
                }

                if (!loop) {
                    loops.push_back(std::make_unique<Loop>());
                    loop = loops.back().get();
                    loop->header = h;
                    loop->blocks.insert(h);
                }

                loop->back_edges.push_back(i);
                populate_loop_body(loop, i);
            }
        }
    }

    // Loop Nesting Treeの構築
    build_nesting_tree();
}

void LoopAnalysis::populate_loop_body(Loop* loop, BlockId back_edge_node) {
    if (loop->blocks.count(back_edge_node))
        return;

    loop->blocks.insert(back_edge_node);

    std::queue<BlockId> q;
    q.push(back_edge_node);

    while (!q.empty()) {
        BlockId m = q.front();
        q.pop();

        for (size_t p = 0; p < func.basic_blocks.size(); ++p) {
            if (!func.basic_blocks[p])
                continue;
            const auto& p_bb = *func.basic_blocks[p];
            if (!p_bb.terminator)
                continue;

            if (loop->blocks.count(p))
                continue;

            bool is_pred = false;
            switch (p_bb.terminator->kind) {
                case MirTerminator::Goto:
                    if (std::get<MirTerminator::GotoData>(p_bb.terminator->data).target == m)
                        is_pred = true;
                    break;
                case MirTerminator::SwitchInt: {
                    auto& data = std::get<MirTerminator::SwitchIntData>(p_bb.terminator->data);
                    if (data.otherwise == m)
                        is_pred = true;
                    for (auto& pair : data.targets)
                        if (pair.second == m)
                            is_pred = true;
                    break;
                }
                case MirTerminator::Call: {
                    auto& data = std::get<MirTerminator::CallData>(p_bb.terminator->data);
                    if (data.success == m)
                        is_pred = true;
                    break;
                }
                default:
                    break;
            }

            if (is_pred) {
                loop->blocks.insert(p);
                q.push(p);
            }
        }
    }
}

void LoopAnalysis::build_nesting_tree() {
    // 親ループを設定
    for (auto& inner : loops) {
        Loop* parent = nullptr;
        for (auto& outer : loops) {
            if (inner.get() == outer.get())
                continue;

            if (outer->contains(inner.get())) {
                if (parent == nullptr || parent->contains(outer.get())) {
                    parent = outer.get();
                }
            }
        }

        if (parent) {
            inner->parent_loop = parent;
            parent->sub_loops.push_back(inner.get());
        } else {
            top_level_loops.push_back(inner.get());
        }
    }

    // 各ブロックの最内ループを設定
    for (auto& loop : loops) {
        for (BlockId b : loop->blocks) {
            Loop* current = block_to_loop[b];
            if (!current) {
                block_to_loop[b] = loop.get();
            } else {
                if (current->contains(loop.get())) {
                    block_to_loop[b] = loop.get();
                }
            }
        }
    }
}

}  // namespace cm::mir
