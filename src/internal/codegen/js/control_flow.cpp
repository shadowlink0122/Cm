#include "control_flow.hpp"

#include <algorithm>
#include <queue>

namespace cm::codegen::js {

// ============================================================
// ControlFlowAnalyzer
// ============================================================

ControlFlowAnalyzer::ControlFlowAnalyzer(const mir::MirFunction& func) : func_(func) {
    if (!func_.basic_blocks.empty()) {
        computeDominators();
        findBackEdges();
        identifyLoops();
    }
}

bool ControlFlowAnalyzer::isLinearFlow() const {
    // 単一ブロックは線形
    if (func_.basic_blocks.size() <= 1) {
        return true;
    }

    // バックエッジ（ループ）がある場合は非線形
    if (!back_edges_.empty()) {
        return false;
    }

    // 全てのターミネーターがGoto, Return, Callのみであること
    // SwitchInt があると分岐があるので非線形
    for (const auto& block : func_.basic_blocks) {
        if (!block || !block->terminator)
            continue;

        switch (block->terminator->kind) {
            case mir::MirTerminator::Goto:
            case mir::MirTerminator::Return:
            case mir::MirTerminator::Call:
            case mir::MirTerminator::Unreachable:
                // これらはOK
                break;
            case mir::MirTerminator::SwitchInt:
                // 分岐がある
                return false;
        }
    }

    return true;
}

std::vector<mir::BlockId> ControlFlowAnalyzer::getLinearBlockOrder() const {
    std::vector<mir::BlockId> order;
    std::set<mir::BlockId> visited;

    if (func_.basic_blocks.empty()) {
        return order;
    }

    // エントリーブロックから開始してGotoチェーンを辿る
    mir::BlockId current = func_.entry_block;

    while (current != mir::INVALID_BLOCK && visited.find(current) == visited.end()) {
        visited.insert(current);
        order.push_back(current);

        if (current >= func_.basic_blocks.size() || !func_.basic_blocks[current]) {
            break;
        }

        const auto& block = func_.basic_blocks[current];
        if (!block->terminator) {
            break;
        }

        switch (block->terminator->kind) {
            case mir::MirTerminator::Goto: {
                const auto& data = std::get<mir::MirTerminator::GotoData>(block->terminator->data);
                current = data.target;
                break;
            }
            case mir::MirTerminator::Call: {
                const auto& data = std::get<mir::MirTerminator::CallData>(block->terminator->data);
                current = data.success;
                break;
            }
            default:
                // Return, Unreachable, SwitchInt で終了
                current = mir::INVALID_BLOCK;
                break;
        }
    }

    return order;
}

bool ControlFlowAnalyzer::isBlockUsed(mir::BlockId block) const {
    if (block >= func_.basic_blocks.size()) {
        return false;
    }
    // 入口ブロックは常に使用される
    if (block == func_.entry_block) {
        return true;
    }
    // predecessorがあれば使用される
    if (func_.basic_blocks[block]) {
        return !func_.basic_blocks[block]->predecessors.empty();
    }
    return false;
}

bool ControlFlowAnalyzer::isLoopHeader(mir::BlockId block) const {
    return loop_headers_.count(block) > 0;
}

bool ControlFlowAnalyzer::isLoopExit(mir::BlockId block) const {
    return loop_exits_.count(block) > 0;
}

const std::set<mir::BlockId>& ControlFlowAnalyzer::getDominators(mir::BlockId block) const {
    static const std::set<mir::BlockId> empty;
    auto it = dominators_.find(block);
    if (it != dominators_.end()) {
        return it->second;
    }
    return empty;
}

void ControlFlowAnalyzer::computeDominators() {
    // 単純な支配者計算（データフロー解析）
    if (func_.basic_blocks.empty())
        return;

    // 初期化: エントリーブロックは自身のみを支配
    // 他のブロックは全ブロックを支配（後で絞り込む）
    std::set<mir::BlockId> all_blocks;
    for (size_t i = 0; i < func_.basic_blocks.size(); ++i) {
        if (func_.basic_blocks[i]) {
            all_blocks.insert(i);
        }
    }

    for (mir::BlockId b : all_blocks) {
        if (b == func_.entry_block) {
            dominators_[b] = {b};
        } else {
            dominators_[b] = all_blocks;
        }
    }

    // 固定点反復
    bool changed = true;
    while (changed) {
        changed = false;
        for (mir::BlockId b : all_blocks) {
            if (b == func_.entry_block)
                continue;

            const auto& block = func_.basic_blocks[b];
            if (!block)
                continue;

            std::set<mir::BlockId> new_dom;
            bool first = true;
            for (mir::BlockId pred : block->predecessors) {
                if (first) {
                    new_dom = dominators_[pred];
                    first = false;
                } else {
                    // 積集合
                    std::set<mir::BlockId> intersection;
                    std::set_intersection(new_dom.begin(), new_dom.end(), dominators_[pred].begin(),
                                          dominators_[pred].end(),
                                          std::inserter(intersection, intersection.begin()));
                    new_dom = intersection;
                }
            }
            new_dom.insert(b);  // 自分自身を追加

            if (new_dom != dominators_[b]) {
                dominators_[b] = new_dom;
                changed = true;
            }
        }
    }
}

void ControlFlowAnalyzer::findBackEdges() {
    // バックエッジ: target が source を支配するエッジ
    for (const auto& block : func_.basic_blocks) {
        if (!block || !block->terminator)
            continue;

        for (mir::BlockId succ : block->successors) {
            // succ が block を支配していればバックエッジ
            const auto& doms = getDominators(block->id);
            if (doms.count(succ) > 0) {
                back_edges_.emplace_back(block->id, succ);
            }
        }
    }
}

void ControlFlowAnalyzer::identifyLoops() {
    // バックエッジのターゲットがループヘッダー
    for (const auto& [from, to] : back_edges_) {
        loop_headers_.insert(to);
    }

    // ループ終了ブロックを特定（ループヘッダーから出るブロック）
    for (mir::BlockId header : loop_headers_) {
        if (header >= func_.basic_blocks.size() || !func_.basic_blocks[header])
            continue;

        const auto& block = func_.basic_blocks[header];
        if (!block->terminator)
            continue;

        // SwitchIntの場合、otherwiseがループ外かどうか確認
        if (block->terminator->kind == mir::MirTerminator::SwitchInt) {
            const auto& data = std::get<mir::MirTerminator::SwitchIntData>(block->terminator->data);
            // otherwiseまたはtargetsの中でループ外に出るものを探す
            // 簡易的に: ヘッダーを支配しないブロックをループ外とみなす
            for (const auto& [val, target] : data.targets) {
                if (loop_headers_.count(target) == 0) {
                    loop_exits_.insert(target);
                }
            }
            if (loop_headers_.count(data.otherwise) == 0) {
                loop_exits_.insert(data.otherwise);
            }
        }
    }
}

// ============================================================
// BlockMerger
// ============================================================

BlockMerger::BlockMerger(const mir::MirFunction& func) : func_(func) {
    if (func_.basic_blocks.empty())
        return;

    // 到達可能なブロックを順序付けて収集
    std::set<mir::BlockId> visited;
    std::queue<mir::BlockId> queue;
    queue.push(func_.entry_block);

    while (!queue.empty()) {
        mir::BlockId current = queue.front();
        queue.pop();

        if (visited.count(current) > 0)
            continue;
        if (current >= func_.basic_blocks.size() || !func_.basic_blocks[current])
            continue;

        visited.insert(current);
        ordered_blocks_.push_back(current);

        const auto& block = func_.basic_blocks[current];
        for (mir::BlockId succ : block->successors) {
            if (visited.count(succ) == 0) {
                queue.push(succ);
            }
        }
    }

    // 線形フローの次ブロックをマッピング
    for (const auto& block : func_.basic_blocks) {
        if (!block || !block->terminator)
            continue;

        if (block->terminator->kind == mir::MirTerminator::Goto) {
            const auto& data = std::get<mir::MirTerminator::GotoData>(block->terminator->data);
            next_block_[block->id] = data.target;
        } else if (block->terminator->kind == mir::MirTerminator::Call) {
            const auto& data = std::get<mir::MirTerminator::CallData>(block->terminator->data);
            next_block_[block->id] = data.success;
        }
    }
}

std::vector<mir::BlockId> BlockMerger::getMergedBlockOrder() const {
    return ordered_blocks_;
}

bool BlockMerger::shouldContinueToNext(mir::BlockId block) const {
    if (block >= func_.basic_blocks.size() || !func_.basic_blocks[block]) {
        return false;
    }

    const auto& blk = func_.basic_blocks[block];
    if (!blk->terminator)
        return false;

    // Gotoの場合、次のブロックに自然にフォールスルーできる
    if (blk->terminator->kind == mir::MirTerminator::Goto) {
        const auto& data = std::get<mir::MirTerminator::GotoData>(blk->terminator->data);
        // 次のブロックがpredecessorが1つだけなら結合可能
        if (data.target < func_.basic_blocks.size() && func_.basic_blocks[data.target]) {
            return func_.basic_blocks[data.target]->predecessors.size() == 1;
        }
    }

    return false;
}

mir::BlockId BlockMerger::getNextBlock(mir::BlockId block) const {
    auto it = next_block_.find(block);
    if (it != next_block_.end()) {
        return it->second;
    }
    return mir::INVALID_BLOCK;
}

}  // namespace cm::codegen::js
