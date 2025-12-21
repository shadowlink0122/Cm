#pragma once

#include "types.hpp"

#include <algorithm>

namespace cm::mir::interp {

/// 配列操作関数の登録
inline void register_array_builtins(BuiltinRegistry& builtins) {
    // 配列スライス
    builtins["__builtin_array_slice"] = [](std::vector<Value> args, const auto&) -> Value {
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
    builtins["__builtin_array_forEach"] = [](std::vector<Value> args,
                                             const auto& /*call_func*/) -> Value {
        if (args.size() < 3)
            return Value{};
        if (args[0].type() != typeid(std::vector<Value>))
            return Value{};
        // プレースホルダー実装
        return Value{};
    };

    // 配列 reduce
    builtins["__builtin_array_reduce"] = [](std::vector<Value> args, const auto&) -> Value {
        // プレースホルダー実装
        if (args.size() < 4)
            return Value(int64_t{0});
        // 初期値を返す
        return args[3];
    };

    // 配列 some
    builtins["__builtin_array_some"] = [](std::vector<Value> /*args*/, const auto&) -> Value {
        // プレースホルダー実装
        return Value(false);
    };

    // 配列 every
    builtins["__builtin_array_every"] = [](std::vector<Value> /*args*/, const auto&) -> Value {
        // プレースホルダー実装
        return Value(true);
    };

    // 配列 findIndex
    builtins["__builtin_array_findIndex"] = [](std::vector<Value> /*args*/, const auto&) -> Value {
        // プレースホルダー実装
        return Value(int64_t{-1});
    };

    // 配列 indexOf（PointerValue/ArrayValueに対応）
    auto indexOf_impl = [](std::vector<Value> args, const auto& locals) -> Value {
        if (args.size() < 3)
            return Value(int64_t{-1});

        // 配列データを取得
        std::vector<Value> const* arr_ptr = nullptr;

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
    builtins["__builtin_array_indexOf"] = indexOf_impl;
    builtins["__builtin_array_indexOf_i32"] = indexOf_impl;

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
    builtins["__builtin_array_includes"] = includes_impl;
    builtins["__builtin_array_includes_i32"] = includes_impl;
}

}  // namespace cm::mir::interp
