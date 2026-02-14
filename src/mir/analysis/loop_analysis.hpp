#pragma once

#include "mir/analysis/dominators.hpp"
#include "mir/nodes.hpp"

#include <map>
#include <memory>
#include <set>
#include <vector>

namespace cm::mir {

struct Loop {
    BlockId header;
    std::set<BlockId> blocks;
    std::vector<BlockId> back_edges;

    std::vector<Loop*> sub_loops;
    Loop* parent_loop = nullptr;

    bool contains(BlockId b) const { return blocks.count(b) > 0; }

    bool contains(const Loop* other) const {
        return contains(other->header) && header != other->header;
    }
};

class LoopAnalysis {
   public:
    LoopAnalysis(const MirFunction& f, const DominatorTree& d);

    const std::vector<std::unique_ptr<Loop>>& get_loops() const { return loops; }

    const std::vector<Loop*>& get_top_level_loops() const { return top_level_loops; }

    Loop* get_inner_most_loop(BlockId b) const;

   private:
    const MirFunction& func;
    const DominatorTree& dom_tree;
    std::vector<std::unique_ptr<Loop>> loops;
    std::vector<Loop*> top_level_loops;
    std::map<BlockId, Loop*> block_to_loop;

    void compute();
    void populate_loop_body(Loop* loop, BlockId back_edge_node);
    void build_nesting_tree();
};

}  // namespace cm::mir
