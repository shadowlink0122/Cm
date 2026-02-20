// JS組み込み関数の実装
#include "builtins.hpp"

#include <unordered_set>

namespace cm::codegen::js {

// 組み込み関数かどうかをチェック
bool isBuiltinFunction(const std::string& name) {
    static const std::unordered_set<std::string> builtins = {
        "println",
        "print",
        "cm_println_string",
        "cm_println_int",
        "cm_println_long",
        "cm_println_ulong",
        "cm_println_uint",
        "cm_println_double",
        "cm_println_bool",
        "cm_println_char",
        "cm_println_format",
        "cm_print_string",
        "cm_print_int",
        "cm_print_long",
        "cm_print_ulong",
        "cm_print_uint",
        "cm_print_double",
        "cm_print_bool",
        "cm_print_char",
        "cm_print_format",
        "cm_string_concat",
        "cm_int_to_string",
        "cm_long_to_string",
        "cm_ulong_to_string",
        "cm_uint_to_string",
        "cm_double_to_string",
        "cm_bool_to_string",
        "cm_format_int",
        "cm_format_long",
        "cm_format_ulong",
        "cm_format_uint",
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
        "cm_slice_push_i8",
        "cm_slice_push_i32",
        "cm_slice_push_i64",
        "cm_slice_push_f32",
        "cm_slice_push_f64",
        "cm_slice_push_ptr",
        "cm_slice_pop_i32",
        "cm_slice_pop_i64",
        "cm_slice_pop_f32",
        "cm_slice_pop_ptr",
        "cm_slice_delete",
        "cm_slice_clear",
        "cm_slice_len",
        "cm_slice_cap",
        "cm_slice_subslice",
        "cm_slice_set_i32",
        "cm_slice_set_i64",
        "cm_slice_set_f32",
        "cm_slice_set_ptr",
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
        // スライス生成
        "cm_slice_new",
        // メモリ管理
        "malloc",
        "realloc",
        "free",
        "memcpy",
        "memset",
        // 低レベルI/O
        "__print__",
    };
    return builtins.count(name) > 0;
}

// 組み込み関数呼び出しをJSコードに変換
std::string emitBuiltinCall(const std::string& name, const std::vector<std::string>& argStrs) {
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
        name == "cm_println_long" || name == "cm_println_ulong" || name == "cm_println_uint" ||
        name == "cm_println_double" || name == "cm_println_bool") {
        if (argStrs.empty()) {
            return "console.log()";
        }
        return "console.log(" + argStrs[0] + ")";
    }
    if (name == "cm_println_char") {
        if (argStrs.empty()) {
            return "console.log()";
        }
        return "console.log(String.fromCharCode(" + argStrs[0] + "))";
    }

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

    if (name == "cm_string_concat") {
        if (argStrs.size() >= 2) {
            return "(" + argStrs[0] + " + " + argStrs[1] + ")";
        }
        return "\"\"";
    }

    // 型変換
    if ((name == "cm_int_to_string" || name == "cm_long_to_string" ||
         name == "cm_ulong_to_string" || name == "cm_uint_to_string") &&
        argStrs.size() >= 1) {
        return "String(" + argStrs[0] + ")";
    }
    if (name == "cm_double_to_string" && argStrs.size() >= 1) {
        return "String(" + argStrs[0] + ")";
    }
    if (name == "cm_bool_to_string" && argStrs.size() >= 1) {
        return "String(" + argStrs[0] + ")";
    }

