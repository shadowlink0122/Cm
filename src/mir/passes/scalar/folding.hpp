#pragma once

#include "../core/base.hpp"

#include <set>
#include <unordered_map>
#include <unordered_set>

namespace cm::mir::opt {

// ============================================================
// 定数畳み込み最適化
// ============================================================
class ConstantFolding : public OptimizationPass {
   public:
    std::string name() const override { return "Constant Folding"; }

    bool run(MirFunction& func) override {
        // std::cout << "DEBUG: ConstantFolding::run " << func.name << "\n" << std::flush;
        bool changed = false;

        // 複数回代入される変数を検出（ループ変数など）
        // std::cout << "DEBUG: detect_multi_assigned\n" << std::flush;
        auto multiAssigned = detect_multi_assigned(func);

        // 各ローカル変数の定数値を追跡
        std::unordered_map<LocalId, MirConstant> constants;

        // 各基本ブロックを処理
        // std::cout << "DEBUG: process_blocks\n" << std::flush;
        for (auto& block : func.basic_blocks) {
            if (block) {  // blockがnullの場合のチェックを追加すべき
                changed |= process_block(*block, constants, multiAssigned);
            }
        }
        // std::cout << "DEBUG: ConstantFolding::run finished\n" << std::flush;

        return changed;
    }

   private:
    // 複数回代入される変数を検出
    std::unordered_set<LocalId> detect_multi_assigned(const MirFunction& func) {
        std::unordered_set<LocalId> assigned;
        std::unordered_set<LocalId> multiAssigned;
        for (const auto& block : func.basic_blocks) {
            if (!block)
                continue;
            for (const auto& stmt : block->statements) {
                if (stmt->kind == MirStatement::Assign) {
                    auto& assign_data = std::get<MirStatement::AssignData>(stmt->data);
                    if (assign_data.place.projections.empty()) {
                        LocalId target = assign_data.place.local;
                        if (assigned.count(target) > 0) {
                            multiAssigned.insert(target);
                        } else {
                            assigned.insert(target);
                        }
                    }
                }
            }
        }
        return multiAssigned;
    }

    bool process_block(BasicBlock& block, std::unordered_map<LocalId, MirConstant>& constants,
                       const std::unordered_set<LocalId>& multiAssigned) {
        bool changed = false;

        // 各文を処理
        for (auto& stmt : block.statements) {
            if (stmt->kind == MirStatement::Assign) {
                auto& assign_data = std::get<MirStatement::AssignData>(stmt->data);

                // 単純な代入（_x = _y）の場合
                if (assign_data.place.projections.empty()) {
                    LocalId target = assign_data.place.local;

                    // 複数回代入される変数は定数追跡から除外
                    if (multiAssigned.count(target) > 0) {
                        constants.erase(target);
                        continue;
                    }

                    // Rvalueを評価して定数化できるかチェック
                    if (auto constant = evaluate_rvalue(*assign_data.rvalue, constants)) {
                        // 定数として記録
                        constants[target] = *constant;

                        // RvalueをUse(Constant)に置き換え
                        auto new_operand = MirOperand::constant(*constant);
                        assign_data.rvalue = MirRvalue::use(std::move(new_operand));
                        changed = true;
                    } else {
                        // 定数でない場合は記録から削除
                        constants.erase(target);
                    }
                }
            }
        }

        // 終端命令の定数畳み込み
        if (block.terminator && block.terminator->kind == MirTerminator::SwitchInt) {
            auto& switch_data = std::get<MirTerminator::SwitchIntData>(block.terminator->data);

            // discriminantが定数の場合、無条件ジャンプに変換
            if (switch_data.discriminant->kind == MirOperand::Constant) {
                if (auto* constant = std::get_if<MirConstant>(&switch_data.discriminant->data)) {
                    if (auto* value = std::get_if<int64_t>(&constant->value)) {
                        BlockId target = switch_data.otherwise;

                        // 一致するターゲットを探す
                        for (const auto& [case_value, case_target] : switch_data.targets) {
                            if (case_value == *value) {
                                target = case_target;
                                break;
                            }
                        }

                        // Gotoに置き換え
                        block.terminator = MirTerminator::goto_block(target);
                        changed = true;
                    }
                }
            }
        }

        return changed;
    }

