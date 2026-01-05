#pragma once

#include "types.hpp"

#include <algorithm>

namespace cm::mir::interp {

/// スライス（動的配列）操作関数の登録
inline void register_slice_builtins(BuiltinRegistry& builtins) {
    // スライスの長さを取得
    builtins["__builtin_slice_len"] = [](std::vector<Value> args, const auto&) -> Value {
        if (args.empty())
            return Value(int64_t{0});

        if (args[0].type() == typeid(SliceValue)) {
            const auto& slice = std::any_cast<SliceValue>(args[0]);
            return Value(static_cast<int64_t>(slice.len()));
        } else if (args[0].type() == typeid(ArrayValue)) {
            const auto& arr = std::any_cast<ArrayValue>(args[0]);
            return Value(static_cast<int64_t>(arr.elements.size()));
        }
        return Value(int64_t{0});
    };

    // スライスの容量を取得
    builtins["__builtin_slice_cap"] = [](std::vector<Value> args, const auto&) -> Value {
        if (args.empty())
            return Value(int64_t{0});

        if (args[0].type() == typeid(SliceValue)) {
            const auto& slice = std::any_cast<SliceValue>(args[0]);
            return Value(static_cast<int64_t>(slice.cap()));
        }
        return Value(int64_t{0});
    };

    // cm_slice_len/cap ランタイム関数
    builtins["cm_slice_len"] = [](std::vector<Value> args, const auto&) -> Value {
        if (args.empty())
            return Value(int64_t{0});

        if (args[0].type() == typeid(SliceValue)) {
            const auto& slice = std::any_cast<SliceValue>(args[0]);
            return Value(static_cast<int64_t>(slice.len()));
        } else if (args[0].type() == typeid(SliceValue*)) {
            auto* slice = std::any_cast<SliceValue*>(args[0]);
            if (slice)
                return Value(static_cast<int64_t>(slice->len()));
        } else if (args[0].type() == typeid(ArrayValue)) {
            const auto& arr = std::any_cast<ArrayValue>(args[0]);
            return Value(static_cast<int64_t>(arr.elements.size()));
        }
        return Value(int64_t{0});
    };

    builtins["cm_slice_cap"] = [](std::vector<Value> args, const auto&) -> Value {
        if (args.empty())
            return Value(int64_t{0});

        if (args[0].type() == typeid(SliceValue)) {
            const auto& slice = std::any_cast<SliceValue>(args[0]);
            return Value(static_cast<int64_t>(slice.cap()));
        } else if (args[0].type() == typeid(SliceValue*)) {
            auto* slice = std::any_cast<SliceValue*>(args[0]);
            if (slice)
                return Value(static_cast<int64_t>(slice->cap()));
        }
        return Value(int64_t{0});
    };

    // スライスに要素を追加（push）
    builtins["__builtin_slice_push"] = [](std::vector<Value> args,
                                          const auto& /* locals */) -> Value {
        if (args.size() < 2)
            return Value{};

        // 注: argsの最初の要素はスライスへの参照（ローカル変数ID）
        // 実際の実装では、スライスを直接変更する必要がある
        // ここではダミー実装
        return Value{};
    };

    // cm_slice_push_* ランタイム関数
    auto slice_push_impl = [](std::vector<Value> args, const auto&) -> Value {
        if (args.size() < 2)
            return Value{};

        if (args[0].type() == typeid(SliceValue*)) {
            auto* slice = std::any_cast<SliceValue*>(args[0]);
            if (slice) {
                slice->push(args[1]);
            }
        }
        return Value{};
    };
    builtins["cm_slice_push_i32"] = slice_push_impl;
    builtins["cm_slice_push_i64"] = slice_push_impl;
    builtins["cm_slice_push_f64"] = slice_push_impl;
    builtins["cm_slice_push_ptr"] = slice_push_impl;

    // スライスから要素を削除して返す（pop）
    builtins["__builtin_slice_pop"] = [](std::vector<Value> args, const auto&) -> Value {
        if (args.empty())
            return Value{};

        if (args[0].type() == typeid(SliceValue)) {
            // 注: 元のスライスを変更する必要があるため、参照経由で操作する必要がある
            return Value{};
        }
        return Value{};
    };

    // cm_slice_pop_* ランタイム関数
    auto slice_pop_impl = [](std::vector<Value> args, const auto&) -> Value {
        if (args.empty())
            return Value{};

        if (args[0].type() == typeid(SliceValue*)) {
            auto* slice = std::any_cast<SliceValue*>(args[0]);
            if (slice && slice->len() > 0) {
                return slice->pop();
            }
        }
        return Value{};
    };
    builtins["cm_slice_pop_i32"] = slice_pop_impl;
    builtins["cm_slice_pop_i64"] = slice_pop_impl;
    builtins["cm_slice_pop_f64"] = slice_pop_impl;
    builtins["cm_slice_pop_ptr"] = slice_pop_impl;

    // スライスの指定位置の要素を削除
    builtins["__builtin_slice_delete"] = [](std::vector<Value> /* args */, const auto&) -> Value {
        return Value{};
    };

    // cm_slice_delete ランタイム関数
    builtins["cm_slice_delete"] = [](std::vector<Value> args, const auto&) -> Value {
        if (args.size() < 2)
            return Value{};

        if (args[0].type() == typeid(SliceValue*)) {
            auto* slice = std::any_cast<SliceValue*>(args[0]);
            if (slice) {
                int64_t idx = 0;
                if (args[1].type() == typeid(int64_t)) {
                    idx = std::any_cast<int64_t>(args[1]);
                } else if (args[1].type() == typeid(int)) {
                    idx = std::any_cast<int>(args[1]);
                }
                slice->remove(static_cast<size_t>(idx));
            }
        }
        return Value{};
    };

    // スライスの指定位置に要素を挿入
    builtins["__builtin_slice_insert"] = [](std::vector<Value> /* args */, const auto&) -> Value {
        return Value{};
    };

    // スライスの全要素を削除
    builtins["__builtin_slice_clear"] = [](std::vector<Value> /* args */, const auto&) -> Value {
        return Value{};
    };

    // cm_slice_clear ランタイム関数
    builtins["cm_slice_clear"] = [](std::vector<Value> args, const auto&) -> Value {
        if (args.empty())
            return Value{};

        if (args[0].type() == typeid(SliceValue*)) {
            auto* slice = std::any_cast<SliceValue*>(args[0]);
            if (slice) {
                slice->clear();
            }
        }
        return Value{};
    };

    // スライスのreverse
    builtins["cm_slice_reverse"] = [](std::vector<Value> args, const auto&) -> Value {
        if (args.empty())
            return Value(SliceValue{});

        std::vector<Value> const* elements = nullptr;
        if (args[0].type() == typeid(SliceValue)) {
            elements = &std::any_cast<const SliceValue&>(args[0]).elements;
        } else if (args[0].type() == typeid(ArrayValue)) {
            elements = &std::any_cast<const ArrayValue&>(args[0]).elements;
        }

        if (!elements)
            return Value(SliceValue{});

        SliceValue result;
        for (auto it = elements->rbegin(); it != elements->rend(); ++it) {
            result.elements.push_back(*it);
        }
        return Value(result);
    };

    // スライスのsort
    builtins["cm_slice_sort"] = [](std::vector<Value> args, const auto&) -> Value {
        if (args.empty())
            return Value(SliceValue{});

        std::vector<Value> elements;
        if (args[0].type() == typeid(SliceValue)) {
            elements = std::any_cast<const SliceValue&>(args[0]).elements;
        } else if (args[0].type() == typeid(ArrayValue)) {
            elements = std::any_cast<const ArrayValue&>(args[0]).elements;
        }

        if (elements.empty())
            return Value(SliceValue{});

        // int64_tでソート
        std::sort(elements.begin(), elements.end(), [](const Value& a, const Value& b) {
            if (a.type() == typeid(int64_t) && b.type() == typeid(int64_t)) {
                return std::any_cast<int64_t>(a) < std::any_cast<int64_t>(b);
            }
            return false;
        });

        SliceValue result;
        result.elements = std::move(elements);
        return Value(result);
    };

    // 固定配列からスライスへの変換
    builtins["cm_array_to_slice"] = [](std::vector<Value> args, const auto& locals) -> Value {
        if (args.size() < 3)
            return Value(SliceValue{});

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
        }

        if (!arr_ptr)
            return Value(SliceValue{});

        SliceValue result;
        result.elements = *arr_ptr;
        return Value(result);
    };

