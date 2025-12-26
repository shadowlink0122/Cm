#pragma once

#include "builtin_format.hpp"
#include "types.hpp"

#include <iostream>

namespace cm::mir::interp {

/// I/O関数の登録
inline void register_io_builtins(BuiltinRegistry& builtins) {
    // println_int
    builtins["cm_println_int"] = [](std::vector<Value> args, const auto&) -> Value {
        if (!args.empty()) {
            if (args[0].type() == typeid(int64_t)) {
                std::cout << std::any_cast<int64_t>(args[0]) << std::endl;
            } else if (args[0].type() == typeid(uint64_t)) {
                std::cout << std::any_cast<uint64_t>(args[0]) << std::endl;
            } else if (args[0].type() == typeid(bool)) {
                std::cout << (std::any_cast<bool>(args[0]) ? "true" : "false") << std::endl;
            }
        }
        return {};
    };

    // println_string
    builtins["cm_println_string"] = [](std::vector<Value> args, const auto&) -> Value {
        if (!args.empty() && args[0].type() == typeid(std::string)) {
            std::cout << std::any_cast<std::string>(args[0]) << std::endl;
        }
        return {};
    };

    // println_double
    builtins["cm_println_double"] = [](std::vector<Value> args, const auto&) -> Value {
        if (!args.empty() && args[0].type() == typeid(double)) {
            double val = std::any_cast<double>(args[0]);
            if (val == static_cast<int64_t>(val)) {
                std::cout << static_cast<int64_t>(val) << std::endl;
            } else {
                std::cout << val << std::endl;
            }
        }
        return {};
    };

    // println_char
    builtins["cm_println_char"] = [](std::vector<Value> args, const auto&) -> Value {
        if (!args.empty() && args[0].type() == typeid(char)) {
            std::cout << std::any_cast<char>(args[0]) << std::endl;
        }
        return {};
    };

    // println_bool
    builtins["cm_println_bool"] = [](std::vector<Value> args, const auto&) -> Value {
        if (!args.empty() && args[0].type() == typeid(bool)) {
            std::cout << (std::any_cast<bool>(args[0]) ? "true" : "false") << std::endl;
        }
        return {};
    };

    // println_uint
    builtins["cm_println_uint"] = [](std::vector<Value> args, const auto&) -> Value {
        if (!args.empty()) {
            if (args[0].type() == typeid(uint64_t)) {
                std::cout << std::any_cast<uint64_t>(args[0]) << std::endl;
            } else if (args[0].type() == typeid(int64_t)) {
                std::cout << static_cast<uint64_t>(std::any_cast<int64_t>(args[0])) << std::endl;
            }
        }
        return {};
    };

    // print_* 系
    builtins["cm_print_int"] = [](std::vector<Value> args, const auto&) -> Value {
        if (!args.empty() && args[0].type() == typeid(int64_t)) {
            std::cout << std::any_cast<int64_t>(args[0]);
        }
        return {};
    };

    builtins["cm_print_string"] = [](std::vector<Value> args, const auto&) -> Value {
        if (!args.empty() && args[0].type() == typeid(std::string)) {
            std::cout << std::any_cast<std::string>(args[0]);
        }
        return {};
    };

    builtins["cm_print_char"] = [](std::vector<Value> args, const auto&) -> Value {
        if (!args.empty() && args[0].type() == typeid(char)) {
            std::cout << std::any_cast<char>(args[0]);
        }
        return {};
    };

    builtins["cm_print_bool"] = [](std::vector<Value> args, const auto&) -> Value {
        if (!args.empty() && args[0].type() == typeid(bool)) {
            std::cout << (std::any_cast<bool>(args[0]) ? "true" : "false");
        }
        return {};
    };

    // cm_println_format - 可変引数フォーマット出力
    builtins["cm_println_format"] = [](std::vector<Value> args, const auto&) -> Value {
        if (args.size() < 2)
            return {};

        std::string format;
        if (args[0].type() == typeid(std::string)) {
            format = std::any_cast<std::string>(args[0]);
        } else {
            return {};
        }

        int64_t argc = 0;
        if (args[1].type() == typeid(int64_t)) {
            argc = std::any_cast<int64_t>(args[1]);
        }

        std::string result = FormatUtils::format_with_args(format, args, argc, 2);
        std::cout << result << std::endl;
        return {};
    };

    // cm_format_string - フォーマット変換（文字列を返す）
    builtins["cm_format_string"] = [](std::vector<Value> args, const auto&) -> Value {
        if (args.size() < 2)
            return Value(std::string(""));

        std::string format;
        if (args[0].type() == typeid(std::string)) {
            format = std::any_cast<std::string>(args[0]);
        } else {
            return Value(std::string(""));
        }

        int64_t argc = 0;
        if (args[1].type() == typeid(int64_t)) {
            argc = std::any_cast<int64_t>(args[1]);
        }

        return Value(FormatUtils::format_with_args(format, args, argc, 2));
    };
}

}  // namespace cm::mir::interp
