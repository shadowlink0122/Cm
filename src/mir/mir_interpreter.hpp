#pragma once

#include "mir_nodes.hpp"
#include <unordered_map>
#include <stack>
#include <any>
#include <iostream>
#include <functional>

namespace cm::mir {

// ============================================================
// MIRインタープリタ
// ============================================================
class MirInterpreter {
public:
    // 値の表現
    using Value = std::any;

    // 実行結果
    struct ExecutionResult {
        bool success;
        Value return_value;
        std::string error_message;
    };

    // MIRプログラムを実行
    ExecutionResult execute(const MirProgram& program, const std::string& entry_point = "main") {
        // main関数を探す
        const MirFunction* main_func = nullptr;
        for (const auto& func : program.functions) {
            if (func->name == entry_point) {
                main_func = func.get();
                break;
            }
        }

        if (!main_func) {
            return {false, Value{}, "エントリポイント '" + entry_point + "' が見つかりません"};
        }

        try {
            Value result = execute_function(*main_func, {});
            return {true, result, ""};
        } catch (const std::exception& e) {
            return {false, Value{}, e.what()};
        }
    }

private:
    // 実行コンテキスト
    struct ExecutionContext {
        const MirFunction* function;
        std::unordered_map<LocalId, Value> locals;  // ローカル変数の値
        std::unordered_map<std::string, std::function<Value(std::vector<Value>)>> builtins;  // 組み込み関数

        ExecutionContext(const MirFunction* func) : function(func) {
            // 組み込み関数を登録
            register_builtins();
        }

        void register_builtins() {
            // println関数
            builtins["println"] = [](std::vector<Value> args) -> Value {
                for (const auto& arg : args) {
                    if (arg.type() == typeid(std::string)) {
                        std::cout << std::any_cast<std::string>(arg);
                    } else if (arg.type() == typeid(int64_t)) {
                        std::cout << std::any_cast<int64_t>(arg);
                    } else if (arg.type() == typeid(double)) {
                        std::cout << std::any_cast<double>(arg);
                    } else if (arg.type() == typeid(bool)) {
                        std::cout << (std::any_cast<bool>(arg) ? "true" : "false");
                    }
                }
                std::cout << std::endl;
                return Value{};
            };

            // 算術関数
            builtins["sqrt"] = [](std::vector<Value> args) -> Value {
                if (!args.empty() && args[0].type() == typeid(double)) {
                    return Value(std::sqrt(std::any_cast<double>(args[0])));
                }
                return Value(0.0);
            };
        }
    };

    // 関数を実行
    Value execute_function(const MirFunction& func, std::vector<Value> args) {
        ExecutionContext ctx(&func);

        // 引数をローカル変数に設定
        for (size_t i = 0; i < args.size() && i < func.arg_locals.size(); i++) {
            ctx.locals[func.arg_locals[i]] = args[i];
        }

        // 戻り値用のローカル変数を初期化
        ctx.locals[func.return_local] = Value{};

        // エントリブロックから実行開始
        execute_block(ctx, func.entry_block);

        // 戻り値を返す
        return ctx.locals[func.return_local];
    }

    // 基本ブロックを実行
    void execute_block(ExecutionContext& ctx, BlockId block_id) {
        if (block_id >= ctx.function->basic_blocks.size()) {
            throw std::runtime_error("無効なブロックID: " + std::to_string(block_id));
        }

        const BasicBlock* block = ctx.function->basic_blocks[block_id].get();

        // ステートメントを順次実行
        for (const auto& stmt : block->statements) {
            execute_statement(ctx, *stmt);
        }

        // 終端命令を実行
        if (block->terminator) {
            execute_terminator(ctx, *block->terminator);
        }
    }

    // ステートメントを実行
    void execute_statement(ExecutionContext& ctx, const MirStatement& stmt) {
        switch (stmt.kind) {
            case MirStatement::Assign: {
                auto& data = std::get<MirStatement::AssignData>(stmt.data);
                Value value = evaluate_rvalue(ctx, *data.rvalue);
                store_to_place(ctx, data.place, value);
                break;
            }
            case MirStatement::StorageLive: {
                // 変数の有効範囲開始（インタープリタでは特に処理不要）
                break;
            }
            case MirStatement::StorageDead: {
                // 変数の有効範囲終了
                auto& data = std::get<MirStatement::StorageData>(stmt.data);
                ctx.locals.erase(data.local);
                break;
            }
            case MirStatement::Nop: {
                // 何もしない
                break;
            }
        }
    }

