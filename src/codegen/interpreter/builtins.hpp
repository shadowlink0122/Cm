#pragma once

#include "../../common/format_string.hpp"
#include "types.hpp"

#include <bitset>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace cm::mir::interp {

/// 組み込み関数を登録するクラス
class BuiltinManager {
   public:
    /// レジストリを取得
    BuiltinRegistry& registry() { return builtins_; }
    const BuiltinRegistry& registry() const { return builtins_; }

    /// 初期化（組み込み関数を登録）
    void initialize() {
        register_io_functions();
        register_format_functions();
        register_string_functions();
    }

   private:
    BuiltinRegistry builtins_;

    /// I/O関数の登録
    void register_io_functions() {
        // println_int
        builtins_["cm_println_int"] = [](std::vector<Value> args, const auto&) -> Value {
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
        builtins_["cm_println_string"] = [](std::vector<Value> args, const auto&) -> Value {
            if (!args.empty() && args[0].type() == typeid(std::string)) {
                std::cout << std::any_cast<std::string>(args[0]) << std::endl;
            }
            return {};
        };

        // println_double
        builtins_["cm_println_double"] = [](std::vector<Value> args, const auto&) -> Value {
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
        builtins_["cm_println_char"] = [](std::vector<Value> args, const auto&) -> Value {
            if (!args.empty() && args[0].type() == typeid(char)) {
                std::cout << std::any_cast<char>(args[0]) << std::endl;
            }
            return {};
        };

        // println_bool
        builtins_["cm_println_bool"] = [](std::vector<Value> args, const auto&) -> Value {
            if (!args.empty() && args[0].type() == typeid(bool)) {
                std::cout << (std::any_cast<bool>(args[0]) ? "true" : "false") << std::endl;
            }
            return {};
        };

        // println_uint
        builtins_["cm_println_uint"] = [](std::vector<Value> args, const auto&) -> Value {
            if (!args.empty()) {
                if (args[0].type() == typeid(uint64_t)) {
                    std::cout << std::any_cast<uint64_t>(args[0]) << std::endl;
                } else if (args[0].type() == typeid(int64_t)) {
                    std::cout << static_cast<uint64_t>(std::any_cast<int64_t>(args[0]))
                              << std::endl;
                }
            }
            return {};
        };

        // print_* 系
        builtins_["cm_print_int"] = [](std::vector<Value> args, const auto&) -> Value {
            if (!args.empty() && args[0].type() == typeid(int64_t)) {
                std::cout << std::any_cast<int64_t>(args[0]);
            }
            return {};
        };

        builtins_["cm_print_string"] = [](std::vector<Value> args, const auto&) -> Value {
            if (!args.empty() && args[0].type() == typeid(std::string)) {
                std::cout << std::any_cast<std::string>(args[0]);
            }
            return {};
        };

        builtins_["cm_print_char"] = [](std::vector<Value> args, const auto&) -> Value {
            if (!args.empty() && args[0].type() == typeid(char)) {
                std::cout << std::any_cast<char>(args[0]);
            }
            return {};
        };

        builtins_["cm_print_bool"] = [](std::vector<Value> args, const auto&) -> Value {
            if (!args.empty() && args[0].type() == typeid(bool)) {
                std::cout << (std::any_cast<bool>(args[0]) ? "true" : "false");
            }
            return {};
        };

        // std::io 名前空間
        builtins_["std::io::println"] = [this](std::vector<Value> args,
                                               const auto& locals) -> Value {
            return format_println(args, locals, true);
        };

        builtins_["std::io::print"] = [this](std::vector<Value> args, const auto& locals) -> Value {
            return format_println(args, locals, false);
        };
    }

    /// フォーマット関数の登録
    void register_format_functions() {
        // cm_println_format - 可変引数フォーマット出力
        builtins_["cm_println_format"] = [](std::vector<Value> args, const auto&) -> Value {
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

            std::string result = format_with_args(format, args, argc, 2);
            std::cout << result << std::endl;
            return {};
        };

        // cm_format_string - フォーマット変換（文字列を返す）
        builtins_["cm_format_string"] = [](std::vector<Value> args, const auto&) -> Value {
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

            return Value(format_with_args(format, args, argc, 2));
        };
    }

    /// 文字列操作関数の登録
    void register_string_functions() {
        // 文字列連結
        builtins_["cm_string_concat"] = [](std::vector<Value> args, const auto&) -> Value {
            std::string result;
            for (const auto& arg : args) {
                result += value_to_string(arg);
            }
            return Value(result);
        };

        // 型変換関数: int -> string
        builtins_["cm_int_to_string"] = [](std::vector<Value> args, const auto&) -> Value {
            if (args.empty())
                return Value(std::string(""));
            if (args[0].type() == typeid(int64_t)) {
                return Value(std::to_string(std::any_cast<int64_t>(args[0])));
            }
            return Value(std::string(""));
        };

        // 型変換関数: char -> string
        builtins_["cm_char_to_string"] = [](std::vector<Value> args, const auto&) -> Value {
            if (args.empty())
                return Value(std::string(""));
            if (args[0].type() == typeid(char)) {
                return Value(std::string(1, std::any_cast<char>(args[0])));
            }
            return Value(std::string(""));
        };

        // 型変換関数: bool -> string
        builtins_["cm_bool_to_string"] = [](std::vector<Value> args, const auto&) -> Value {
            if (args.empty())
                return Value(std::string(""));
            if (args[0].type() == typeid(bool)) {
                return Value(std::string(std::any_cast<bool>(args[0]) ? "true" : "false"));
            }
            return Value(std::string(""));
        };

        // 型変換関数: double -> string
        builtins_["cm_double_to_string"] = [](std::vector<Value> args, const auto&) -> Value {
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
        builtins_["cm_uint_to_string"] = [](std::vector<Value> args, const auto&) -> Value {
            if (args.empty())
                return Value(std::string(""));
            if (args[0].type() == typeid(int64_t)) {
                return Value(
                    std::to_string(static_cast<uint64_t>(std::any_cast<int64_t>(args[0]))));
            } else if (args[0].type() == typeid(uint64_t)) {
                return Value(std::to_string(std::any_cast<uint64_t>(args[0])));
            }
            return Value(std::string(""));
        };

        // 文字列の長さを取得
        builtins_["__builtin_string_len"] = [](std::vector<Value> args, const auto&) -> Value {
            if (args.empty())
                return Value(uint64_t{0});
            if (args[0].type() == typeid(std::string)) {
                return Value(static_cast<uint64_t>(std::any_cast<std::string>(args[0]).size()));
            }
            return Value(uint64_t{0});
        };

        // 文字列の指定位置の文字を取得
        builtins_["__builtin_string_charAt"] = [](std::vector<Value> args, const auto&) -> Value {
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

        // 部分文字列を取得
        builtins_["__builtin_string_substring"] = [](std::vector<Value> args,
                                                     const auto&) -> Value {
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
            // Python風: 負の値は末尾からの位置（-1は最後）
            if (start < 0)
                start = std::max(int64_t{0}, static_cast<int64_t>(str.size()) + start);
            if (end < 0)
                end = static_cast<int64_t>(str.size()) + end + 1;  // -1 => len
            if (end > static_cast<int64_t>(str.size()))
                end = static_cast<int64_t>(str.size());
            if (start >= end)
                return Value(std::string(""));
            return Value(str.substr(start, end - start));
        };

        // 部分文字列の位置を検索
        builtins_["__builtin_string_indexOf"] = [](std::vector<Value> args, const auto&) -> Value {
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
        builtins_["__builtin_string_toUpperCase"] = [](std::vector<Value> args,
                                                       const auto&) -> Value {
            if (args.empty() || args[0].type() != typeid(std::string))
                return Value(std::string(""));
            std::string str = std::any_cast<std::string>(args[0]);
            for (auto& c : str)
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            return Value(str);
        };

        // 小文字に変換
        builtins_["__builtin_string_toLowerCase"] = [](std::vector<Value> args,
                                                       const auto&) -> Value {
            if (args.empty() || args[0].type() != typeid(std::string))
                return Value(std::string(""));
            std::string str = std::any_cast<std::string>(args[0]);
            for (auto& c : str)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return Value(str);
        };

        // 空白を除去
        builtins_["__builtin_string_trim"] = [](std::vector<Value> args, const auto&) -> Value {
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
        builtins_["__builtin_string_startsWith"] = [](std::vector<Value> args,
                                                      const auto&) -> Value {
            if (args.size() < 2)
                return Value(false);
            if (args[0].type() != typeid(std::string) || args[1].type() != typeid(std::string))
                return Value(false);
            const auto& str = std::any_cast<std::string>(args[0]);
            const auto& prefix = std::any_cast<std::string>(args[1]);
            return Value(str.rfind(prefix, 0) == 0);
        };

        // 末尾一致判定
        builtins_["__builtin_string_endsWith"] = [](std::vector<Value> args, const auto&) -> Value {
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
        builtins_["__builtin_string_includes"] = [](std::vector<Value> args, const auto&) -> Value {
            if (args.size() < 2)
                return Value(false);
            if (args[0].type() != typeid(std::string) || args[1].type() != typeid(std::string))
                return Value(false);
            const auto& str = std::any_cast<std::string>(args[0]);
            const auto& substr = std::any_cast<std::string>(args[1]);
            return Value(str.find(substr) != std::string::npos);
        };

        // 文字列を繰り返す
        builtins_["__builtin_string_repeat"] = [](std::vector<Value> args, const auto&) -> Value {
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
        builtins_["__builtin_string_replace"] = [](std::vector<Value> args, const auto&) -> Value {
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

        // 配列スライス
        builtins_["__builtin_array_slice"] = [](std::vector<Value> args, const auto&) -> Value {
            // args: [array, elem_size, arr_len, start, end]
            if (args.size() < 5)
                return Value(std::vector<Value>{});

            // 配列を取得
            if (args[0].type() != typeid(std::vector<Value>)) {
                return Value(std::vector<Value>{});
            }
            const auto& arr = std::any_cast<std::vector<Value>>(args[0]);

            int64_t arr_len = 0;
            if (args[2].type() == typeid(int64_t))
                arr_len = std::any_cast<int64_t>(args[2]);
            else if (args[2].type() == typeid(uint64_t))
                arr_len = static_cast<int64_t>(std::any_cast<uint64_t>(args[2]));

            int64_t start = 0;
            if (args[3].type() == typeid(int64_t))
                start = std::any_cast<int64_t>(args[3]);
            else if (args[3].type() == typeid(uint64_t))
                start = static_cast<int64_t>(std::any_cast<uint64_t>(args[3]));

            int64_t end = arr_len;
            if (args[4].type() == typeid(int64_t))
                end = std::any_cast<int64_t>(args[4]);
            else if (args[4].type() == typeid(uint64_t))
                end = static_cast<int64_t>(std::any_cast<uint64_t>(args[4]));

            // Python風: 負のインデックス処理
            if (start < 0)
                start = std::max(int64_t{0}, arr_len + start);
            if (end < 0)
                end = arr_len + end + 1;
            if (end > arr_len)
                end = arr_len;
            if (start >= end || start >= arr_len) {
                return Value(std::vector<Value>{});
            }

            std::vector<Value> result;
            for (int64_t i = start; i < end; i++) {
                if (i < static_cast<int64_t>(arr.size())) {
                    result.push_back(arr[i]);
                }
            }
            return Value(result);
        };

        // 配列 forEach
        builtins_["__builtin_array_forEach"] = [](std::vector<Value> args,
                                                  const auto& /*call_func*/) -> Value {
            if (args.size() < 3)
                return Value{};
            if (args[0].type() != typeid(std::vector<Value>))
                return Value{};
            const auto& arr = std::any_cast<std::vector<Value>>(args[0]);
            // int64_t size = 0;  // 未使用
            // if (args[1].type() == typeid(int64_t))
            //     size = std::any_cast<int64_t>(args[1]);
            // args[2] は関数ポインタ（インタプリタでは直接呼び出せないため、forループで代替）
            // この実装はプレースホルダー - 実際の実装は呼び出し側で行う
            return Value{};
        };

        // 配列 reduce
        builtins_["__builtin_array_reduce"] = [](std::vector<Value> args, const auto&) -> Value {
            // プレースホルダー実装
            if (args.size() < 4)
                return Value(int64_t{0});
            // 初期値を返す
            return args[3];
        };

        // 配列 some
        builtins_["__builtin_array_some"] = [](std::vector<Value> /*args*/, const auto&) -> Value {
            // プレースホルダー実装
            return Value(false);
        };

        // 配列 every
        builtins_["__builtin_array_every"] = [](std::vector<Value> /*args*/, const auto&) -> Value {
            // プレースホルダー実装
            return Value(true);
        };

        // 配列 findIndex
        builtins_["__builtin_array_findIndex"] = [](std::vector<Value> /*args*/, const auto&) -> Value {
            // プレースホルダー実装
            return Value(int64_t{-1});
        };

        // 配列 indexOf（PointerValue/ArrayValueに対応）
        auto indexOf_impl = [](std::vector<Value> args, const auto& locals) -> Value {
            if (args.size() < 3)
                return Value(int64_t{-1});

            // 配列データを取得
            std::vector<Value> const* arr_ptr = nullptr;
            ArrayValue av_storage;  // ストレージ用

            if (args[0].type() == typeid(PointerValue)) {
                // ポインタから配列を解決
                const auto& pv = std::any_cast<PointerValue>(args[0]);
                auto it = locals.find(pv.target_local);
                if (it != locals.end() && it->second.type() == typeid(ArrayValue)) {
                    arr_ptr = &std::any_cast<const ArrayValue&>(it->second).elements;
                }
            } else if (args[0].type() == typeid(ArrayValue)) {
                arr_ptr = &std::any_cast<const ArrayValue&>(args[0]).elements;
            } else if (args[0].type() == typeid(std::vector<Value>)) {
                arr_ptr = &std::any_cast<const std::vector<Value>&>(args[0]);
            }

            if (!arr_ptr)
                return Value(int64_t{-1});
            const auto& arr = *arr_ptr;

            int64_t size = 0;
            if (args[1].type() == typeid(int64_t))
                size = std::any_cast<int64_t>(args[1]);
            const auto& target = args[2];

            for (int64_t i = 0; i < size && i < static_cast<int64_t>(arr.size()); i++) {
                // 整数比較
                if (arr[i].type() == typeid(int64_t) && target.type() == typeid(int64_t)) {
                    if (std::any_cast<int64_t>(arr[i]) == std::any_cast<int64_t>(target)) {
                        return Value(i);
                    }
                }
            }
            return Value(int64_t{-1});
        };
        builtins_["__builtin_array_indexOf"] = indexOf_impl;
        builtins_["__builtin_array_indexOf_i32"] = indexOf_impl;

        // 配列 includes（PointerValue/ArrayValueに対応）
        auto includes_impl = [](std::vector<Value> args, const auto& locals) -> Value {
            if (args.size() < 3)
                return Value(false);

            // 配列データを取得
            std::vector<Value> const* arr_ptr = nullptr;

            if (args[0].type() == typeid(PointerValue)) {
                const auto& pv = std::any_cast<PointerValue>(args[0]);
                auto it = locals.find(pv.target_local);
                if (it != locals.end() && it->second.type() == typeid(ArrayValue)) {
                    arr_ptr = &std::any_cast<const ArrayValue&>(it->second).elements;
                }
            } else if (args[0].type() == typeid(ArrayValue)) {
                arr_ptr = &std::any_cast<const ArrayValue&>(args[0]).elements;
            } else if (args[0].type() == typeid(std::vector<Value>)) {
                arr_ptr = &std::any_cast<const std::vector<Value>&>(args[0]);
            }

            if (!arr_ptr)
                return Value(false);
            const auto& arr = *arr_ptr;

            int64_t size = 0;
            if (args[1].type() == typeid(int64_t))
                size = std::any_cast<int64_t>(args[1]);
            const auto& target = args[2];

            for (int64_t i = 0; i < size && i < static_cast<int64_t>(arr.size()); i++) {
                if (arr[i].type() == typeid(int64_t) && target.type() == typeid(int64_t)) {
                    if (std::any_cast<int64_t>(arr[i]) == std::any_cast<int64_t>(target)) {
                        return Value(true);
                    }
                }
            }
            return Value(false);
        };
        builtins_["__builtin_array_includes"] = includes_impl;
        builtins_["__builtin_array_includes_i32"] = includes_impl;
    }

    /// フォーマット文字列を処理
    static std::string format_with_args(const std::string& format, const std::vector<Value>& args,
                                        int64_t argc, size_t start_idx) {
        std::string result;
        size_t pos = 0;
        int arg_index = 0;

        while (pos < format.length()) {
            // エスケープされた {{ を処理
            if (pos + 1 < format.length() && format[pos] == '{' && format[pos + 1] == '{') {
                result += '{';
                pos += 2;
                continue;
            }
            // エスケープされた }} を処理
            if (pos + 1 < format.length() && format[pos] == '}' && format[pos + 1] == '}') {
                result += '}';
                pos += 2;
                continue;
            }

            if (format[pos] == '{') {
                size_t end = pos + 1;
                while (end < format.length() && format[end] != '}') {
                    end++;
                }

                if (end < format.length() && arg_index < argc &&
                    start_idx + arg_index < args.size()) {
                    std::string spec = format.substr(pos + 1, end - pos - 1);
                    const auto& arg = args[start_idx + arg_index];
                    result += format_value(arg, spec);
                    arg_index++;
                }
                pos = (end < format.length()) ? end + 1 : format.length();
            } else {
                result += format[pos];
                pos++;
            }
        }

        return result;
    }

    /// 値をフォーマット
    static std::string format_value(const Value& arg, const std::string& spec) {
        // フォーマット指定子を解析
        std::string alignment;
        char fill_char = ' ';
        int width = 0;
        int precision = -1;
        std::string type_spec;

        size_t spec_pos = 0;
        if (!spec.empty() && spec[0] == ':') {
            spec_pos = 1;

            // ゼロパディング
            if (spec_pos < spec.length() && spec[spec_pos] == '0') {
                fill_char = '0';
                spec_pos++;
            }

            // アラインメント
            if (spec_pos < spec.length() &&
                (spec[spec_pos] == '<' || spec[spec_pos] == '>' || spec[spec_pos] == '^')) {
                alignment = spec[spec_pos];
                spec_pos++;
            }

            // 幅
            std::string width_str;
            while (spec_pos < spec.length() && std::isdigit(spec[spec_pos])) {
                width_str += spec[spec_pos++];
            }
            if (!width_str.empty()) {
                width = std::stoi(width_str);
            }

            // 精度
            if (spec_pos < spec.length() && spec[spec_pos] == '.') {
                spec_pos++;
                std::string prec_str;
                while (spec_pos < spec.length() && std::isdigit(spec[spec_pos])) {
                    prec_str += spec[spec_pos++];
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

        std::string formatted = format_value_with_type(arg, type_spec, precision);

        // 幅とアラインメントを適用
        if (width > 0 && static_cast<int>(formatted.length()) < width) {
            int padding = width - formatted.length();
            if (alignment == ">") {
                formatted = std::string(padding, fill_char) + formatted;
            } else if (alignment == "^") {
                int left_pad = padding / 2;
                int right_pad = padding - left_pad;
                formatted = std::string(left_pad, ' ') + formatted + std::string(right_pad, ' ');
            } else {
                formatted = formatted + std::string(padding, ' ');
            }
        }

        return formatted;
    }

    /// 型指定子に従って値をフォーマット
    static std::string format_value_with_type(const Value& arg, const std::string& type_spec,
                                              int precision) {
        if (arg.type() == typeid(int64_t)) {
            int64_t val = std::any_cast<int64_t>(arg);
            if (type_spec == "x") {
                char buf[32];
                snprintf(buf, sizeof(buf), "%llx", (long long)val);
                return buf;
            } else if (type_spec == "X") {
                char buf[32];
                snprintf(buf, sizeof(buf), "%llX", (long long)val);
                return buf;
            } else if (type_spec == "b") {
                if (val == 0)
                    return "0";
                std::string binary;
                int64_t temp = val;
                while (temp > 0) {
                    binary = (char)('0' + (temp & 1)) + binary;
                    temp >>= 1;
                }
                return binary;
            } else if (type_spec == "o") {
                char buf[32];
                snprintf(buf, sizeof(buf), "%llo", (long long)val);
                return buf;
            }
            return std::to_string(val);
        } else if (arg.type() == typeid(uint64_t)) {
            uint64_t val = std::any_cast<uint64_t>(arg);
            if (type_spec == "x") {
                char buf[32];
                snprintf(buf, sizeof(buf), "%llx", (unsigned long long)val);
                return buf;
            } else if (type_spec == "X") {
                char buf[32];
                snprintf(buf, sizeof(buf), "%llX", (unsigned long long)val);
                return buf;
            } else if (type_spec == "b") {
                if (val == 0)
                    return "0";
                std::string binary;
                uint64_t temp = val;
                while (temp > 0) {
                    binary = (char)('0' + (temp & 1)) + binary;
                    temp >>= 1;
                }
                return binary;
            } else if (type_spec == "o") {
                char buf[32];
                snprintf(buf, sizeof(buf), "%llo", (unsigned long long)val);
                return buf;
            }
            return std::to_string(val);
        } else if (arg.type() == typeid(double)) {
            double val = std::any_cast<double>(arg);
            char buf[64];

            // 科学記法（小文字）
            if (type_spec == "e") {
                if (precision >= 0) {
                    snprintf(buf, sizeof(buf), "%.*e", precision, val);
                } else {
                    snprintf(buf, sizeof(buf), "%e", val);
                }
                return buf;
            }
            // 科学記法（大文字）
            else if (type_spec == "E") {
                if (precision >= 0) {
                    snprintf(buf, sizeof(buf), "%.*E", precision, val);
                } else {
                    snprintf(buf, sizeof(buf), "%E", val);
                }
                return buf;
            }
            // 固定小数点
            else if (type_spec == "f" || type_spec == "F") {
                if (precision >= 0) {
                    snprintf(buf, sizeof(buf), "%.*f", precision, val);
                } else {
                    snprintf(buf, sizeof(buf), "%f", val);
                }
                return buf;
            }
            // デフォルト
            else if (precision >= 0) {
                snprintf(buf, sizeof(buf), "%.*f", precision, val);
                return buf;
            } else if (val == static_cast<int64_t>(val)) {
                return std::to_string(static_cast<int64_t>(val));
            } else {
                snprintf(buf, sizeof(buf), "%g", val);
                return buf;
            }
        } else if (arg.type() == typeid(bool)) {
            return std::any_cast<bool>(arg) ? "true" : "false";
        } else if (arg.type() == typeid(char)) {
            return std::string(1, std::any_cast<char>(arg));
        } else if (arg.type() == typeid(std::string)) {
            return std::any_cast<std::string>(arg);
        }
        return "{}";
    }

    /// 値を文字列に変換
    static std::string value_to_string(const Value& val) {
        if (val.type() == typeid(std::string)) {
            return std::any_cast<std::string>(val);
        } else if (val.type() == typeid(int64_t)) {
            return std::to_string(std::any_cast<int64_t>(val));
        } else if (val.type() == typeid(uint64_t)) {
            return std::to_string(std::any_cast<uint64_t>(val));
        } else if (val.type() == typeid(double)) {
            return std::to_string(std::any_cast<double>(val));
        } else if (val.type() == typeid(char)) {
            return std::string(1, std::any_cast<char>(val));
        } else if (val.type() == typeid(bool)) {
            return std::any_cast<bool>(val) ? "true" : "false";
        }
        return "";
    }

    /// フォーマット付きprintln の共通処理
    Value format_println(std::vector<Value>& args, const auto& /* locals */, bool newline) {
        if (args.empty()) {
            if (newline)
                std::cout << std::endl;
            return {};
        }

        if (args[0].type() == typeid(std::string)) {
            std::string format_str = std::any_cast<std::string>(args[0]);

            // プレースホルダがある場合はフォーマット
            if (format_str.find('{') != std::string::npos) {
                std::vector<std::any> format_args;
                for (size_t i = 1; i < args.size(); ++i) {
                    format_args.push_back(args[i]);
                }
                std::string output = FormatStringFormatter::format(format_str, format_args);
                std::cout << output;
            } else {
                std::cout << format_str;
            }
        } else {
            // 単純出力
            std::cout << value_to_string(args[0]);
        }

        if (newline)
            std::cout << std::endl;
        return {};
    }
};

}  // namespace cm::mir::interp
