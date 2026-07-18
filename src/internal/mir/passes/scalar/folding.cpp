#include "folding.hpp"

#include "const_eval.hpp"

namespace cm::mir::opt {

bool ConstantFolding::run(MirFunction& func) {
    bool changed = false;

    // 複数回代入される変数を検出（ループ変数など）
    auto multiAssigned = detect_multi_assigned(func);

    // 関数引数は定数追跡から除外（呼び出し元から任意の値が渡される）
    for (LocalId arg : func.arg_locals) {
        multiAssigned.insert(arg);
    }

    // 各基本ブロックを処理
    // 注意: 定数情報はブロック単位で管理（ブロック間の伝播は行わない）
    // これは保守的だが安全なアプローチ（SCCPがグローバルな定数伝播を担当）
    for (auto& block : func.basic_blocks) {
        if (block) {
            // 各ブロックの開始時に定数情報をクリア
            std::unordered_map<LocalId, MirConstant> constants;
            changed |= process_block(func, *block, constants, multiAssigned);
        }
    }

    return changed;
}

std::unordered_set<LocalId> ConstantFolding::detect_multi_assigned(const MirFunction& func) {
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
            // ASM出力制約（=r, +r等）も代入としてカウント（Bug1修正）
            if (stmt->kind == MirStatement::Asm) {
                const auto& asm_data = std::get<MirStatement::AsmData>(stmt->data);
                for (const auto& operand : asm_data.operands) {
                    if (!operand.constraint.empty() &&
                        (operand.constraint[0] == '+' || operand.constraint[0] == '=')) {
                        if (assigned.count(operand.local_id) > 0) {
                            multiAssigned.insert(operand.local_id);
                        } else {
                            assigned.insert(operand.local_id);
                        }
                    }
                }
            }
        }
    }
    return multiAssigned;
}