    // Rvalueを評価して定数を返す（可能な場合）
    std::optional<MirConstant> evaluate_rvalue(
        const MirRvalue& rvalue, const std::unordered_map<LocalId, MirConstant>& constants) {
        switch (rvalue.kind) {
            case MirRvalue::Use: {
                if (auto* use_data = std::get_if<MirRvalue::UseData>(&rvalue.data)) {
                    if (!use_data->operand)
                        return std::nullopt;
                    return evaluate_operand(*use_data->operand, constants);
                }
                return std::nullopt;
            }

            case MirRvalue::BinaryOp: {
                auto& bin_data = std::get<MirRvalue::BinaryOpData>(rvalue.data);

                // 両オペランドを評価
                auto lhs = evaluate_operand(*bin_data.lhs, constants);
                auto rhs = evaluate_operand(*bin_data.rhs, constants);

                if (lhs && rhs) {
                    // 両方が定数なら演算を実行
                    return eval_binary_op(bin_data.op, *lhs, *rhs);
                }
                break;
            }

            case MirRvalue::UnaryOp: {
                auto& unary_data = std::get<MirRvalue::UnaryOpData>(rvalue.data);

                // オペランドを評価
                auto operand = evaluate_operand(*unary_data.operand, constants);

                if (operand) {
                    // 定数なら演算を実行
                    return eval_unary_op(unary_data.op, *operand);
                }
                break;
            }

            case MirRvalue::Cast: {
                auto& cast_data = std::get<MirRvalue::CastData>(rvalue.data);
                if (!cast_data.operand || !cast_data.target_type) {
                    break;
                }

                // オペランドを評価
                auto operand = evaluate_operand(*cast_data.operand, constants);
                if (!operand) {
                    break;
                }

                // 型変換を実行
                return eval_cast(*operand, cast_data.target_type);
            }

            default:
                break;
        }

        return std::nullopt;
    }

    // Operandを評価して定数を返す（可能な場合）
    std::optional<MirConstant> evaluate_operand(
        const MirOperand& operand, const std::unordered_map<LocalId, MirConstant>& constants) {
        if (operand.kind == MirOperand::Constant) {
            if (auto* constant = std::get_if<MirConstant>(&operand.data)) {
                return *constant;
            }
        } else if (operand.kind == MirOperand::Copy) {
            if (auto* place = std::get_if<MirPlace>(&operand.data)) {
                // 単純な変数参照の場合
                if (place->projections.empty()) {
                    auto it = constants.find(place->local);
                    if (it != constants.end()) {
                        return it->second;
                    }
                }
            }
        }

        return std::nullopt;
    }