    // 終端命令を実行
    void execute_terminator(ExecutionContext& ctx, const MirTerminator& term) {
        switch (term.kind) {
            case MirTerminator::Goto: {
                auto& data = std::get<MirTerminator::GotoData>(term.data);
                execute_block(ctx, data.target);
                break;
            }
            case MirTerminator::SwitchInt: {
                auto& data = std::get<MirTerminator::SwitchIntData>(term.data);
                Value discriminant = evaluate_operand(ctx, *data.discriminant);

                // 条件分岐
                if (discriminant.type() == typeid(int64_t)) {
                    int64_t value = std::any_cast<int64_t>(discriminant);
                    for (const auto& [case_value, target] : data.targets) {
                        if (value == case_value) {
                            execute_block(ctx, target);
                            return;
                        }
                    }
                } else if (discriminant.type() == typeid(bool)) {
                    bool value = std::any_cast<bool>(discriminant);
                    for (const auto& [case_value, target] : data.targets) {
                        if ((case_value != 0) == value) {
                            execute_block(ctx, target);
                            return;
                        }
                    }
                }

                // デフォルトブランチ
                execute_block(ctx, data.otherwise);
                break;
            }
            case MirTerminator::Return: {
                // 関数から戻る（既に戻り値は_0に設定済み）
                break;
            }
            case MirTerminator::Call: {
                auto& data = std::get<MirTerminator::CallData>(term.data);

                // 関数名を取得（簡略化：定数オペランドから）
                if (data.func->kind == MirOperand::Constant) {
                    auto& constant = std::get<MirConstant>(data.func->data);
                    if (constant.value.index() == 8) {  // std::string
                        std::string func_name = std::any_cast<std::string>(
                            std::get<std::string>(constant.value));

                        // 引数を評価
                        std::vector<Value> args;
                        for (const auto& arg : data.args) {
                            args.push_back(evaluate_operand(ctx, *arg));
                        }

                        // 組み込み関数を呼び出し
                        if (ctx.builtins.count(func_name)) {
                            Value result = ctx.builtins[func_name](args);
                            if (data.destination) {
                                store_to_place(ctx, *data.destination, result);
                            }
                        }
                    }
                }

                // 次のブロックへ
                execute_block(ctx, data.success);
                break;
            }
            case MirTerminator::Unreachable: {
                throw std::runtime_error("到達不能コードに到達しました");
            }
        }
    }

    // Rvalueを評価
    Value evaluate_rvalue(ExecutionContext& ctx, const MirRvalue& rvalue) {
        switch (rvalue.kind) {
            case MirRvalue::Use: {
                auto& data = std::get<MirRvalue::UseData>(rvalue.data);
                return evaluate_operand(ctx, *data.operand);
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
            default:
                return Value{};
        }
    }

    // オペランドを評価
    Value evaluate_operand(ExecutionContext& ctx, const MirOperand& operand) {
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

    // 定数を値に変換
    Value constant_to_value(const MirConstant& constant) {
        return std::visit([](auto&& val) -> Value {
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
                return Value(static_cast<int64_t>(val));
            } else if constexpr (std::is_same_v<T, std::string>) {
                return Value(val);
            }
            return Value{};
        }, constant.value);
    }

    // 二項演算を評価
    Value evaluate_binary_op(MirBinaryOp op, Value lhs, Value rhs) {
        // 整数演算
        if (lhs.type() == typeid(int64_t) && rhs.type() == typeid(int64_t)) {
            int64_t l = std::any_cast<int64_t>(lhs);
            int64_t r = std::any_cast<int64_t>(rhs);

            switch (op) {
                case MirBinaryOp::Add: return Value(l + r);
                case MirBinaryOp::Sub: return Value(l - r);
                case MirBinaryOp::Mul: return Value(l * r);
                case MirBinaryOp::Div: return Value(l / r);
                case MirBinaryOp::Mod: return Value(l % r);
                case MirBinaryOp::Eq: return Value(l == r);
                case MirBinaryOp::Ne: return Value(l != r);
                case MirBinaryOp::Lt: return Value(l < r);
                case MirBinaryOp::Le: return Value(l <= r);
                case MirBinaryOp::Gt: return Value(l > r);
                case MirBinaryOp::Ge: return Value(l >= r);
                default: break;
            }
        }

        // 浮動小数点演算
        if (lhs.type() == typeid(double) && rhs.type() == typeid(double)) {
            double l = std::any_cast<double>(lhs);
            double r = std::any_cast<double>(rhs);

            switch (op) {
                case MirBinaryOp::Add: return Value(l + r);
                case MirBinaryOp::Sub: return Value(l - r);
                case MirBinaryOp::Mul: return Value(l * r);
                case MirBinaryOp::Div: return Value(l / r);
                default: break;
            }
        }

        return Value{};
    }

    // 単項演算を評価
    Value evaluate_unary_op(MirUnaryOp op, Value operand) {
        switch (op) {
            case MirUnaryOp::Neg:
                if (operand.type() == typeid(int64_t)) {
                    return Value(-std::any_cast<int64_t>(operand));
                } else if (operand.type() == typeid(double)) {
                    return Value(-std::any_cast<double>(operand));
                }
                break;
            case MirUnaryOp::Not:
                if (operand.type() == typeid(bool)) {
                    return Value(!std::any_cast<bool>(operand));
                }
                break;
            case MirUnaryOp::BitNot:
                if (operand.type() == typeid(int64_t)) {
                    return Value(~std::any_cast<int64_t>(operand));
                }
                break;
        }
        return Value{};
    }

    // Placeから値を読み込み
    Value load_from_place(ExecutionContext& ctx, const MirPlace& place) {
        auto it = ctx.locals.find(place.local);
        if (it != ctx.locals.end()) {
            return it->second;
        }
        return Value{};
    }

    // Placeに値を格納
    void store_to_place(ExecutionContext& ctx, const MirPlace& place, Value value) {
        ctx.locals[place.local] = value;
    }
};

}  // namespace cm::mir