bool ConstantFolding::process_block(const MirFunction& func, BasicBlock& block,
                                    std::unordered_map<LocalId, MirConstant>& constants,
                                    const std::unordered_set<LocalId>& multiAssigned) {
    bool changed = false;

    // 各文を処理
    for (auto& stmt : block.statements) {
        // ASMステートメント: no_optフラグに関わらず、出力オペランドの変数は常に定数追跡から除外する必要がある（インラインアセンブリは実行時に変数を変更するため）
        if (stmt->kind == MirStatement::Asm) {
            const auto& asm_data = std::get<MirStatement::AsmData>(stmt->data);
            for (const auto& operand : asm_data.operands) {
                if (!operand.constraint.empty() &&
                    (operand.constraint[0] == '+' || operand.constraint[0] == '=')) {
                    constants.erase(operand.local_id);
                }
            }
            continue;
        }

        // no_optフラグがtrueの場合は最適化スキップ
        if (stmt->no_opt) {
            // mustブロック内の代入は定数追跡から除外
            if (stmt->kind == MirStatement::Assign) {
                auto& assign_data = std::get<MirStatement::AssignData>(stmt->data);
                if (assign_data.place.projections.empty()) {
                    constants.erase(assign_data.place.local);
                }
            }
            continue;
        }

        if (stmt->kind == MirStatement::Assign) {
            auto& assign_data = std::get<MirStatement::AssignData>(stmt->data);

            // デリファレンス書き込みチェック（_p.* = ... の形式）
            // エイリアスの可能性があるため、全ての定数情報をクリアする
            bool has_deref = false;
            for (const auto& proj : assign_data.place.projections) {
                if (proj.kind == ProjectionKind::Deref) {
                    has_deref = true;
                    break;
                }
            }
            if (has_deref) {
                // ポインタ経由の書き込みは任意のローカル変数に影響する可能性がある
                // 保守的にすべての定数情報をクリア
                constants.clear();
                continue;
            }

            // フィールドやインデックスへの代入の場合
            // ベース変数に関する定数情報を無効化
            if (!assign_data.place.projections.empty()) {
                constants.erase(assign_data.place.local);
                continue;
            }

            // 単純な代入（_x = _y）の場合
            LocalId target = assign_data.place.local;

            // 複数回代入される変数は定数追跡から除外
            if (multiAssigned.count(target) > 0) {
                constants.erase(target);
                continue;
            }

            // Rvalueを評価して定数化できるかチェック
            auto folded = evaluate_rvalue(*assign_data.rvalue, constants);
            if (!folded) {
                // 完全畳み込みできない場合は代数的恒等式の簡約を試みる（x*1→x, x+0→x, x*0→0 等。文数・制御フローは変えない）
                if (simplify_identity(assign_data, constants)) {
                    changed = true;
                    // x*0→0 等、簡約結果が定数になったケースを追跡へ乗せる
                    folded = evaluate_rvalue(*assign_data.rvalue, constants);
                }
            }
            if (auto constant = folded) {
                // 代入先ローカルの型幅に正規化（狭い型への代入はラップ）
                if (target < func.locals.size()) {
                    const auto& local_type = func.locals[target].type;
                    if (const_eval::integer_bit_width(local_type) > 0) {
                        if (auto* int_val = std::get_if<int64_t>(&constant->value)) {
                            constant->value = const_eval::normalize_int(*int_val, local_type);
                            constant->type = local_type;
                        }
                    }
                }

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

    // 終端命令の定数畳み込み（fold_terminators_=falseの場合はCFG形状を保持）
    if (fold_terminators_ && block.terminator &&
        block.terminator->kind == MirTerminator::SwitchInt) {
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

std::optional<MirConstant> ConstantFolding::evaluate_rvalue(
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
                return eval_binary_op(bin_data.op, *lhs, *rhs, bin_data.result_type);
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

            // ポインタ型への変換は定数畳み込みしない
            // ポインタ型変換は実行時のアドレスに依存するため
            if (cast_data.target_type && cast_data.target_type->kind == hir::TypeKind::Pointer) {
                break;
            }

            // オペランドを評価
            auto operand = evaluate_operand(*cast_data.operand, constants);
            if (!operand) {
                break;
            }

            // 型変換を実行（ポインタ型以外）
            return eval_cast(*operand, cast_data.target_type);
        }

        default:
            break;
    }

    return std::nullopt;
}

std::optional<MirConstant> ConstantFolding::evaluate_operand(
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

std::optional<MirConstant> ConstantFolding::eval_binary_op(MirBinaryOp op, const MirConstant& lhs,
                                                           const MirConstant& rhs,
                                                           const hir::TypePtr& result_type) {
    // Void型（nullリテラル）の演算は定数畳み込みしない
    // nullable型のnull比較は実行時に行う必要がある
    if ((lhs.type && lhs.type->kind == hir::TypeKind::Void) ||
        (rhs.type && rhs.type->kind == hir::TypeKind::Void)) {
        return std::nullopt;
    }

    // 整数演算
    if (auto* lhs_int = std::get_if<int64_t>(&lhs.value)) {
        if (auto* rhs_int = std::get_if<int64_t>(&rhs.value)) {
            MirConstant result;
            // 結果型: MIRのresult_typeを優先し、無ければ幅の広い方へ昇格
            result.type = (result_type && const_eval::integer_bit_width(result_type) > 0)
                              ? result_type
                              : const_eval::promote_types(lhs.type, rhs.type);
            // 型幅に正規化した値で演算する（オーバーフローは2の補数でラップ）
            const int64_t lv = const_eval::normalize_int(*lhs_int, lhs.type);
            const int64_t rv = const_eval::normalize_int(*rhs_int, rhs.type);
            // どちらかが符号なし型なら比較・除算・剰余・右シフトは符号なしで行う
            const bool uns = const_eval::use_unsigned_op(lhs.type, rhs.type);
            const uint64_t ulv = static_cast<uint64_t>(lv);
            const uint64_t urv = static_cast<uint64_t>(rv);

            switch (op) {
                case MirBinaryOp::Add:
                    result.value = const_eval::normalize_int(lv + rv, result.type);
                    return result;
                case MirBinaryOp::Sub:
                    result.value = const_eval::normalize_int(lv - rv, result.type);
                    return result;
                case MirBinaryOp::Mul:
                    result.value = const_eval::normalize_int(lv * rv, result.type);
                    return result;
                case MirBinaryOp::Div:
                    if (rv != 0) {
                        result.value = const_eval::normalize_int(
                            uns ? static_cast<int64_t>(ulv / urv) : lv / rv, result.type);
                        return result;
                    }
                    break;
                case MirBinaryOp::Mod:
                    if (rv != 0) {
                        result.value = const_eval::normalize_int(
                            uns ? static_cast<int64_t>(ulv % urv) : lv % rv, result.type);
                        return result;
                    }
                    break;
                case MirBinaryOp::BitAnd:
                    result.value = const_eval::normalize_int(lv & rv, result.type);
                    return result;
                case MirBinaryOp::BitOr:
                    result.value = const_eval::normalize_int(lv | rv, result.type);
                    return result;
                case MirBinaryOp::BitXor:
                    result.value = const_eval::normalize_int(lv ^ rv, result.type);
                    return result;
                case MirBinaryOp::Shl:
                    result.value = const_eval::normalize_int(
                        static_cast<int64_t>(ulv << (urv & 63)), result.type);
                    return result;
                case MirBinaryOp::Shr:
                    // 符号なし型は論理シフト、符号付き型は算術シフト
                    result.value = const_eval::normalize_int(
                        uns ? static_cast<int64_t>(ulv >> (urv & 63)) : lv >> (rv & 63),
                        result.type);
                    return result;

                // 比較演算（bool結果）
                case MirBinaryOp::Eq:
                    result.value = (lv == rv);
                    return result;
                case MirBinaryOp::Ne:
                    result.value = (lv != rv);
                    return result;
                case MirBinaryOp::Lt:
                    result.value = uns ? (ulv < urv) : (lv < rv);
                    return result;
                case MirBinaryOp::Le:
                    result.value = uns ? (ulv <= urv) : (lv <= rv);
                    return result;
                case MirBinaryOp::Gt:
                    result.value = uns ? (ulv > urv) : (lv > rv);
                    return result;
                case MirBinaryOp::Ge:
                    result.value = uns ? (ulv >= urv) : (lv >= rv);
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

    // 浮動小数点演算
    if (auto* lhs_double = std::get_if<double>(&lhs.value)) {
        if (auto* rhs_double = std::get_if<double>(&rhs.value)) {
            MirConstant result;
            result.type = lhs.type;

            switch (op) {
                case MirBinaryOp::Add:
                    result.value = *lhs_double + *rhs_double;
                    return result;
                case MirBinaryOp::Sub:
                    result.value = *lhs_double - *rhs_double;
                    return result;
                case MirBinaryOp::Mul:
                    result.value = *lhs_double * *rhs_double;
                    return result;
                case MirBinaryOp::Div:
                    if (*rhs_double != 0.0) {
                        result.value = *lhs_double / *rhs_double;
                        return result;
                    }
                    break;
                // 比較演算
                case MirBinaryOp::Eq:
                    result.value = (*lhs_double == *rhs_double);
                    return result;
                case MirBinaryOp::Ne:
                    result.value = (*lhs_double != *rhs_double);
                    return result;
                case MirBinaryOp::Lt:
                    result.value = (*lhs_double < *rhs_double);
                    return result;
                case MirBinaryOp::Le:
                    result.value = (*lhs_double <= *rhs_double);
                    return result;
                case MirBinaryOp::Gt:
                    result.value = (*lhs_double > *rhs_double);
                    return result;
                case MirBinaryOp::Ge:
                    result.value = (*lhs_double >= *rhs_double);
                    return result;
                default:
                    break;
            }
        }
    }

    return std::nullopt;
}

std::optional<MirConstant> ConstantFolding::eval_unary_op(MirUnaryOp op,
                                                          const MirConstant& operand) {
    MirConstant result;
    result.type = operand.type;

    if (auto* int_val = std::get_if<int64_t>(&operand.value)) {
        switch (op) {
            case MirUnaryOp::Neg:
                result.value = const_eval::normalize_int(-*int_val, result.type);
                return result;
            case MirUnaryOp::BitNot:
                result.value = const_eval::normalize_int(~*int_val, result.type);
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

    if (auto* double_val = std::get_if<double>(&operand.value)) {
        if (op == MirUnaryOp::Neg) {
            result.value = -*double_val;
            return result;
        }
    }

    return std::nullopt;
}

std::optional<MirConstant> ConstantFolding::eval_cast(const MirConstant& operand,
                                                      const hir::TypePtr& target_type) {
    MirConstant result;
    result.type = target_type;

    // 整数 -> 整数: ターゲット型の幅・符号に正規化してキャスト（例: utiny 255 as int → 255、tiny -1 as utiny → 255、int 300 as utiny → 44）
    if (const_eval::integer_bit_width(target_type) > 0) {
        if (auto* int_val = std::get_if<int64_t>(&operand.value)) {
            result.value = const_eval::normalize_int(*int_val, target_type);
            return result;
        }
    }

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

bool ConstantFolding::simplify_identity(MirStatement::AssignData& assign_data,
                                        const std::unordered_map<LocalId, MirConstant>& constants) {
    if (!assign_data.rvalue || assign_data.rvalue->kind != MirRvalue::BinaryOp) {
        return false;
    }
    auto& bin = std::get<MirRvalue::BinaryOpData>(assign_data.rvalue->data);
    if (!bin.lhs || !bin.rhs) {
        return false;
    }
    // 整数型のみ対象（浮動小数点はNaN・-0.0の意味論があるため対象外）
    if (const_eval::integer_bit_width(bin.result_type) <= 0) {
        return false;
    }

    auto lhs_const = evaluate_operand(*bin.lhs, constants);
    auto rhs_const = evaluate_operand(*bin.rhs, constants);
    auto int_of = [](const std::optional<MirConstant>& c) -> std::optional<int64_t> {
        if (!c) {
            return std::nullopt;
        }
        if (const auto* v = std::get_if<int64_t>(&c->value)) {
            return *v;
        }
        return std::nullopt;
    };
    auto lv = int_of(lhs_const);
    auto rv = int_of(rhs_const);
    if (!lv && !rv) {
        return false;
    }

    // rvalue全体を「もう一方のオペランド」に置き換える
    auto replace_with_operand = [&](MirOperandPtr& keep) {
        assign_data.rvalue = MirRvalue::use(std::move(keep));
    };
    // rvalue全体を整数定数に置き換える
    auto replace_with_const = [&](int64_t value) {
        MirConstant c;
        c.value = value;
        c.type = bin.result_type;
        assign_data.rvalue = MirRvalue::use(MirOperand::constant(std::move(c)));
    };

    switch (bin.op) {
        case MirBinaryOp::Add:
            if (rv && *rv == 0) {
                replace_with_operand(bin.lhs);
                return true;
            }
            if (lv && *lv == 0) {
                replace_with_operand(bin.rhs);
                return true;
            }
            return false;
        case MirBinaryOp::Sub:
            // x - 0 → x（0 - x は否定になるため対象外）
            if (rv && *rv == 0) {
                replace_with_operand(bin.lhs);
                return true;
            }
            return false;
        case MirBinaryOp::Mul:
            if ((rv && *rv == 1)) {
                replace_with_operand(bin.lhs);
                return true;
            }
            if (lv && *lv == 1) {
                replace_with_operand(bin.rhs);
                return true;
            }
            // オペランドはCopy/Constantのみで副作用が無いため x*0→0 は安全
            if ((rv && *rv == 0) || (lv && *lv == 0)) {
                replace_with_const(0);
                return true;
            }
            return false;
        case MirBinaryOp::Div:
            // x / 1 → x（0 / x はxのゼロ検査を消すため対象外）
            if (rv && *rv == 1) {
                replace_with_operand(bin.lhs);
                return true;
            }
            return false;
        case MirBinaryOp::Mod:
            // x % 1 → 0（除数1は常に非ゼロで安全）
            if (rv && *rv == 1) {
                replace_with_const(0);
                return true;
            }
            return false;
        case MirBinaryOp::Shl:
        case MirBinaryOp::Shr:
            if (rv && *rv == 0) {
                replace_with_operand(bin.lhs);
                return true;
            }
            return false;
        case MirBinaryOp::BitOr:
        case MirBinaryOp::BitXor:
            if (rv && *rv == 0) {
                replace_with_operand(bin.lhs);
                return true;
            }
            if (lv && *lv == 0) {
                replace_with_operand(bin.rhs);
                return true;
            }
            return false;
        case MirBinaryOp::BitAnd:
            if ((rv && *rv == 0) || (lv && *lv == 0)) {
                replace_with_const(0);
                return true;
            }
            return false;
        default:
            return false;
    }
}

}  // namespace cm::mir::opt
