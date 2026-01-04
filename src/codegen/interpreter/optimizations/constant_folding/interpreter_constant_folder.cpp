#include "interpreter_constant_folder.hpp"

#include <cmath>
#include <iostream>

namespace cm::codegen::interpreter::optimizations {

bool InterpreterConstantFolder::optimize(mir::MirFunction& func) {
    bool changed = false;
    constantValues.clear();

    // 各基本ブロックを処理
    for (auto& block : func.basic_blocks) {
        if (block) {
            changed |= foldInBasicBlock(*block);

            // ターミネータの簡約化
            if (block->terminator) {
                changed |= simplifyTerminator(*block->terminator);
            }
        }
    }

    return changed;
}

bool InterpreterConstantFolder::optimizeProgram(mir::MirProgram& program) {
    bool changed = false;

    // グローバル定数を収集
    for (const auto& global : program.globals) {
        if (global.second.is_const && global.second.initializer) {
            if (auto value = evaluateOperand(*global.second.initializer)) {
                globalConstants[global.first] = *value;
            }
        }
    }

    // 各関数を最適化
    for (auto& func : program.functions) {
        if (func && !func->is_extern) {
            changed |= optimize(*func);
        }
    }

#ifdef DEBUG
    printStatistics();
#endif

    return changed;
}

bool InterpreterConstantFolder::foldInBasicBlock(mir::BasicBlock& block) {
    bool changed = false;
    std::vector<size_t> toRemove;

    for (size_t i = 0; i < block.statements.size(); ++i) {
        auto& stmt = block.statements[i];
        if (stmt) {
            if (foldStatement(*stmt)) {
                changed = true;

                // Nop命令は削除対象
                if (stmt->kind == mir::StatementKind::Nop) {
                    toRemove.push_back(i);
                    stats.deadCodeEliminated++;
                }
            }
        }
    }

    // 削除対象を後ろから削除
    for (auto it = toRemove.rbegin(); it != toRemove.rend(); ++it) {
        block.statements.erase(block.statements.begin() + *it);
    }

    return changed;
}

bool InterpreterConstantFolder::foldStatement(mir::MirStatement& stmt) {
    switch (stmt.kind) {
        case mir::StatementKind::Assign: {
            auto* assign = static_cast<mir::AssignStatement*>(&stmt);

            // Rvalueを評価
            if (auto value = evaluateRvalue(assign->value)) {
                // 定数値として記録
                constantValues[assign->local] = *value;

                // 定数代入に置き換え
                assign->value.kind = mir::RvalueKind::Use;
                assign->value.operand =
                    mir::MirOperand{mir::MirOperand::Constant, 0, valueToMirConstant(*value)};

                stats.statementsOptimized++;
                return true;
            }

            // 定数でない場合、その変数は定数でなくなる
            constantValues.erase(assign->local);
            break;
        }

        case mir::StatementKind::Store: {
            auto* store = static_cast<mir::StoreStatement*>(&stmt);

            // 値を評価
            if (auto value = evaluateOperand(store->value)) {
                // 定数値で置き換え
                store->value =
                    mir::MirOperand{mir::MirOperand::Constant, 0, valueToMirConstant(*value)};

                stats.statementsOptimized++;
                return true;
            }
            break;
        }

        case mir::StatementKind::Call: {
            // 純粋関数の呼び出しで全引数が定数の場合は評価可能
            // （現在は未実装）
            break;
        }

        default:
            break;
    }

    return false;
}

std::optional<Value> InterpreterConstantFolder::evaluateRvalue(const mir::MirRvalue& rvalue) {
    switch (rvalue.kind) {
        case mir::RvalueKind::Use:
            return evaluateOperand(rvalue.operand);

        case mir::RvalueKind::BinaryOp: {
            auto lhs = evaluateOperand(rvalue.operands[0]);
            auto rhs = evaluateOperand(rvalue.operands[1]);

            if (lhs && rhs) {
                return evaluateBinaryOp(rvalue.binary_op, *lhs, *rhs);
            }
            break;
        }

        case mir::RvalueKind::UnaryOp: {
            auto operand = evaluateOperand(rvalue.operand);

            if (operand) {
                return evaluateUnaryOp(rvalue.unary_op, *operand);
            }
            break;
        }

        case mir::RvalueKind::Cast: {
            auto value = evaluateOperand(rvalue.operand);

            if (value) {
                return evaluateCast(*value, rvalue.cast_type);
            }
            break;
        }

        case mir::RvalueKind::Aggregate:
            return evaluateAggregate(rvalue);

        default:
            break;
    }

    return std::nullopt;
}

std::optional<Value> InterpreterConstantFolder::evaluateOperand(const mir::MirOperand& operand) {
    switch (operand.kind) {
        case mir::MirOperand::Constant:
            return mirConstantToValue(operand.constant);

        case mir::MirOperand::Local: {
            auto it = constantValues.find(operand.local);
            if (it != constantValues.end()) {
                return it->second;
            }
            break;
        }

        case mir::MirOperand::Global: {
            auto it = globalConstants.find(operand.global);
            if (it != globalConstants.end()) {
                return it->second;
            }
            break;
        }

        default:
            break;
    }

    return std::nullopt;
}

std::optional<Value> InterpreterConstantFolder::evaluateBinaryOp(mir::MirBinaryOp op,
                                                                 const Value& lhs,
                                                                 const Value& rhs) {
    stats.binaryOpsEvaluated++;

    // 整数演算
    if (std::holds_alternative<int64_t>(lhs) && std::holds_alternative<int64_t>(rhs)) {
        int64_t l = std::get<int64_t>(lhs);
        int64_t r = std::get<int64_t>(rhs);

        switch (op) {
            case mir::MirBinaryOp::Add:
                return Value(l + r);
            case mir::MirBinaryOp::Sub:
                return Value(l - r);
            case mir::MirBinaryOp::Mul:
                return Value(l * r);
            case mir::MirBinaryOp::Div:
                if (r != 0)
                    return Value(l / r);
                break;
            case mir::MirBinaryOp::Mod:
                if (r != 0)
                    return Value(l % r);
                break;
            case mir::MirBinaryOp::BitAnd:
                return Value(l & r);
            case mir::MirBinaryOp::BitOr:
                return Value(l | r);
            case mir::MirBinaryOp::BitXor:
                return Value(l ^ r);
            case mir::MirBinaryOp::Shl:
                return Value(l << r);
            case mir::MirBinaryOp::Shr:
                return Value(l >> r);
            case mir::MirBinaryOp::Eq:
                return Value(l == r);
            case mir::MirBinaryOp::Ne:
                return Value(l != r);
            case mir::MirBinaryOp::Lt:
                return Value(l < r);
            case mir::MirBinaryOp::Le:
                return Value(l <= r);
            case mir::MirBinaryOp::Gt:
                return Value(l > r);
            case mir::MirBinaryOp::Ge:
                return Value(l >= r);
            default:
                break;
        }
    }

    // 浮動小数点演算
    if (std::holds_alternative<double>(lhs) && std::holds_alternative<double>(rhs)) {
        double l = std::get<double>(lhs);
        double r = std::get<double>(rhs);

        switch (op) {
            case mir::MirBinaryOp::Add:
                return Value(l + r);
            case mir::MirBinaryOp::Sub:
                return Value(l - r);
            case mir::MirBinaryOp::Mul:
                return Value(l * r);
            case mir::MirBinaryOp::Div:
                if (r != 0.0)
                    return Value(l / r);
                break;
            case mir::MirBinaryOp::Eq:
                return Value(l == r);
            case mir::MirBinaryOp::Ne:
                return Value(l != r);
            case mir::MirBinaryOp::Lt:
                return Value(l < r);
            case mir::MirBinaryOp::Le:
                return Value(l <= r);
            case mir::MirBinaryOp::Gt:
                return Value(l > r);
            case mir::MirBinaryOp::Ge:
                return Value(l >= r);
            default:
                break;
        }
    }

    // 文字列連結
    if (op == mir::MirBinaryOp::Add && std::holds_alternative<std::string>(lhs) &&
        std::holds_alternative<std::string>(rhs)) {
        return Value(std::get<std::string>(lhs) + std::get<std::string>(rhs));
    }

    return std::nullopt;
}

std::optional<Value> InterpreterConstantFolder::evaluateUnaryOp(mir::MirUnaryOp op,
                                                                const Value& operand) {
    stats.unaryOpsEvaluated++;

    switch (op) {
        case mir::MirUnaryOp::Neg: {
            if (std::holds_alternative<int64_t>(operand)) {
                return Value(-std::get<int64_t>(operand));
            }
            if (std::holds_alternative<double>(operand)) {
                return Value(-std::get<double>(operand));
            }
            break;
        }

        case mir::MirUnaryOp::Not: {
            if (std::holds_alternative<bool>(operand)) {
                return Value(!std::get<bool>(operand));
            }
            if (std::holds_alternative<int64_t>(operand)) {
                return Value(std::get<int64_t>(operand) == 0);
            }
            break;
        }

        case mir::MirUnaryOp::BitNot: {
            if (std::holds_alternative<int64_t>(operand)) {
                return Value(~std::get<int64_t>(operand));
            }
            break;
        }

        default:
            break;
    }

    return std::nullopt;
}

std::optional<Value> InterpreterConstantFolder::evaluateComparison(mir::MirCompareOp op,
                                                                   const Value& lhs,
                                                                   const Value& rhs) {
    stats.comparisonsEvaluated++;

    // 整数比較
    if (std::holds_alternative<int64_t>(lhs) && std::holds_alternative<int64_t>(rhs)) {
        int64_t l = std::get<int64_t>(lhs);
        int64_t r = std::get<int64_t>(rhs);

        switch (op) {
            case mir::MirCompareOp::Eq:
                return Value(l == r);
            case mir::MirCompareOp::Ne:
                return Value(l != r);
            case mir::MirCompareOp::Lt:
                return Value(l < r);
            case mir::MirCompareOp::Le:
                return Value(l <= r);
            case mir::MirCompareOp::Gt:
                return Value(l > r);
            case mir::MirCompareOp::Ge:
                return Value(l >= r);
        }
    }

    // 浮動小数点比較
    if (std::holds_alternative<double>(lhs) && std::holds_alternative<double>(rhs)) {
        double l = std::get<double>(lhs);
        double r = std::get<double>(rhs);

        switch (op) {
            case mir::MirCompareOp::Eq:
                return Value(l == r);
            case mir::MirCompareOp::Ne:
                return Value(l != r);
            case mir::MirCompareOp::Lt:
                return Value(l < r);
            case mir::MirCompareOp::Le:
                return Value(l <= r);
            case mir::MirCompareOp::Gt:
                return Value(l > r);
            case mir::MirCompareOp::Ge:
                return Value(l >= r);
        }
    }

    return std::nullopt;
}

std::optional<Value> InterpreterConstantFolder::evaluateCast(
    const Value& value, const std::shared_ptr<hir::Type>& targetType) {
    stats.castsEvaluated++;

    if (!targetType)
        return std::nullopt;

    // int -> float
    if (targetType->kind == hir::TypeKind::Float || targetType->kind == hir::TypeKind::Double) {
        if (std::holds_alternative<int64_t>(value)) {
            return Value(static_cast<double>(std::get<int64_t>(value)));
        }
    }

    // float -> int
    if (targetType->kind == hir::TypeKind::Int || targetType->kind == hir::TypeKind::Long) {
        if (std::holds_alternative<double>(value)) {
            return Value(static_cast<int64_t>(std::get<double>(value)));
        }
    }

    // int -> bool
    if (targetType->kind == hir::TypeKind::Bool) {
        if (std::holds_alternative<int64_t>(value)) {
            return Value(std::get<int64_t>(value) != 0);
        }
    }

    // bool -> int
    if (targetType->kind == hir::TypeKind::Int) {
        if (std::holds_alternative<bool>(value)) {
            return Value(static_cast<int64_t>(std::get<bool>(value) ? 1 : 0));
        }
    }

    return std::nullopt;
}

std::optional<Value> InterpreterConstantFolder::evaluateAggregate(const mir::MirRvalue& rvalue) {
    // 配列や構造体の定数初期化
    // 全要素が定数の場合のみ評価可能
    std::vector<Value> elements;

    for (const auto& op : rvalue.aggregate_ops) {
        if (auto value = evaluateOperand(op)) {
            elements.push_back(*value);
        } else {
            return std::nullopt;  // 一つでも非定数があれば評価不可
        }
    }

    stats.aggregatesEvaluated++;
    return Value(std::move(elements));  // 配列として返す
}

bool InterpreterConstantFolder::simplifyTerminator(mir::Terminator& terminator) {
    switch (terminator.kind) {
        case mir::TerminatorKind::SwitchInt: {
            // Switch文の条件が定数の場合、直接該当ブランチへジャンプ
            if (auto value = evaluateOperand(terminator.switch_value)) {
                if (std::holds_alternative<int64_t>(*value)) {
                    int64_t switchVal = std::get<int64_t>(*value);

                    // 該当するケースを探す
                    for (size_t i = 0; i < terminator.switch_values.size(); ++i) {
                        if (terminator.switch_values[i] == switchVal) {
                            // 直接Gotoに置き換え
                            terminator.kind = mir::TerminatorKind::Goto;
                            terminator.target = terminator.switch_targets[i];
                            stats.terminatorsSimplified++;
                            return true;
                        }
                    }

                    // デフォルトケース
                    terminator.kind = mir::TerminatorKind::Goto;
                    terminator.target = terminator.switch_targets.back();
                    stats.terminatorsSimplified++;
                    return true;
                }
            }
            break;
        }

        case mir::TerminatorKind::Call: {
            // 純粋関数で全引数が定数の場合の最適化（未実装）
            break;
        }

        default:
            break;
    }

    return false;
}

bool InterpreterConstantFolder::isConstant(const mir::MirOperand& operand) const {
    switch (operand.kind) {
        case mir::MirOperand::Constant:
            return true;
        case mir::MirOperand::Local:
            return constantValues.find(operand.local) != constantValues.end();
        case mir::MirOperand::Global:
            return globalConstants.find(operand.global) != globalConstants.end();
        default:
            return false;
    }
}

bool InterpreterConstantFolder::hasSideEffects(const mir::MirRvalue& rvalue) const {
    // 副作用のある操作をチェック
    switch (rvalue.kind) {
        case mir::RvalueKind::Call:
        case mir::RvalueKind::InlineAsm:
            return true;
        default:
            return false;
    }
}

mir::MirConstant InterpreterConstantFolder::valueToMirConstant(const Value& value) const {
    mir::MirConstant constant;

    if (std::holds_alternative<bool>(value)) {
        constant.kind = mir::MirConstant::Bool;
        constant.value.b = std::get<bool>(value);
    } else if (std::holds_alternative<int64_t>(value)) {
        constant.kind = mir::MirConstant::Int;
        constant.value.i = std::get<int64_t>(value);
    } else if (std::holds_alternative<double>(value)) {
        constant.kind = mir::MirConstant::Float;
        constant.value.f = std::get<double>(value);
    } else if (std::holds_alternative<std::string>(value)) {
        constant.kind = mir::MirConstant::String;
        constant.str_value = std::get<std::string>(value);
    }

    return constant;
}

Value InterpreterConstantFolder::mirConstantToValue(const mir::MirConstant& constant) const {
    switch (constant.kind) {
        case mir::MirConstant::Bool:
            return Value(constant.value.b);
        case mir::MirConstant::Char:
            return Value(static_cast<int64_t>(constant.value.c));
        case mir::MirConstant::Int:
            return Value(constant.value.i);
        case mir::MirConstant::Float:
            return Value(constant.value.f);
        case mir::MirConstant::String:
            return Value(constant.str_value);
        default:
            return Value();  // Null value
    }
}

void InterpreterConstantFolder::printStatistics() const {
    std::cerr << "\n=== Interpreter Constant Folding Statistics ===\n";
    std::cerr << "  Statements optimized: " << stats.statementsOptimized << "\n";
    std::cerr << "  Binary operations evaluated: " << stats.binaryOpsEvaluated << "\n";
    std::cerr << "  Unary operations evaluated: " << stats.unaryOpsEvaluated << "\n";
    std::cerr << "  Comparisons evaluated: " << stats.comparisonsEvaluated << "\n";
    std::cerr << "  Casts evaluated: " << stats.castsEvaluated << "\n";
    std::cerr << "  Aggregates evaluated: " << stats.aggregatesEvaluated << "\n";
    std::cerr << "  String operations evaluated: " << stats.stringOpsEvaluated << "\n";
    std::cerr << "  Terminators simplified: " << stats.terminatorsSimplified << "\n";
    std::cerr << "  Dead code eliminated: " << stats.deadCodeEliminated << "\n";
    std::cerr << "===============================================\n";
}

}  // namespace cm::codegen::interpreter::optimizations