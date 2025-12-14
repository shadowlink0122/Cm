#pragma once

#include "types.hpp"

namespace cm::mir::interp {

/// 式・値の評価を担当するクラス
class Evaluator {
   public:
    /// 定数を値に変換
    static Value constant_to_value(const MirConstant& constant) {
        // 型情報がある場合、それに基づいて値を変換
        if (constant.type) {
            if (constant.type->kind == hir::TypeKind::Char) {
                // char型の場合、int64_tからcharに変換
                if (std::holds_alternative<int64_t>(constant.value)) {
                    return Value(static_cast<char>(std::get<int64_t>(constant.value)));
                } else if (std::holds_alternative<char>(constant.value)) {
                    return Value(std::get<char>(constant.value));
                }
            }
        }

        // デフォルトの変換
        return std::visit(
            [](auto&& val) -> Value {
                using T = std::decay_t<decltype(val)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    return Value{};
                } else if constexpr (std::is_same_v<T, bool>) {
                    return Value(val);
                } else if constexpr (std::is_same_v<T, int64_t>) {
                    return Value(val);
                } else if constexpr (std::is_same_v<T, double>) {
                    return Value(val);
                } else if constexpr (std::is_same_v<T, char>) {
                    return Value(val);
                } else if constexpr (std::is_same_v<T, std::string>) {
                    return Value(val);
                }
                return Value{};
            },
            constant.value);
    }

    /// Place から値をロード
    static Value load_from_place(ExecutionContext& ctx, const MirPlace& place) {
        auto it = ctx.locals.find(place.local);
        if (it == ctx.locals.end()) {
            return Value{};
        }

        Value result = it->second;

        // プロジェクションを適用
        for (const auto& proj : place.projections) {
            if (proj.kind == ProjectionKind::Field) {
                if (result.type() == typeid(StructValue)) {
                    auto& struct_val = std::any_cast<StructValue&>(result);
                    auto field_it = struct_val.fields.find(proj.field_id);
                    if (field_it != struct_val.fields.end()) {
                        result = field_it->second;
                    } else {
                        return Value{};
                    }
                } else {
                    return Value{};
                }
            } else if (proj.kind == ProjectionKind::Index) {
                // 配列インデックスアクセス
                // インデックスをローカル変数から取得
                auto idx_it = ctx.locals.find(proj.index_local);
                if (idx_it == ctx.locals.end()) {
                    return Value{};
                }
                
                int64_t index = 0;
                if (idx_it->second.type() == typeid(int64_t)) {
                    index = std::any_cast<int64_t>(idx_it->second);
                } else if (idx_it->second.type() == typeid(int)) {
                    index = std::any_cast<int>(idx_it->second);
                }
                
                if (result.type() == typeid(ArrayValue)) {
                    auto& arr = std::any_cast<ArrayValue&>(result);
                    if (index >= 0 && static_cast<size_t>(index) < arr.elements.size()) {
                        result = arr.elements[index];
                    } else {
                        return Value{};  // 範囲外
                    }
                } else {
                    return Value{};
                }
            } else if (proj.kind == ProjectionKind::Deref) {
                // ポインタのデリファレンス
                if (result.type() == typeid(PointerValue)) {
                    auto& ptr = std::any_cast<PointerValue&>(result);
                    // ターゲットローカル変数から値を取得
                    auto target_it = ctx.locals.find(ptr.target_local);
                    if (target_it != ctx.locals.end()) {
                        // 配列要素への参照の場合
                        if (ptr.array_index.has_value()) {
                            if (target_it->second.type() == typeid(ArrayValue)) {
                                auto& arr = std::any_cast<ArrayValue&>(target_it->second);
                                int64_t idx = ptr.array_index.value();
                                if (idx >= 0 && static_cast<size_t>(idx) < arr.elements.size()) {
                                    result = arr.elements[idx];
                                } else {
                                    return Value{};
                                }
                            } else {
                                return Value{};
                            }
                        } else {
                            result = target_it->second;
                        }
                    } else {
                        return Value{};
                    }
                } else {
                    return Value{};
                }
            }
        }

        return result;
    }

