#pragma once

#include "../../mir/nodes.hpp"

#include <map>
#include <set>
#include <vector>

namespace cm::codegen::js {

// 制御フロー構造の種類
enum class ControlFlowKind {
    Sequence,    // 連続した文の列
    IfThen,      // if (cond) { body }
    IfThenElse,  // if (cond) { then } else { else }
    WhileLoop,   // while (cond) { body }
    DoWhile,     // do { body } while (cond)
    Return,      // return
    Break,       // break (ループ脱出)
    Continue,    // continue (ループ継続)
};

// 構造化された制御フローノード
struct StructuredCF {
    ControlFlowKind kind;
    mir::BlockId start_block;
    mir::BlockId end_block;
    std::vector<std::unique_ptr<StructuredCF>> children;

    // IfThenElse用
    mir::BlockId then_block = mir::INVALID_BLOCK;
    mir::BlockId else_block = mir::INVALID_BLOCK;

    // Loop用
    mir::BlockId loop_header = mir::INVALID_BLOCK;
    mir::BlockId loop_exit = mir::INVALID_BLOCK;
};

// 制御フロー解析器
class ControlFlowAnalyzer {
   public:
    explicit ControlFlowAnalyzer(const mir::MirFunction& func);

    // 関数が単純な線形フローかどうか判定
    // (単一ブロック、または連続したGotoのチェーン)
    bool isLinearFlow() const;

    // 線形フローのブロック順序を取得
    std::vector<mir::BlockId> getLinearBlockOrder() const;

    // ブロックが使用されるかどうか
    bool isBlockUsed(mir::BlockId block) const;

    // ループのバックエッジを検出
    bool hasLoops() const { return !back_edges_.empty(); }

    // ループヘッダーかどうか
    bool isLoopHeader(mir::BlockId block) const;

    // ループ終了ブロックかどうか
    bool isLoopExit(mir::BlockId block) const;

    // ブロックの支配者を取得
    const std::set<mir::BlockId>& getDominators(mir::BlockId block) const;

   private:
    const mir::MirFunction& func_;
    std::map<mir::BlockId, std::set<mir::BlockId>> dominators_;
    std::vector<std::pair<mir::BlockId, mir::BlockId>> back_edges_;  // (from, to)
    std::set<mir::BlockId> loop_headers_;
    std::set<mir::BlockId> loop_exits_;

    void computeDominators();
    void findBackEdges();
    void identifyLoops();
};

// ブロック結合器 - 連続する単純なブロックをマージ
class BlockMerger {
   public:
    explicit BlockMerger(const mir::MirFunction& func);

    // ブロックを結合した順序リストを取得
    std::vector<mir::BlockId> getMergedBlockOrder() const;

    // このブロックで出力を停止すべきか（次のブロックに続く場合）
    bool shouldContinueToNext(mir::BlockId block) const;

    // このブロックの次のブロック（線形フローの場合）
    mir::BlockId getNextBlock(mir::BlockId block) const;

   private:
    const mir::MirFunction& func_;
    std::vector<mir::BlockId> ordered_blocks_;
    std::map<mir::BlockId, mir::BlockId> next_block_;
};

}  // namespace cm::codegen::js
