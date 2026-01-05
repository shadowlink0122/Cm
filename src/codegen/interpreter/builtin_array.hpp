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
            return Value(SliceValue{});

        // 配列を取得（複数の型に対応）
        std::vector<Value> const* arr_ptr = nullptr;
        if (args[0].type() == typeid(std::vector<Value>)) {
            arr_ptr = &std::any_cast<const std::vector<Value>&>(args[0]);
        } else if (args[0].type() == typeid(ArrayValue)) {
            arr_ptr = &std::any_cast<const ArrayValue&>(args[0]).elements;
        } else if (args[0].type() == typeid(SliceValue)) {
            arr_ptr = &std::any_cast<const SliceValue&>(args[0]).elements;
        }

        if (!arr_ptr) {
            return Value(SliceValue{});
        }
        const auto& arr = *arr_ptr;

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
            end = arr_len + end;  // -1なら最後から1つ前まで
        if (end > arr_len)
            end = arr_len;
        if (start >= end || start >= arr_len) {
            return Value(SliceValue{});
        }

        SliceValue result;
        for (int64_t i = start; i < end; i++) {
            if (i < static_cast<int64_t>(arr.size())) {
                result.elements.push_back(arr[i]);
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

    // 配列 reverse
    builtins["__builtin_array_reverse"] = [](std::vector<Value> args, const auto& locals) -> Value {
        if (args.size() < 2)
            return Value(SliceValue{});

        // 配列またはスライスデータを取得
        std::vector<Value> const* arr_ptr = nullptr;
        if (args[0].type() == typeid(PointerValue)) {
            const auto& pv = std::any_cast<PointerValue>(args[0]);
            auto it = locals.find(pv.target_local);
            if (it != locals.end() && it->second.type() == typeid(ArrayValue)) {
                arr_ptr = &std::any_cast<const ArrayValue&>(it->second).elements;
            } else if (it != locals.end() && it->second.type() == typeid(SliceValue)) {
                arr_ptr = &std::any_cast<const SliceValue&>(it->second).elements;
            }
        } else if (args[0].type() == typeid(ArrayValue)) {
            arr_ptr = &std::any_cast<const ArrayValue&>(args[0]).elements;
        } else if (args[0].type() == typeid(SliceValue)) {
            arr_ptr = &std::any_cast<const SliceValue&>(args[0]).elements;
        }

        if (!arr_ptr)
            return Value(SliceValue{});

        int64_t size = 0;
        if (args[1].type() == typeid(int64_t))
            size = std::any_cast<int64_t>(args[1]);

        // サイズが0の場合は配列/スライスのサイズを使用
        if (size == 0) {
            size = static_cast<int64_t>(arr_ptr->size());
        }

        SliceValue result;
        for (int64_t i = size - 1; i >= 0 && i < static_cast<int64_t>(arr_ptr->size()); i--) {
            result.elements.push_back((*arr_ptr)[i]);
        }
        return Value(result);
    };

    // 配列 first
    builtins["__builtin_array_first"] = [](std::vector<Value> args, const auto& locals) -> Value {
        if (args.size() < 2)
            return Value{};

        // 配列データを取得
        std::vector<Value> const* arr_ptr = nullptr;
        if (args[0].type() == typeid(PointerValue)) {
            const auto& pv = std::any_cast<PointerValue>(args[0]);
            auto it = locals.find(pv.target_local);
            if (it != locals.end() && it->second.type() == typeid(ArrayValue)) {
                arr_ptr = &std::any_cast<const ArrayValue&>(it->second).elements;
            } else if (it != locals.end() && it->second.type() == typeid(SliceValue)) {
                arr_ptr = &std::any_cast<const SliceValue&>(it->second).elements;
            }
        } else if (args[0].type() == typeid(ArrayValue)) {
            arr_ptr = &std::any_cast<const ArrayValue&>(args[0]).elements;
        } else if (args[0].type() == typeid(SliceValue)) {
            arr_ptr = &std::any_cast<const SliceValue&>(args[0]).elements;
        }

        if (!arr_ptr || arr_ptr->empty())
            return Value{};

        return (*arr_ptr)[0];
    };

    // 配列 last
    builtins["__builtin_array_last"] = [](std::vector<Value> args, const auto& locals) -> Value {
        if (args.size() < 2)
            return Value{};

        // 配列データを取得
        std::vector<Value> const* arr_ptr = nullptr;
        int64_t arr_len = 0;

        if (args[0].type() == typeid(PointerValue)) {
            const auto& pv = std::any_cast<PointerValue>(args[0]);
            auto it = locals.find(pv.target_local);
            if (it != locals.end() && it->second.type() == typeid(ArrayValue)) {
                const auto& av = std::any_cast<const ArrayValue&>(it->second);
                arr_ptr = &av.elements;
                arr_len = static_cast<int64_t>(av.elements.size());
            } else if (it != locals.end() && it->second.type() == typeid(SliceValue)) {
                const auto& sv = std::any_cast<const SliceValue&>(it->second);
                arr_ptr = &sv.elements;
                arr_len = static_cast<int64_t>(sv.elements.size());
            }
        } else if (args[0].type() == typeid(ArrayValue)) {
            const auto& av = std::any_cast<const ArrayValue&>(args[0]);
            arr_ptr = &av.elements;
            arr_len = static_cast<int64_t>(av.elements.size());
        } else if (args[0].type() == typeid(SliceValue)) {
            const auto& sv = std::any_cast<const SliceValue&>(args[0]);
            arr_ptr = &sv.elements;
            arr_len = static_cast<int64_t>(sv.elements.size());
        }

        // 引数のサイズが0でなければそれを使用、0ならば配列の実際のサイズを使用
        int64_t size = 0;
        if (args[1].type() == typeid(int64_t))
            size = std::any_cast<int64_t>(args[1]);
        if (size == 0)
            size = arr_len;

        if (!arr_ptr || arr_ptr->empty() || size <= 0)
            return Value{};

        int64_t idx = std::min(size - 1, static_cast<int64_t>(arr_ptr->size()) - 1);
        return (*arr_ptr)[idx];
    };

    // サフィックス付きバージョンも登録（LLVMと互換性のため）
    builtins["__builtin_array_first_i32"] = builtins["__builtin_array_first"];
    builtins["__builtin_array_first_i64"] = builtins["__builtin_array_first"];
    builtins["__builtin_array_last_i32"] = builtins["__builtin_array_last"];
    builtins["__builtin_array_last_i64"] = builtins["__builtin_array_last"];
    builtins["__builtin_array_find_i32"] = builtins["__builtin_array_find"];
    builtins["__builtin_array_find_i64"] = builtins["__builtin_array_find"];

    // スライス用のfirst/last（引数1つ版）
    builtins["cm_slice_first_i32"] = [](std::vector<Value> args, const auto& /*locals*/) -> Value {
        if (args.empty())
            return Value{};

        if (args[0].type() == typeid(SliceValue)) {
            const auto& sv = std::any_cast<const SliceValue&>(args[0]);
            if (sv.elements.empty())
                return Value{};
            return sv.elements[0];
        }
        return Value{};
    };
    builtins["cm_slice_first_i64"] = builtins["cm_slice_first_i32"];

    builtins["cm_slice_last_i32"] = [](std::vector<Value> args, const auto& /*locals*/) -> Value {
        if (args.empty())
            return Value{};

        if (args[0].type() == typeid(SliceValue)) {
            const auto& sv = std::any_cast<const SliceValue&>(args[0]);
            if (sv.elements.empty())
                return Value{};
            return sv.elements[sv.elements.size() - 1];
        }
        return Value{};
    };
    builtins["cm_slice_last_i64"] = builtins["cm_slice_last_i32"];
}

}  // namespace cm::mir::interp