    // スライスのサブスライス
    builtins["cm_slice_subslice"] = [](std::vector<Value> args, const auto&) -> Value {
        if (args.size() < 3)
            return Value(SliceValue{});

        // スライスデータを取得
        std::vector<Value> const* arr_ptr = nullptr;
        if (args[0].type() == typeid(SliceValue)) {
            arr_ptr = &std::any_cast<const SliceValue&>(args[0]).elements;
        } else if (args[0].type() == typeid(ArrayValue)) {
            arr_ptr = &std::any_cast<const ArrayValue&>(args[0]).elements;
        }

        if (!arr_ptr)
            return Value(SliceValue{});

        int64_t len = static_cast<int64_t>(arr_ptr->size());
        int64_t start = 0, end = -1;

        if (args[1].type() == typeid(int64_t))
            start = std::any_cast<int64_t>(args[1]);
        if (args[2].type() == typeid(int64_t))
            end = std::any_cast<int64_t>(args[2]);

        // 負のインデックスの処理
        if (start < 0)
            start = len + start;
        if (start < 0)
            start = 0;
        if (end < 0)
            end = len + end + 1;
        if (end > len)
            end = len;

        if (start >= end || start >= len)
            return Value(SliceValue{});

        SliceValue result;
        for (int64_t i = start; i < end; i++) {
            result.elements.push_back((*arr_ptr)[i]);
        }
        return Value(result);
    };