    /// Place に値を格納
    static void store_to_place(ExecutionContext& ctx, const MirPlace& place, Value value) {
        if (place.projections.empty()) {
            // 直接格納
            ctx.locals[place.local] = value;
        } else {
            // プロジェクションがある場合
            auto it = ctx.locals.find(place.local);
            if (it == ctx.locals.end()) {
                // 新しいStructValueまたはArrayValueを作成
                // 最初のプロジェクションの種類で判断
                if (place.projections[0].kind == ProjectionKind::Index) {
                    ArrayValue av;
                    av.elements.resize(100);  // デフォルトサイズ
                    ctx.locals[place.local] = Value(av);
                } else {
                    StructValue sv;
                    ctx.locals[place.local] = Value(sv);
                }
                it = ctx.locals.find(place.local);
            }

            // プロジェクションを辿って格納先を見つける
            Value* current = &it->second;
            for (size_t i = 0; i < place.projections.size() - 1; ++i) {
                const auto& proj = place.projections[i];
                if (proj.kind == ProjectionKind::Field) {
                    if (current->type() == typeid(StructValue)) {
                        auto& sv = std::any_cast<StructValue&>(*current);
                        current = &sv.fields[proj.field_id];
                    }
                } else if (proj.kind == ProjectionKind::Index) {
                    auto idx_it = ctx.locals.find(proj.index_local);
                    if (idx_it != ctx.locals.end()) {
                        int64_t index = 0;
                        if (idx_it->second.type() == typeid(int64_t)) {
                            index = std::any_cast<int64_t>(idx_it->second);
                        } else if (idx_it->second.type() == typeid(int)) {
                            index = std::any_cast<int>(idx_it->second);
                        }
                        if (current->type() == typeid(ArrayValue)) {
                            auto& arr = std::any_cast<ArrayValue&>(*current);
                            if (static_cast<size_t>(index) >= arr.elements.size()) {
                                arr.elements.resize(index + 1);
                            }
                            current = &arr.elements[index];
                        }
                    }
                } else if (proj.kind == ProjectionKind::Deref) {
                    // ポインタのデリファレンス - 参照先に移動
                    if (current->type() == typeid(PointerValue)) {
                        auto& ptr = std::any_cast<PointerValue&>(*current);
                        auto target_it = ctx.locals.find(ptr.target_local);
                        if (target_it != ctx.locals.end()) {
                            current = &target_it->second;
                        }
                    }
                }
            }

            // 最後のプロジェクションで格納
            const auto& last_proj = place.projections.back();
            if (last_proj.kind == ProjectionKind::Field) {
                if (current->type() == typeid(StructValue)) {
                    auto& sv = std::any_cast<StructValue&>(*current);
                    sv.fields[last_proj.field_id] = value;
                }
            } else if (last_proj.kind == ProjectionKind::Index) {
                // 配列インデックスへの格納
                auto idx_it = ctx.locals.find(last_proj.index_local);
                if (idx_it != ctx.locals.end()) {
                    int64_t index = 0;
                    if (idx_it->second.type() == typeid(int64_t)) {
                        index = std::any_cast<int64_t>(idx_it->second);
                    } else if (idx_it->second.type() == typeid(int)) {
                        index = std::any_cast<int>(idx_it->second);
                    }
                    if (current->type() == typeid(ArrayValue)) {
                        auto& arr = std::any_cast<ArrayValue&>(*current);
                        if (static_cast<size_t>(index) >= arr.elements.size()) {
                            arr.elements.resize(index + 1);
                        }
                        arr.elements[index] = value;
                    }
                }
            } else if (last_proj.kind == ProjectionKind::Deref) {
                // ポインタのデリファレンスへの格納
                if (current->type() == typeid(PointerValue)) {
                    auto& ptr = std::any_cast<PointerValue&>(*current);
                    // 配列要素への参照の場合
                    if (ptr.array_index.has_value()) {
                        auto target_it = ctx.locals.find(ptr.target_local);
                        if (target_it != ctx.locals.end() && 
                            target_it->second.type() == typeid(ArrayValue)) {
                            auto& arr = std::any_cast<ArrayValue&>(target_it->second);
                            int64_t idx = ptr.array_index.value();
                            if (idx >= 0) {
                                if (static_cast<size_t>(idx) >= arr.elements.size()) {
                                    arr.elements.resize(idx + 1);
                                }
                                arr.elements[idx] = value;
                            }
                        }
                    } else {
                        ctx.locals[ptr.target_local] = value;
                    }
                }
            }
        }
    }

