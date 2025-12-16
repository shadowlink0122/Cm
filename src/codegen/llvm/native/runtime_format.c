// Cm Language Runtime - Format Functions (LLVM Backend)
// String formatting and conversion implementations

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include "../../common/runtime_common.h"

// ============================================================
// String Builtin Functions
// ============================================================
size_t __builtin_string_len(const char* str) {
    return str ? strlen(str) : 0;
}

char __builtin_string_charAt(const char* str, int64_t index) {
    if (!str || index < 0) return '\0';
    size_t len = strlen(str);
    if ((size_t)index >= len) return '\0';
    return str[index];
}

char* __builtin_string_substring(const char* str, int64_t start, int64_t end) {
    if (!str) return NULL;
    size_t len = strlen(str);
    // Python風: 負の値は末尾からの位置
    if (start < 0) {
        start = (int64_t)len + start;
        if (start < 0) start = 0;
    }
    if (end < 0) {
        end = (int64_t)len + end + 1;  // -1 => len
    }
    if ((size_t)end > len) end = (int64_t)len;
    if (start >= end) {
        char* empty = (char*)malloc(1);
        if (empty) empty[0] = '\0';
        return empty;
    }
    size_t result_len = (size_t)(end - start);
    char* result = (char*)malloc(result_len + 1);
    if (!result) return NULL;
    memcpy(result, str + start, result_len);
    result[result_len] = '\0';
    return result;
}

int64_t __builtin_string_indexOf(const char* str, const char* substr) {
    if (!str || !substr) return -1;
    const char* pos = strstr(str, substr);
    if (!pos) return -1;
    return (int64_t)(pos - str);
}

char* __builtin_string_toUpperCase(const char* str) {
    if (!str) return NULL;
    size_t len = strlen(str);
    char* result = (char*)malloc(len + 1);
    if (!result) return NULL;
    for (size_t i = 0; i < len; i++) {
        char c = str[i];
        if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
        result[i] = c;
    }
    result[len] = '\0';
    return result;
}

char* __builtin_string_toLowerCase(const char* str) {
    if (!str) return NULL;
    size_t len = strlen(str);
    char* result = (char*)malloc(len + 1);
    if (!result) return NULL;
    for (size_t i = 0; i < len; i++) {
        char c = str[i];
        if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
        result[i] = c;
    }
    result[len] = '\0';
    return result;
}

char* __builtin_string_trim(const char* str) {
    if (!str) return NULL;
    size_t len = strlen(str);
    size_t start = 0, end = len;
    while (start < len && (str[start] == ' ' || str[start] == '\t' || 
           str[start] == '\n' || str[start] == '\r')) start++;
    while (end > start && (str[end-1] == ' ' || str[end-1] == '\t' || 
           str[end-1] == '\n' || str[end-1] == '\r')) end--;
    size_t result_len = end - start;
    char* result = (char*)malloc(result_len + 1);
    if (!result) return NULL;
    memcpy(result, str + start, result_len);
    result[result_len] = '\0';
    return result;
}

bool __builtin_string_startsWith(const char* str, const char* prefix) {
    if (!str || !prefix) return false;
    size_t str_len = strlen(str);
    size_t prefix_len = strlen(prefix);
    if (prefix_len > str_len) return false;
    return strncmp(str, prefix, prefix_len) == 0;
}

bool __builtin_string_endsWith(const char* str, const char* suffix) {
    if (!str || !suffix) return false;
    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);
    if (suffix_len > str_len) return false;
    return strcmp(str + str_len - suffix_len, suffix) == 0;
}

bool __builtin_string_includes(const char* str, const char* substr) {
    if (!str || !substr) return false;
    return strstr(str, substr) != NULL;
}

char* __builtin_string_repeat(const char* str, int64_t count) {
    if (!str || count <= 0) {
        char* empty = (char*)malloc(1);
        if (empty) empty[0] = '\0';
        return empty;
    }
    size_t len = strlen(str);
    size_t total_len = len * (size_t)count;
    char* result = (char*)malloc(total_len + 1);
    if (!result) return NULL;
    for (int64_t i = 0; i < count; i++) {
        memcpy(result + (i * len), str, len);
    }
    result[total_len] = '\0';
    return result;
}

