#pragma once

#include "../../../../mir/nodes.hpp"
#include "../../../interpreter/value.hpp"

#include <optional>
#include <unordered_map>

namespace cm::codegen::interpreter::optimizations {

/// インタープリタ用定数畳み込み最適化
/// MIR実行前に定数式を事前評価して高速化
class InterpreterConstantFolder {
   public:
    /// MIR関数に対して定数畳み込みを実行
    bool optimize(mir::MirFunction& func);

    /// プログラム全体に対して最適化
    bool optimizeProgram(mir::MirProgram& program);

   private:
    /// 定数値のキャッシュ（LocalId -> 定数値）
    std::unordered_map<mir::LocalId, Value> constantValues;

    /// グローバル定数のキャッシュ
    std::unordered_map<std::string, Value> globalConstants;

    /// 基本ブロック内での定数畳み込み
    bool foldInBasicBlock(mir::BasicBlock& block);

    /// ステートメントの定数畳み込み
    bool foldStatement(mir::MirStatement& stmt);

    /// Rvalueの評価
    std::optional<Value> evaluateRvalue(const mir::MirRvalue& rvalue);

    /// オペランドの評価
    std::optional<Value> evaluateOperand(const mir::MirOperand& operand);

    /// 二項演算の評価
    std::optional<Value> evaluateBinaryOp(mir::MirBinaryOp op, const Value& lhs, const Value& rhs);

    /// 単項演算の評価
    std::optional<Value> evaluateUnaryOp(mir::MirUnaryOp op, const Value& operand);

    /// 比較演算の評価
    std::optional<Value> evaluateComparison(mir::MirCompareOp op, const Value& lhs,
                                            const Value& rhs);

    /// キャスト演算の評価
    std::optional<Value> evaluateCast(const Value& value,
                                      const std::shared_ptr<hir::Type>& targetType);

    /// 配列・構造体の定数初期化を評価
    std::optional<Value> evaluateAggregate(const mir::MirRvalue& rvalue);

    /// 文字列操作の評価
    std::optional<Value> evaluateStringOperation(const std::string& op,
                                                 const std::vector<Value>& args);

    /// 値が定数かチェック
    bool isConstant(const mir::MirOperand& operand) const;

    /// 副作用のある操作かチェック
    bool hasSideEffects(const mir::MirRvalue& rvalue) const;

    /// ターミネータの簡約化
    bool simplifyTerminator(mir::Terminator& terminator);

    /// 統計情報
    struct Stats {
        unsigned statementsOptimized = 0;
        unsigned binaryOpsEvaluated = 0;
        unsigned unaryOpsEvaluated = 0;
        unsigned comparisonsEvaluated = 0;
        unsigned castsEvaluated = 0;
        unsigned aggregatesEvaluated = 0;
        unsigned stringOpsEvaluated = 0;
        unsigned terminatorsSimplified = 0;
        unsigned deadCodeEliminated = 0;
    } stats;

    /// 統計情報を出力
    void printStatistics() const;

    /// Valueをmir::MirConstantに変換
    mir::MirConstant valueToMirConstant(const Value& value) const;

    /// mir::MirConstantをValueに変換
    Value mirConstantToValue(const mir::MirConstant& constant) const;
};

}  // namespace cm::codegen::interpreter::optimizations