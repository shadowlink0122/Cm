// JS組み込み関数の実装
#include "builtins.hpp"

#include <string>
#include <unordered_set>
#include <vector>

namespace cm::codegen::js {

// 組み込み関数かどうかをチェック
bool isBuiltinFunction(const std::string& name) {
    static const std::unordered_set<std::string> builtins = {
        "println",
        "print",
        "panic",
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
        "cm_char_to_string",
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
        // 配列メソッドは__builtin_array_*名でのみ扱う。
        // 裸名（find/map等）を登録するとemitBuiltinCallに発行ケースが無く
        // 常にundefinedになる上、同名のユーザー定義関数を飲み込むため登録しない
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
        "__builtin_array_forEach_i32",
        "__builtin_array_forEach_i64",
        // クロージャー版
        "__builtin_array_map_closure",
        "__builtin_array_filter_closure",
        // スライス操作
        "cm_slice_get_i8",
        "cm_slice_get_i16",
        "cm_slice_get_i32",
        "cm_slice_get_i64",
        "cm_slice_get_f64",
        "cm_slice_get_ptr",
        "cm_slice_first_i32",
        "cm_slice_first_i64",
        "cm_slice_last_i32",
        "cm_slice_last_i64",
        "cm_slice_push_i8",
        "cm_slice_push_i16",
        "cm_slice_push_i32",
        "cm_slice_push_i64",
        "cm_slice_push_f32",
        "cm_slice_push_f64",
        "cm_slice_push_ptr",
        "cm_slice_push_blob",
        "cm_slice_get_element_ptr",
        "cm_slice_pop_i16",
        "cm_slice_pop_i32",
        "cm_slice_pop_i64",
        "cm_slice_pop_f32",
        "cm_slice_pop_f64",
        "cm_slice_pop_ptr",
        "cm_slice_delete",
        "cm_slice_clear",
        "cm_slice_len",
        "cm_bounds_error",
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
        "cm_slice_sort_i8",
        "cm_slice_sort_u8",
        "cm_slice_sort_i16",
        "cm_slice_sort_u16",
        "cm_slice_sort_i32",
        "cm_slice_sort_u32",
        "cm_slice_sort_i64",
        "cm_slice_sort_u64",
        "cm_slice_sort_f32",
        "cm_slice_sort_f64",
        "cm_slice_sort_str",
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
        // プロセス終了
        "exit",
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

    // exit(code): Node.jsでは process.exit、ブラウザ等では例外で停止する
    // 末尾の undefined 返却は、戻り値が型付き一時変数へ代入されるTS出力で void 型不一致にならないようにするため（JSでは実際には到達前に停止する）
    if (name == "exit") {
        std::string code = argStrs.empty() ? "0" : argStrs[0];
        return "((c) => { if (typeof process !== \"undefined\") process.exit(c); "
               "throw new Error(\"exit(\" + c + \")\"); return undefined; })(" +
               code + ")";
    }

    // 境界外アクセスのトラップ（--sanitize=bounds。M1。native/wasmのcm_bounds_errorと同一メッセージ・終了コード1）
    if (name == "cm_bounds_error") {
        std::string idx = argStrs.empty() ? "0" : argStrs[0];
        std::string len = argStrs.size() > 1 ? argStrs[1] : "0";
        return "((i, l) => { console.log(\"error: index out of bounds: index \" + i + \", length "
               "\" + "
               "l); if (typeof process !== \"undefined\") process.exit(1); throw new "
               "Error(\"index out of bounds\"); return undefined; })(" +
               idx + ", " + len + ")";
    }

    // panic(msg): "panic: <msg>" を出力して異常終了する（Result/Optionのunwrap等で使用。
    // ネイティブランタイムの__cm_panicと同じ形式・終了コード134）
    if (name == "panic") {
        std::string msg = argStrs.empty() ? "\"panic\"" : argStrs[0];
        return "((m) => { console.log(\"panic: \" + m); if (typeof process !== \"undefined\") "
               "process.exit(134); throw new Error(\"panic: \" + m); return undefined; })(" +
               msg + ")";
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
    // char を string へ変換する組み込み（string + char の連結でloweringが挿入する）。
    // JSではcharは数値表現なので文字コードから文字列を作る。定義しないと未定義参照で落ちる
    if (name == "cm_char_to_string" && argStrs.size() >= 1) {
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
    if ((name == "cm_slice_get_i8" || name == "cm_slice_get_i16" || name == "cm_slice_get_i32" ||
         name == "cm_slice_get_i64" || name == "cm_slice_get_f64" || name == "cm_slice_get_ptr") &&
        argStrs.size() >= 2) {
        return "__cm_unwrap(" + argStrs[0] + ")[" + argStrs[1] + "]";
    }
    if ((name == "cm_slice_first_i32" || name == "cm_slice_first_i64") && argStrs.size() >= 1) {
        return "__cm_unwrap(" + argStrs[0] + ")[0]";
    }
    if ((name == "cm_slice_last_i32" || name == "cm_slice_last_i64") && argStrs.size() >= 1) {
        return "__cm_unwrap(" + argStrs[0] + ")[__cm_unwrap(" + argStrs[0] + ").length - 1]";
    }
    if ((name == "cm_slice_push_i8" || name == "cm_slice_push_i16" || name == "cm_slice_push_i32" ||
         name == "cm_slice_push_i64" || name == "cm_slice_push_f32" ||
         name == "cm_slice_push_f64" || name == "cm_slice_push_ptr") &&
        argStrs.size() >= 2) {
        return "__cm_unwrap(" + argStrs[0] + ").push(" + argStrs[1] + ")";
    }
    if ((name == "__builtin_array_forEach_i32" || name == "__builtin_array_forEach_i64") &&
        argStrs.size() >= 3) {
        // forEach: 第2引数（サイズ）はJSでは不要（配列自身が長さを持つ）
        return "__cm_unwrap(" + argStrs[0] + ").forEach((x) => " + argStrs[2] + "(x))";
    }
    if (name == "cm_slice_push_blob" && argStrs.size() >= 2) {
        // blob push: 参照が {__arr, __idx} 形式（ユニオン等のboxed値）なら指し先を、オブジェクト直接参照（構造体ローカルの&）ならそのままpushする
        return "__cm_unwrap(" + argStrs[0] + ").push(__cm_deref(" + argStrs[1] + "))";
    }
    if (name == "cm_slice_get_element_ptr" && argStrs.size() >= 2) {
        // 要素へのポインタオブジェクトを返す（デリファレンス構文 __arr[__idx] で要素を読む）
        return "({__arr: __cm_unwrap(" + argStrs[0] + "), __idx: " + argStrs[1] + "})";
    }
    if ((name == "cm_slice_pop_i16" || name == "cm_slice_pop_i32" || name == "cm_slice_pop_i64" ||
         name == "cm_slice_pop_f32" || name == "cm_slice_pop_f64" || name == "cm_slice_pop_ptr") &&
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
    if (name == "cm_slice_sort_str" && argStrs.size() >= 1) {
        // 文字列スライスは辞書順で安定ソートする（C5）
        return "[...__cm_unwrap(" + argStrs[0] + ")].sort((a, b) => (a < b ? -1 : a > b ? 1 : 0))";
    }
    if ((name == "cm_slice_sort" || name == "cm_slice_sort_i8" || name == "cm_slice_sort_u8" ||
         name == "cm_slice_sort_i16" || name == "cm_slice_sort_u16" ||
         name == "cm_slice_sort_i32" || name == "cm_slice_sort_u32" ||
         name == "cm_slice_sort_i64" || name == "cm_slice_sort_u64" ||
         name == "cm_slice_sort_f32" || name == "cm_slice_sort_f64") &&
        argStrs.size() >= 1) {
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