char* __builtin_string_replace(const char* str, const char* from, const char* to) {
    if (!str) return NULL;
    if (!from || !to) {
        char* copy = (char*)malloc(strlen(str) + 1);
        if (copy) strcpy(copy, str);
        return copy;
    }
    const char* pos = strstr(str, from);
    if (!pos) {
        char* copy = (char*)malloc(strlen(str) + 1);
        if (copy) strcpy(copy, str);
        return copy;
    }
    size_t str_len = strlen(str);
    size_t from_len = strlen(from);
    size_t to_len = strlen(to);
    size_t result_len = str_len - from_len + to_len;
    char* result = (char*)malloc(result_len + 1);
    if (!result) return NULL;
    size_t prefix_len = (size_t)(pos - str);
    memcpy(result, str, prefix_len);
    memcpy(result + prefix_len, to, to_len);
    strcpy(result + prefix_len + to_len, pos + from_len);
    return result;
}

// ============================================================
// Array Slice Functions
// ============================================================
// 配列スライス: 新しい配列を作成して要素をコピー
// arr: ソース配列へのポインタ
// elem_size: 各要素のバイトサイズ
// arr_len: ソース配列の長さ
// start: 開始インデックス
// end: 終了インデックス（-1は最後まで）
// out_len: 出力配列の長さを格納するポインタ
void* __builtin_array_slice(void* arr, int64_t elem_size, int64_t arr_len, 
                            int64_t start, int64_t end, int64_t* out_len) {
    if (!arr || elem_size <= 0 || arr_len <= 0) {
        if (out_len) *out_len = 0;
        return NULL;
    }
    
    // Python風: 負のインデックス処理
    if (start < 0) {
        start = arr_len + start;
        if (start < 0) start = 0;
    }
    if (end < 0) {
        end = arr_len + end + 1;
    }
    if (end > arr_len) end = arr_len;
    if (start >= end || start >= arr_len) {
        if (out_len) *out_len = 0;
        return NULL;
    }
    
    int64_t slice_len = end - start;
    void* result = malloc((size_t)(slice_len * elem_size));
    if (!result) {
        if (out_len) *out_len = 0;
        return NULL;
    }
    
    memcpy(result, (char*)arr + (start * elem_size), (size_t)(slice_len * elem_size));
    if (out_len) *out_len = slice_len;
    return result;
}

// 整数配列スライス（便利関数）
int64_t* __builtin_array_slice_int(int64_t* arr, int64_t arr_len, 
                                   int64_t start, int64_t end, int64_t* out_len) {
    return (int64_t*)__builtin_array_slice(arr, sizeof(int64_t), arr_len, start, end, out_len);
}

// i32配列スライス
int32_t* __builtin_array_slice_i32(int32_t* arr, int64_t arr_len,
                                   int64_t start, int64_t end, int64_t* out_len) {
    return (int32_t*)__builtin_array_slice(arr, sizeof(int32_t), arr_len, start, end, out_len);
}

// ============================================================
// Array Higher-Order Functions
// ============================================================

// forEach: 各要素に対して関数を実行
void __builtin_array_forEach_i64(int64_t* arr, int64_t size, void (*callback)(int64_t)) {
    if (!arr || !callback) return;
    for (int64_t i = 0; i < size; i++) {
        callback(arr[i]);
    }
}

void __builtin_array_forEach_i32(int32_t* arr, int64_t size, void (*callback)(int32_t)) {
    if (!arr || !callback) return;
    for (int64_t i = 0; i < size; i++) {
        callback(arr[i]);
    }
}

// reduce: 要素を畳み込み
int64_t __builtin_array_reduce_i64(int64_t* arr, int64_t size, 
                                   int64_t (*callback)(int64_t, int64_t), int64_t init) {
    if (!arr || !callback) return init;
    int64_t acc = init;
    for (int64_t i = 0; i < size; i++) {
        acc = callback(acc, arr[i]);
    }
    return acc;
}

int32_t __builtin_array_reduce_i32(int32_t* arr, int64_t size,
                                   int32_t (*callback)(int32_t, int32_t), int32_t init) {
    if (!arr || !callback) return init;
    int32_t acc = init;
    for (int64_t i = 0; i < size; i++) {
        acc = callback(acc, arr[i]);
    }
    return acc;
}