    // 固定配列の等値比較
    builtins["cm_array_equal"] = [](std::vector<Value> args, const auto&) -> Value {
        if (args.size() < 5)
            return Value(false);

        // 両方の配列を取得
        std::vector<Value> const* lhs_ptr = nullptr;
        std::vector<Value> const* rhs_ptr = nullptr;

        if (args[0].type() == typeid(ArrayValue)) {
            lhs_ptr = &std::any_cast<const ArrayValue&>(args[0]).elements;
        } else if (args[0].type() == typeid(std::vector<Value>)) {
            lhs_ptr = &std::any_cast<const std::vector<Value>&>(args[0]);
        }

        if (args[1].type() == typeid(ArrayValue)) {
            rhs_ptr = &std::any_cast<const ArrayValue&>(args[1]).elements;
        } else if (args[1].type() == typeid(std::vector<Value>)) {
            rhs_ptr = &std::any_cast<const std::vector<Value>&>(args[1]);
        }

        if (!lhs_ptr || !rhs_ptr) {
            return Value(lhs_ptr == rhs_ptr);
        }

        if (lhs_ptr->size() != rhs_ptr->size()) {
            return Value(false);
        }

        // 要素ごとに比較
        for (size_t i = 0; i < lhs_ptr->size(); i++) {
            const auto& l = (*lhs_ptr)[i];
            const auto& r = (*rhs_ptr)[i];

            // int64_tで比較
            if (l.type() == typeid(int64_t) && r.type() == typeid(int64_t)) {
                if (std::any_cast<int64_t>(l) != std::any_cast<int64_t>(r)) {
                    return Value(false);
                }
            } else if (l.type() == typeid(double) && r.type() == typeid(double)) {
                if (std::any_cast<double>(l) != std::any_cast<double>(r)) {
                    return Value(false);
                }
            } else if (l.type() == typeid(std::string) && r.type() == typeid(std::string)) {
                if (std::any_cast<std::string>(l) != std::any_cast<std::string>(r)) {
                    return Value(false);
                }
            } else if (l.type() == typeid(bool) && r.type() == typeid(bool)) {
                if (std::any_cast<bool>(l) != std::any_cast<bool>(r)) {
                    return Value(false);
                }
            } else {
                // 型が異なる場合は不一致
                return Value(false);
            }
        }

        return Value(true);
    };

    // 動的スライスの等値比較
    builtins["cm_slice_equal"] = [](std::vector<Value> args, const auto&) -> Value {
        if (args.size() < 2)
            return Value(false);

        std::vector<Value> const* lhs_ptr = nullptr;
        std::vector<Value> const* rhs_ptr = nullptr;

        if (args[0].type() == typeid(SliceValue)) {
            lhs_ptr = &std::any_cast<const SliceValue&>(args[0]).elements;
        }
        if (args[1].type() == typeid(SliceValue)) {
            rhs_ptr = &std::any_cast<const SliceValue&>(args[1]).elements;
        }

        if (!lhs_ptr || !rhs_ptr) {
            return Value(lhs_ptr == rhs_ptr);
        }

        if (lhs_ptr->size() != rhs_ptr->size()) {
            return Value(false);
        }

        // 要素ごとに比較
        for (size_t i = 0; i < lhs_ptr->size(); i++) {
            const auto& l = (*lhs_ptr)[i];
            const auto& r = (*rhs_ptr)[i];

            if (l.type() == typeid(int64_t) && r.type() == typeid(int64_t)) {
                if (std::any_cast<int64_t>(l) != std::any_cast<int64_t>(r)) {
                    return Value(false);
                }
            } else if (l.type() == typeid(double) && r.type() == typeid(double)) {
                if (std::any_cast<double>(l) != std::any_cast<double>(r)) {
                    return Value(false);
                }
            } else if (l.type() == typeid(std::string) && r.type() == typeid(std::string)) {
                if (std::any_cast<std::string>(l) != std::any_cast<std::string>(r)) {
                    return Value(false);
                }
            } else if (l.type() == typeid(bool) && r.type() == typeid(bool)) {
                if (std::any_cast<bool>(l) != std::any_cast<bool>(r)) {
                    return Value(false);
                }
            } else {
                return Value(false);
            }
        }

        return Value(true);
    };

