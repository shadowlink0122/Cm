#pragma once

#include "../../common/format_string.hpp"
#include "builtin_array.hpp"
#include "builtin_format.hpp"
#include "builtin_io.hpp"
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
        register_std_io_functions();
    }

   private:
    BuiltinRegistry builtins_;

    /// std::io 名前空間の関数を登録
    void register_std_io_functions() {
        builtins_["std::io::println"] = [this](std::vector<Value> args,
                                               const auto& locals) -> Value {
            return format_println(args, locals, true);
        };

        builtins_["std::io::print"] = [this](std::vector<Value> args, const auto& locals) -> Value {
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
