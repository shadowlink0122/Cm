#include "dominators.hpp"

#include <algorithm>
#include <queue>

namespace cm::mir {

DominatorTree::DominatorTree(const MirFunction& f) : func(f) {
    compute();
}

bool DominatorTree::dominates(BlockId a, BlockId b) const {
    if (a == b)
        return true;

    // entryは全てを支配する
    if (a == 0 && reachable.count(b))
        return true;

    // 到達不能ブロックは考慮しない
    if (!reachable.count(a) || !reachable.count(b))
        return false;

    auto it = dom_sets.find(b);
    if (it != dom_sets.end()) {
        return it->second.count(a) > 0;
    }
    return false;
}

std::optional<BlockId> DominatorTree::get_idom(BlockId b) const {
    auto it = idoms.find(b);
    if (it != idoms.end()) {
        return it->second;
    }
    return std::nullopt;
}

void DominatorTree::compute() {
    if (func.basic_blocks.empty())
        return;

    // 到達可能ブロックを特定（CFGトラバーサル）
    std::queue<BlockId> q;
    q.push(0);  // entry
    reachable.insert(0);

    while (!q.empty()) {
        BlockId curr = q.front();
        q.pop();

        if (curr >= func.basic_blocks.size() || !func.basic_blocks[curr])
            continue;

        const auto& bb = *func.basic_blocks[curr];
        if (bb.terminator) {
            std::vector<BlockId> succs;
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
                case MirTerminator::Return:
                case MirTerminator::Unreachable:
                    break;
            }

            for (BlockId s : succs) {
                if (reachable.find(s) == reachable.end()) {
                    reachable.insert(s);
                    q.push(s);
                }
            }
        }
    }

    // 全ブロックの集合（到達可能のみ）
    std::set<BlockId> all_blocks = reachable;

    // 初期化: Dom(n0) = {n0}, Dom(n) = All for n != n0
    dom_sets[0] = {0};
    for (BlockId b : all_blocks) {
        if (b != 0) {
            dom_sets[b] = all_blocks;
        }
    }

    // 反復計算
    bool changed = true;
    while (changed) {
        changed = false;
        for (BlockId b : all_blocks) {
            if (b == 0)
                continue;

            // bへのPredecessorを探す
            std::vector<BlockId> preds;
            for (BlockId p : all_blocks) {
                if (p >= func.basic_blocks.size() || !func.basic_blocks[p])
                    continue;
                const auto& p_bb = *func.basic_blocks[p];
                if (!p_bb.terminator)
                    continue;

                bool is_pred = false;
                switch (p_bb.terminator->kind) {
                    case MirTerminator::Goto:
                        if (std::get<MirTerminator::GotoData>(p_bb.terminator->data).target == b)
                            is_pred = true;
                        break;
                    case MirTerminator::SwitchInt: {
                        auto& data = std::get<MirTerminator::SwitchIntData>(p_bb.terminator->data);
                        if (data.otherwise == b)
                            is_pred = true;
                        for (auto& pair : data.targets)
                            if (pair.second == b)
                                is_pred = true;
                        break;
                    }
                    case MirTerminator::Call: {
                        auto& data = std::get<MirTerminator::CallData>(p_bb.terminator->data);
                        if (data.success == b)
                            is_pred = true;
                        break;
                    }
                    default:
                        break;
                }
                if (is_pred)
                    preds.push_back(p);
            }

            if (preds.empty())
                continue;

            // Intersection計算
            std::set<BlockId> new_dom = dom_sets[preds[0]];
            for (size_t i = 1; i < preds.size(); ++i) {
                std::set<BlockId> intersection;
                const auto& p_dom = dom_sets[preds[i]];
                std::set_intersection(new_dom.begin(), new_dom.end(), p_dom.begin(), p_dom.end(),
                                      std::inserter(intersection, intersection.begin()));
                new_dom = intersection;
            }

            new_dom.insert(b);

            if (dom_sets[b] != new_dom) {
                dom_sets[b] = new_dom;
                changed = true;
            }
        }
    }

    // Immediate Dominator (idom) の計算
    for (BlockId b : all_blocks) {
        if (b == 0)
            continue;

        std::set<BlockId> strict_doms = dom_sets[b];
        strict_doms.erase(b);

        if (strict_doms.empty())
            continue;

        BlockId candidate = *strict_doms.begin();
        size_t max_size = 0;

        for (BlockId d : strict_doms) {
            if (dom_sets[d].size() > max_size) {
                max_size = dom_sets[d].size();
                candidate = d;
            }
        }
        idoms[b] = candidate;
    }
}

}  // namespace cm::mir