    /// オペランドを評価
    static Value evaluate_operand(ExecutionContext& ctx, const MirOperand& operand) {
        switch (operand.kind) {
            case MirOperand::Move:
            case MirOperand::Copy: {
                const MirPlace& place = std::get<MirPlace>(operand.data);
                return load_from_place(ctx, place);
            }
            case MirOperand::Constant: {
                const MirConstant& constant = std::get<MirConstant>(operand.data);
                return constant_to_value(constant);
            }
            default:
                return Value{};
        }
    }

    /// 二項演算を評価
    static Value evaluate_binary_op(MirBinaryOp op, const Value& lhs, const Value& rhs) {
        // 整数演算
        if (lhs.type() == typeid(int64_t) && rhs.type() == typeid(int64_t)) {
            int64_t l = std::any_cast<int64_t>(lhs);
            int64_t r = std::any_cast<int64_t>(rhs);

            switch (op) {
                case MirBinaryOp::Add:
                    return Value(l + r);
                case MirBinaryOp::Sub:
                    return Value(l - r);
                case MirBinaryOp::Mul:
                    return Value(l * r);
                case MirBinaryOp::Div:
                    return r != 0 ? Value(l / r) : Value(int64_t(0));
                case MirBinaryOp::Mod:
                    return r != 0 ? Value(l % r) : Value(int64_t(0));
                case MirBinaryOp::BitAnd:
                    return Value(l & r);
                case MirBinaryOp::BitOr:
                    return Value(l | r);
                case MirBinaryOp::BitXor:
                    return Value(l ^ r);
                case MirBinaryOp::Shl:
                    return Value(l << r);
                case MirBinaryOp::Shr:
                    return Value(l >> r);
                case MirBinaryOp::Eq:
                    return Value(l == r);
                case MirBinaryOp::Ne:
                    return Value(l != r);
                case MirBinaryOp::Lt:
                    return Value(l < r);
                case MirBinaryOp::Le:
                    return Value(l <= r);
                case MirBinaryOp::Gt:
                    return Value(l > r);
                case MirBinaryOp::Ge:
                    return Value(l >= r);
                case MirBinaryOp::And:
                    return Value(l && r);
                case MirBinaryOp::Or:
                    return Value(l || r);
            }
        }

        // 浮動小数点演算
        if (lhs.type() == typeid(double) && rhs.type() == typeid(double)) {
            double l = std::any_cast<double>(lhs);
            double r = std::any_cast<double>(rhs);

            switch (op) {
                case MirBinaryOp::Add:
                    return Value(l + r);
                case MirBinaryOp::Sub:
                    return Value(l - r);
                case MirBinaryOp::Mul:
                    return Value(l * r);
                case MirBinaryOp::Div:
                    return Value(l / r);
                case MirBinaryOp::Eq:
                    return Value(l == r);
                case MirBinaryOp::Ne:
                    return Value(l != r);
                case MirBinaryOp::Lt:
                    return Value(l < r);
                case MirBinaryOp::Le:
                    return Value(l <= r);
                case MirBinaryOp::Gt:
                    return Value(l > r);
                case MirBinaryOp::Ge:
                    return Value(l >= r);
                default:
                    break;
            }
        }

        // bool演算
        if (lhs.type() == typeid(bool) && rhs.type() == typeid(bool)) {
            bool l = std::any_cast<bool>(lhs);
            bool r = std::any_cast<bool>(rhs);

            switch (op) {
                case MirBinaryOp::And:
                    return Value(l && r);
                case MirBinaryOp::Or:
                    return Value(l || r);
                case MirBinaryOp::Eq:
                    return Value(l == r);
                case MirBinaryOp::Ne:
                    return Value(l != r);
                default:
                    break;
            }
        }

        // 文字列連結
        if (op == MirBinaryOp::Add && lhs.type() == typeid(std::string) &&
            rhs.type() == typeid(std::string)) {
            return Value(std::any_cast<std::string>(lhs) + std::any_cast<std::string>(rhs));
        }

        // char演算
        if (lhs.type() == typeid(char) && rhs.type() == typeid(char)) {
            char l = std::any_cast<char>(lhs);
            char r = std::any_cast<char>(rhs);

            switch (op) {
                case MirBinaryOp::Eq:
                    return Value(l == r);
                case MirBinaryOp::Ne:
                    return Value(l != r);
                case MirBinaryOp::Lt:
                    return Value(l < r);
                case MirBinaryOp::Le:
                    return Value(l <= r);
                case MirBinaryOp::Gt:
                    return Value(l > r);
                case MirBinaryOp::Ge:
                    return Value(l >= r);
                default:
                    break;
            }
        }

        return Value{};
    }