    // スライスの要素を取得（インデックスアクセス）- 共通実装
    auto slice_get_impl = [](std::vector<Value> args, const auto&) -> Value {
        if (args.size() < 2)
            return Value{};

        int64_t idx = 0;
        if (args[1].type() == typeid(int64_t)) {
            idx = std::any_cast<int64_t>(args[1]);
        } else if (args[1].type() == typeid(int)) {
            idx = std::any_cast<int>(args[1]);
        }

        if (args[0].type() == typeid(SliceValue)) {
            const auto& slice = std::any_cast<SliceValue>(args[0]);
            if (idx >= 0 && static_cast<size_t>(idx) < slice.len()) {
                return slice.get(static_cast<size_t>(idx));
            }
        } else if (args[0].type() == typeid(ArrayValue)) {
            // ArrayValueも処理（cm_slice_get_element_ptrの結果など）
            const auto& arr = std::any_cast<ArrayValue>(args[0]);
            if (idx >= 0 && static_cast<size_t>(idx) < arr.elements.size()) {
                return arr.elements[static_cast<size_t>(idx)];
            }
        }
        return Value{};
    };

    // 全ての型バリアントを登録
    builtins["__builtin_slice_get"] = slice_get_impl;
    builtins["__builtin_slice_get_i32"] = slice_get_impl;
    builtins["__builtin_slice_get_i64"] = slice_get_impl;
    builtins["__builtin_slice_get_f64"] = slice_get_impl;
    builtins["__builtin_slice_get_ptr"] = slice_get_impl;
    // ランタイム関数名のエイリアス
    builtins["cm_slice_get_i8"] = slice_get_impl;
    builtins["cm_slice_get_i32"] = slice_get_impl;
    builtins["cm_slice_get_i64"] = slice_get_impl;
    builtins["cm_slice_get_f64"] = slice_get_impl;
    builtins["cm_slice_get_ptr"] = slice_get_impl;

    // 多次元スライス用：サブスライスを取得
    // ArrayValueをSliceValueに変換して返す
    auto subslice_impl = [](std::vector<Value> args, const auto&) -> Value {
        if (args.size() < 2)
            return Value{};

        if (args[0].type() == typeid(SliceValue)) {
            const auto& slice = std::any_cast<SliceValue>(args[0]);
            int64_t idx = 0;
            if (args[1].type() == typeid(int64_t)) {
                idx = std::any_cast<int64_t>(args[1]);
            } else if (args[1].type() == typeid(int)) {
                idx = std::any_cast<int>(args[1]);
            }

            if (idx >= 0 && static_cast<size_t>(idx) < slice.len()) {
                Value elem = slice.get(static_cast<size_t>(idx));
                // ArrayValueをSliceValueに変換
                if (elem.type() == typeid(ArrayValue)) {
                    const auto& arr = std::any_cast<ArrayValue>(elem);
                    SliceValue sv;
                    sv.elements = arr.elements;
                    return Value(sv);
                }
                // すでにSliceValueの場合はそのまま返す
                if (elem.type() == typeid(SliceValue)) {
                    return elem;
                }
                return elem;
            }
        }
        return Value{};
    };
    builtins["cm_slice_get_subslice"] = subslice_impl;
    builtins["cm_slice_get_element_ptr"] = subslice_impl;

    // 多次元スライス用：first/lastポインタ
    builtins["cm_slice_first_ptr"] = [](std::vector<Value> args, const auto&) -> Value {
        if (args.empty())
            return Value{};
        if (args[0].type() == typeid(SliceValue)) {
            const auto& slice = std::any_cast<SliceValue>(args[0]);
            if (slice.len() > 0) {
                Value elem = slice.get(0);
                // ArrayValueをSliceValueに変換
                if (elem.type() == typeid(ArrayValue)) {
                    const auto& arr = std::any_cast<ArrayValue>(elem);
                    SliceValue sv;
                    sv.elements = arr.elements;
                    return Value(sv);
                }
                return elem;
            }
        }
        return Value{};
    };

    builtins["cm_slice_last_ptr"] = [](std::vector<Value> args, const auto&) -> Value {
        if (args.empty())
            return Value{};
        if (args[0].type() == typeid(SliceValue)) {
            const auto& slice = std::any_cast<SliceValue>(args[0]);
            if (slice.len() > 0) {
                Value elem = slice.get(slice.len() - 1);
                // ArrayValueをSliceValueに変換
                if (elem.type() == typeid(ArrayValue)) {
                    const auto& arr = std::any_cast<ArrayValue>(elem);
                    SliceValue sv;
                    sv.elements = arr.elements;
                    return Value(sv);
                }
                return elem;
            }
        }
        return Value{};
    };
}

}  // namespace cm::mir::interp