// some: いずれかの要素が条件を満たすか
bool __builtin_array_some_i64(int64_t* arr, int64_t size, bool (*predicate)(int64_t)) {
    if (!arr || !predicate) return false;
    for (int64_t i = 0; i < size; i++) {
        if (predicate(arr[i])) return true;
    }
    return false;
}

bool __builtin_array_some_i32(int32_t* arr, int64_t size, bool (*predicate)(int32_t)) {
    if (!arr || !predicate) return false;
    for (int64_t i = 0; i < size; i++) {
        if (predicate(arr[i])) return true;
    }
    return false;
}

// every: すべての要素が条件を満たすか
bool __builtin_array_every_i64(int64_t* arr, int64_t size, bool (*predicate)(int64_t)) {
    if (!arr || !predicate) return true;
    for (int64_t i = 0; i < size; i++) {
        if (!predicate(arr[i])) return false;
    }
    return true;
}

bool __builtin_array_every_i32(int32_t* arr, int64_t size, bool (*predicate)(int32_t)) {
    if (!arr || !predicate) return true;
    for (int64_t i = 0; i < size; i++) {
        if (!predicate(arr[i])) return false;
    }
    return true;
}

// findIndex: 条件を満たす最初の要素のインデックス
int64_t __builtin_array_findIndex_i64(int64_t* arr, int64_t size, bool (*predicate)(int64_t)) {
    if (!arr || !predicate) return -1;
    for (int64_t i = 0; i < size; i++) {
        if (predicate(arr[i])) return i;
    }
    return -1;
}

int64_t __builtin_array_findIndex_i32(int32_t* arr, int64_t size, bool (*predicate)(int32_t)) {
    if (!arr || !predicate) return -1;
    for (int64_t i = 0; i < size; i++) {
        if (predicate(arr[i])) return i;
    }
    return -1;
}

// indexOf: 要素の位置を検索
int64_t __builtin_array_indexOf_i64(int64_t* arr, int64_t size, int64_t value) {
    if (!arr) return -1;
    for (int64_t i = 0; i < size; i++) {
        if (arr[i] == value) return i;
    }
    return -1;
}

int64_t __builtin_array_indexOf_i32(int32_t* arr, int64_t size, int32_t value) {
    if (!arr) return -1;
    for (int64_t i = 0; i < size; i++) {
        if (arr[i] == value) return i;
    }
    return -1;
}

// includes: 要素が含まれているか
bool __builtin_array_includes_i64(int64_t* arr, int64_t size, int64_t value) {
    return __builtin_array_indexOf_i64(arr, size, value) >= 0;
}

bool __builtin_array_includes_i32(int32_t* arr, int64_t size, int32_t value) {
    return __builtin_array_indexOf_i32(arr, size, value) >= 0;
}

// ============================================================
// Escape Processing
// ============================================================
char* cm_unescape_braces(const char* str) {
    if (!str) return NULL;
    
    size_t len = strlen(str);
    char* result = (char*)malloc(len + 1);
    if (!result) return NULL;
    
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (str[i] == '{' && i + 1 < len && str[i + 1] == '{') {
            result[j++] = '{';
            i++;
        } else if (str[i] == '}' && i + 1 < len && str[i + 1] == '}') {
            result[j++] = '}';
            i++;
        } else {
            result[j++] = str[i];
        }
    }
    result[j] = '\0';
    return result;
}

char* cm_format_unescape_braces(const char* str) {
    return cm_unescape_braces(str);
}

// ============================================================
// Type to String Conversion
// ============================================================
char* cm_format_int(int value) {
    char* buffer = (char*)malloc(32);
    if (buffer) {
        snprintf(buffer, 32, "%d", value);
    }
    return buffer;
}

char* cm_format_uint(unsigned int value) {
    char* buffer = (char*)malloc(32);
    if (buffer) {
        snprintf(buffer, 32, "%u", value);
    }
    return buffer;
}

char* cm_format_double(double value) {
    char* buffer = (char*)malloc(64);
    if (buffer) {
        if (value == (int)value) {
            snprintf(buffer, 64, "%d", (int)value);
        } else {
            snprintf(buffer, 64, "%g", value);
        }
    }
    return buffer;
}

