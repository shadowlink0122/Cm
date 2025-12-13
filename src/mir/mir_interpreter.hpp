#pragma once

#include "../common/debug/interp.hpp"
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
        debug::interp::log(debug::interp::Id::Start, "Starting interpreter execution",
                           debug::Level::Info);
        debug::interp::log(debug::interp::Id::EntryPoint, "Entry point: " + entry_point,
                           debug::Level::Debug);

        // プログラムへの参照を保存
        current_program_ = &program;
        debug::interp::log(
            debug::interp::Id::ProgramLoad,
            "Program loaded with " + std::to_string(program.functions.size()) + " functions",
            debug::Level::Debug);

        // main関数を探す
        const MirFunction* main_func = nullptr;
        debug::interp::log(debug::interp::Id::FunctionSearch,
                           "Searching for function: " + entry_point, debug::Level::Debug);
        for (const auto& func : program.functions) {
            if (func) {
                debug::interp::log(debug::interp::Id::FunctionCheck,
                                   "Checking function: " + func->name, debug::Level::Trace);
                if (func->name == entry_point) {
                    main_func = func.get();
                    debug::interp::log(debug::interp::Id::FunctionFound,
                                       "Found entry point function: " + entry_point,
                                       debug::Level::Debug);
                    break;
                }
            }
        }

        if (!main_func) {
            debug::interp::log(debug::interp::Id::Error,
                               "Entry point '" + entry_point + "' not found", debug::Level::Error);
            return {false, Value{}, "エントリポイント '" + entry_point + "' が見つかりません"};
        }

        try {
            debug::interp::log(debug::interp::Id::ExecuteStart,
                               "Executing function: " + entry_point, debug::Level::Info);
            Value result = execute_function(*main_func, {});
            debug::interp::log(debug::interp::Id::ExecuteEnd, "Execution completed successfully",
                               debug::Level::Info);
            return {true, result, ""};
        } catch (const std::exception& e) {
            debug::interp::log(debug::interp::Id::Exception,
                               std::string("Exception caught: ") + e.what(), debug::Level::Error);
            return {false, Value{}, e.what()};
        }
    }

   private:
    // 現在実行中のプログラム
    const MirProgram* current_program_ = nullptr;

    // 関数を名前で検索
    const MirFunction* find_function(const std::string& name) const {
        if (!current_program_)
            return nullptr;
        for (const auto& func : current_program_->functions) {
            if (func && func->name == name) {
                return func.get();
            }
        }
        return nullptr;
    }

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
                   const std::unordered_map<LocalId, Value>& /* locals */) -> Value {
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
                    } else if (arg.type() == typeid(char)) {
                        std::cout << std::any_cast<char>(arg);
                    }
                }
                std::cout << std::endl;

                return Value{};
            };

            // ランタイム関数: cm_println_int
            builtins["cm_println_int"] =
                [](std::vector<Value> args,
                   const std::unordered_map<LocalId, Value>& /* locals */) -> Value {
                if (!args.empty()) {
                    if (args[0].type() == typeid(int64_t)) {
                        std::cout << std::any_cast<int64_t>(args[0]) << std::endl;
                    } else if (args[0].type() == typeid(bool)) {
                        // bool値を"true"/"false"として出力
                        std::cout << (std::any_cast<bool>(args[0]) ? "true" : "false") << std::endl;
                    }
                }
                return Value{};
            };

            // ランタイム関数: cm_println_string
            builtins["cm_println_string"] =
                [](std::vector<Value> args,
                   const std::unordered_map<LocalId, Value>& /* locals */) -> Value {
                if (!args.empty() && args[0].type() == typeid(std::string)) {
                    std::cout << std::any_cast<std::string>(args[0]) << std::endl;
                }
                return Value{};
            };

            // ランタイム関数: cm_println_bool
            builtins["cm_println_bool"] =
                [](std::vector<Value> args,
                   const std::unordered_map<LocalId, Value>& /* locals */) -> Value {
                if (!args.empty() && args[0].type() == typeid(bool)) {
                    std::cout << (std::any_cast<bool>(args[0]) ? "true" : "false") << std::endl;
                }
                return Value{};
            };

            // ランタイム関数: cm_println_char
            builtins["cm_println_char"] =
                [](std::vector<Value> args,
                   const std::unordered_map<LocalId, Value>& /* locals */) -> Value {
                if (!args.empty() && args[0].type() == typeid(char)) {
                    std::cout << std::any_cast<char>(args[0]) << std::endl;
                }
                return Value{};
            };

            // ランタイム関数: cm_println_double
            builtins["cm_println_double"] =
                [](std::vector<Value> args,
                   const std::unordered_map<LocalId, Value>& /* locals */) -> Value {
                if (!args.empty() && args[0].type() == typeid(double)) {
                    double val = std::any_cast<double>(args[0]);
                    if (val == static_cast<int>(val)) {
                        std::cout << static_cast<int>(val) << std::endl;
                    } else {
                        std::cout << val << std::endl;
                    }
                }
                return Value{};
            };

            // ランタイム関数: cm_println_uint
            builtins["cm_println_uint"] =
                [](std::vector<Value> args,
                   const std::unordered_map<LocalId, Value>& /* locals */) -> Value {
                if (!args.empty() && args[0].type() == typeid(uint64_t)) {
                    std::cout << std::any_cast<uint64_t>(args[0]) << std::endl;
                }
                return Value{};
            };

            // ランタイム関数: cm_println_format
            builtins["cm_println_format"] =
                [](std::vector<Value> args,
                   const std::unordered_map<LocalId, Value>& /* locals */) -> Value {
                if (args.size() >= 2) {
                    // 第1引数: フォーマット文字列
                    // 第2引数: 引数の数
                    std::string format = std::any_cast<std::string>(args[0]);
                    int64_t argc = std::any_cast<int64_t>(args[1]);

                    size_t pos = 0;
                    int arg_index = 0;
                    std::string result;

                    while (pos < format.length()) {
                        // エスケープされた中括弧を処理
                        if (pos + 1 < format.length() && format[pos] == '{' &&
                            format[pos + 1] == '{') {
                            result += '{';
                            pos += 2;
                            continue;
                        }
                        if (pos + 1 < format.length() && format[pos] == '}' &&
                            format[pos + 1] == '}') {
                            result += '}';
                            pos += 2;
                            continue;
                        }

                        // フォーマット指定子を探す
                        if (format[pos] == '{') {
                            size_t end = pos + 1;
                            while (end < format.length() && format[end] != '}') {
                                end++;
                            }

                            if (end < format.length() && arg_index < argc &&
                                (size_t)(2 + arg_index) < args.size()) {
                                // フォーマット指定子を解析
                                std::string spec = format.substr(pos + 1, end - pos - 1);
                                const auto& arg = args[2 + arg_index];

                                // フォーマット指定子のパラメータ
                                std::string alignment;  // <, >, ^
                                char fill_char = ' ';
                                int width = 0;
                                int precision = -1;
                                std::string type_spec;  // x, X, b, o, e, E

                                // フォーマット指定子を解析
                                size_t spec_pos = 0;
                                if (spec.length() > 0 && spec[0] == ':') {
                                    spec_pos = 1;

                                    // ゼロパディング（:0>5 のような形式）
                                    if (spec_pos < spec.length() && spec[spec_pos] == '0' &&
                                        spec_pos + 1 < spec.length() &&
                                        (spec[spec_pos + 1] == '<' || spec[spec_pos + 1] == '>' ||
                                         spec[spec_pos + 1] == '^')) {
                                        fill_char = '0';
                                        spec_pos++;
                                    }

                                    // アラインメント
                                    if (spec_pos < spec.length() &&
                                        (spec[spec_pos] == '<' || spec[spec_pos] == '>' ||
                                         spec[spec_pos] == '^')) {
                                        alignment = spec[spec_pos];
                                        spec_pos++;
                                    }

                                    // 幅
                                    std::string width_str;
                                    while (spec_pos < spec.length() &&
                                           std::isdigit(spec[spec_pos])) {
                                        width_str += spec[spec_pos];
                                        spec_pos++;
                                    }
                                    if (!width_str.empty()) {
                                        width = std::stoi(width_str);
                                    }

                                    // 精度（.2 のような形式）
                                    if (spec_pos < spec.length() && spec[spec_pos] == '.') {
                                        spec_pos++;
                                        std::string prec_str;
                                        while (spec_pos < spec.length() &&
                                               std::isdigit(spec[spec_pos])) {
                                            prec_str += spec[spec_pos];
                                            spec_pos++;
                                        }
                                        if (!prec_str.empty()) {
                                            precision = std::stoi(prec_str);
                                        }
                                    }

                                    // 型指定子
                                    if (spec_pos < spec.length()) {
                                        type_spec = spec.substr(spec_pos);
                                    }
                                }

                                // 値をフォーマット
                                std::string formatted;
                                if (arg.type() == typeid(int64_t)) {
                                    int64_t val = std::any_cast<int64_t>(arg);

                                    if (type_spec == "x") {
                                        // 小文字16進数
                                        char buffer[32];
                                        snprintf(buffer, sizeof(buffer), "%llx", (long long)val);
                                        formatted = buffer;
                                    } else if (type_spec == "X") {
                                        // 大文字16進数
                                        char buffer[32];
                                        snprintf(buffer, sizeof(buffer), "%llX", (long long)val);
                                        formatted = buffer;
                                    } else if (type_spec == "b") {
                                        // 2進数
                                        if (val == 0) {
                                            formatted = "0";
                                        } else {
                                            std::string binary;
                                            int64_t temp = val;
                                            while (temp > 0) {
                                                binary = (char)('0' + (temp & 1)) + binary;
                                                temp >>= 1;
                                            }
                                            formatted = binary;
                                        }
                                    } else if (type_spec == "o") {
                                        // 8進数
                                        char buffer[32];
                                        snprintf(buffer, sizeof(buffer), "%llo", (long long)val);
                                        formatted = buffer;
                                    } else {
                                        // デフォルト（10進数）
                                        formatted = std::to_string(val);
                                    }
                                } else if (arg.type() == typeid(double)) {
                                    double val = std::any_cast<double>(arg);
                                    char buffer[64];

                                    if (type_spec == "e") {
                                        // 小文字科学記法
                                        snprintf(buffer, sizeof(buffer), "%e", val);
                                        formatted = buffer;
                                    } else if (type_spec == "E") {
                                        // 大文字科学記法
                                        snprintf(buffer, sizeof(buffer), "%E", val);
                                        formatted = buffer;
                                    } else if (precision >= 0) {
                                        // 精度指定あり
                                        snprintf(buffer, sizeof(buffer), "%.*f", precision, val);
                                        formatted = buffer;
                                    } else {
                                        // デフォルト
                                        if (val == (int64_t)val) {
                                            formatted = std::to_string((int64_t)val);
                                        } else {
                                            snprintf(buffer, sizeof(buffer), "%g", val);
                                            formatted = buffer;
                                        }
                                    }
                                } else if (arg.type() == typeid(bool)) {
                                    formatted = std::any_cast<bool>(arg) ? "true" : "false";
                                } else if (arg.type() == typeid(char)) {
                                    formatted = std::string(1, std::any_cast<char>(arg));
                                } else if (arg.type() == typeid(std::string)) {
                                    formatted = std::any_cast<std::string>(arg);
                                } else {
                                    formatted = "{}";
                                }

                                // 幅とアラインメントを適用
                                if (width > 0 && static_cast<int>(formatted.length()) < width) {
                                    int padding = width - formatted.length();

                                    if (alignment == ">") {
                                        // 右寄せ
                                        formatted = std::string(padding, fill_char) + formatted;
                                    } else if (alignment == "^") {
                                        // 中央寄せ
                                        int left_pad = padding / 2;
                                        int right_pad = padding - left_pad;
                                        formatted = std::string(left_pad, ' ') + formatted +
                                                    std::string(right_pad, ' ');
                                    } else {
                                        // 左寄せ（デフォルト）
                                        formatted = formatted + std::string(padding, ' ');
                                    }
                                }

                                result += formatted;
                                arg_index++;
                            } else if (end < format.length()) {
                                // 引数がない場合はそのまま出力
                                result += format.substr(pos, end - pos + 1);
                            }

                            pos = (end < format.length()) ? end + 1 : format.length();
                        } else {
                            result += format[pos];
                            pos++;
                        }
                    }

                    std::cout << result << std::endl;
                }
                return Value{};
            };

            // ランタイム関数: cm_print_int
            builtins["cm_print_int"] =
                [](std::vector<Value> args,
                   const std::unordered_map<LocalId, Value>& /* locals */) -> Value {
                if (!args.empty() && args[0].type() == typeid(int64_t)) {
                    std::cout << std::any_cast<int64_t>(args[0]);
                }
                return Value{};
            };

            // ランタイム関数: cm_print_string
            builtins["cm_print_string"] =
                [](std::vector<Value> args,
                   const std::unordered_map<LocalId, Value>& /* locals */) -> Value {
                if (!args.empty() && args[0].type() == typeid(std::string)) {
                    std::cout << std::any_cast<std::string>(args[0]);
                }
                return Value{};
            };

            // ランタイム関数: cm_print_bool
            builtins["cm_print_bool"] =
                [](std::vector<Value> args,
                   const std::unordered_map<LocalId, Value>& /* locals */) -> Value {
                if (!args.empty() && args[0].type() == typeid(bool)) {
                    std::cout << (std::any_cast<bool>(args[0]) ? "true" : "false");
                }
                return Value{};
            };

            // ランタイム関数: cm_print_char
            builtins["cm_print_char"] =
                [](std::vector<Value> args,
                   const std::unordered_map<LocalId, Value>& /* locals */) -> Value {
                if (!args.empty() && args[0].type() == typeid(char)) {
                    std::cout << std::any_cast<char>(args[0]);
                }
                return Value{};
            };

            // ランタイム関数: cm_format_string (文字列を作成して返す)
            builtins["cm_format_string"] =
                [](std::vector<Value> args,
                   const std::unordered_map<LocalId, Value>& /* locals */) -> Value {
                if (args.size() >= 2) {
                    // 第1引数: フォーマット文字列
                    // 第2引数: 引数の数
                    std::string format = std::any_cast<std::string>(args[0]);
                    int64_t argc = std::any_cast<int64_t>(args[1]);

                    size_t pos = 0;
                    int arg_index = 0;
                    std::string result;

                    while (pos < format.length()) {
                        // エスケープされた {{ を処理
                        if (pos + 1 < format.length() && format[pos] == '{' &&
                            format[pos + 1] == '{') {
                            result += '{';
                            pos += 2;
                            continue;
                        }
                        // エスケープされた }} を処理
                        if (pos + 1 < format.length() && format[pos] == '}' &&
                            format[pos + 1] == '}') {
                            result += '}';
                            pos += 2;
                            continue;
                        }

                        if (format[pos] == '{') {
                            size_t end = format.find('}', pos + 1);
                            if (end != std::string::npos) {
                                // フォーマット指定子をチェック
                                std::string spec = format.substr(pos + 1, end - pos - 1);
                                if (arg_index < argc &&
                                    arg_index + 2 < static_cast<int>(args.size())) {
                                    const auto& arg = args[arg_index + 2];

                                    // フォーマット指定子の解析
                                    size_t colon_pos = spec.find(':');
                                    std::string fmt_type = "";
                                    if (colon_pos != std::string::npos) {
                                        fmt_type = spec.substr(colon_pos + 1);
                                    }

                                    // 値のフォーマット
                                    if (arg.type() == typeid(int64_t)) {
                                        int64_t val = std::any_cast<int64_t>(arg);
                                        if (fmt_type == "x") {
                                            // 16進数（小文字）
                                            std::stringstream ss;
                                            ss << std::hex << val;
                                            result += ss.str();
                                        } else if (fmt_type == "X") {
                                            // 16進数（大文字）
                                            std::stringstream ss;
                                            ss << std::hex << std::uppercase << val;
                                            result += ss.str();
                                        } else if (fmt_type == "b") {
                                            // 2進数
                                            result += std::bitset<64>(val).to_string().erase(
                                                0, std::bitset<64>(val).to_string().find('1'));
                                        } else if (fmt_type == "o") {
                                            // 8進数
                                            std::stringstream ss;
                                            ss << std::oct << val;
                                            result += ss.str();
                                        } else {
                                            result += std::to_string(val);
                                        }
                                    } else if (arg.type() == typeid(double)) {
                                        double val = std::any_cast<double>(arg);
                                        // 精度の指定をチェック
                                        if (!fmt_type.empty() && fmt_type[0] == '.') {
                                            int precision = std::stoi(fmt_type.substr(1));
                                            std::stringstream ss;
                                            ss << std::fixed << std::setprecision(precision) << val;
                                            result += ss.str();
                                        } else {
                                            result += std::to_string(val);
                                        }
                                    } else if (arg.type() == typeid(bool)) {
                                        result += std::any_cast<bool>(arg) ? "true" : "false";
                                    } else if (arg.type() == typeid(char)) {
                                        result += std::any_cast<char>(arg);
                                    } else if (arg.type() == typeid(std::string)) {
                                        result += std::any_cast<std::string>(arg);
                                    }
                                    arg_index++;
                                }
                                pos = end + 1;
                            } else {
                                result += format[pos];
                                pos++;
                            }
                        } else {
                            result += format[pos];
                            pos++;
                        }
                    }

                    return Value{result};
                }
                return Value{std::string("")};
            };

            // ランタイム関数: cm_println_formatted
            builtins["cm_println_formatted"] =
                [](std::vector<Value> args,
                   const std::unordered_map<LocalId, Value>& /* locals */) -> Value {
                if (args.empty())
                    return Value{};

                // 最初の引数はフォーマット文字列
                if (args[0].type() != typeid(std::string))
                    return Value{};
                std::string format_str = std::any_cast<std::string>(args[0]);

                std::string result;
                size_t arg_index = 1;
                size_t pos = 0;

                while (pos < format_str.length()) {
                    size_t placeholder_pos = format_str.find("{}", pos);
                    if (placeholder_pos == std::string::npos) {
                        // 残りの文字列を追加
                        result += format_str.substr(pos);
                        break;
                    }

                    // プレースホルダーまでの文字列を追加
                    result += format_str.substr(pos, placeholder_pos - pos);

                    // 引数が残っている場合は値を挿入
                    if (arg_index < args.size()) {
                        // 値を文字列に変換
                        if (args[arg_index].type() == typeid(int64_t)) {
                            result += std::to_string(std::any_cast<int64_t>(args[arg_index]));
                        } else if (args[arg_index].type() == typeid(bool)) {
                            result += std::any_cast<bool>(args[arg_index]) ? "true" : "false";
                        } else if (args[arg_index].type() == typeid(std::string)) {
                            result += std::any_cast<std::string>(args[arg_index]);
                        } else if (args[arg_index].type() == typeid(double)) {
                            result += std::to_string(std::any_cast<double>(args[arg_index]));
                        } else if (args[arg_index].type() == typeid(char)) {
                            result += std::any_cast<char>(args[arg_index]);
                        } else {
                            result += "?";  // 未知の型
                        }
                        arg_index++;
                    } else {
                        result += "{}";  // 引数が不足
                    }

                    pos = placeholder_pos + 2;  // "{}"の長さ分進める
                }

                std::cout << result << std::endl;
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
                        auto val = std::any_cast<int64_t>(args[0]);
                        if (val == 0)
                            return Value(std::string("0"));
                        std::string binary = std::bitset<64>(val).to_string();
                        size_t firstOne = binary.find('1');
                        if (firstOne != std::string::npos) {
                            return Value(binary.substr(firstOne));
                        }
                    } else if (args[0].type() == typeid(int32_t)) {
                        auto val = std::any_cast<int32_t>(args[0]);
                        if (val == 0)
                            return Value(std::string("0"));
                        std::string binary = std::bitset<32>(val).to_string();
                        size_t firstOne = binary.find('1');
                        if (firstOne != std::string::npos) {
                            return Value(binary.substr(firstOne));
                        }
                    }
                }
                return Value(std::string("0"));
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

            // 文字列連結
            builtins["cm_string_concat"] = [](std::vector<Value> args,
                                              const std::unordered_map<LocalId, Value>&) -> Value {
                if (args.size() == 2) {
                    std::string left = "";
                    std::string right = "";

                    // 左辺を文字列に変換
                    if (args[0].type() == typeid(std::string)) {
                        left = std::any_cast<std::string>(args[0]);
                    } else if (args[0].type() == typeid(int64_t)) {
                        left = std::to_string(std::any_cast<int64_t>(args[0]));
                    } else if (args[0].type() == typeid(int32_t)) {
                        left = std::to_string(std::any_cast<int32_t>(args[0]));
                    }

                    // 右辺を文字列に変換
                    if (args[1].type() == typeid(std::string)) {
                        right = std::any_cast<std::string>(args[1]);
                    } else if (args[1].type() == typeid(int64_t)) {
                        right = std::to_string(std::any_cast<int64_t>(args[1]));
                    } else if (args[1].type() == typeid(int32_t)) {
                        right = std::to_string(std::any_cast<int32_t>(args[1]));
                    }

                    return Value(left + right);
                }
                return Value(std::string(""));
            };

            // 整数を文字列に変換
            builtins["cm_int_to_string"] = [](std::vector<Value> args,
                                              const std::unordered_map<LocalId, Value>&) -> Value {
                if (args.size() == 1) {
                    if (args[0].type() == typeid(int64_t)) {
                        return Value(std::to_string(std::any_cast<int64_t>(args[0])));
                    } else if (args[0].type() == typeid(int32_t)) {
                        return Value(std::to_string(std::any_cast<int32_t>(args[0])));
                    }
                }
                return Value(std::string(""));
            };
        }
    };

    // 関数を実行
    Value execute_function(const MirFunction& func, std::vector<Value> args) {
        debug::interp::log(debug::interp::Id::FunctionEnter, "Entering function: " + func.name,
                           debug::Level::Info);
        debug::interp::log(debug::interp::Id::FunctionArgs,
                           "Arguments count: " + std::to_string(args.size()), debug::Level::Debug);

        ExecutionContext ctx(&func);

        // 引数をローカル変数に設定
        for (size_t i = 0; i < args.size() && i < func.arg_locals.size(); i++) {
            debug::interp::log(debug::interp::Id::ArgStore,
                               "Storing argument " + std::to_string(i) + " to local _" +
                                   std::to_string(func.arg_locals[i]),
                               debug::Level::Trace);
            ctx.locals[func.arg_locals[i]] = args[i];
            debug::interp::dump_value("Argument value", args[i]);
        }

        // 戻り値用のローカル変数を初期化
        debug::interp::log(debug::interp::Id::ReturnInit,
                           "Initializing return local _" + std::to_string(func.return_local),
                           debug::Level::Trace);
        ctx.locals[func.return_local] = Value{};

        // エントリブロックから実行開始
        debug::interp::log(debug::interp::Id::BlockEnter,
                           "Starting from entry block: bb" + std::to_string(func.entry_block),
                           debug::Level::Debug);
        execute_block(ctx, func.entry_block);

        // 戻り値を返す
        Value ret_val = ctx.locals[func.return_local];
        debug::interp::log(debug::interp::Id::FunctionExit, "Exiting function: " + func.name,
                           debug::Level::Info);
        debug::interp::dump_value("Return value", ret_val);
        return ret_val;
    }

    // コンストラクタを実行（selfへの変更を呼び出し元に反映）
    void execute_constructor(const MirFunction& func, std::vector<Value>& args) {
        debug::interp::log(debug::interp::Id::FunctionEnter, "Entering constructor: " + func.name,
                           debug::Level::Info);

        ExecutionContext ctx(&func);

        // 引数をローカル変数に設定
        for (size_t i = 0; i < args.size() && i < func.arg_locals.size(); i++) {
            ctx.locals[func.arg_locals[i]] = args[i];
        }

        // 戻り値用のローカル変数を初期化
        ctx.locals[func.return_local] = Value{};

        // エントリブロックから実行開始
        execute_block(ctx, func.entry_block);

        // selfへの変更を呼び出し元に反映
        if (!func.arg_locals.empty() && !args.empty()) {
            args[0] = ctx.locals[func.arg_locals[0]];
            debug::interp::log(debug::interp::Id::FunctionExit,
                               "Constructor: copying self back to caller", debug::Level::Debug);
        }

        debug::interp::log(debug::interp::Id::FunctionExit, "Exiting constructor: " + func.name,
                           debug::Level::Info);
    }

    // 基本ブロックを実行
    void execute_block(ExecutionContext& ctx, BlockId block_id) {
        debug::interp::log(debug::interp::Id::BlockExecute,
                           "Executing block: bb" + std::to_string(block_id), debug::Level::Debug);

        // ブロックIDに対応するブロックを検索
        const BasicBlock* block = nullptr;
        for (const auto& bb : ctx.function->basic_blocks) {
            if (bb->id == block_id) {
                block = bb.get();
                break;
            }
        }

        if (!block) {
            debug::interp::log(debug::interp::Id::Error,
                               "Block not found: " + std::to_string(block_id), debug::Level::Error);
            throw std::runtime_error("ブロックが見つかりません: " + std::to_string(block_id));
        }
        debug::interp::log(debug::interp::Id::BlockStats,
                           "Block has " + std::to_string(block->statements.size()) + " statements",
                           debug::Level::Trace);

        // ステートメントを順次実行
        int stmt_idx = 0;
        for (const auto& stmt : block->statements) {
            debug::interp::log(debug::interp::Id::StmtExecute,
                               "Executing statement " + std::to_string(stmt_idx++) + " in bb" +
                                   std::to_string(block_id),
                               debug::Level::Trace);
            execute_statement(ctx, *stmt);
        }

        // 終端命令を実行
        if (block->terminator) {
            debug::interp::log(debug::interp::Id::TerminatorExecute,
                               "Executing terminator in bb" + std::to_string(block_id),
                               debug::Level::Debug);
            execute_terminator(ctx, *block->terminator);
        } else {
            debug::interp::log(debug::interp::Id::NoTerminator,
                               "Block bb" + std::to_string(block_id) + " has no terminator",
                               debug::Level::Trace);
        }
    }

    // ステートメントを実行
    void execute_statement(ExecutionContext& ctx, const MirStatement& stmt) {
        switch (stmt.kind) {
            case MirStatement::Assign: {
                debug::interp::log(debug::interp::Id::Assign, "Executing assignment",
                                   debug::Level::Debug);
                auto& data = std::get<MirStatement::AssignData>(stmt.data);

                // 代入先の情報をログ
                std::string place_str = "Place: _" + std::to_string(data.place.local);
                if (!data.place.projections.empty()) {
                    place_str += " with projection";
                }
                debug::interp::log(debug::interp::Id::AssignDest, place_str, debug::Level::Trace);

                // 値を評価
                debug::interp::log(debug::interp::Id::RvalueEval, "Evaluating rvalue",
                                   debug::Level::Trace);
                Value value = evaluate_rvalue(ctx, *data.rvalue);
                debug::interp::dump_value("Computed value", value);

                // 格納
                debug::interp::log(debug::interp::Id::Store, "Storing value to " + place_str,
                                   debug::Level::Debug);
                store_to_place(ctx, data.place, value);
                break;
            }
            case MirStatement::StorageLive: {
                auto& data = std::get<MirStatement::StorageData>(stmt.data);
                debug::interp::log(debug::interp::Id::StorageLive,
                                   "Variable _" + std::to_string(data.local) + " becomes live",
                                   debug::Level::Trace);
                // 変数の有効範囲開始（インタープリタでは特に処理不要）
                break;
            }
            case MirStatement::StorageDead: {
                // 変数の有効範囲終了
                auto& data = std::get<MirStatement::StorageData>(stmt.data);
                debug::interp::log(debug::interp::Id::StorageDead,
                                   "Variable _" + std::to_string(data.local) + " becomes dead",
                                   debug::Level::Trace);
                ctx.locals.erase(data.local);
                break;
            }
            case MirStatement::Nop: {
                debug::interp::log(debug::interp::Id::Nop, "NOP statement", debug::Level::Trace);
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
                debug::interp::log(debug::interp::Id::Goto,
                                   "Unconditional jump to bb" + std::to_string(data.target),
                                   debug::Level::Debug);
                execute_block(ctx, data.target);
                break;
            }
            case MirTerminator::SwitchInt: {
                auto& data = std::get<MirTerminator::SwitchIntData>(term.data);
                debug::interp::log(debug::interp::Id::SwitchInt, "Evaluating switch condition",
                                   debug::Level::Debug);
                Value discriminant = evaluate_operand(ctx, *data.discriminant);
                debug::interp::dump_value("Switch discriminant", discriminant);

                // 条件分岐
                if (discriminant.type() == typeid(int64_t)) {
                    int64_t value = std::any_cast<int64_t>(discriminant);
                    debug::interp::log(debug::interp::Id::SwitchValue,
                                       "Switch on integer: " + std::to_string(value),
                                       debug::Level::Debug);
                    for (const auto& [case_value, target] : data.targets) {
                        debug::interp::log(debug::interp::Id::SwitchCase,
                                           "Checking case: " + std::to_string(case_value) +
                                               " -> bb" + std::to_string(target),
                                           debug::Level::Trace);
                        if (value == case_value) {
                            debug::interp::log(
                                debug::interp::Id::SwitchMatch,
                                "Match found! Jumping to bb" + std::to_string(target),
                                debug::Level::Debug);
                            execute_block(ctx, target);
                            return;
                        }
                    }
                } else if (discriminant.type() == typeid(bool)) {
                    bool value = std::any_cast<bool>(discriminant);
                    debug::interp::log(
                        debug::interp::Id::SwitchValue,
                        "Switch on boolean: " + std::string(value ? "true" : "false"),
                        debug::Level::Debug);
                    // boolをint64_tに変換して比較
                    int64_t int_value = value ? 1 : 0;
                    for (const auto& [case_value, target] : data.targets) {
                        debug::interp::log(debug::interp::Id::SwitchCase,
                                           "Checking case: " + std::to_string(case_value) +
                                               " -> bb" + std::to_string(target) +
                                               ", bool as int: " + std::to_string(int_value),
                                           debug::Level::Debug);
                        if (case_value == int_value) {
                            debug::interp::log(
                                debug::interp::Id::SwitchMatch,
                                "Match found! Jumping to bb" + std::to_string(target),
                                debug::Level::Debug);
                            execute_block(ctx, target);
                            return;
                        }
                    }
                }

                // デフォルトブランチ
                debug::interp::log(
                    debug::interp::Id::SwitchDefault,
                    "No match, taking default branch to bb" + std::to_string(data.otherwise),
                    debug::Level::Debug);
                execute_block(ctx, data.otherwise);
                break;
            }
            case MirTerminator::Return: {
                debug::interp::log(debug::interp::Id::Return, "Return from function",
                                   debug::Level::Debug);
                // 関数から戻る（既に戻り値は_0に設定済み）
                break;
            }
            case MirTerminator::Call: {
                debug::interp::log(debug::interp::Id::Call, "Processing function call",
                                   debug::Level::Debug);
                auto& data = std::get<MirTerminator::CallData>(term.data);

                // 新しい構造では func_name が直接文字列として格納されている
                // func オペランドから関数名を取得
                std::string func_name;
                if (data.func && data.func->kind == MirOperand::FunctionRef) {
                    if (auto* name = std::get_if<std::string>(&data.func->data)) {
                        func_name = *name;
                    }
                }

                if (!func_name.empty()) {
                    debug::interp::log(debug::interp::Id::CallTarget,
                                       "Calling function: " + func_name, debug::Level::Info);

                    // 引数を評価
                    std::vector<Value> args;
                    debug::interp::log(
                        debug::interp::Id::CallArgs,
                        "Evaluating " + std::to_string(data.args.size()) + " arguments",
                        debug::Level::Debug);
                    int arg_idx = 0;
                    for (const auto& arg : data.args) {
                        debug::interp::log(debug::interp::Id::CallArgEval,
                                           "Evaluating argument " + std::to_string(arg_idx++),
                                           debug::Level::Trace);
                        Value arg_val = evaluate_operand(ctx, *arg);
                        debug::interp::dump_value("Argument " + std::to_string(arg_idx - 1),
                                                  arg_val);
                        args.push_back(arg_val);
                    }

                    // 組み込み関数を呼び出し
                    if (ctx.builtins.count(func_name)) {
                        debug::interp::log(debug::interp::Id::CallBuiltin,
                                           "Calling built-in function: " + func_name,
                                           debug::Level::Debug);
                        Value result = ctx.builtins[func_name](args, ctx.locals);
                        debug::interp::dump_value("Built-in function result", result);
                        if (data.destination) {
                            debug::interp::log(debug::interp::Id::CallStore,
                                               "Storing result to destination",
                                               debug::Level::Debug);
                            store_to_place(ctx, *data.destination, result);
                        }
                    } else {
                        // ユーザー定義関数を呼び出し
                        const MirFunction* user_func = find_function(func_name);
                        if (user_func) {
                            debug::interp::log(debug::interp::Id::CallUser,
                                               "Calling user-defined function: " + func_name,
                                               debug::Level::Debug);

                            // コンストラクタかどうかを判定（__ctorを含む）
                            bool is_constructor = func_name.find("__ctor") != std::string::npos;

                            if (is_constructor && !args.empty()) {
                                // コンストラクタ: selfへの変更を呼び出し元に反映
                                // 第一引数がどの変数を指すかを取得
                                LocalId self_local = 0;
                                if (!data.args.empty() && data.args[0]->kind == MirOperand::Copy) {
                                    auto& place = std::get<MirPlace>(data.args[0]->data);
                                    self_local = place.local;
                                }

                                execute_constructor(*user_func, args);

                                // コンストラクタ実行後、argsの最初の要素（self）が更新されている
                                // それを元の変数にコピーバック
                                if (self_local != 0) {
                                    ctx.locals[self_local] = args[0];
                                    debug::interp::log(debug::interp::Id::CallStore,
                                                       "Constructor: copying self back to _" +
                                                           std::to_string(self_local),
                                                       debug::Level::Debug);
                                }
                            } else {
                                // 通常の関数呼び出し
                                Value result = execute_function(*user_func, args);
                                debug::interp::dump_value("Function result", result);
                                if (data.destination) {
                                    debug::interp::log(debug::interp::Id::CallStore,
                                                       "Storing result to destination",
                                                       debug::Level::Debug);
                                    store_to_place(ctx, *data.destination, result);
                                }
                            }
                        } else {
                            // 関数が見つからない場合、インターフェースメソッドの動的ディスパッチを試みる
                            // 関数名の形式: InterfaceName__methodName
                            bool dispatched = false;
                            size_t sep_pos = func_name.find("__");
                            if (sep_pos != std::string::npos && !args.empty()) {
                                std::string method_name = func_name.substr(sep_pos + 2);

                                // 最初の引数（self）の実際の型を値から取得
                                Value& self_arg = args[0];
                                if (self_arg.type() == typeid(StructValue)) {
                                    auto& struct_val = std::any_cast<StructValue&>(self_arg);
                                    std::string actual_type = struct_val.type_name;

                                    if (!actual_type.empty()) {
                                        std::string actual_func_name =
                                            actual_type + "__" + method_name;

                                        const MirFunction* actual_func =
                                            find_function(actual_func_name);
                                        if (actual_func) {
                                            debug::interp::log(debug::interp::Id::CallUser,
                                                               "Dynamic dispatch: " + func_name +
                                                                   " -> " + actual_func_name,
                                                               debug::Level::Debug);

                                            Value result = execute_function(*actual_func, args);
                                            debug::interp::dump_value("Function result", result);
                                            if (data.destination) {
                                                store_to_place(ctx, *data.destination, result);
                                            }
                                            dispatched = true;
                                        }
                                    }
                                }
                            }

                            if (!dispatched) {
                                debug::interp::log(debug::interp::Id::CallNotFound,
                                                   "Function not found: " + func_name,
                                                   debug::Level::Error);
                            }
                        }
                    }
                } else {
                    debug::interp::log(debug::interp::Id::CallNoName,
                                       "Could not determine function name", debug::Level::Error);
                }

                // 次のブロックへ
                debug::interp::log(debug::interp::Id::CallSuccess,
                                   "Continuing to success block bb" + std::to_string(data.success),
                                   debug::Level::Debug);
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
        debug::interp::log(debug::interp::Id::RvalueType, "Evaluating rvalue", debug::Level::Trace);
        switch (rvalue.kind) {
            case MirRvalue::Use: {
                debug::interp::log(debug::interp::Id::RvalueUse, "Rvalue type: Use",
                                   debug::Level::Trace);
                // UseDataには operand フィールドがある
                auto& use_data = std::get<MirRvalue::UseData>(rvalue.data);
                Value result = evaluate_operand(ctx, *use_data.operand);
                debug::interp::dump_value("Use result", result);
                return result;
            }
            case MirRvalue::BinaryOp: {
                auto& data = std::get<MirRvalue::BinaryOpData>(rvalue.data);
                debug::interp::log(debug::interp::Id::BinaryOp,
                                   "Binary operation: " + mir_binop_to_string(data.op),
                                   debug::Level::Debug);
                debug::interp::log(debug::interp::Id::BinaryLhs, "Evaluating LHS",
                                   debug::Level::Trace);
                Value lhs = evaluate_operand(ctx, *data.lhs);
                debug::interp::dump_value("LHS value", lhs);
                debug::interp::log(debug::interp::Id::BinaryRhs, "Evaluating RHS",
                                   debug::Level::Trace);
                Value rhs = evaluate_operand(ctx, *data.rhs);
                debug::interp::dump_value("RHS value", rhs);
                Value result = evaluate_binary_op(data.op, lhs, rhs);
                debug::interp::dump_value("Binary operation result", result);
                return result;
            }
            case MirRvalue::UnaryOp: {
                auto& data = std::get<MirRvalue::UnaryOpData>(rvalue.data);
                debug::interp::log(debug::interp::Id::UnaryOp,
                                   "Unary operation: " + mir_unop_to_string(data.op),
                                   debug::Level::Debug);
                Value operand = evaluate_operand(ctx, *data.operand);
                debug::interp::dump_value("Unary operand", operand);
                Value result = evaluate_unary_op(data.op, operand);
                debug::interp::dump_value("Unary operation result", result);
                return result;
            }
            case MirRvalue::FormatConvert: {
                auto& data = std::get<MirRvalue::FormatConvertData>(rvalue.data);
                debug::interp::log(debug::interp::Id::FormatConvert,
                                   "Format conversion: " + data.format_spec, debug::Level::Debug);
                Value operand = evaluate_operand(ctx, *data.operand);
                Value result = apply_format_conversion(operand, data.format_spec);
                debug::interp::dump_value("Format conversion result", result);
                return result;
            }
            default:
                debug::interp::log(debug::interp::Id::RvalueUnknown, "Unknown rvalue type",
                                   debug::Level::Error);
                return Value{};
        }
    }

    // オペランドを評価
    Value evaluate_operand(ExecutionContext& ctx, const MirOperand& operand) {
        debug::interp::log(debug::interp::Id::OperandEval, "Evaluating operand",
                           debug::Level::Trace);
        switch (operand.kind) {
            case MirOperand::Move:
                debug::interp::log(debug::interp::Id::OperandMove, "Move operand",
                                   debug::Level::Trace);
                [[fallthrough]];
            case MirOperand::Copy: {
                if (operand.kind == MirOperand::Copy) {
                    debug::interp::log(debug::interp::Id::OperandCopy, "Copy operand",
                                       debug::Level::Trace);
                }
                const MirPlace& place = std::get<MirPlace>(operand.data);
                Value result = load_from_place(ctx, place);
                debug::interp::dump_value("Loaded value", result);
                return result;
            }
            case MirOperand::Constant: {
                debug::interp::log(debug::interp::Id::OperandConst, "Constant operand",
                                   debug::Level::Trace);
                const MirConstant& constant = std::get<MirConstant>(operand.data);
                Value result = constant_to_value(constant);
                debug::interp::dump_value("Constant value", result);
                return result;
            }
            default:
                debug::interp::log(debug::interp::Id::OperandUnknown, "Unknown operand type",
                                   debug::Level::Error);
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
                    return Value(val);  // charとして保持
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
            // 2進数（プレフィックスなし）
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

        // 文字列の比較演算
        if (lhs.type() == typeid(std::string) && rhs.type() == typeid(std::string)) {
            std::string l = std::any_cast<std::string>(lhs);
            std::string r = std::any_cast<std::string>(rhs);

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

        // 論理演算（bool型）
        if (lhs.type() == typeid(bool) && rhs.type() == typeid(bool)) {
            bool l = std::any_cast<bool>(lhs);
            bool r = std::any_cast<bool>(rhs);

            switch (op) {
                case MirBinaryOp::BitAnd:
                    return Value(l && r);
                case MirBinaryOp::BitOr:
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
                default:
                    break;
            }
        }

        // char型の演算
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
                    // 他の演算は整数に変換して処理
                    return evaluate_binary_op(op, Value(static_cast<int64_t>(l)),
                                              Value(static_cast<int64_t>(r)));
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

    // 構造体の値を表現（型名 + フィールド値のマップ）
    struct StructValue {
        std::string type_name;
        std::unordered_map<FieldId, Value> fields;
    };

    // デバッグ用: 二項演算子を文字列に変換
    std::string mir_binop_to_string(MirBinaryOp op) {
        switch (op) {
            case MirBinaryOp::Add:
                return "Add";
            case MirBinaryOp::Sub:
                return "Sub";
            case MirBinaryOp::Mul:
                return "Mul";
            case MirBinaryOp::Div:
                return "Div";
            case MirBinaryOp::Mod:
                return "Mod";
            case MirBinaryOp::Eq:
                return "Eq";
            case MirBinaryOp::Ne:
                return "Ne";
            case MirBinaryOp::Lt:
                return "Lt";
            case MirBinaryOp::Gt:
                return "Gt";
            case MirBinaryOp::Le:
                return "Le";
            case MirBinaryOp::Ge:
                return "Ge";
            case MirBinaryOp::BitAnd:
                return "BitAnd";
            case MirBinaryOp::BitOr:
                return "BitOr";
            case MirBinaryOp::BitXor:
                return "BitXor";
            case MirBinaryOp::Shl:
                return "Shl";
            case MirBinaryOp::Shr:
                return "Shr";
            default:
                return "Unknown";
        }
    }

    // デバッグ用: 単項演算子を文字列に変換
    std::string mir_unop_to_string(MirUnaryOp op) {
        switch (op) {
            case MirUnaryOp::Not:
                return "Not";
            case MirUnaryOp::Neg:
                return "Neg";
            case MirUnaryOp::BitNot:
                return "BitNot";
            default:
                return "Unknown";
        }
    }

    // Placeから値を読み込み
    Value load_from_place(ExecutionContext& ctx, const MirPlace& place) {
        debug::interp::log(debug::interp::Id::Load,
                           "Loading from local _" + std::to_string(place.local),
                           debug::Level::Trace);

        auto it = ctx.locals.find(place.local);
        if (it == ctx.locals.end()) {
            debug::interp::log(debug::interp::Id::LoadNotFound,
                               "Local _" + std::to_string(place.local) + " not found",
                               debug::Level::Debug);
            return Value{};
        }

        Value result = it->second;
        debug::interp::dump_value("Initial value from _" + std::to_string(place.local), result);

        // プロジェクションを適用
        for (const auto& proj : place.projections) {
            if (proj.kind == ProjectionKind::Field) {
                debug::interp::log(debug::interp::Id::FieldAccess,
                                   "Accessing field " + std::to_string(proj.field_id),
                                   debug::Level::Trace);
                // 構造体のフィールドアクセス
                if (result.type() == typeid(StructValue)) {
                    auto& struct_val = std::any_cast<StructValue&>(result);
                    auto field_it = struct_val.fields.find(proj.field_id);
                    if (field_it != struct_val.fields.end()) {
                        result = field_it->second;
                        debug::interp::dump_value("Field value", result);
                    } else {
                        debug::interp::log(debug::interp::Id::FieldNotFound,
                                           "Field " + std::to_string(proj.field_id) + " not found",
                                           debug::Level::Debug);
                        return Value{};  // フィールドが見つからない
                    }
                } else {
                    debug::interp::log(debug::interp::Id::NotStruct, "Value is not a struct",
                                       debug::Level::Debug);
                    return Value{};  // 構造体ではない
                }
            }
            // TODO: Index, Deref
        }

        debug::interp::log(debug::interp::Id::LoadComplete, "Load complete", debug::Level::Trace);
        return result;
    }

    // Placeに値を格納
    void store_to_place(ExecutionContext& ctx, const MirPlace& place, Value value) {
        debug::interp::log(debug::interp::Id::Store,
                           "Storing to local _" + std::to_string(place.local), debug::Level::Debug);
        debug::interp::dump_value("Value to store", value);

        if (place.projections.empty()) {
            // プロジェクションがない場合は直接格納
            debug::interp::log(debug::interp::Id::StoreDirect,
                               "Direct store to _" + std::to_string(place.local),
                               debug::Level::Trace);
            ctx.locals[place.local] = value;
            debug::interp::log(debug::interp::Id::StoreComplete, "Store complete",
                               debug::Level::Trace);
        } else {
            // プロジェクションがある場合
            debug::interp::log(debug::interp::Id::StoreProjection, "Store with projection",
                               debug::Level::Trace);
            // まずローカル変数を取得（なければ作成）
            auto it = ctx.locals.find(place.local);
            if (it == ctx.locals.end()) {
                // 構造体として初期化
                debug::interp::log(debug::interp::Id::StoreInitStruct,
                                   "Initializing new struct for _" + std::to_string(place.local),
                                   debug::Level::Trace);
                // 型名を取得
                std::string type_name;
                for (const auto& local : ctx.function->locals) {
                    if (local.id == place.local && local.type) {
                        type_name = hir::type_to_string(*local.type);
                        break;
                    }
                }
                StructValue sv;
                sv.type_name = type_name;
                ctx.locals[place.local] = Value(sv);
                it = ctx.locals.find(place.local);
            }

            // フィールドアクセスの場合
            if (place.projections[0].kind == ProjectionKind::Field) {
                debug::interp::log(
                    debug::interp::Id::StoreField,
                    "Storing to field " + std::to_string(place.projections[0].field_id),
                    debug::Level::Debug);
                if (it->second.type() != typeid(StructValue)) {
                    // 構造体ではない場合は初期化
                    debug::interp::log(debug::interp::Id::StoreConvertStruct,
                                       "Converting to struct type", debug::Level::Trace);
                    // 型名を取得
                    std::string type_name;
                    for (const auto& local : ctx.function->locals) {
                        if (local.id == place.local && local.type) {
                            type_name = hir::type_to_string(*local.type);
                            break;
                        }
                    }
                    StructValue sv;
                    sv.type_name = type_name;
                    it->second = Value(sv);
                }
                auto& struct_val = std::any_cast<StructValue&>(it->second);
                struct_val.fields[place.projections[0].field_id] = value;
            }
            // TODO: 複数のプロジェクション、Index, Deref
        }
    }
};

}  // namespace cm::mir