    // cm_format_*
    if ((name == "cm_format_int" || name == "cm_format_long" || name == "cm_format_ulong" ||
         name == "cm_format_uint") &&
        argStrs.size() >= 1) {
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

    if (name == "cm_format_string" || name == "cm_format_string_1" ||
        name == "cm_format_string_2" || name == "cm_format_string_3" ||
        name == "cm_format_string_4") {
        if (argStrs.size() >= 2) {
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
        name == "cm_print_long" || name == "cm_print_ulong" || name == "cm_print_uint" ||
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
        return argStrs[0] + ".charCodeAt(" + argStrs[1] + ")";
    }
    if (name == "__builtin_string_substring" && argStrs.size() >= 3) {
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
        return "(" + argStrs[0] + ".length > 0 ? " + argStrs[0] + ".charCodeAt(0) : 0)";
    }
    if (name == "__builtin_string_last" && argStrs.size() >= 1) {
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
    if ((name == "__builtin_array_find_i32" || name == "__builtin_array_find_i64") &&
        argStrs.size() >= 3) {
        return "(__cm_unwrap(" + argStrs[0] + ").find(" + argStrs[2] + ") ?? 0)";
    }
    if ((name == "__builtin_array_findIndex_i32" || name == "__builtin_array_findIndex_i64") &&
        argStrs.size() >= 3) {
        return "__cm_unwrap(" + argStrs[0] + ").findIndex(" + argStrs[2] + ")";
    }
    if ((name == "__builtin_array_some_i32" || name == "__builtin_array_some_i64") &&
        argStrs.size() >= 3) {
        return "__cm_unwrap(" + argStrs[0] + ").some(" + argStrs[2] + ")";
    }
    if ((name == "__builtin_array_every_i32" || name == "__builtin_array_every_i64") &&
        argStrs.size() >= 3) {
        return "__cm_unwrap(" + argStrs[0] + ").every(" + argStrs[2] + ")";
    }
    if ((name == "__builtin_array_reduce_i32" || name == "__builtin_array_reduce_i64") &&
        argStrs.size() >= 4) {
        return "__cm_unwrap(" + argStrs[0] + ").reduce(" + argStrs[2] + ", " + argStrs[3] + ")";
    }
    if ((name == "__builtin_array_map_i32" || name == "__builtin_array_map_i64" ||
         name == "__builtin_array_map") &&
        argStrs.size() >= 3) {
        return "__cm_unwrap(" + argStrs[0] + ").map(" + argStrs[2] + ")";
    }
    if ((name == "__builtin_array_filter_i32" || name == "__builtin_array_filter_i64" ||
         name == "__builtin_array_filter") &&
        argStrs.size() >= 3) {
        return "__cm_unwrap(" + argStrs[0] + ").filter(" + argStrs[2] + ")";
    }
    if (name == "__builtin_array_slice" && argStrs.size() >= 5) {
        return "__cm_unwrap(" + argStrs[0] + ").slice(" + argStrs[3] + ", " + argStrs[4] + ")";
    }
    if (name == "__builtin_array_reverse" && argStrs.size() >= 1) {
        return "[...__cm_unwrap(" + argStrs[0] + ")].reverse()";
    }
    if ((name == "__builtin_array_first_i32" || name == "__builtin_array_first_i64") &&
        argStrs.size() >= 2) {
        return "__cm_unwrap(" + argStrs[0] + ")[0]";
    }
    if ((name == "__builtin_array_last_i32" || name == "__builtin_array_last_i64") &&
        argStrs.size() >= 2) {
        return "__cm_unwrap(" + argStrs[0] + ")[__cm_unwrap(" + argStrs[0] + ").length - 1]";
    }
    if ((name == "__builtin_array_sortBy_i32" || name == "__builtin_array_sortBy_i64" ||
         name == "__builtin_array_sortBy") &&
        argStrs.size() >= 3) {
        return "[...__cm_unwrap(" + argStrs[0] + ")].sort((a, b) => " + argStrs[2] + "(a, b))";
    }

    // クロージャー版
    if (name == "__builtin_array_map_closure" && argStrs.size() >= 4) {
        return "__cm_unwrap(" + argStrs[0] + ").map((x) => " + argStrs[2] + "(x, " + argStrs[3] +
               "))";
    }
    if (name == "__builtin_array_filter_closure" && argStrs.size() >= 4) {
        return "__cm_unwrap(" + argStrs[0] + ").filter((x) => " + argStrs[2] + "(x, " + argStrs[3] +
               "))";
    }

    // スライス操作
    if ((name == "cm_slice_get_i8" || name == "cm_slice_get_i32" || name == "cm_slice_get_i64" ||
         name == "cm_slice_get_f64" || name == "cm_slice_get_ptr") &&
        argStrs.size() >= 2) {
        return "__cm_unwrap(" + argStrs[0] + ")[" + argStrs[1] + "]";
    }
    if ((name == "cm_slice_first_i32" || name == "cm_slice_first_i64") && argStrs.size() >= 1) {
        return "__cm_unwrap(" + argStrs[0] + ")[0]";
    }
    if ((name == "cm_slice_last_i32" || name == "cm_slice_last_i64") && argStrs.size() >= 1) {
        return "__cm_unwrap(" + argStrs[0] + ")[__cm_unwrap(" + argStrs[0] + ").length - 1]";
    }
    if ((name == "cm_slice_push_i8" || name == "cm_slice_push_i32" || name == "cm_slice_push_i64" ||
         name == "cm_slice_push_f32" || name == "cm_slice_push_f64" ||
         name == "cm_slice_push_ptr") &&
        argStrs.size() >= 2) {
        return "__cm_unwrap(" + argStrs[0] + ").push(" + argStrs[1] + ")";
    }
    if ((name == "cm_slice_pop_i32" || name == "cm_slice_pop_i64" || name == "cm_slice_pop_f32" ||
         name == "cm_slice_pop_ptr") &&
        argStrs.size() >= 1) {
        return "__cm_unwrap(" + argStrs[0] + ").pop()";
    }
    if (name == "cm_slice_delete" && argStrs.size() >= 2) {
        return "__cm_unwrap(" + argStrs[0] + ").splice(" + argStrs[1] + ", 1)";
    }
    if (name == "cm_slice_clear" && argStrs.size() >= 1) {
        return "(__cm_unwrap(" + argStrs[0] + ").length = 0)";
    }
    if (name == "cm_slice_len" && argStrs.size() >= 1) {
        return "__cm_unwrap(" + argStrs[0] + ").length";
    }
    if (name == "cm_slice_cap" && argStrs.size() >= 1) {
        return "__cm_unwrap(" + argStrs[0] + ").length";
    }
    if (name == "cm_slice_subslice" && argStrs.size() >= 3) {
        return "__cm_unwrap(" + argStrs[0] + ").slice(" + argStrs[1] + ", " + argStrs[2] + ")";
    }
    if ((name == "cm_slice_set_i32" || name == "cm_slice_set_i64" || name == "cm_slice_set_f32" ||
         name == "cm_slice_set_ptr") &&
        argStrs.size() >= 3) {
        return "(__cm_unwrap(" + argStrs[0] + ")[" + argStrs[1] + "] = " + argStrs[2] + ")";
    }
    if (name == "cm_slice_push_slice" && argStrs.size() >= 2) {
        return "__cm_unwrap(" + argStrs[0] + ").push(" + argStrs[1] + ")";
    }
    if (name == "malloc" && argStrs.size() >= 1) {
        return "{value: 0}";
    }
    if (name == "realloc" && argStrs.size() >= 2) {
        return argStrs[0];
    }
    if (name == "free") {
        return "undefined";
    }
    if (name == "memcpy" && argStrs.size() >= 3) {
        return "Object.assign(" + argStrs[0] + ", " + argStrs[1] + ")";
    }
    if (name == "memset" && argStrs.size() >= 3) {
        return argStrs[0];
    }
    if (name == "cm_make_slice" && argStrs.size() >= 2) {
        return "[]";
    }
    if (name == "cm_slice_get_subslice" && argStrs.size() >= 2) {
        return "__cm_unwrap(" + argStrs[0] + ")[" + argStrs[1] + "]";
    }
    if (name == "cm_slice_sort" && argStrs.size() >= 1) {
        return "[...__cm_unwrap(" + argStrs[0] + ")].sort((a, b) => a - b)";
    }
    if (name == "cm_slice_reverse" && argStrs.size() >= 1) {
        return "[...__cm_unwrap(" + argStrs[0] + ")].reverse()";
    }
    if (name == "cm_slice_first" && argStrs.size() >= 1) {
        return "__cm_unwrap(" + argStrs[0] + ")[0]";
    }
    if (name == "cm_slice_last" && argStrs.size() >= 1) {
        return "__cm_unwrap(" + argStrs[0] + ")[__cm_unwrap(" + argStrs[0] + ").length - 1]";
    }
    if (name == "cm_array_equal" && argStrs.size() >= 2) {
        return "__cm_deep_equal(" + argStrs[0] + ", " + argStrs[1] + ")";
    }
    if (name == "cm_slice_equal" && argStrs.size() >= 2) {
        return "__cm_deep_equal(" + argStrs[0] + ", " + argStrs[1] + ")";
    }
    if ((name == "__builtin_array_find" || name == "__builtin_array_find_i32" ||
         name == "__builtin_array_find_i64") &&
        argStrs.size() >= 3) {
        return "(__cm_unwrap(" + argStrs[0] + ").find(" + argStrs[2] + ") ?? 0)";
    }
    if ((name == "__builtin_array_sort" || name == "__builtin_array_sort_i32" ||
         name == "__builtin_array_sort_i64") &&
        argStrs.size() >= 2) {
        return "[...__cm_unwrap(" + argStrs[0] + ")].sort((a, b) => a - b)";
    }
    if ((name == "__builtin_array_map" || name == "__builtin_array_map_i32" ||
         name == "__builtin_array_map_i64") &&
        argStrs.size() >= 3) {
        return argStrs[0] + ".map(" + argStrs[2] + ")";
    }
    if ((name == "__builtin_array_filter" || name == "__builtin_array_filter_i32" ||
         name == "__builtin_array_filter_i64") &&
        argStrs.size() >= 3) {
        return argStrs[0] + ".filter(" + argStrs[2] + ")";
    }
    if (name == "cm_array_to_slice" && argStrs.size() >= 3) {
        return "[...__cm_unwrap(" + argStrs[0] + ")]";
    }
    if (name == "cm_slice_to_array" && argStrs.size() >= 1) {
        return "[...__cm_unwrap(" + argStrs[0] + ")]";
    }
    if (name == "cm_slice_new" && argStrs.size() >= 2) {
        return "[]";
    }
    if (name == "__print__" && argStrs.size() >= 1) {
        return "process.stdout.write(String(" + argStrs[0] + "))";
    }

    // 不明な組み込み関数
    return "/* unknown builtin: " + name + " */ undefined";
}

}  // namespace cm::codegen::js
