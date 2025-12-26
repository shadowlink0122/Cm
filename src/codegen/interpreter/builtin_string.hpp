#pragma once

#include "builtin_format.hpp"
#include "types.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace cm::mir::interp {

/// 文字列操作関数の登録
inline void register_string_builtins(BuiltinRegistry& builtins) {
    // 文字列連結
    builtins["cm_string_concat"] = [](std::vector<Value> args, const auto&) -> Value {
        std::string result;
        for (const auto& arg : args) {
            result += FormatUtils::value_to_string(arg);
        }
        return Value(result);
    };

    // 型変換関数: int -> string
    builtins["cm_int_to_string"] = [](std::vector<Value> args, const auto&) -> Value {
        if (args.empty())
            return Value(std::string(""));
        if (args[0].type() == typeid(int64_t)) {
            return Value(std::to_string(std::any_cast<int64_t>(args[0])));
        }
        return Value(std::string(""));
    };

    // 型変換関数: char -> string
    builtins["cm_char_to_string"] = [](std::vector<Value> args, const auto&) -> Value {
        if (args.empty())
            return Value(std::string(""));
        if (args[0].type() == typeid(char)) {
            return Value(std::string(1, std::any_cast<char>(args[0])));
        }
        return Value(std::string(""));
    };

    // 型変換関数: bool -> string
    builtins["cm_bool_to_string"] = [](std::vector<Value> args, const auto&) -> Value {
        if (args.empty())
            return Value(std::string(""));
        if (args[0].type() == typeid(bool)) {
            return Value(std::string(std::any_cast<bool>(args[0]) ? "true" : "false"));
        }
        return Value(std::string(""));
    };

    // 型変換関数: double -> string
    builtins["cm_double_to_string"] = [](std::vector<Value> args, const auto&) -> Value {
        if (args.empty())
            return Value(std::string(""));
        if (args[0].type() == typeid(double)) {
            double val = std::any_cast<double>(args[0]);
            if (val == static_cast<int64_t>(val)) {
                return Value(std::to_string(static_cast<int64_t>(val)));
            }
            char buf[64];
            snprintf(buf, sizeof(buf), "%g", val);
            return Value(std::string(buf));
        }
        return Value(std::string(""));
    };

    // 型変換関数: uint -> string
    builtins["cm_uint_to_string"] = [](std::vector<Value> args, const auto&) -> Value {
        if (args.empty())
            return Value(std::string(""));
        if (args[0].type() == typeid(int64_t)) {
            return Value(std::to_string(static_cast<uint64_t>(std::any_cast<int64_t>(args[0]))));
        } else if (args[0].type() == typeid(uint64_t)) {
            return Value(std::to_string(std::any_cast<uint64_t>(args[0])));
        }
        return Value(std::string(""));
    };

    // Debug/Display用のフォーマット関数
    builtins["cm_format_int"] = [](std::vector<Value> args, const auto&) -> Value {
        if (args.empty())
            return Value(std::string("0"));
        if (args[0].type() == typeid(int64_t)) {
            return Value(std::to_string(std::any_cast<int64_t>(args[0])));
        }
        return Value(std::string("0"));
    };

    builtins["cm_format_uint"] = [](std::vector<Value> args, const auto&) -> Value {
        if (args.empty())
            return Value(std::string("0"));
        if (args[0].type() == typeid(int64_t)) {
            return Value(std::to_string(static_cast<uint64_t>(std::any_cast<int64_t>(args[0]))));
        } else if (args[0].type() == typeid(uint64_t)) {
            return Value(std::to_string(std::any_cast<uint64_t>(args[0])));
        }
        return Value(std::string("0"));
    };

    builtins["cm_format_double"] = [](std::vector<Value> args, const auto&) -> Value {
        if (args.empty())
            return Value(std::string("0.0"));
        if (args[0].type() == typeid(double)) {
            double val = std::any_cast<double>(args[0]);
            if (val == static_cast<int64_t>(val)) {
                return Value(std::to_string(static_cast<int64_t>(val)));
            }
            char buf[64];
            snprintf(buf, sizeof(buf), "%g", val);
            return Value(std::string(buf));
        }
        return Value(std::string("0.0"));
    };

    builtins["cm_format_bool"] = [](std::vector<Value> args, const auto&) -> Value {
        if (args.empty())
            return Value(std::string("false"));
        if (args[0].type() == typeid(bool)) {
            return Value(std::string(std::any_cast<bool>(args[0]) ? "true" : "false"));
        }
        return Value(std::string("false"));
    };

    builtins["cm_format_char"] = [](std::vector<Value> args, const auto&) -> Value {
        if (args.empty())
            return Value(std::string(""));
        if (args[0].type() == typeid(char)) {
            return Value(std::string(1, std::any_cast<char>(args[0])));
        }
        return Value(std::string(""));
    };

    // 文字列の長さを取得
    builtins["__builtin_string_len"] = [](std::vector<Value> args, const auto&) -> Value {
        if (args.empty())
            return Value(uint64_t{0});
        if (args[0].type() == typeid(std::string)) {
            return Value(static_cast<uint64_t>(std::any_cast<std::string>(args[0]).size()));
        }
        return Value(uint64_t{0});
    };

    // 文字列の指定位置の文字を取得
    builtins["__builtin_string_charAt"] = [](std::vector<Value> args, const auto&) -> Value {
        if (args.size() < 2)
            return Value(char{0});
        if (args[0].type() != typeid(std::string))
            return Value(char{0});
        const auto& str = std::any_cast<std::string>(args[0]);
        int64_t index = 0;
        if (args[1].type() == typeid(int64_t))
            index = std::any_cast<int64_t>(args[1]);
        else if (args[1].type() == typeid(uint64_t))
            index = static_cast<int64_t>(std::any_cast<uint64_t>(args[1]));
        if (index < 0 || static_cast<size_t>(index) >= str.size())
            return Value(char{0});
        return Value(str[index]);
    };

    // 文字列の最初の文字を取得
    builtins["__builtin_string_first"] = [](std::vector<Value> args, const auto&) -> Value {
        if (args.empty())
            return Value(char{0});
        if (args[0].type() != typeid(std::string))
            return Value(char{0});
        const auto& str = std::any_cast<std::string>(args[0]);
        if (str.empty())
            return Value(char{0});
        return Value(str[0]);
    };

    // 文字列の最後の文字を取得
    builtins["__builtin_string_last"] = [](std::vector<Value> args, const auto&) -> Value {
        if (args.empty())
            return Value(char{0});
        if (args[0].type() != typeid(std::string))
            return Value(char{0});
        const auto& str = std::any_cast<std::string>(args[0]);
        if (str.empty())
            return Value(char{0});
        return Value(str[str.size() - 1]);
    };

    // 部分文字列を取得
    builtins["__builtin_string_substring"] = [](std::vector<Value> args, const auto&) -> Value {
        if (args.empty())
            return Value(std::string(""));
        if (args[0].type() != typeid(std::string))
            return Value(std::string(""));
        const auto& str = std::any_cast<std::string>(args[0]);
        int64_t start = 0, end = static_cast<int64_t>(str.size());
        if (args.size() > 1) {
            if (args[1].type() == typeid(int64_t))
                start = std::any_cast<int64_t>(args[1]);
            else if (args[1].type() == typeid(uint64_t))
                start = static_cast<int64_t>(std::any_cast<uint64_t>(args[1]));
        }
        if (args.size() > 2) {
            if (args[2].type() == typeid(int64_t))
                end = std::any_cast<int64_t>(args[2]);
            else if (args[2].type() == typeid(uint64_t))
                end = static_cast<int64_t>(std::any_cast<uint64_t>(args[2]));
        }
        // Python風: 負の値は末尾からの位置
        if (start < 0)
            start = std::max(int64_t{0}, static_cast<int64_t>(str.size()) + start);
        if (end < 0)
            end = static_cast<int64_t>(str.size()) + end + 1;
        if (end > static_cast<int64_t>(str.size()))
            end = static_cast<int64_t>(str.size());
        if (start >= end)
            return Value(std::string(""));
        return Value(str.substr(start, end - start));
    };

    // 部分文字列の位置を検索
    builtins["__builtin_string_indexOf"] = [](std::vector<Value> args, const auto&) -> Value {
        if (args.size() < 2)
            return Value(int64_t{-1});
        if (args[0].type() != typeid(std::string) || args[1].type() != typeid(std::string))
            return Value(int64_t{-1});
        const auto& str = std::any_cast<std::string>(args[0]);
        const auto& substr = std::any_cast<std::string>(args[1]);
        auto pos = str.find(substr);
        if (pos == std::string::npos)
            return Value(int64_t{-1});
        return Value(static_cast<int64_t>(pos));
    };

    // 大文字に変換
    builtins["__builtin_string_toUpperCase"] = [](std::vector<Value> args, const auto&) -> Value {
        if (args.empty() || args[0].type() != typeid(std::string))
            return Value(std::string(""));
        std::string str = std::any_cast<std::string>(args[0]);
        for (auto& c : str)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return Value(str);
    };

    // 小文字に変換
    builtins["__builtin_string_toLowerCase"] = [](std::vector<Value> args, const auto&) -> Value {
        if (args.empty() || args[0].type() != typeid(std::string))
            return Value(std::string(""));
        std::string str = std::any_cast<std::string>(args[0]);
        for (auto& c : str)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return Value(str);
    };

    // 空白を除去
    builtins["__builtin_string_trim"] = [](std::vector<Value> args, const auto&) -> Value {
        if (args.empty() || args[0].type() != typeid(std::string))
            return Value(std::string(""));
        std::string str = std::any_cast<std::string>(args[0]);
        size_t start = str.find_first_not_of(" \t\n\r\f\v");
        if (start == std::string::npos)
            return Value(std::string(""));
        size_t end = str.find_last_not_of(" \t\n\r\f\v");
        return Value(str.substr(start, end - start + 1));
    };

    // 先頭一致判定
    builtins["__builtin_string_startsWith"] = [](std::vector<Value> args, const auto&) -> Value {
        if (args.size() < 2)
            return Value(false);
        if (args[0].type() != typeid(std::string) || args[1].type() != typeid(std::string))
            return Value(false);
        const auto& str = std::any_cast<std::string>(args[0]);
        const auto& prefix = std::any_cast<std::string>(args[1]);
        return Value(str.rfind(prefix, 0) == 0);
    };

    // 末尾一致判定
    builtins["__builtin_string_endsWith"] = [](std::vector<Value> args, const auto&) -> Value {
        if (args.size() < 2)
            return Value(false);
        if (args[0].type() != typeid(std::string) || args[1].type() != typeid(std::string))
            return Value(false);
        const auto& str = std::any_cast<std::string>(args[0]);
        const auto& suffix = std::any_cast<std::string>(args[1]);
        if (suffix.size() > str.size())
            return Value(false);
        return Value(str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0);
    };

    // 部分文字列を含むか判定
    builtins["__builtin_string_includes"] = [](std::vector<Value> args, const auto&) -> Value {
        if (args.size() < 2)
            return Value(false);
        if (args[0].type() != typeid(std::string) || args[1].type() != typeid(std::string))
            return Value(false);
        const auto& str = std::any_cast<std::string>(args[0]);
        const auto& substr = std::any_cast<std::string>(args[1]);
        return Value(str.find(substr) != std::string::npos);
    };

    // 文字列を繰り返す
    builtins["__builtin_string_repeat"] = [](std::vector<Value> args, const auto&) -> Value {
        if (args.size() < 2)
            return Value(std::string(""));
        if (args[0].type() != typeid(std::string))
            return Value(std::string(""));
        const auto& str = std::any_cast<std::string>(args[0]);
        int64_t count = 0;
        if (args[1].type() == typeid(int64_t))
            count = std::any_cast<int64_t>(args[1]);
        else if (args[1].type() == typeid(uint64_t))
            count = static_cast<int64_t>(std::any_cast<uint64_t>(args[1]));
        if (count <= 0)
            return Value(std::string(""));
        std::string result;
        result.reserve(str.size() * count);
        for (int64_t i = 0; i < count; ++i)
            result += str;
        return Value(result);
    };

    // 文字列を置換
    builtins["__builtin_string_replace"] = [](std::vector<Value> args, const auto&) -> Value {
        if (args.size() < 3)
            return args.empty() ? Value(std::string("")) : args[0];
        if (args[0].type() != typeid(std::string) || args[1].type() != typeid(std::string) ||
            args[2].type() != typeid(std::string)) {
            return args[0];
        }
        std::string str = std::any_cast<std::string>(args[0]);
        const auto& from = std::any_cast<std::string>(args[1]);
        const auto& to = std::any_cast<std::string>(args[2]);
        size_t pos = str.find(from);
        if (pos != std::string::npos) {
            str.replace(pos, from.length(), to);
        }
        return Value(str);
    };
}

}  // namespace cm::mir::interp