    // 二項演算を評価
    std::optional<MirConstant> eval_binary_op(MirBinaryOp op, const MirConstant& lhs,
                                              const MirConstant& rhs) {
        // 整数演算
        if (auto* lhs_int = std::get_if<int64_t>(&lhs.value)) {
            if (auto* rhs_int = std::get_if<int64_t>(&rhs.value)) {
                MirConstant result;
                result.type = lhs.type;  // 型を保持

                switch (op) {
                    case MirBinaryOp::Add:
                        result.value = *lhs_int + *rhs_int;
                        return result;
                    case MirBinaryOp::Sub:
                        result.value = *lhs_int - *rhs_int;
                        return result;
                    case MirBinaryOp::Mul:
                        result.value = *lhs_int * *rhs_int;
                        return result;
                    case MirBinaryOp::Div:
                        if (*rhs_int != 0) {
                            result.value = *lhs_int / *rhs_int;
                            return result;
                        }
                        break;
                    case MirBinaryOp::Mod:
                        if (*rhs_int != 0) {
                            result.value = *lhs_int % *rhs_int;
                            return result;
                        }
                        break;
                    case MirBinaryOp::BitAnd:
                        result.value = *lhs_int & *rhs_int;
                        return result;
                    case MirBinaryOp::BitOr:
                        result.value = *lhs_int | *rhs_int;
                        return result;
                    case MirBinaryOp::BitXor:
                        result.value = *lhs_int ^ *rhs_int;
                        return result;
                    case MirBinaryOp::Shl:
                        result.value = *lhs_int << *rhs_int;
                        return result;
                    case MirBinaryOp::Shr:
                        result.value = *lhs_int >> *rhs_int;
                        return result;

                    // 比較演算（bool結果）
                    case MirBinaryOp::Eq:
                        result.value = (*lhs_int == *rhs_int);
                        return result;
                    case MirBinaryOp::Ne:
                        result.value = (*lhs_int != *rhs_int);
                        return result;
                    case MirBinaryOp::Lt:
                        result.value = (*lhs_int < *rhs_int);
                        return result;
                    case MirBinaryOp::Le:
                        result.value = (*lhs_int <= *rhs_int);
                        return result;
                    case MirBinaryOp::Gt:
                        result.value = (*lhs_int > *rhs_int);
                        return result;
                    case MirBinaryOp::Ge:
                        result.value = (*lhs_int >= *rhs_int);
                        return result;

                    default:
                        break;
                }
            }
        }

        // bool演算
        if (auto* lhs_bool = std::get_if<bool>(&lhs.value)) {
            if (auto* rhs_bool = std::get_if<bool>(&rhs.value)) {
                MirConstant result;
                result.type = lhs.type;

                switch (op) {
                    // 論理演算は MIR では直接サポートされない
                    // （ビット演算として処理されるか、条件分岐で実装される）
                    case MirBinaryOp::Eq:
                        result.value = (*lhs_bool == *rhs_bool);
                        return result;
                    case MirBinaryOp::Ne:
                        result.value = (*lhs_bool != *rhs_bool);
                        return result;
                    default:
                        break;
                }
            }
        }

        // TODO: 浮動小数点演算

        return std::nullopt;
    }

    // 単項演算を評価
    std::optional<MirConstant> eval_unary_op(MirUnaryOp op, const MirConstant& operand) {
        MirConstant result;
        result.type = operand.type;

        if (auto* int_val = std::get_if<int64_t>(&operand.value)) {
            switch (op) {
                case MirUnaryOp::Neg:
                    result.value = -*int_val;
                    return result;
                case MirUnaryOp::BitNot:
                    result.value = ~*int_val;
                    return result;
                default:
                    break;
            }
        }

        if (auto* bool_val = std::get_if<bool>(&operand.value)) {
            if (op == MirUnaryOp::Not) {
                result.value = !*bool_val;
                return result;
            }
        }

        return std::nullopt;
    }

    // 型変換を評価
    std::optional<MirConstant> eval_cast(const MirConstant& operand,
                                         const hir::TypePtr& target_type) {
        MirConstant result;
        result.type = target_type;

        // Int -> Double
        if (target_type->kind == hir::TypeKind::Float) {
            if (auto* int_val = std::get_if<int64_t>(&operand.value)) {
                result.value = static_cast<double>(*int_val);
                return result;
            }
        }

        // Double -> Int
        if (target_type->kind == hir::TypeKind::Int) {
            if (auto* double_val = std::get_if<double>(&operand.value)) {
                result.value = static_cast<int64_t>(*double_val);
                return result;
            }
        }

        // Int -> Char
        if (target_type->kind == hir::TypeKind::Char) {
            if (auto* int_val = std::get_if<int64_t>(&operand.value)) {
                result.value = static_cast<char>(*int_val);
                return result;
            }
        }

        // Char -> Int
        if (target_type->kind == hir::TypeKind::Int) {
            if (auto* char_val = std::get_if<char>(&operand.value)) {
                result.value = static_cast<int64_t>(*char_val);
                return result;
            }
        }

        // Int -> Bool
        if (target_type->kind == hir::TypeKind::Bool) {
            if (auto* int_val = std::get_if<int64_t>(&operand.value)) {
                result.value = (*int_val != 0);
                return result;
            }
        }

        // Bool -> Int
        if (target_type->kind == hir::TypeKind::Int) {
            if (auto* bool_val = std::get_if<bool>(&operand.value)) {
                result.value = static_cast<int64_t>(*bool_val ? 1 : 0);
                return result;
            }
        }

        return std::nullopt;
    }
};

}  // namespace cm::mir::opt