    /// 単項演算を評価
    static Value evaluate_unary_op(MirUnaryOp op, const Value& operand) {
        if (operand.type() == typeid(int64_t)) {
            int64_t val = std::any_cast<int64_t>(operand);
            switch (op) {
                case MirUnaryOp::Neg:
                    return Value(-val);
                case MirUnaryOp::Not:
                    return Value(!val);
                case MirUnaryOp::BitNot:
                    return Value(~val);
            }
        }

        if (operand.type() == typeid(double)) {
            double val = std::any_cast<double>(operand);
            switch (op) {
                case MirUnaryOp::Neg:
                    return Value(-val);
                default:
                    break;
            }
        }

        if (operand.type() == typeid(bool)) {
            bool val = std::any_cast<bool>(operand);
            switch (op) {
                case MirUnaryOp::Not:
                    return Value(!val);
                default:
                    break;
            }
        }

        return Value{};
    }

    /// Rvalue を評価
    static Value evaluate_rvalue(ExecutionContext& ctx, const MirRvalue& rvalue) {
        switch (rvalue.kind) {
            case MirRvalue::Use: {
                auto& data = std::get<MirRvalue::UseData>(rvalue.data);
                return data.operand ? evaluate_operand(ctx, *data.operand) : Value{};
            }
            case MirRvalue::BinaryOp: {
                auto& data = std::get<MirRvalue::BinaryOpData>(rvalue.data);
                Value lhs = evaluate_operand(ctx, *data.lhs);
                Value rhs = evaluate_operand(ctx, *data.rhs);
                return evaluate_binary_op(data.op, lhs, rhs);
            }
            case MirRvalue::UnaryOp: {
                auto& data = std::get<MirRvalue::UnaryOpData>(rvalue.data);
                Value operand = evaluate_operand(ctx, *data.operand);
                return evaluate_unary_op(data.op, operand);
            }
            case MirRvalue::Ref: {
                // アドレス取得：PointerValueを作成
                auto& data = std::get<MirRvalue::RefData>(rvalue.data);
                PointerValue ptr;
                ptr.target_local = data.place.local;
                
                // プロジェクションがある場合（配列要素への参照など）
                if (!data.place.projections.empty()) {
                    for (const auto& proj : data.place.projections) {
                        if (proj.kind == ProjectionKind::Index) {
                            // インデックスをローカル変数から取得
                            auto idx_it = ctx.locals.find(proj.index_local);
                            if (idx_it != ctx.locals.end()) {
                                if (idx_it->second.type() == typeid(int64_t)) {
                                    ptr.array_index = std::any_cast<int64_t>(idx_it->second);
                                } else if (idx_it->second.type() == typeid(int)) {
                                    ptr.array_index = std::any_cast<int>(idx_it->second);
                                }
                            }
                        }
                    }
                }
                
                return Value(ptr);
            }
            default:
                return Value{};
        }
    }
};

}  // namespace cm::mir::interp