char* cm_format_double_precision(double value, int precision) {
    char* buffer = (char*)malloc(64);
    if (buffer) {
        snprintf(buffer, 64, "%.*f", precision, value);
    }
    return buffer;
}

char* cm_format_bool(char value) {
    if (value) {
        char* buffer = (char*)malloc(5);
        if (buffer) strcpy(buffer, "true");
        return buffer;
    } else {
        char* buffer = (char*)malloc(6);
        if (buffer) strcpy(buffer, "false");
        return buffer;
    }
}

char* cm_format_char(char value) {
    char* buffer = (char*)malloc(2);
    if (buffer) {
        buffer[0] = value;
        buffer[1] = '\0';
    }
    return buffer;
}

// ============================================================
// Integer Format Variants
// ============================================================
char* cm_format_int_hex(long long value) {
    char* buffer = (char*)malloc(32);
    if (buffer) {
        snprintf(buffer, 32, "%llx", value);
    }
    return buffer;
}

char* cm_format_int_HEX(long long value) {
    char* buffer = (char*)malloc(32);
    if (buffer) {
        snprintf(buffer, 32, "%llX", value);
    }
    return buffer;
}

char* cm_format_int_binary(long long value) {
    char* buffer = (char*)malloc(65);
    if (!buffer) return NULL;

    if (value == 0) {
        strcpy(buffer, "0");
        return buffer;
    }

    unsigned long long uval = (unsigned long long)value;
    char temp[65];
    int i = 0;

    while (uval > 0) {
        temp[i++] = (uval & 1) ? '1' : '0';
        uval >>= 1;
    }

    int j = 0;
    while (i > 0) {
        buffer[j++] = temp[--i];
    }
    buffer[j] = '\0';

    return buffer;
}

char* cm_format_int_octal(long long value) {
    char* buffer = (char*)malloc(32);
    if (buffer) {
        snprintf(buffer, 32, "%llo", value);
    }
    return buffer;
}

// ============================================================
// Double Format Variants
// ============================================================
char* cm_format_double_exp(double value) {
    char* buffer = (char*)malloc(64);
    if (buffer) {
        snprintf(buffer, 64, "%e", value);
    }
    return buffer;
}

char* cm_format_double_EXP(double value) {
    char* buffer = (char*)malloc(64);
    if (buffer) {
        snprintf(buffer, 64, "%E", value);
    }
    return buffer;
}

char* cm_format_double_scientific(double value, int uppercase) {
    if (uppercase) {
        return cm_format_double_EXP(value);
    } else {
        return cm_format_double_exp(value);
    }
}

// ============================================================
// String Utilities
// ============================================================
char* cm_string_concat(const char* left, const char* right) {
    if (!left) left = "";
    if (!right) right = "";

    size_t len1 = strlen(left);
    size_t len2 = strlen(right);
    char* result = (char*)malloc(len1 + len2 + 1);

    if (result) {
        strcpy(result, left);
        strcat(result, right);
    }

    return result;
}

// Type to string conversion aliases
char* cm_int_to_string(int value) {
    return cm_format_int(value);
}

char* cm_uint_to_string(unsigned int value) {
    return cm_format_uint(value);
}

char* cm_char_to_string(char value) {
    return cm_format_char(value);
}

char* cm_bool_to_string(char value) {
    return cm_format_bool(value);
}

char* cm_double_to_string(double value) {
    char* buffer = (char*)malloc(64);
    if (buffer) {
        if (value == (long long)value) {
            snprintf(buffer, 64, "%lld", (long long)value);
        } else {
            snprintf(buffer, 64, "%g", value);
        }
    }
    return buffer;
}

// ============================================================
// Format Replace Functions
// ============================================================
char* cm_format_replace(const char* format, const char* value) {
    if (!format) return NULL;
    if (!value) value = "";

    const char* start = strchr(format, '{');
    if (!start) {
        char* result = (char*)malloc(strlen(format) + 1);
        if (result) strcpy(result, format);
        return result;
    }

    const char* end = strchr(start, '}');
    if (!end) {
        char* result = (char*)malloc(strlen(format) + 1);
        if (result) strcpy(result, format);
        return result;
    }

    size_t placeholder_len = end - start + 1;
    size_t formatLen = strlen(format);
    size_t valueLen = strlen(value);
    size_t resultLen = formatLen - placeholder_len + valueLen + 1;

    char* result = (char*)malloc(resultLen);
    if (!result) return NULL;

    size_t prefixLen = start - format;
    strncpy(result, format, prefixLen);
    result[prefixLen] = '\0';
    strcat(result, value);
    strcat(result, end + 1);

    return result;
}

