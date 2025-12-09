#pragma once

#include "../common/format_string.hpp"
#include "mir_nodes.hpp"

#include <any>
#include <bitset>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stack>
#include <unordered_map>

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
        std::unordered_map<
            std::string,
            std::function<Value(std::vector<Value>, const std::unordered_map<LocalId, Value>&)>>
            builtins;  // 組み込み関数（ローカル変数へのアクセス付き）

        ExecutionContext(const MirFunction* func) : function(func) {
            // 組み込み関数を登録
            register_builtins();
        }

        void register_builtins() {
            // std::io::println関数 - 外部モジュールとして提供
            // printlnは直接使用不可、std::ioからインポートが必要
            builtins["std::io::println"] =
                [](std::vector<Value> args,
                   const std::unordered_map<LocalId, Value>& locals) -> Value {
                if (args.empty()) {
                    std::cout << std::endl;
                    return Value{};
                }

                // 第1引数はフォーマット文字列かチェック
                if (args[0].type() == typeid(std::string)) {
                    // フォーマット文字列モード
                    std::string format_str = std::any_cast<std::string>(args[0]);

                    // プレースホルダーがあるかチェック
                    if (format_str.find('{') != std::string::npos &&
                        format_str.find('}') != std::string::npos) {
                        // フォーマット引数を準備
                        std::vector<std::any> format_args;
                        for (size_t i = 1; i < args.size(); ++i) {
                            format_args.push_back(args[i]);
                        }

                        // フォーマット処理
                        std::string output = FormatStringFormatter::format(format_str, format_args);
                        std::cout << output << std::endl;
                        return Value{};
                    } else {
                        // プレースホルダーがない場合は単純出力
                        std::cout << format_str << std::endl;
                        return Value{};
                    }
                }

                // 後方互換性: 非文字列引数またはフォーマットなしの出力
                for (const auto& arg : args) {
                    if (arg.type() == typeid(std::string)) {
                        std::cout << std::any_cast<std::string>(arg);
                    } else if (arg.type() == typeid(int64_t)) {
                        std::cout << std::any_cast<int64_t>(arg);
                    } else if (arg.type() == typeid(int32_t)) {
                        std::cout << std::any_cast<int32_t>(arg);
                    } else if (arg.type() == typeid(double)) {
                        std::cout << std::any_cast<double>(arg);
                    } else if (arg.type() == typeid(float)) {
                        std::cout << std::any_cast<float>(arg);
                    } else if (arg.type() == typeid(bool)) {
                        std::cout << (std::any_cast<bool>(arg) ? "true" : "false");
                    }
                }
                std::cout << std::endl;

                return Value{};
            };

            // 算術関数
            builtins["sqrt"] = [](std::vector<Value> args,
                                  const std::unordered_map<LocalId, Value>&) -> Value {
                if (!args.empty() && args[0].type() == typeid(double)) {
                    return Value(std::sqrt(std::any_cast<double>(args[0])));
                }
                return Value(0.0);
            };

            // フォーマット変換関数

            // 16進数変換（小文字）
            builtins["toHex"] = [](std::vector<Value> args,
                                   const std::unordered_map<LocalId, Value>&) -> Value {
                if (!args.empty()) {
                    std::stringstream ss;
                    if (args[0].type() == typeid(int64_t)) {
                        ss << std::hex << std::any_cast<int64_t>(args[0]);
                    } else if (args[0].type() == typeid(int32_t)) {
                        ss << std::hex << std::any_cast<int32_t>(args[0]);
                    }
                    return Value(ss.str());
                }
                return Value(std::string(""));
            };

            // 16進数変換（大文字）
            builtins["toHexUpper"] = [](std::vector<Value> args,
                                        const std::unordered_map<LocalId, Value>&) -> Value {
                if (!args.empty()) {
                    std::stringstream ss;
                    if (args[0].type() == typeid(int64_t)) {
                        ss << std::hex << std::uppercase << std::any_cast<int64_t>(args[0]);
                    } else if (args[0].type() == typeid(int32_t)) {
                        ss << std::hex << std::uppercase << std::any_cast<int32_t>(args[0]);
                    }
                    return Value(ss.str());
                }
                return Value(std::string(""));
            };

            // 2進数変換
            builtins["toBin"] = [](std::vector<Value> args,
                                   const std::unordered_map<LocalId, Value>&) -> Value {
                if (!args.empty()) {
                    if (args[0].type() == typeid(int64_t)) {
                        return Value(std::string("0b") +
                                     std::bitset<64>(std::any_cast<int64_t>(args[0]))
                                         .to_string()
                                         .substr(std::bitset<64>(std::any_cast<int64_t>(args[0]))
                                                     .to_string()
                                                     .find('1')));
                    } else if (args[0].type() == typeid(int32_t)) {
                        auto val = std::any_cast<int32_t>(args[0]);
                        if (val == 0)
                            return Value(std::string("0b0"));
                        std::string binary = std::bitset<32>(val).to_string();
                        size_t firstOne = binary.find('1');
                        if (firstOne != std::string::npos) {
                            return Value(std::string("0b") + binary.substr(firstOne));
                        }
                    }
                }
                return Value(std::string("0b0"));
            };

            // 8進数変換
            builtins["toOct"] = [](std::vector<Value> args,
                                   const std::unordered_map<LocalId, Value>&) -> Value {
                if (!args.empty()) {
                    std::stringstream ss;
                    if (args[0].type() == typeid(int64_t)) {
                        ss << std::oct << std::any_cast<int64_t>(args[0]);
                    } else if (args[0].type() == typeid(int32_t)) {
                        ss << std::oct << std::any_cast<int32_t>(args[0]);
                    }
                    return Value(ss.str());
                }
                return Value(std::string(""));
            };

            // 文字列変換（汎用）
            builtins["toString"] = [](std::vector<Value> args,
                                      const std::unordered_map<LocalId, Value>&) -> Value {
                if (!args.empty()) {
                    if (args[0].type() == typeid(std::string)) {
                        return args[0];
                    } else if (args[0].type() == typeid(int64_t)) {
                        return Value(std::to_string(std::any_cast<int64_t>(args[0])));
                    } else if (args[0].type() == typeid(int32_t)) {
                        return Value(std::to_string(std::any_cast<int32_t>(args[0])));
                    } else if (args[0].type() == typeid(double)) {
                        return Value(std::to_string(std::any_cast<double>(args[0])));
                    } else if (args[0].type() == typeid(float)) {
                        return Value(std::to_string(std::any_cast<float>(args[0])));
                    } else if (args[0].type() == typeid(bool)) {
                        return Value(std::string(std::any_cast<bool>(args[0]) ? "true" : "false"));
                    }
                }
                return Value(std::string(""));
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

                std::string func_name;

                // 関数名を取得（FunctionRefまたはConstantから）
                if (data.func->kind == MirOperand::FunctionRef) {
                    // 新しいFunctionRef形式
                    if (auto* str_ptr = std::get_if<std::string>(&data.func->data)) {
                        func_name = *str_ptr;
                    }
                } else if (data.func->kind == MirOperand::Constant) {
                    // 後方互換性のためConstantもサポート
                    auto& constant = std::get<MirConstant>(data.func->data);
                    if (auto* str_ptr = std::get_if<std::string>(&constant.value)) {
                        func_name = *str_ptr;
                    }
                }

                if (!func_name.empty()) {
                    // 引数を評価
                    std::vector<Value> args;
                    for (const auto& arg : data.args) {
                        args.push_back(evaluate_operand(ctx, *arg));
                    }

                    // デバッグ出力
                    // std::cerr << "Calling function: " << func_name << " with " << args.size()
                    // << " args" << std::endl;

                    // 組み込み関数を呼び出し
                    if (ctx.builtins.count(func_name)) {
                        Value result = ctx.builtins[func_name](args, ctx.locals);
                        if (data.destination) {
                            store_to_place(ctx, *data.destination, result);
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
            case MirRvalue::FormatConvert: {
                auto& data = std::get<MirRvalue::FormatConvertData>(rvalue.data);
                Value operand = evaluate_operand(ctx, *data.operand);
                return apply_format_conversion(operand, data.format_spec);
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
                    return Value(static_cast<int64_t>(val));
                } else if constexpr (std::is_same_v<T, std::string>) {
                    return Value(val);
                }
                return Value{};
            },
            constant.value);
    }

    // フォーマット変換を適用
    Value apply_format_conversion(Value value, const std::string& format_spec) {
        std::stringstream ss;

        if (format_spec == "x") {
            // 16進数小文字
            if (value.type() == typeid(int64_t)) {
                ss << std::hex << std::any_cast<int64_t>(value);
            } else if (value.type() == typeid(int32_t)) {
                ss << std::hex << std::any_cast<int32_t>(value);
            }
        } else if (format_spec == "X") {
            // 16進数大文字
            if (value.type() == typeid(int64_t)) {
                ss << std::hex << std::uppercase << std::any_cast<int64_t>(value);
            } else if (value.type() == typeid(int32_t)) {
                ss << std::hex << std::uppercase << std::any_cast<int32_t>(value);
            }
        } else if (format_spec == "b") {
            // 2進数
            if (value.type() == typeid(int64_t)) {
                int64_t val = std::any_cast<int64_t>(value);
                if (val == 0) {
                    ss << "0";
                } else {
                    std::string binary = std::bitset<64>(val).to_string();
                    size_t firstOne = binary.find('1');
                    if (firstOne != std::string::npos) {
                        ss << binary.substr(firstOne);
                    }
                }
            } else if (value.type() == typeid(int32_t)) {
                int32_t val = std::any_cast<int32_t>(value);
                if (val == 0) {
                    ss << "0";
                } else {
                    std::string binary = std::bitset<32>(val).to_string();
                    size_t firstOne = binary.find('1');
                    if (firstOne != std::string::npos) {
                        ss << binary.substr(firstOne);
                    }
                }
            }
        } else if (format_spec == "o") {
            // 8進数
            if (value.type() == typeid(int64_t)) {
                ss << std::oct << std::any_cast<int64_t>(value);
            } else if (value.type() == typeid(int32_t)) {
                ss << std::oct << std::any_cast<int32_t>(value);
            }
        } else if (format_spec.size() > 1 && format_spec[0] == '.') {
            // 浮動小数点精度指定
            if (value.type() == typeid(double)) {
                int precision = std::stoi(format_spec.substr(1));
                ss << std::fixed << std::setprecision(precision) << std::any_cast<double>(value);
            } else if (value.type() == typeid(float)) {
                int precision = std::stoi(format_spec.substr(1));
                ss << std::fixed << std::setprecision(precision) << std::any_cast<float>(value);
            }
        } else {
            // デフォルト：通常の文字列変換
            if (value.type() == typeid(std::string)) {
                return value;
            } else if (value.type() == typeid(int64_t)) {
                ss << std::any_cast<int64_t>(value);
            } else if (value.type() == typeid(int32_t)) {
                ss << std::any_cast<int32_t>(value);
            } else if (value.type() == typeid(double)) {
                ss << std::any_cast<double>(value);
            } else if (value.type() == typeid(float)) {
                ss << std::any_cast<float>(value);
            } else if (value.type() == typeid(bool)) {
                ss << (std::any_cast<bool>(value) ? "true" : "false");
            }
        }

        return Value(ss.str());
    }

    // 二項演算を評価
    Value evaluate_binary_op(MirBinaryOp op, Value lhs, Value rhs) {
        // 文字列連結（Add演算のみ）
        if (op == MirBinaryOp::Add) {
            // 左辺または右辺が文字列の場合、文字列連結として処理
            if (lhs.type() == typeid(std::string) || rhs.type() == typeid(std::string)) {
                std::string left_str, right_str;

                // 左辺を文字列に変換
                if (lhs.type() == typeid(std::string)) {
                    left_str = std::any_cast<std::string>(lhs);
                } else if (lhs.type() == typeid(int64_t)) {
                    left_str = std::to_string(std::any_cast<int64_t>(lhs));
                } else if (lhs.type() == typeid(int32_t)) {
                    left_str = std::to_string(std::any_cast<int32_t>(lhs));
                } else if (lhs.type() == typeid(double)) {
                    left_str = std::to_string(std::any_cast<double>(lhs));
                } else if (lhs.type() == typeid(bool)) {
                    left_str = std::any_cast<bool>(lhs) ? "true" : "false";
                }

                // 右辺を文字列に変換
                if (rhs.type() == typeid(std::string)) {
                    right_str = std::any_cast<std::string>(rhs);
                } else if (rhs.type() == typeid(int64_t)) {
                    right_str = std::to_string(std::any_cast<int64_t>(rhs));
                } else if (rhs.type() == typeid(int32_t)) {
                    right_str = std::to_string(std::any_cast<int32_t>(rhs));
                } else if (rhs.type() == typeid(double)) {
                    right_str = std::to_string(std::any_cast<double>(rhs));
                } else if (rhs.type() == typeid(bool)) {
                    right_str = std::any_cast<bool>(rhs) ? "true" : "false";
                }

                return Value(left_str + right_str);
            }
        }

        // 論理演算（bool型）
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
                    // bool型の他の演算は整数に変換して処理
                    return evaluate_binary_op(op, Value(static_cast<int64_t>(l)),
                                              Value(static_cast<int64_t>(r)));
            }
        }

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
                    return Value(l / r);
                case MirBinaryOp::Mod:
                    return Value(l % r);
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
                case MirBinaryOp::And:
                    return Value(l != 0 && r != 0);
                case MirBinaryOp::Or:
                    return Value(l != 0 || r != 0);
                default:
                    break;
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
                default:
                    break;
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

    // 構造体の値を表現（フィールド名 → 値のマップ）
    using StructValue = std::unordered_map<FieldId, Value>;

    // Placeから値を読み込み
    Value load_from_place(ExecutionContext& ctx, const MirPlace& place) {
        auto it = ctx.locals.find(place.local);
        if (it == ctx.locals.end()) {
            return Value{};
        }

        Value result = it->second;

        // プロジェクションを適用
        for (const auto& proj : place.projections) {
            if (proj.kind == ProjectionKind::Field) {
                // 構造体のフィールドアクセス
                if (result.type() == typeid(StructValue)) {
                    auto& struct_val = std::any_cast<StructValue&>(result);
                    auto field_it = struct_val.find(proj.field_id);
                    if (field_it != struct_val.end()) {
                        result = field_it->second;
                    } else {
                        return Value{};  // フィールドが見つからない
                    }
                } else {
                    return Value{};  // 構造体ではない
                }
            }
            // TODO: Index, Deref
        }

        return result;
    }

    // Placeに値を格納
    void store_to_place(ExecutionContext& ctx, const MirPlace& place, Value value) {
        if (place.projections.empty()) {
            // プロジェクションがない場合は直接格納
            ctx.locals[place.local] = value;
        } else {
            // プロジェクションがある場合
            // まずローカル変数を取得（なければ作成）
            auto it = ctx.locals.find(place.local);
            if (it == ctx.locals.end()) {
                // 構造体として初期化
                ctx.locals[place.local] = Value(StructValue{});
                it = ctx.locals.find(place.local);
            }

            // フィールドアクセスの場合
            if (place.projections[0].kind == ProjectionKind::Field) {
                if (it->second.type() != typeid(StructValue)) {
                    // 構造体ではない場合は初期化
                    it->second = Value(StructValue{});
                }
                auto& struct_val = std::any_cast<StructValue&>(it->second);
                struct_val[place.projections[0].field_id] = value;
            }
            // TODO: 複数のプロジェクション、Index, Deref
        }
    }
};

}  // namespace cm::mir