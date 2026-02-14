#pragma once

#include "mir/nodes.hpp"

#include <map>
#include <optional>
#include <set>
#include <vector>

namespace cm::mir {

class DominatorTree {
   public:
    explicit DominatorTree(const MirFunction& f);

    // ブロック A が ブロック B を支配しているか
    bool dominates(BlockId a, BlockId b) const;

    // 即時支配ブロックを取得
    std::optional<BlockId> get_idom(BlockId b) const;

   private:
    const MirFunction& func;
    std::set<BlockId> reachable;
    std::map<BlockId, std::set<BlockId>> dom_sets;
    std::map<BlockId, BlockId> idoms;

    void compute();
};

}  // namespace cm::mir