char* cm_format_replace_int(const char* format, int value) {
    if (!format) return NULL;

    const char* start = strchr(format, '{');
    if (!start) {
        char* result = (char*)malloc(strlen(format) + 1);
        if (result) strcpy(result, format);
        return result;
    }

    const char* end = strchr(start, '}');
    if (!end) {
        char* result = (char*)malloc(strlen(format) + 1);
        if (result) strcpy(result, format);
        return result;
    }

    char specifier[32] = {0};
    size_t spec_len = end - start - 1;
    if (spec_len > 0 && spec_len < sizeof(specifier)) {
        strncpy(specifier, start + 1, spec_len);
    }

    char* formatted_value = NULL;
    if (strcmp(specifier, ":x") == 0) {
        formatted_value = cm_format_int_hex(value);
    } else if (strcmp(specifier, ":X") == 0) {
        formatted_value = cm_format_int_HEX(value);
    } else if (strcmp(specifier, ":b") == 0) {
        formatted_value = cm_format_int_binary(value);
    } else if (strcmp(specifier, ":o") == 0) {
        formatted_value = cm_format_int_octal(value);
    } else if (strncmp(specifier, ":0>", 3) == 0) {
        int width = atoi(specifier + 3);
        char* int_str = cm_format_int(value);
        int val_len = strlen(int_str);

        if (width <= val_len) {
            formatted_value = int_str;
        } else {
            formatted_value = (char*)malloc(width + 1);
            if (formatted_value) {
                int padding = width - val_len;
                for (int i = 0; i < padding; i++) formatted_value[i] = '0';
                strcpy(formatted_value + padding, int_str);
            }
            free(int_str);
        }
    } else {
        formatted_value = cm_format_int(value);
    }

    if (!formatted_value) return NULL;

    char* result = cm_format_replace(format, formatted_value);
    free(formatted_value);
    return result;
}

char* cm_format_replace_uint(const char* format, unsigned int value) {
    if (!format) return NULL;

    char* formatted_value = cm_format_uint(value);
    if (!formatted_value) return NULL;

    char* result = cm_format_replace(format, formatted_value);
    free(formatted_value);
    return result;
}

char* cm_format_replace_double(const char* format, double value) {
    if (!format) return NULL;

    const char* start = strchr(format, '{');
    if (!start) {
        char* result = (char*)malloc(strlen(format) + 1);
        if (result) strcpy(result, format);
        return result;
    }

    const char* end = strchr(start, '}');
    if (!end) {
        char* result = (char*)malloc(strlen(format) + 1);
        if (result) strcpy(result, format);
        return result;
    }

    char specifier[32] = {0};
    size_t spec_len = end - start - 1;
    if (spec_len > 0 && spec_len < sizeof(specifier)) {
        strncpy(specifier, start + 1, spec_len);
    }

    char* formatted_value = NULL;
    if (strcmp(specifier, ":e") == 0) {
        formatted_value = cm_format_double_exp(value);
    } else if (strcmp(specifier, ":E") == 0) {
        formatted_value = cm_format_double_EXP(value);
    } else if (specifier[0] == ':' && specifier[1] == '.') {
        int precision = atoi(specifier + 2);
        formatted_value = cm_format_double_precision(value, precision);
    } else {
        formatted_value = cm_format_double(value);
    }

    if (!formatted_value) return NULL;

    char* result = cm_format_replace(format, formatted_value);
    free(formatted_value);
    return result;
}

