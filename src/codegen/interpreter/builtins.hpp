#pragma once

#include "../../common/format_string.hpp"
#include "builtin_array.hpp"
#include "builtin_format.hpp"
#include "builtin_io.hpp"
#include "builtin_slice.hpp"
#include "builtin_string.hpp"
#include "types.hpp"

#include <iostream>

namespace cm::mir::interp {

/// 組み込み関数を登録するクラス
class BuiltinManager {
   public:
    /// レジストリを取得
    BuiltinRegistry& registry() { return builtins_; }
    const BuiltinRegistry& registry() const { return builtins_; }

    /// 初期化（組み込み関数を登録）
    void initialize() {
        register_io_builtins(builtins_);
        register_string_builtins(builtins_);
        register_array_builtins(builtins_);
        register_slice_builtins(builtins_);
        register_std_io_functions();
        register_libc_ffi_functions();
    }

   private:
    BuiltinRegistry builtins_;

    /// libc FFI関数を登録（インタプリタ用）
    void register_libc_ffi_functions() {
        // puts - 文字列を出力して改行
        builtins_["puts"] = [](std::vector<Value> args, const auto& /* locals */) -> Value {
            if (!args.empty() && args[0].type() == typeid(std::string)) {
                std::cout << std::any_cast<std::string>(args[0]) << std::endl;
                return Value(int64_t(0));
            }
            return Value(int64_t(-1));
        };

        // printf - フォーマット出力（簡易実装）
        builtins_["printf"] = [](std::vector<Value> args, const auto& /* locals */) -> Value {
            if (args.empty())
                return Value(int64_t(0));

            std::string format;
            if (args[0].type() == typeid(std::string)) {
                format = std::any_cast<std::string>(args[0]);
            } else {
                return Value(int64_t(-1));
            }

            // 簡易printf実装：%d, %s, %f をサポート
            std::string output;
            size_t arg_idx = 1;
            for (size_t i = 0; i < format.size(); ++i) {
                if (format[i] == '%' && i + 1 < format.size()) {
                    char spec = format[i + 1];
                    if (spec == 'd' || spec == 'i') {
                        if (arg_idx < args.size()) {
                            if (args[arg_idx].type() == typeid(int64_t)) {
                                output += std::to_string(std::any_cast<int64_t>(args[arg_idx]));
                            }
                            arg_idx++;
                        }
                        i++;
                    } else if (spec == 's') {
                        if (arg_idx < args.size()) {
                            if (args[arg_idx].type() == typeid(std::string)) {
                                output += std::any_cast<std::string>(args[arg_idx]);
                            }
                            arg_idx++;
                        }
                        i++;
                    } else if (spec == 'f') {
                        if (arg_idx < args.size()) {
                            if (args[arg_idx].type() == typeid(double)) {
                                output += std::to_string(std::any_cast<double>(args[arg_idx]));
                            }
                            arg_idx++;
                        }
                        i++;
                    } else if (spec == '%') {
                        output += '%';
                        i++;
                    } else {
                        output += format[i];
                    }
                } else if (format[i] == '\\' && i + 1 < format.size() && format[i + 1] == 'n') {
                    output += '\n';
                    i++;
                } else {
                    output += format[i];
                }
            }
            std::cout << output;
            return Value(int64_t(output.size()));
        };

        // strlen - 文字列長を取得
        builtins_["strlen"] = [](std::vector<Value> args, const auto& /* locals */) -> Value {
            if (!args.empty() && args[0].type() == typeid(std::string)) {
                return Value(int64_t(std::any_cast<std::string>(args[0]).size()));
            }
            return Value(int64_t(0));
        };

        // malloc - メモリ確保（インタプリタでは実際のmallocを呼ぶ）
        builtins_["malloc"] = [](std::vector<Value> args, const auto& /* locals */) -> Value {
            if (!args.empty() && args[0].type() == typeid(int64_t)) {
                size_t size = static_cast<size_t>(std::any_cast<int64_t>(args[0]));
                void* ptr = std::malloc(size);
                // PointerValueとして返す（外部メモリへの参照）
                PointerValue pv;
                pv.target_local = static_cast<LocalId>(-1);  // 無効なローカルID
                pv.raw_ptr = ptr;
                return Value(pv);
            }
            PointerValue null_ptr;
            null_ptr.target_local = static_cast<LocalId>(-1);
            null_ptr.raw_ptr = nullptr;
            return Value(null_ptr);
        };

        // realloc - メモリ再確保
        builtins_["realloc"] = [](std::vector<Value> args, const auto& /* locals */) -> Value {
            if (args.size() >= 2) {
                void* old_ptr = nullptr;
                if (args[0].type() == typeid(PointerValue)) {
                    old_ptr = std::any_cast<PointerValue>(args[0]).raw_ptr;
                } else if (args[0].type() == typeid(int64_t)) {
                    old_ptr = reinterpret_cast<void*>(std::any_cast<int64_t>(args[0]));
                }

                size_t new_size = 0;
                if (args[1].type() == typeid(int64_t)) {
                    new_size = static_cast<size_t>(std::any_cast<int64_t>(args[1]));
                } else if (args[1].type() == typeid(uint64_t)) {
                    new_size = static_cast<size_t>(std::any_cast<uint64_t>(args[1]));
                }

                void* new_ptr = std::realloc(old_ptr, new_size);
                PointerValue pv;
                pv.target_local = static_cast<LocalId>(-1);
                pv.raw_ptr = new_ptr;
                return Value(pv);
            }
            PointerValue null_ptr;
            null_ptr.target_local = static_cast<LocalId>(-1);
            null_ptr.raw_ptr = nullptr;
            return Value(null_ptr);
        };

        // calloc - ゼロ初期化メモリ確保
        builtins_["calloc"] = [](std::vector<Value> args, const auto& /* locals */) -> Value {
            if (args.size() >= 2) {
                size_t nmemb = 0;
                size_t size = 0;
                if (args[0].type() == typeid(int64_t)) {
                    nmemb = static_cast<size_t>(std::any_cast<int64_t>(args[0]));
                }
                if (args[1].type() == typeid(int64_t)) {
                    size = static_cast<size_t>(std::any_cast<int64_t>(args[1]));
                }

                void* ptr = std::calloc(nmemb, size);
                PointerValue pv;
                pv.target_local = static_cast<LocalId>(-1);
                pv.raw_ptr = ptr;
                return Value(pv);
            }
            PointerValue null_ptr;
            null_ptr.target_local = static_cast<LocalId>(-1);
            null_ptr.raw_ptr = nullptr;
            return Value(null_ptr);
        };

        // free - メモリ解放
        builtins_["free"] = [](std::vector<Value> args, const auto& /* locals */) -> Value {
            if (!args.empty()) {
                void* ptr = nullptr;
                if (args[0].type() == typeid(PointerValue)) {
                    ptr = std::any_cast<PointerValue>(args[0]).raw_ptr;
                } else if (args[0].type() == typeid(int64_t)) {
                    ptr = reinterpret_cast<void*>(std::any_cast<int64_t>(args[0]));
                }
                if (ptr) {
                    std::free(ptr);
                }
            }
            return Value{};
        };
    }

    /// std::io 名前空間の関数を登録
    void register_std_io_functions() {
        builtins_["std::io::println"] = [this](std::vector<Value> args,
                                               const auto& locals) -> Value {
            return format_println(args, locals, true);
        };

        builtins_["std::io::print"] = [this](std::vector<Value> args, const auto& locals) -> Value {
            return format_println(args, locals, false);
        };

        // println と print は短縮形（import std::io::println 後に使用）
        builtins_["println"] = [this](std::vector<Value> args, const auto& locals) -> Value {
            return format_println(args, locals, true);
        };

        builtins_["print"] = [this](std::vector<Value> args, const auto& locals) -> Value {
            return format_println(args, locals, false);
        };

        // __println__ と __print__ は内部実装として登録
        builtins_["__println__"] = [this](std::vector<Value> args, const auto& locals) -> Value {
            return format_println(args, locals, true);
        };

        builtins_["__print__"] = [this](std::vector<Value> args, const auto& locals) -> Value {
            return format_println(args, locals, false);
        };
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
            std::cout << FormatUtils::value_to_string(args[0]);
        }

        if (newline)
            std::cout << std::endl;
        return {};
    }
};

}  // namespace cm::mir::interp
