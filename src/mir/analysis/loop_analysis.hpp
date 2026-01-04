#pragma once

#include "mir/analysis/dominators.hpp"
#include "mir/nodes.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <vector>

namespace cm::mir {

struct Loop {
    BlockId header;
    std::set<BlockId> blocks;
    std::vector<BlockId> back_edges;  // headerへのバックエッジを持つブロック

    std::vector<Loop*> sub_loops;
    Loop* parent_loop = nullptr;

    bool contains(BlockId b) const { return blocks.count(b) > 0; }

    // 他のループを包含するか
    bool contains(const Loop* other) const {
        // ヘッダーが自分の中にあり、かつ自分が相手のヘッダーでない（厳密な包含でなくとも、親子関係はある）
        // Nesting logic: Loop A contains Loop B if Header(B) is in Loop A and Header(A) !=
        // Header(B)
        return contains(other->header) && header != other->header;
    }
};

class LoopAnalysis {
   public:
    LoopAnalysis(const MirFunction& f, const DominatorTree& d) : func(f), dom_tree(d) { compute(); }

    const std::vector<std::unique_ptr<Loop>>& get_loops() const { return loops; }

    // ネストの最上位ループを取得
    const std::vector<Loop*>& get_top_level_loops() const { return top_level_loops; }

    // ブロックが属する最も内側のループを取得
    Loop* get_inner_most_loop(BlockId b) const {
        auto it = block_to_loop.find(b);
        if (it != block_to_loop.end()) {
            return it->second;
        }
        return nullptr;
    }

   private:
    const MirFunction& func;
    const DominatorTree& dom_tree;
    std::vector<std::unique_ptr<Loop>> loops;
    std::vector<Loop*> top_level_loops;
    std::map<BlockId, Loop*> block_to_loop;  // 各ブロックの所属する最内ループ

    void compute() {
        if (func.basic_blocks.empty())
            return;

        // バックエッジを検出してループを特定
        // Back-edge: n -> h where h dominates n
        for (size_t i = 0; i < func.basic_blocks.size(); ++i) {
            if (!func.basic_blocks[i])
                continue;
            const auto& bb = *func.basic_blocks[i];

            // Successorsを取得
            std::vector<BlockId> succs;
            if (bb.terminator) {
                switch (bb.terminator->kind) {
                    case MirTerminator::Goto:
                        succs.push_back(
                            std::get<MirTerminator::GotoData>(bb.terminator->data).target);
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
                    // Loop detected with header h

                    // 既存のループを探す（同じヘッダーを持つループは一つにまとめる）
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

    void populate_loop_body(Loop* loop, BlockId back_edge_node) {
        if (loop->blocks.count(back_edge_node))
            return;  // 既に追加済み

        loop->blocks.insert(back_edge_node);

        std::queue<BlockId> q;
        q.push(back_edge_node);

        while (!q.empty()) {
            BlockId m = q.front();
            q.pop();

            // mのPredecessorsを探索し、ループに追加
            // Predecessorsが見つからない場合、func全体を走査（非効率だが正確）
            // DominatorTreeで使ったロジックを再利用すべきだが、ここでは都度計算する
            for (size_t p = 0; p < func.basic_blocks.size(); ++p) {
                if (!func.basic_blocks[p])
                    continue;
                const auto& p_bb = *func.basic_blocks[p];
                if (!p_bb.terminator)
                    continue;

                bool is_pred = false;
                // Successors check...
                // (Optimized: only if p is not already in loop)
                if (loop->blocks.count(p))
                    continue;

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

    void build_nesting_tree() {
        // 親ループを設定
        for (auto& inner : loops) {
            Loop* parent = nullptr;
            for (auto& outer : loops) {
                if (inner.get() == outer.get())
                    continue;

                // outerがinnerを包含するか
                if (outer->contains(inner.get())) {
                    // より小さい（内側の）親を選ぶ
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
                    // より内側のループで更新
                    if (current->contains(loop.get())) {
                        block_to_loop[b] = loop.get();
                    }
                }
            }
        }
    }
};

}  // namespace cm::mir
