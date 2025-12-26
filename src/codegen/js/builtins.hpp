#pragma once

#include "../../mir/nodes.hpp"

#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

namespace cm::codegen::js {

// 組み込み関数かどうかをチェック
inline bool isBuiltinFunction(const std::string& name) {
    static const std::unordered_set<std::string> builtins = {
        "println",
        "print",
        "cm_println_string",
        "cm_println_int",
        "cm_println_double",
        "cm_println_bool",
        "cm_println_char",
        "cm_println_format",
        "cm_print_string",
        "cm_print_int",
        "cm_print_double",
        "cm_print_bool",
        "cm_print_char",
        "cm_print_format",
        "cm_string_concat",
        "cm_int_to_string",
        "cm_double_to_string",
        "cm_bool_to_string",
        "cm_format_int",
        "cm_format_double",
        "cm_format_bool",
        "cm_format_char",
        "cm_format_string",
        "cm_format_string_1",
        "cm_format_string_2",
        "cm_format_string_3",
        "cm_format_string_4",
        "__builtin_string_len",
        "__builtin_string_charAt",
        "__builtin_string_substring",
        "__builtin_string_indexOf",
        "__builtin_string_toUpperCase",
        "__builtin_string_toLowerCase",
        "__builtin_string_trim",
        "__builtin_string_startsWith",
        "__builtin_string_endsWith",
        "__builtin_string_includes",
        "__builtin_string_repeat",
        "__builtin_string_replace",
        "__builtin_string_slice",
        "__builtin_string_concat",
        "__builtin_string_first",
        "__builtin_string_last",
        // 配列メソッド (JS Native Wrappers)
        "every",
        "some",
        "find",
        "findIndex",
        "reduce",
        "filter",
        "map",
        "sort",
        "sortBy",
        "__builtin_array_indexOf_i32",
        "__builtin_array_indexOf_i64",
        "__builtin_array_includes_i32",
        "__builtin_array_includes_i64",
        "__builtin_array_find_i32",
        "__builtin_array_find_i64",
        "__builtin_array_findIndex_i32",
        "__builtin_array_findIndex_i64",
        "__builtin_array_some_i32",
        "__builtin_array_some_i64",
        "__builtin_array_every_i32",
        "__builtin_array_every_i64",
        "__builtin_array_reduce_i32",
        "__builtin_array_reduce_i64",
        "__builtin_array_map_i32",
        "__builtin_array_map_i64",
        "__builtin_array_map",
        "__builtin_array_filter_i32",
        "__builtin_array_filter_i64",
        "__builtin_array_filter",
        "__builtin_array_slice",
        "__builtin_array_reverse",
        "__builtin_array_first_i32",
        "__builtin_array_first_i64",
        "__builtin_array_last_i32",
        "__builtin_array_last_i64",
        "__builtin_array_sortBy_i32",
        "__builtin_array_sortBy_i64",
        "__builtin_array_sortBy",
        // クロージャー版
        "__builtin_array_map_closure",
        "__builtin_array_filter_closure",
        // スライス操作
        "cm_slice_get_i8",
        "cm_slice_get_i32",
        "cm_slice_get_i64",
        "cm_slice_get_f64",
        "cm_slice_get_ptr",
        "cm_slice_first_i32",
        "cm_slice_first_i64",
        "cm_slice_last_i32",
        "cm_slice_last_i64",
        "cm_slice_push_i32",
        "cm_slice_push_i64",
        "cm_slice_pop_i32",
        "cm_slice_pop_i64",
        "cm_slice_len",
        "cm_slice_cap",
        "cm_slice_subslice",
        "cm_slice_set_i32",
        "cm_slice_set_i64",
        "cm_slice_push_slice",
        "cm_make_slice",
        "cm_slice_get_subslice",
        "cm_slice_sort",
        "cm_slice_reverse",
        "cm_slice_first",
        "cm_slice_last",
        // 配列比較・ソート
        "cm_array_equal",
        "cm_slice_equal",
        "__builtin_array_sort",
        "__builtin_array_sort_i32",
        "__builtin_array_sort_i64",
        "__builtin_array_find",
        // 配列/スライス変換
        "cm_array_to_slice",
        "cm_slice_to_array",
        // ランタイムヘルパー
        "__cm_slice",
        "__cm_str_slice",
    };
    return builtins.count(name) > 0;
}

// 組み込み関数呼び出しをJSコードに変換
// argStrsは事前に変換済みの引数文字列
inline std::string emitBuiltinCall(const std::string& name,
                                   const std::vector<std::string>& argStrs) {
    // __cm_slice: (arr, start, end)
    if (name == "__cm_slice" && argStrs.size() >= 3) {
        return "__cm_slice(" + argStrs[0] + ", " + argStrs[1] + ", " + argStrs[2] + ")";
    }
    // __cm_str_slice: (str, start, end)
    if (name == "__cm_str_slice" && argStrs.size() >= 3) {
        return "__cm_str_slice(" + argStrs[0] + ", " + argStrs[1] + ", " + argStrs[2] + ")";
    }

    // println系
    if (name == "println" || name == "cm_println_string" || name == "cm_println_int" ||
        name == "cm_println_double" || name == "cm_println_bool") {
        if (argStrs.empty()) {
            return "console.log()";
        }
        return "console.log(" + argStrs[0] + ")";
    }
    // cm_println_char - char型は文字として出力
    if (name == "cm_println_char") {
        if (argStrs.empty()) {
            return "console.log()";
        }
        return "console.log(String.fromCharCode(" + argStrs[0] + "))";
    }

    // cm_println_format - フォーマット文字列付き出力
    if (name == "cm_println_format" || name == "cm_print_format") {
        if (argStrs.size() >= 2) {
            std::string format = argStrs[0];
            std::string values;
            for (size_t i = 2; i < argStrs.size(); ++i) {
                if (i > 2)
                    values += ", ";
                values += argStrs[i];
            }
            std::string jsCode = "__cm_format_string(" + format + ", [" + values + "])";
            if (name == "cm_println_format") {
                return "console.log(" + jsCode + ")";
            } else {
                return "process.stdout.write(" + jsCode + ")";
            }
        }
        return "console.log()";
    }

    // cm_string_concat
    if (name == "cm_string_concat") {
        if (argStrs.size() >= 2) {
            return "(" + argStrs[0] + " + " + argStrs[1] + ")";
        }
        return "\"\"";
    }

    // 型変換
    if (name == "cm_int_to_string" && argStrs.size() >= 1) {
        return "String(" + argStrs[0] + ")";
    }
    if (name == "cm_double_to_string" && argStrs.size() >= 1) {
        return "String(" + argStrs[0] + ")";
    }
    if (name == "cm_bool_to_string" && argStrs.size() >= 1) {
        return "String(" + argStrs[0] + ")";
    }

    // cm_format_* - 値をフォーマットして文字列として返す
    if (name == "cm_format_int" && argStrs.size() >= 1) {
        return "String(" + argStrs[0] + ")";
    }
    if (name == "cm_format_double" && argStrs.size() >= 1) {
        return "String(" + argStrs[0] + ")";
    }
    if (name == "cm_format_bool" && argStrs.size() >= 1) {
        return "(" + argStrs[0] + " ? \"true\" : \"false\")";
    }
    if (name == "cm_format_char" && argStrs.size() >= 1) {
        return "String.fromCharCode(" + argStrs[0] + ")";
    }

    // cm_format_string - フォーマット文字列を返す（代入用）
    if (name == "cm_format_string" || name == "cm_format_string_1" ||
        name == "cm_format_string_2" || name == "cm_format_string_3" ||
        name == "cm_format_string_4") {
        if (argStrs.size() >= 2) {
            // 引数形式: (format, num_args, arg1, arg2, ...)
            std::string format = argStrs[0];
            std::string values;
            for (size_t i = 2; i < argStrs.size(); ++i) {
                if (i > 2)
                    values += ", ";
                values += argStrs[i];
            }
            return "__cm_format_string(" + format + ", [" + values + "])";
        }
        return "\"\"";
    }

    // print系
    if (name == "print" || name == "cm_print_string" || name == "cm_print_int" ||
        name == "cm_print_double" || name == "cm_print_bool" || name == "cm_print_char") {
        if (argStrs.empty()) {
            return "process.stdout.write('')";
        }
        return "process.stdout.write(String(" + argStrs[0] + "))";
    }

    // 文字列メソッド
    if (name == "__builtin_string_len" && argStrs.size() >= 1) {
        return argStrs[0] + ".length";
    }
    if (name == "__builtin_string_charAt" && argStrs.size() >= 2) {
        // charCodeAtを使用してchar型（数値）を返す
        return argStrs[0] + ".charCodeAt(" + argStrs[1] + ")";
    }
    if (name == "__builtin_string_substring" && argStrs.size() >= 3) {
        // __cm_str_sliceは負のインデックスをサポート
        return "__cm_str_slice(" + argStrs[0] + ", " + argStrs[1] + ", " + argStrs[2] + ")";
    }
    if (name == "__builtin_string_indexOf" && argStrs.size() >= 2) {
        return argStrs[0] + ".indexOf(" + argStrs[1] + ")";
    }
    if (name == "__builtin_string_toUpperCase" && argStrs.size() >= 1) {
        return argStrs[0] + ".toUpperCase()";
    }
    if (name == "__builtin_string_toLowerCase" && argStrs.size() >= 1) {
        return argStrs[0] + ".toLowerCase()";
    }
    if (name == "__builtin_string_trim" && argStrs.size() >= 1) {
        return argStrs[0] + ".trim()";
    }
    if (name == "__builtin_string_startsWith" && argStrs.size() >= 2) {
        return argStrs[0] + ".startsWith(" + argStrs[1] + ")";
    }
    if (name == "__builtin_string_endsWith" && argStrs.size() >= 2) {
        return argStrs[0] + ".endsWith(" + argStrs[1] + ")";
    }
    if (name == "__builtin_string_includes" && argStrs.size() >= 2) {
        return argStrs[0] + ".includes(" + argStrs[1] + ")";
    }
    if (name == "__builtin_string_repeat" && argStrs.size() >= 2) {
        return argStrs[0] + ".repeat(" + argStrs[1] + ")";
    }
    if (name == "__builtin_string_replace" && argStrs.size() >= 3) {
        return argStrs[0] + ".replace(" + argStrs[1] + ", " + argStrs[2] + ")";
    }
    if (name == "__builtin_string_slice" && argStrs.size() >= 3) {
        return argStrs[0] + ".slice(" + argStrs[1] + ", " + argStrs[2] + ")";
    }
    if (name == "__builtin_string_concat" && argStrs.size() >= 2) {
        return "__cm_str_concat(" + argStrs[0] + ", " + argStrs[1] + ")";
    }
    if (name == "__builtin_string_first" && argStrs.size() >= 1) {
        // charCodeAtを使用してchar型（数値）を返す
        return "(" + argStrs[0] + ".length > 0 ? " + argStrs[0] + ".charCodeAt(0) : 0)";
    }
    if (name == "__builtin_string_last" && argStrs.size() >= 1) {
        // charCodeAtを使用してchar型（数値）を返す
        return "(" + argStrs[0] + ".length > 0 ? " + argStrs[0] + ".charCodeAt(" + argStrs[0] +
               ".length - 1) : 0)";
    }

    // 配列メソッド
    if ((name == "__builtin_array_indexOf_i32" || name == "__builtin_array_indexOf_i64") &&
        argStrs.size() >= 3) {
        return "__cm_unwrap(" + argStrs[0] + ").indexOf(" + argStrs[2] + ")";
    }
    if ((name == "__builtin_array_includes_i32" || name == "__builtin_array_includes_i64") &&
        argStrs.size() >= 3) {
        return "__cm_unwrap(" + argStrs[0] + ").includes(" + argStrs[2] + ")";
    }
    // find: (arr, len, predicate) -> 要素 or デフォルト
    if ((name == "__builtin_array_find_i32" || name == "__builtin_array_find_i64") &&
        argStrs.size() >= 3) {
        return "(__cm_unwrap(" + argStrs[0] + ").find(" + argStrs[2] + ") ?? 0)";
    }
    // findIndex: (arr, len, predicate) -> インデックス or -1
    if ((name == "__builtin_array_findIndex_i32" || name == "__builtin_array_findIndex_i64") &&
        argStrs.size() >= 3) {
        return "__cm_unwrap(" + argStrs[0] + ").findIndex(" + argStrs[2] + ")";
    }
    // some: (arr, len, predicate) -> bool
    if ((name == "__builtin_array_some_i32" || name == "__builtin_array_some_i64") &&
        argStrs.size() >= 3) {
        return "__cm_unwrap(" + argStrs[0] + ").some(" + argStrs[2] + ")";
    }
    // every: (arr, len, predicate) -> bool
    if ((name == "__builtin_array_every_i32" || name == "__builtin_array_every_i64") &&
        argStrs.size() >= 3) {
        return "__cm_unwrap(" + argStrs[0] + ").every(" + argStrs[2] + ")";
    }
    // reduce: (arr, len, reducer, initial) -> 結果
    if ((name == "__builtin_array_reduce_i32" || name == "__builtin_array_reduce_i64") &&
        argStrs.size() >= 4) {
        return "__cm_unwrap(" + argStrs[0] + ").reduce(" + argStrs[2] + ", " + argStrs[3] + ")";
    }
    // map: (arr, len, mapper) -> 新しい配列
    if ((name == "__builtin_array_map_i32" || name == "__builtin_array_map_i64" ||
         name == "__builtin_array_map") &&
        argStrs.size() >= 3) {
        return "__cm_unwrap(" + argStrs[0] + ").map(" + argStrs[2] + ")";
    }
    // filter: (arr, len, predicate) -> 新しい配列
    if ((name == "__builtin_array_filter_i32" || name == "__builtin_array_filter_i64" ||
         name == "__builtin_array_filter") &&
        argStrs.size() >= 3) {
        return "__cm_unwrap(" + argStrs[0] + ").filter(" + argStrs[2] + ")";
    }
    // slice: (arr, elem_size, len, start, end) -> 新しい配列
    if (name == "__builtin_array_slice" && argStrs.size() >= 5) {
        return "__cm_unwrap(" + argStrs[0] + ").slice(" + argStrs[3] + ", " + argStrs[4] + ")";
    }
    // reverse: (arr) -> 新しい配列（元の配列を変更しない）
    if (name == "__builtin_array_reverse" && argStrs.size() >= 1) {
        return "[...__cm_unwrap(" + argStrs[0] + ")].reverse()";
    }
    // first: (arr, len) -> 最初の要素
    if ((name == "__builtin_array_first_i32" || name == "__builtin_array_first_i64") &&
        argStrs.size() >= 2) {
        return "__cm_unwrap(" + argStrs[0] + ")[0]";
    }
    // last: (arr, len) -> 最後の要素
    if ((name == "__builtin_array_last_i32" || name == "__builtin_array_last_i64") &&
        argStrs.size() >= 2) {
        return "__cm_unwrap(" + argStrs[0] + ")[__cm_unwrap(" + argStrs[0] + ").length - 1]";
    }
    // sortBy: ...
    if ((name == "__builtin_array_sortBy_i32" || name == "__builtin_array_sortBy_i64" ||
         name == "__builtin_array_sortBy") &&
        argStrs.size() >= 3) {
        return "[...__cm_unwrap(" + argStrs[0] + ")].sort((a, b) => " + argStrs[2] + "(a, b))";
    }

    // クロージャー版map: (arr, len, mapper, capture) -> 新しい配列
    if (name == "__builtin_array_map_closure" && argStrs.size() >= 4) {
        return "__cm_unwrap(" + argStrs[0] + ").map((x) => " + argStrs[2] + "(x, " + argStrs[3] +
               "))";
    }
    // クロージャー版filter: (arr, len, predicate, capture) -> 新しい配列
    if (name == "__builtin_array_filter_closure" && argStrs.size() >= 4) {
        return "__cm_unwrap(" + argStrs[0] + ").filter((x) => " + argStrs[2] + "(x, " + argStrs[3] +
               "))";
    }

    // スライス操作: 配列アクセス
    // cm_slice_get_*: (slice, index) -> 要素
    if ((name == "cm_slice_get_i8" || name == "cm_slice_get_i32" || name == "cm_slice_get_i64" ||
         name == "cm_slice_get_f64" || name == "cm_slice_get_ptr") &&
        argStrs.size() >= 2) {
        return "__cm_unwrap(" + argStrs[0] + ")[" + argStrs[1] + "]";
    }
    // cm_slice_first_*: (slice) -> 最初の要素
    if ((name == "cm_slice_first_i32" || name == "cm_slice_first_i64") && argStrs.size() >= 1) {
        return "__cm_unwrap(" + argStrs[0] + ")[0]";
    }
    // cm_slice_last_*: (slice) -> 最後の要素
    if ((name == "cm_slice_last_i32" || name == "cm_slice_last_i64") && argStrs.size() >= 1) {
        return "__cm_unwrap(" + argStrs[0] + ")[__cm_unwrap(" + argStrs[0] + ").length - 1]";
    }
    // cm_slice_push_*: (slice, element) -> void
    if ((name == "cm_slice_push_i32" || name == "cm_slice_push_i64") && argStrs.size() >= 2) {
        return "__cm_unwrap(" + argStrs[0] + ").push(" + argStrs[1] + ")";
    }
    // cm_slice_pop_*: (slice) -> 要素
    if ((name == "cm_slice_pop_i32" || name == "cm_slice_pop_i64") && argStrs.size() >= 1) {
        return "__cm_unwrap(" + argStrs[0] + ").pop()";
    }
    // cm_slice_len: (slice) -> 長さ
    if (name == "cm_slice_len" && argStrs.size() >= 1) {
        return "__cm_unwrap(" + argStrs[0] + ").length";
    }
    // cm_slice_cap: (slice) -> 容量 (JSでは長さと同じ)
    if (name == "cm_slice_cap" && argStrs.size() >= 1) {
        return "__cm_unwrap(" + argStrs[0] + ").length";
    }
    // cm_slice_subslice: (slice, start, end) -> 部分スライス
    if (name == "cm_slice_subslice" && argStrs.size() >= 3) {
        return "__cm_unwrap(" + argStrs[0] + ").slice(" + argStrs[1] + ", " + argStrs[2] + ")";
    }
    // cm_slice_set_*: (slice, index, value) -> void
    if ((name == "cm_slice_set_i32" || name == "cm_slice_set_i64") && argStrs.size() >= 3) {
        return "(__cm_unwrap(" + argStrs[0] + ")[" + argStrs[1] + "] = " + argStrs[2] + ")";
    }
    // cm_slice_push_slice: (outer_slice, inner_slice) -> void
    if (name == "cm_slice_push_slice" && argStrs.size() >= 2) {
        return "__cm_unwrap(" + argStrs[0] + ").push(" + argStrs[1] + ")";
    }
    // cm_make_slice: (capacity, elem_size) -> 新しい空スライス
    if (name == "cm_make_slice" && argStrs.size() >= 2) {
        return "[]";
    }
    // cm_slice_get_subslice: (2D_slice, index) -> 内部スライス
    if (name == "cm_slice_get_subslice" && argStrs.size() >= 2) {
        return "__cm_unwrap(" + argStrs[0] + ")[" + argStrs[1] + "]";
    }
    // cm_slice_sort: (slice) -> ソート済みスライス（新しい配列）
    if (name == "cm_slice_sort" && argStrs.size() >= 1) {
        return "[...__cm_unwrap(" + argStrs[0] + ")].sort((a, b) => a - b)";
    }
    // cm_slice_reverse: (slice) -> 反転スライス（新しい配列）
    if (name == "cm_slice_reverse" && argStrs.size() >= 1) {
        return "[...__cm_unwrap(" + argStrs[0] + ")].reverse()";
    }
    // cm_slice_first: (slice) -> 最初の要素
    if (name == "cm_slice_first" && argStrs.size() >= 1) {
        return "__cm_unwrap(" + argStrs[0] + ")[0]";
    }
    // cm_slice_last: (slice) -> 最後の要素
    if (name == "cm_slice_last" && argStrs.size() >= 1) {
        return "__cm_unwrap(" + argStrs[0] + ")[__cm_unwrap(" + argStrs[0] + ").length - 1]";
    }
    // cm_array_equal: (arr1, arr2, len1, len2, type_size) OR (arr1, arr2, len...)
    // Based on generated code observation, arg0=arr1, arg1=arr2.
    if (name == "cm_array_equal" && argStrs.size() >= 2) {
        return "__cm_deep_equal(" + argStrs[0] + ", " + argStrs[1] + ")";
    }

    // cm_slice_equal: (slice1, slice2) -> bool
    if (name == "cm_slice_equal" && argStrs.size() >= 2) {
        return "__cm_deep_equal(" + argStrs[0] + ", " + argStrs[1] + ")";
    }

    // __builtin_array_find: (arr, len, predicate) -> 最初に条件を満たす要素
    if ((name == "__builtin_array_find" || name == "__builtin_array_find_i32" ||
         name == "__builtin_array_find_i64") &&
        argStrs.size() >= 3) {
        return "(__cm_unwrap(" + argStrs[0] + ").find(" + argStrs[2] + ") ?? 0)";
    }

    // __builtin_array_sort: (arr, len) -> ソート済み配列
    if ((name == "__builtin_array_sort" || name == "__builtin_array_sort_i32" ||
         name == "__builtin_array_sort_i64") &&
        argStrs.size() >= 2) {
        return "[...__cm_unwrap(" + argStrs[0] + ")].sort((a, b) => a - b)";
    }

    // __builtin_array_map: (arr, len, fn) -> 変換後配列
    if ((name == "__builtin_array_map" || name == "__builtin_array_map_i32" ||
         name == "__builtin_array_map_i64") &&
        argStrs.size() >= 3) {
        return argStrs[0] + ".map(" + argStrs[2] + ")";
    }

    // __builtin_array_filter: (arr, len, predicate) -> フィルタ後配列
    if ((name == "__builtin_array_filter" || name == "__builtin_array_filter_i32" ||
         name == "__builtin_array_filter_i64") &&
        argStrs.size() >= 3) {
        return argStrs[0] + ".filter(" + argStrs[2] + ")";
    }

    // cm_array_to_slice: (arr, len, elem_size) -> スライス (JSでは配列のコピー)
    if (name == "cm_array_to_slice" && argStrs.size() >= 3) {
        return "[...__cm_unwrap(" + argStrs[0] + ")]";
    }
    // cm_slice_to_array: 同様にコピー
    if (name == "cm_slice_to_array" && argStrs.size() >= 1) {
        return "[...__cm_unwrap(" + argStrs[0] + ")]";
    }

    // 不明な組み込み関数
    return "/* unknown builtin: " + name + " */ undefined";
}

}  // namespace cm::codegen::js