char* cm_format_replace_string(const char* format, const char* value) {
    if (!format) return NULL;
    if (!value) value = "";

    const char* start = strchr(format, '{');
    if (!start) {
        char* result = (char*)malloc(strlen(format) + 1);
        if (result) strcpy(result, format);
        return result;
    }

    const char* end = strchr(start, '}');
    if (!end) {
        char* result = (char*)malloc(strlen(format) + 1);
        if (result) strcpy(result, format);
        return result;
    }

    char specifier[32] = {0};
    size_t spec_len = end - start - 1;
    if (spec_len > 0 && spec_len < sizeof(specifier)) {
        strncpy(specifier, start + 1, spec_len);
    }

    char* formatted_value = NULL;
    if (strncmp(specifier, ":<", 2) == 0 || strncmp(specifier, ":>", 2) == 0 || strncmp(specifier, ":^", 2) == 0) {
        char align = specifier[1];
        int width = atoi(specifier + 2);
        int val_len = strlen(value);

        if (width <= val_len) {
            formatted_value = (char*)malloc(val_len + 1);
            if (formatted_value) strcpy(formatted_value, value);
        } else {
            formatted_value = (char*)malloc(width + 1);
            if (formatted_value) {
                int padding = width - val_len;
                if (align == '<') {
                    strcpy(formatted_value, value);
                    for (int i = val_len; i < width; i++) formatted_value[i] = ' ';
                } else if (align == '>') {
                    for (int i = 0; i < padding; i++) formatted_value[i] = ' ';
                    strcpy(formatted_value + padding, value);
                } else if (align == '^') {
                    int left_pad = padding / 2;
                    for (int i = 0; i < left_pad; i++) formatted_value[i] = ' ';
                    strcpy(formatted_value + left_pad, value);
                    for (int i = left_pad + val_len; i < width; i++) formatted_value[i] = ' ';
                }
                formatted_value[width] = '\0';
            }
        }
    } else {
        formatted_value = (char*)malloc(strlen(value) + 1);
        if (formatted_value) strcpy(formatted_value, value);
    }

    if (!formatted_value) return NULL;

    char* result = cm_format_replace(format, formatted_value);
    free(formatted_value);
    return result;
}

// ============================================================
// Format String Functions
// ============================================================
char* cm_format_string(const char* format, int num_args, ...) {
    if (!format) return NULL;
    
    va_list args;
    va_start(args, num_args);
    
    char* result = NULL;
    size_t result_len = 0;
    const char* p = format;
    int arg_idx = 0;
    
    while (*p) {
        if (*p == '{' && *(p + 1) == '}') {
            if (arg_idx < num_args) {
                const char* arg = va_arg(args, const char*);
                if (!arg) arg = "";
                size_t arg_len = strlen(arg);
                result = (char*)realloc(result, result_len + arg_len + 1);
                if (result) {
                    strcpy(result + result_len, arg);
                    result_len += arg_len;
                }
                arg_idx++;
            }
            p += 2;
        } else {
            result = (char*)realloc(result, result_len + 2);
            if (result) {
                result[result_len++] = *p;
                result[result_len] = '\0';
            }
            p++;
        }
    }
    
    va_end(args);
    
    if (!result) {
        result = (char*)malloc(1);
        if (result) result[0] = '\0';
    }
    
    return result;
}

char* cm_format_string_1(const char* fmt, const char* arg1) {
    return cm_format_replace(fmt, arg1);
}

char* cm_format_string_2(const char* fmt, const char* arg1, const char* arg2) {
    char* temp = cm_format_replace(fmt, arg1);
    char* result = cm_format_replace(temp, arg2);
    free(temp);
    return result;
}

char* cm_format_string_3(const char* fmt, const char* arg1, const char* arg2, const char* arg3) {
    char* temp1 = cm_format_replace(fmt, arg1);
    char* temp2 = cm_format_replace(temp1, arg2);
    char* result = cm_format_replace(temp2, arg3);
    free(temp1);
    free(temp2);
    return result;
}

char* cm_format_string_4(const char* fmt, const char* arg1, const char* arg2, const char* arg3, const char* arg4) {
    char* temp1 = cm_format_replace(fmt, arg1);
    char* temp2 = cm_format_replace(temp1, arg2);
    char* temp3 = cm_format_replace(temp2, arg3);
    char* result = cm_format_replace(temp3, arg4);
    free(temp1);
    free(temp2);
    free(temp3);
    return result;
}

// ============================================================
// Panic
// ============================================================
void __cm_panic(const char* message) {
    fprintf(stderr, "panic: %s\n", message);
    abort();
}
