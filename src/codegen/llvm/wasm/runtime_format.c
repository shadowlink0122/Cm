// Cm Language Runtime - Format Functions (WASM Backend)
// String formatting and conversion implementations for WASM

#include <stddef.h>
#include <stdint.h>

// Boolean type
#ifndef __cplusplus
typedef int bool;
#define true 1
#define false 0
#endif

// ============================================================
// CmSlice Structure (used by array methods)
// ============================================================
#ifndef CM_SLICE_DEFINED
#define CM_SLICE_DEFINED
typedef struct {
    void* data;
    int64_t len;
    int64_t cap;
    int64_t elem_size;
} CmSlice;
#endif

// ============================================================
// String Length Function (defined here to avoid dependency issues)
// ============================================================
static size_t wasm_strlen(const char* str) {
    if (!str) return 0;
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

// Builtin string length function (used by Cm .len() method)
// Returns uint64_t to match the Cm type system
uint64_t __builtin_string_len(const char* str) {
    return (uint64_t)wasm_strlen(str);
}

// ============================================================
// Memory Allocator (Static Pool)
// ============================================================
static char memory_pool[65536];  // 64KB
static size_t pool_offset = 0;

static void* wasm_alloc(size_t size) {
    if (pool_offset + size > sizeof(memory_pool)) {
        pool_offset = 0;  // Simple GC: reset pool
    }
    void* ptr = &memory_pool[pool_offset];
    pool_offset += size;
    return ptr;
}

// ============================================================
// String Builtin Functions
// ============================================================
char __builtin_string_charAt(const char* str, int64_t index) {
    if (!str || index < 0) return '\0';
    size_t len = wasm_strlen(str);
    if ((size_t)index >= len) return '\0';
    return str[index];
}

char __builtin_string_first(const char* str) {
    if (!str || str[0] == '\0') return '\0';
    return str[0];
}

char __builtin_string_last(const char* str) {
    if (!str || str[0] == '\0') return '\0';
    size_t len = wasm_strlen(str);
    return str[len - 1];
}

char* __builtin_string_substring(const char* str, int64_t start, int64_t end) {
    if (!str) {
        char* empty = (char*)wasm_alloc(1);
        empty[0] = '\0';
        return empty;
    }
    size_t len = wasm_strlen(str);
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
        char* empty = (char*)wasm_alloc(1);
        empty[0] = '\0';
        return empty;
    }
    size_t result_len = (size_t)(end - start);
    char* result = (char*)wasm_alloc(result_len + 1);
    for (size_t i = 0; i < result_len; i++) {
        result[i] = str[start + i];
    }
    result[result_len] = '\0';
    return result;
}

// Simple strstr implementation for WASM
static const char* wasm_strstr(const char* haystack, const char* needle) {
    if (!haystack || !needle) return 0;
    if (!*needle) return haystack;
    for (; *haystack; haystack++) {
        const char* h = haystack;
        const char* n = needle;
        while (*n && *h == *n) { h++; n++; }
        if (!*n) return haystack;
    }
    return 0;
}

int64_t __builtin_string_indexOf(const char* str, const char* substr) {
    if (!str || !substr) return -1;
    const char* pos = wasm_strstr(str, substr);
    if (!pos) return -1;
    return (int64_t)(pos - str);
}

char* __builtin_string_toUpperCase(const char* str) {
    if (!str) {
        char* empty = (char*)wasm_alloc(1);
        empty[0] = '\0';
        return empty;
    }
    size_t len = wasm_strlen(str);
    char* result = (char*)wasm_alloc(len + 1);
    for (size_t i = 0; i < len; i++) {
        char c = str[i];
        if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
        result[i] = c;
    }
    result[len] = '\0';
    return result;
}

char* __builtin_string_toLowerCase(const char* str) {
    if (!str) {
        char* empty = (char*)wasm_alloc(1);
        empty[0] = '\0';
        return empty;
    }
    size_t len = wasm_strlen(str);
    char* result = (char*)wasm_alloc(len + 1);
    for (size_t i = 0; i < len; i++) {
        char c = str[i];
        if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
        result[i] = c;
    }
    result[len] = '\0';
    return result;
}

char* __builtin_string_trim(const char* str) {
    if (!str) {
        char* empty = (char*)wasm_alloc(1);
        empty[0] = '\0';
        return empty;
    }
    size_t len = wasm_strlen(str);
    size_t start = 0, end = len;
    while (start < len && (str[start] == ' ' || str[start] == '\t' || 
           str[start] == '\n' || str[start] == '\r')) start++;
    while (end > start && (str[end-1] == ' ' || str[end-1] == '\t' || 
           str[end-1] == '\n' || str[end-1] == '\r')) end--;
    size_t result_len = end - start;
    char* result = (char*)wasm_alloc(result_len + 1);
    for (size_t i = 0; i < result_len; i++) {
        result[i] = str[start + i];
    }
    result[result_len] = '\0';
    return result;
}

bool __builtin_string_startsWith(const char* str, const char* prefix) {
    if (!str || !prefix) return false;
    while (*prefix) {
        if (*str != *prefix) return false;
        str++;
        prefix++;
    }
    return true;
}

bool __builtin_string_endsWith(const char* str, const char* suffix) {
    if (!str || !suffix) return false;
    size_t str_len = wasm_strlen(str);
    size_t suffix_len = wasm_strlen(suffix);
    if (suffix_len > str_len) return false;
    const char* str_end = str + str_len - suffix_len;
    while (*suffix) {
        if (*str_end != *suffix) return false;
        str_end++;
        suffix++;
    }
    return true;
}

bool __builtin_string_includes(const char* str, const char* substr) {
    if (!str || !substr) return false;
    return wasm_strstr(str, substr) != 0;
}

char* __builtin_string_repeat(const char* str, int64_t count) {
    if (!str || count <= 0) {
        char* empty = (char*)wasm_alloc(1);
        empty[0] = '\0';
        return empty;
    }
    size_t len = wasm_strlen(str);
    size_t total_len = len * (size_t)count;
    char* result = (char*)wasm_alloc(total_len + 1);
    for (int64_t i = 0; i < count; i++) {
        for (size_t j = 0; j < len; j++) {
            result[i * len + j] = str[j];
        }
    }
    result[total_len] = '\0';
    return result;
}

char* __builtin_string_replace(const char* str, const char* from, const char* to) {
    if (!str) {
        char* empty = (char*)wasm_alloc(1);
        empty[0] = '\0';
        return empty;
    }
    if (!from || !to) {
        size_t len = wasm_strlen(str);
        char* copy = (char*)wasm_alloc(len + 1);
        for (size_t i = 0; i <= len; i++) copy[i] = str[i];
        return copy;
    }
    const char* pos = wasm_strstr(str, from);
    if (!pos) {
        size_t len = wasm_strlen(str);
        char* copy = (char*)wasm_alloc(len + 1);
        for (size_t i = 0; i <= len; i++) copy[i] = str[i];
        return copy;
    }
    size_t str_len = wasm_strlen(str);
    size_t from_len = wasm_strlen(from);
    size_t to_len = wasm_strlen(to);
    size_t result_len = str_len - from_len + to_len;
    char* result = (char*)wasm_alloc(result_len + 1);
    size_t prefix_len = (size_t)(pos - str);
    for (size_t i = 0; i < prefix_len; i++) result[i] = str[i];
    for (size_t i = 0; i < to_len; i++) result[prefix_len + i] = to[i];
    const char* rest = pos + from_len;
    size_t rest_len = wasm_strlen(rest);
    for (size_t i = 0; i <= rest_len; i++) result[prefix_len + to_len + i] = rest[i];
    return result;
}

// ============================================================
// Array Slice Functions
// ============================================================
void* __builtin_array_slice(void* arr, int64_t elem_size, int64_t arr_len, 
                            int64_t start, int64_t end, int64_t* out_len) {
    // スライス構造体（runtime_slice.cと同じ形式）
    typedef struct {
        void* data;
        int64_t len;
        int64_t cap;
        int64_t elem_size;
    } CmSlice;
    
    if (!arr || elem_size <= 0 || arr_len <= 0) {
        if (out_len) *out_len = 0;
        CmSlice* slice = (CmSlice*)wasm_alloc(sizeof(CmSlice));
        if (slice) {
            slice->data = 0;
            slice->len = 0;
            slice->cap = 0;
            slice->elem_size = elem_size;
        }
        return slice;
    }
    
    // Python風: 負のインデックス処理
    if (start < 0) {
        start = arr_len + start;
        if (start < 0) start = 0;
    }
    if (end < 0) {
        end = arr_len + end;
        if (end < 0) end = 0;
    }
    if (end > arr_len) end = arr_len;
    if (start >= end || start >= arr_len) {
        if (out_len) *out_len = 0;
        CmSlice* slice = (CmSlice*)wasm_alloc(sizeof(CmSlice));
        if (slice) {
            slice->data = 0;
            slice->len = 0;
            slice->cap = 0;
            slice->elem_size = elem_size;
        }
        return slice;
    }
    
    int64_t slice_len = end - start;
    
    // CmSlice構造体を作成
    CmSlice* slice = (CmSlice*)wasm_alloc(sizeof(CmSlice));
    if (!slice) {
        if (out_len) *out_len = 0;
        return 0;
    }
    
    slice->data = wasm_alloc((size_t)(slice_len * elem_size));
    if (!slice->data) {
        if (out_len) *out_len = 0;
        return 0;
    }
    
    // memcpyの代わりにバイト単位でコピー
    char* src = (char*)arr + (start * elem_size);
    char* dst = (char*)slice->data;
    for (int64_t i = 0; i < slice_len * elem_size; i++) {
        dst[i] = src[i];
    }
    
    slice->len = slice_len;
    slice->cap = slice_len;
    slice->elem_size = elem_size;
    
    if (out_len) *out_len = slice_len;
    return slice;
}

int64_t* __builtin_array_slice_int(int64_t* arr, int64_t arr_len,
                                   int64_t start, int64_t end, int64_t* out_len) {
    return (int64_t*)__builtin_array_slice(arr, sizeof(int64_t), arr_len, start, end, out_len);
}

int32_t* __builtin_array_slice_i32(int32_t* arr, int64_t arr_len,
                                   int64_t start, int64_t end, int64_t* out_len) {
    return (int32_t*)__builtin_array_slice(arr, sizeof(int32_t), arr_len, start, end, out_len);
}

// ============================================================
// Integer to String Conversion
// ============================================================
static void wasm_int_to_str(int value, char* buffer, size_t* len) {
    int is_negative = 0;
    unsigned int uval;

    if (value < 0) {
        is_negative = 1;
        if (value == -2147483648) {
            const char* int_min = "-2147483648";
            int i = 0;
            while (int_min[i]) {
                buffer[i] = int_min[i];
                i++;
            }
            *len = i;
            return;
        }
        uval = -value;
    } else {
        uval = value;
    }

    char temp[32];
    int i = 0;
    do {
        temp[i++] = '0' + (uval % 10);
        uval /= 10;
    } while (uval > 0);

    int j = 0;
    if (is_negative) {
        buffer[j++] = '-';
    }

    while (i > 0) {
        buffer[j++] = temp[--i];
    }

    *len = j;
}

static void wasm_uint_to_str(unsigned int value, char* buffer, size_t* len) {
    char temp[32];
    int i = 0;
    do {
        temp[i++] = '0' + (value % 10);
        value /= 10;
    } while (value > 0);

    int j = 0;
    while (i > 0) {
        buffer[j++] = temp[--i];
    }

    *len = j;
}

static void wasm_int64_to_str(long long value, char* buffer, size_t* len) {
    int is_negative = 0;
    unsigned long long uval;

    if (value < 0) {
        is_negative = 1;
        uval = (unsigned long long)(-value);
    } else {
        uval = (unsigned long long)value;
    }

    char temp[32];
    int i = 0;
    do {
        temp[i++] = '0' + (uval % 10);
        uval /= 10;
    } while (uval > 0);

    int j = 0;
    if (is_negative) {
        buffer[j++] = '-';
    }

    while (i > 0) {
        buffer[j++] = temp[--i];
    }

    *len = j;
}

// ============================================================
// Escape Processing
// ============================================================
char* cm_unescape_braces(const char* str) {
    if (!str) return 0;

    size_t len = wasm_strlen(str);
    char* result = (char*)wasm_alloc(len + 1);

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
    char* buffer = (char*)wasm_alloc(32);
    size_t len;
    wasm_int_to_str(value, buffer, &len);
    buffer[len] = '\0';
    return buffer;
}

char* cm_format_uint(unsigned int value) {
    char* buffer = (char*)wasm_alloc(32);
    size_t len;
    wasm_uint_to_str(value, buffer, &len);
    buffer[len] = '\0';
    return buffer;
}

char* cm_format_long(long long value) {
    char* buffer = (char*)wasm_alloc(32);
    size_t len = 0;

    if (value == 0) {
        buffer[0] = '0';
        buffer[1] = '\0';
        return buffer;
    }

    int is_negative = value < 0;
    unsigned long long abs_value = is_negative ? -value : value;

    // 数値を文字列に変換（逆順）
    char temp[32];
    int temp_len = 0;
    while (abs_value > 0) {
        temp[temp_len++] = '0' + (abs_value % 10);
        abs_value /= 10;
    }

    // 負の符号を追加
    if (is_negative) {
        buffer[len++] = '-';
    }

    // 逆順を正順に
    for (int i = temp_len - 1; i >= 0; i--) {
        buffer[len++] = temp[i];
    }

    buffer[len] = '\0';
    return buffer;
}

char* cm_format_ulong(unsigned long long value) {
    char* buffer = (char*)wasm_alloc(32);
    size_t len = 0;

    if (value == 0) {
        buffer[0] = '0';
        buffer[1] = '\0';
        return buffer;
    }

    // 数値を文字列に変換（逆順）
    char temp[32];
    int temp_len = 0;
    while (value > 0) {
        temp[temp_len++] = '0' + (value % 10);
        value /= 10;
    }

    // 逆順を正順に
    for (int i = temp_len - 1; i >= 0; i--) {
        buffer[len++] = temp[i];
    }

    buffer[len] = '\0';
    return buffer;
}

char* cm_format_bool(char value) {
    if (value) {
        char* buffer = (char*)wasm_alloc(5);
        buffer[0] = 't'; buffer[1] = 'r'; buffer[2] = 'u'; buffer[3] = 'e'; buffer[4] = '\0';
        return buffer;
    } else {
        char* buffer = (char*)wasm_alloc(6);
        buffer[0] = 'f'; buffer[1] = 'a'; buffer[2] = 'l'; buffer[3] = 's'; buffer[4] = 'e'; buffer[5] = '\0';
        return buffer;
    }
}

char* cm_format_char(char value) {
    char* buffer = (char*)wasm_alloc(2);
    buffer[0] = value;
    buffer[1] = '\0';
    return buffer;
}

char* cm_format_double(double value) {
    char* buffer = (char*)wasm_alloc(64);
    
    int is_negative = 0;
    if (value < 0) {
        is_negative = 1;
        value = -value;
    }
    
    value += 0.000005;  // Rounding
    
    int int_part = (int)value;
    double frac_part = value - int_part;

    size_t len = 0;
    if (is_negative) {
        buffer[len++] = '-';
    }
    
    char int_buffer[32];
    size_t int_len;
    wasm_int_to_str(int_part, int_buffer, &int_len);
    for (size_t i = 0; i < int_len; i++) {
        buffer[len++] = int_buffer[i];
    }

    buffer[len++] = '.';

    int frac_int = (int)(frac_part * 100000);
    int temp = frac_int;
    int trailing_zeros = 0;
    if (temp == 0) {
        trailing_zeros = 5;
    } else {
        while (temp % 10 == 0) {
            trailing_zeros++;
            temp /= 10;
        }
    }

    int num_digits = 5 - trailing_zeros;
    if (num_digits < 1) num_digits = 1;
    if (num_digits > 5) num_digits = 5;

    int divisor = 10000;
    for (int i = 0; i < num_digits; i++) {
        buffer[len++] = '0' + ((frac_int / divisor) % 10);
        divisor /= 10;
    }

    buffer[len] = '\0';
    return buffer;
}

char* cm_format_double_precision(double value, int precision) {
    char* buffer = (char*)wasm_alloc(64);
    
    double round_adj = 0.5;
    for (int i = 0; i < precision; i++) {
        round_adj /= 10.0;
    }
    if (value >= 0) {
        value += round_adj;
    } else {
        value -= round_adj;
    }
    
    int int_part = (int)value;
    double frac_part = value - int_part;
    if (frac_part < 0) frac_part = -frac_part;

    size_t len;
    wasm_int_to_str(int_part, buffer, &len);
    buffer[len++] = '.';

    for (int i = 0; i < precision; i++) {
        frac_part *= 10;
        int digit = (int)frac_part % 10;
        buffer[len++] = '0' + digit;
    }
    buffer[len] = '\0';

    return buffer;
}

// ============================================================
// Integer Format Variants
// ============================================================
char* cm_format_int_hex(long long value) {
    char* buffer = (char*)wasm_alloc(32);
    unsigned long long uval = (unsigned long long)value;

    char hex_chars[] = "0123456789abcdef";
    char temp[32];
    int i = 0;

    if (uval == 0) {
        buffer[0] = '0';
        buffer[1] = '\0';
        return buffer;
    }

    while (uval > 0) {
        temp[i++] = hex_chars[uval % 16];
        uval /= 16;
    }

    // WASMでは線形メモリオフセットなので0xプレフィックスは付けない
    int j = 0;
    while (i > 0) {
        buffer[j++] = temp[--i];
    }
    buffer[j] = '\0';

    return buffer;
}

char* cm_format_int_HEX(long long value) {
    char* buffer = (char*)wasm_alloc(32);
    unsigned long long uval = (unsigned long long)value;

    char hex_chars[] = "0123456789ABCDEF";
    char temp[32];
    int i = 0;

    if (uval == 0) {
        buffer[0] = '0';
        buffer[1] = '\0';
        return buffer;
    }

    while (uval > 0) {
        temp[i++] = hex_chars[uval % 16];
        uval /= 16;
    }

    // WASMでは線形メモリオフセットなので0xプレフィックスは付けない
    int j = 0;
    while (i > 0) {
        buffer[j++] = temp[--i];
    }
    buffer[j] = '\0';

    return buffer;
}

char* cm_format_int_binary(long long value) {
    char* buffer = (char*)wasm_alloc(65);
    unsigned long long uval = (unsigned long long)value;

    if (uval == 0) {
        buffer[0] = '0';
        buffer[1] = '\0';
        return buffer;
    }

    char temp[64];
    int i = 0;

    while (uval > 0) {
        temp[i++] = (uval % 2) ? '1' : '0';
        uval /= 2;
    }

    int j = 0;
    while (i > 0) {
        buffer[j++] = temp[--i];
    }
    buffer[j] = '\0';

    return buffer;
}

char* cm_format_int_octal(long long value) {
    char* buffer = (char*)wasm_alloc(32);
    unsigned long long uval = (unsigned long long)value;

    if (uval == 0) {
        buffer[0] = '0';
        buffer[1] = '\0';
        return buffer;
    }

    char temp[32];
    int i = 0;

    while (uval > 0) {
        temp[i++] = '0' + (uval % 8);
        uval /= 8;
    }

    int j = 0;
    while (i > 0) {
        buffer[j++] = temp[--i];
    }
    buffer[j] = '\0';

    return buffer;
}

// ============================================================
// Double Format Variants
// ============================================================
char* cm_format_double_scientific(double value, int uppercase) {
    char* buffer = (char*)wasm_alloc(32);

    int is_negative = 0;
    if (value < 0) {
        is_negative = 1;
        value = -value;
    }

    int exponent = 0;
    double mantissa = value;

    if (value != 0.0) {
        while (mantissa >= 10.0) {
            mantissa /= 10.0;
            exponent++;
        }
        while (mantissa < 1.0) {
            mantissa *= 10.0;
            exponent--;
        }
    }

    mantissa += 0.0000005;
    if (mantissa >= 10.0) {
        mantissa /= 10.0;
        exponent++;
    }

    int mantissa_int = (int)mantissa;
    int mantissa_frac = (int)((mantissa - mantissa_int) * 1000000);

    int idx = 0;
    if (is_negative) buffer[idx++] = '-';
    buffer[idx++] = '0' + mantissa_int;
    buffer[idx++] = '.';

    int divisor = 100000;
    for (int i = 0; i < 6; i++) {
        buffer[idx++] = '0' + ((mantissa_frac / divisor) % 10);
        divisor /= 10;
    }

    buffer[idx++] = uppercase ? 'E' : 'e';
    if (exponent < 0) {
        buffer[idx++] = '-';
        exponent = -exponent;
    } else {
        buffer[idx++] = '+';
    }

    buffer[idx++] = '0' + (exponent / 10);
    buffer[idx++] = '0' + (exponent % 10);
    buffer[idx] = '\0';

    return buffer;
}

char* cm_format_double_exp(double value) {
    return cm_format_double_scientific(value, 0);
}

char* cm_format_double_EXP(double value) {
    return cm_format_double_scientific(value, 1);
}

// ============================================================
// String Utilities
// ============================================================
char* cm_string_concat(const char* left, const char* right) {
    if (!left) left = "";
    if (!right) right = "";

    size_t len1 = wasm_strlen(left);
    size_t len2 = wasm_strlen(right);
    char* result = (char*)wasm_alloc(len1 + len2 + 1);

    for (size_t i = 0; i < len1; i++) {
        result[i] = left[i];
    }
    for (size_t i = 0; i < len2; i++) {
        result[len1 + i] = right[i];
    }
    result[len1 + len2] = '\0';

    return result;
}

char* cm_int_to_string(int value) {
    return cm_format_int(value);
}

char* cm_char_to_string(char value) {
    return cm_format_char(value);
}

char* cm_bool_to_string(char value) {
    return cm_format_bool(value);
}

// ============================================================
// Format Spec Extraction
// ============================================================
// プレースホルダーからフォーマット指定子を抽出
// 例: "{name:x}" -> 'x', "{value:b}" -> 'b', "{foo}" -> '\0'
static char extract_format_spec(const char* format, size_t start, size_t end) {
    // start は '{' の位置、end は '}' の位置
    for (size_t i = start + 1; i < end; i++) {
        if (format[i] == ':' && i + 1 < end) {
            return format[i + 1];  // ':' の次の文字を返す
        }
    }
    return '\0';  // フォーマット指定子なし
}

// ============================================================
// Format Replace Functions
// ============================================================
char* cm_format_replace(const char* format, const char* value) {
    if (!format) return 0;
    if (!value) value = "";

    size_t fmt_len = wasm_strlen(format);

    size_t start = 0;
    int found = 0;
    for (size_t i = 0; i < fmt_len; i++) {
        if (format[i] == '{') {
            start = i;
            found = 1;
            break;
        }
    }

    if (!found) {
        char* result = (char*)wasm_alloc(fmt_len + 1);
        for (size_t i = 0; i < fmt_len; i++) {
            result[i] = format[i];
        }
        result[fmt_len] = '\0';
        return result;
    }

    size_t end = start;
    for (size_t i = start + 1; i < fmt_len; i++) {
        if (format[i] == '}') {
            end = i;
            break;
        }
    }

    if (end == start) {
        char* result = (char*)wasm_alloc(fmt_len + 1);
        for (size_t i = 0; i < fmt_len; i++) {
            result[i] = format[i];
        }
        result[fmt_len] = '\0';
        return result;
    }

    size_t placeholder_len = end - start + 1;
    size_t val_len = wasm_strlen(value);
    size_t result_len = fmt_len - placeholder_len + val_len + 1;
    char* result = (char*)wasm_alloc(result_len);

    size_t result_idx = 0;
    for (size_t i = 0; i < start; i++) {
        result[result_idx++] = format[i];
    }

    for (size_t i = 0; i < val_len; i++) {
        result[result_idx++] = value[i];
    }

    for (size_t i = end + 1; i < fmt_len; i++) {
        result[result_idx++] = format[i];
    }
    result[result_idx] = '\0';

    return result;
}

char* cm_format_replace_int(const char* format, int value) {
    if (!format) return 0;

    size_t fmt_len = wasm_strlen(format);

    // プレースホルダーを探す
    size_t start = 0;
    int found = 0;
    for (size_t i = 0; i < fmt_len; i++) {
        if (format[i] == '{') {
            start = i;
            found = 1;
            break;
        }
    }

    if (!found) {
        char* result = (char*)wasm_alloc(fmt_len + 1);
        for (size_t i = 0; i < fmt_len; i++) {
            result[i] = format[i];
        }
        result[fmt_len] = '\0';
        return result;
    }

    size_t end = start;
    for (size_t i = start + 1; i < fmt_len; i++) {
        if (format[i] == '}') {
            end = i;
            break;
        }
    }

    // フォーマット指定子を抽出
    char spec = extract_format_spec(format, start, end);
    
    // アライメントとパディングを解析
    char align = '\0';
    char fill_char = ' ';
    int width = 0;
    
    if (end > start + 1 && format[start + 1] == ':') {
        size_t spec_start = start + 2;
        
        // {:0>5} のようなパターンを検出
        if (spec_start < end) {
            char c = format[spec_start];
            if (c == '<' || c == '>' || c == '^') {
                align = c;
                spec_start++;
            } else if (spec_start + 1 < end && 
                       (format[spec_start + 1] == '<' || 
                        format[spec_start + 1] == '>' || 
                        format[spec_start + 1] == '^')) {
                fill_char = c;
                align = format[spec_start + 1];
                spec_start += 2;
            }
        }
        
        // 幅を解析
        while (spec_start < end && format[spec_start] >= '0' && format[spec_start] <= '9') {
            width = width * 10 + (format[spec_start] - '0');
            spec_start++;
        }
    }
    
    // 指定子に応じた値の文字列化
    char* value_str;
    switch (spec) {
        case 'x':
            value_str = cm_format_int_hex((long long)value);  // 整数には0xプレフィックスなし
            break;
        case 'X':
            value_str = cm_format_int_HEX((long long)value);  // 整数には0xプレフィックスなし
            break;
        case 'b':
            value_str = cm_format_int_binary((long long)value);
            break;
        case 'o':
            value_str = cm_format_int_octal((long long)value);
            break;
        default:
            value_str = cm_format_int(value);
            break;
    }
    
    // パディングを適用
    size_t val_len = wasm_strlen(value_str);
    char* formatted_value;
    
    if (width > 0 && (size_t)width > val_len && align != '\0') {
        formatted_value = (char*)wasm_alloc(width + 1);
        size_t padding = width - val_len;
        
        if (align == '<') {
            for (size_t i = 0; i < val_len; i++) formatted_value[i] = value_str[i];
            for (size_t i = val_len; i < (size_t)width; i++) formatted_value[i] = fill_char;
        } else if (align == '>') {
            for (size_t i = 0; i < padding; i++) formatted_value[i] = fill_char;
            for (size_t i = 0; i < val_len; i++) formatted_value[padding + i] = value_str[i];
        } else if (align == '^') {
            size_t left_pad = padding / 2;
            for (size_t i = 0; i < left_pad; i++) formatted_value[i] = fill_char;
            for (size_t i = 0; i < val_len; i++) formatted_value[left_pad + i] = value_str[i];
            for (size_t i = left_pad + val_len; i < (size_t)width; i++) formatted_value[i] = fill_char;
        }
        formatted_value[width] = '\0';
    } else {
        formatted_value = value_str;
    }
    
    // 置換を実行
    size_t placeholder_len = end - start + 1;
    size_t formatted_len = wasm_strlen(formatted_value);
    size_t result_len = fmt_len - placeholder_len + formatted_len + 1;
    char* result = (char*)wasm_alloc(result_len);
    
    size_t result_idx = 0;
    for (size_t i = 0; i < start; i++) {
        result[result_idx++] = format[i];
    }
    for (size_t i = 0; i < formatted_len; i++) {
        result[result_idx++] = formatted_value[i];
    }
    for (size_t i = end + 1; i < fmt_len; i++) {
        result[result_idx++] = format[i];
    }
    result[result_idx] = '\0';
    
    return result;
}

char* cm_format_replace_uint(const char* format, unsigned int value) {
    if (!format) return 0;

    size_t fmt_len = wasm_strlen(format);

    // プレースホルダーを探す
    size_t start = 0;
    int found = 0;
    for (size_t i = 0; i < fmt_len; i++) {
        if (format[i] == '{') {
            start = i;
            found = 1;
            break;
        }
    }

    if (!found) {
        char* result = (char*)wasm_alloc(fmt_len + 1);
        for (size_t i = 0; i < fmt_len; i++) {
            result[i] = format[i];
        }
        result[fmt_len] = '\0';
        return result;
    }

    size_t end = start;
    for (size_t i = start + 1; i < fmt_len; i++) {
        if (format[i] == '}') {
            end = i;
            break;
        }
    }

    // フォーマット指定子を抽出
    char spec = extract_format_spec(format, start, end);
    
    // 指定子に応じた値の文字列化
    char* value_str;
    switch (spec) {
        case 'x':
            value_str = cm_format_int_hex((long long)value);  // 整数には0xプレフィックスなし
            break;
        case 'X':
            value_str = cm_format_int_HEX((long long)value);  // 整数には0xプレフィックスなし
            break;
        case 'b':
            value_str = cm_format_int_binary((long long)value);
            break;
        case 'o':
            value_str = cm_format_int_octal((long long)value);
            break;
        default:
            value_str = cm_format_uint(value);
            break;
    }
    
    return cm_format_replace(format, value_str);
}

// ポインタ専用のフォーマット関数（デフォルトで10進数表示）
char* cm_format_replace_ptr(const char* format, long long value) {
    if (!format) return 0;

    size_t fmt_len = wasm_strlen(format);

    // プレースホルダーを探す
    size_t start = 0;
    int found = 0;
    for (size_t i = 0; i < fmt_len; i++) {
        if (format[i] == '{') {
            start = i;
            found = 1;
            break;
        }
    }

    if (!found) {
        char* result = (char*)wasm_alloc(fmt_len + 1);
        for (size_t i = 0; i < fmt_len; i++) {
            result[i] = format[i];
        }
        result[fmt_len] = '\0';
        return result;
    }

    size_t end = start;
    for (size_t i = start + 1; i < fmt_len; i++) {
        if (format[i] == '}') {
            end = i;
            break;
        }
    }

    // フォーマット指定子を抽出
    char spec = extract_format_spec(format, start, end);

    // 指定子に応じた値の文字列化
    char* value_str;
    if (spec == 'x') {
        // 小文字16進数（明示的指定時は0xプレフィックス付き）
        char* hex_str = cm_format_int_hex(value);
        size_t hex_len = wasm_strlen(hex_str);
        value_str = (char*)wasm_alloc(hex_len + 3);  // "0x" + hex + '\0'
        value_str[0] = '0';
        value_str[1] = 'x';
        for (size_t i = 0; i <= hex_len; i++) {
            value_str[i + 2] = hex_str[i];
        }
    } else if (spec == 'X') {
        // 大文字16進数（明示的指定時は0xプレフィックス付き）
        char* hex_str = cm_format_int_HEX(value);
        size_t hex_len = wasm_strlen(hex_str);
        value_str = (char*)wasm_alloc(hex_len + 3);  // "0x" + hex + '\0'
        value_str[0] = '0';
        value_str[1] = 'x';
        for (size_t i = 0; i <= hex_len; i++) {
            value_str[i + 2] = hex_str[i];
        }
    } else if (spec == 'b') {
        value_str = cm_format_int_binary(value);
    } else if (spec == 'o') {
        value_str = cm_format_int_octal(value);
    } else {
        // デフォルトは10進数
        value_str = cm_format_long(value);
    }

    return cm_format_replace(format, value_str);
}

char* cm_format_replace_long(const char* format, long long value) {
    if (!format) return 0;

    size_t fmt_len = wasm_strlen(format);

    // プレースホルダーを探す
    size_t start = 0;
    int found = 0;
    for (size_t i = 0; i < fmt_len; i++) {
        if (format[i] == '{') {
            start = i;
            found = 1;
            break;
        }
    }

    if (!found) {
        char* result = (char*)wasm_alloc(fmt_len + 1);
        for (size_t i = 0; i < fmt_len; i++) {
            result[i] = format[i];
        }
        result[fmt_len] = '\0';
        return result;
    }

    size_t end = start;
    for (size_t i = start + 1; i < fmt_len; i++) {
        if (format[i] == '}') {
            end = i;
            break;
        }
    }

    // フォーマット指定子を抽出
    char spec = extract_format_spec(format, start, end);

    // 指定子に応じた値の文字列化
    char* value_str;
    switch (spec) {
        case 'x': {
            // 小文字16進数（明示的指定時は0xプレフィックス付き）
            char* hex_str = cm_format_int_hex(value);
            size_t hex_len = wasm_strlen(hex_str);
            value_str = (char*)wasm_alloc(hex_len + 3);  // "0x" + hex + '\0'
            value_str[0] = '0';
            value_str[1] = 'x';
            for (size_t i = 0; i <= hex_len; i++) {
                value_str[i + 2] = hex_str[i];
            }
            break;
        }
        case 'X': {
            // 大文字16進数（明示的指定時は0xプレフィックス付き）
            char* hex_str = cm_format_int_HEX(value);
            size_t hex_len = wasm_strlen(hex_str);
            value_str = (char*)wasm_alloc(hex_len + 3);  // "0x" + hex + '\0'
            value_str[0] = '0';
            value_str[1] = 'x';
            for (size_t i = 0; i <= hex_len; i++) {
                value_str[i + 2] = hex_str[i];
            }
            break;
        }
        case 'b':
            value_str = cm_format_int_binary(value);
            break;
        case 'o':
            value_str = cm_format_int_octal(value);
            break;
        default:
            value_str = cm_format_long(value);
            break;
    }

    return cm_format_replace(format, value_str);
}

char* cm_format_replace_ulong(const char* format, unsigned long long value) {
    if (!format) return 0;

    char* value_str = cm_format_ulong(value);
    return cm_format_replace(format, value_str);
}

char* cm_format_replace_double(const char* format, double value) {
    if (!format) return 0;

    size_t fmt_len = wasm_strlen(format);

    // プレースホルダーを探す
    size_t start = 0;
    int found = 0;
    for (size_t i = 0; i < fmt_len; i++) {
        if (format[i] == '{') {
            start = i;
            found = 1;
            break;
        }
    }

    if (!found) {
        char* result = (char*)wasm_alloc(fmt_len + 1);
        for (size_t i = 0; i < fmt_len; i++) {
            result[i] = format[i];
        }
        result[fmt_len] = '\0';
        return result;
    }

    size_t end = start;
    for (size_t i = start + 1; i < fmt_len; i++) {
        if (format[i] == '}') {
            end = i;
            break;
        }
    }

    // フォーマット指定子を抽出（精度も含む）
    // 例: ":e", ":E", ":.2" など
    char spec = '\0';
    int precision = -1;
    
    for (size_t i = start + 1; i < end; i++) {
        if (format[i] == ':') {
            // ':' の後を解析
            size_t j = i + 1;
            // 精度指定があるか確認 (例: .2)
            if (j < end && format[j] == '.') {
                j++;
                precision = 0;
                while (j < end && format[j] >= '0' && format[j] <= '9') {
                    precision = precision * 10 + (format[j] - '0');
                    j++;
                }
            }
            // 型指定子があるか確認
            if (j < end) {
                spec = format[j];
            }
            break;
        }
    }
    
    // 指定子に応じた値の文字列化
    char* value_str;
    switch (spec) {
        case 'e':
            value_str = cm_format_double_exp(value);
            break;
        case 'E':
            value_str = cm_format_double_EXP(value);
            break;
        default:
            if (precision >= 0) {
                value_str = cm_format_double_precision(value, precision);
            } else {
                value_str = cm_format_double(value);
            }
            break;
    }
    
    return cm_format_replace(format, value_str);
}

char* cm_format_replace_string(const char* format, const char* value) {
    if (!format) return 0;
    if (!value) value = "";
    
    size_t fmt_len = wasm_strlen(format);
    
    // プレースホルダーを探す
    size_t start = 0;
    int found = 0;
    for (size_t i = 0; i < fmt_len; i++) {
        if (format[i] == '{') {
            start = i;
            found = 1;
            break;
        }
    }
    
    if (!found) {
        return cm_format_replace(format, value);
    }
    
    size_t end = start;
    for (size_t i = start + 1; i < fmt_len; i++) {
        if (format[i] == '}') {
            end = i;
            break;
        }
    }
    
    // フォーマット指定子を解析 {:align width}
    size_t placeholder_len = end - start + 1;
    char align = '\0';
    int width = 0;
    char fill_char = ' ';
    
    // 指定子を解析
    if (end > start + 1 && format[start + 1] == ':') {
        size_t spec_start = start + 2;
        
        // アライメントとフィル文字をチェック
        if (spec_start < end) {
            // {:<width}, {:>width}, {:^width} または {:0>width}
            char c = format[spec_start];
            if (c == '<' || c == '>' || c == '^') {
                align = c;
                spec_start++;
            } else if (spec_start + 1 < end && 
                       (format[spec_start + 1] == '<' || 
                        format[spec_start + 1] == '>' || 
                        format[spec_start + 1] == '^')) {
                fill_char = c;
                align = format[spec_start + 1];
                spec_start += 2;
            }
        }
        
        // 幅を解析
        while (spec_start < end && format[spec_start] >= '0' && format[spec_start] <= '9') {
            width = width * 10 + (format[spec_start] - '0');
            spec_start++;
        }
    }
    
    // 幅指定がある場合
    size_t val_len = wasm_strlen(value);
    char* formatted_value;
    
    if (width > 0 && (size_t)width > val_len && align != '\0') {
        formatted_value = (char*)wasm_alloc(width + 1);
        size_t padding = width - val_len;
        
        if (align == '<') {
            // 左揃え
            for (size_t i = 0; i < val_len; i++) formatted_value[i] = value[i];
            for (size_t i = val_len; i < (size_t)width; i++) formatted_value[i] = fill_char;
        } else if (align == '>') {
            // 右揃え
            for (size_t i = 0; i < padding; i++) formatted_value[i] = fill_char;
            for (size_t i = 0; i < val_len; i++) formatted_value[padding + i] = value[i];
        } else if (align == '^') {
            // 中央揃え
            size_t left_pad = padding / 2;
            for (size_t i = 0; i < left_pad; i++) formatted_value[i] = fill_char;
            for (size_t i = 0; i < val_len; i++) formatted_value[left_pad + i] = value[i];
            for (size_t i = left_pad + val_len; i < (size_t)width; i++) formatted_value[i] = fill_char;
        }
        formatted_value[width] = '\0';
    } else {
        formatted_value = (char*)wasm_alloc(val_len + 1);
        for (size_t i = 0; i < val_len; i++) formatted_value[i] = value[i];
        formatted_value[val_len] = '\0';
    }
    
    // 置換を実行
    size_t formatted_len = wasm_strlen(formatted_value);
    size_t result_len = fmt_len - placeholder_len + formatted_len + 1;
    char* result = (char*)wasm_alloc(result_len);
    
    size_t result_idx = 0;
    for (size_t i = 0; i < start; i++) {
        result[result_idx++] = format[i];
    }
    for (size_t i = 0; i < formatted_len; i++) {
        result[result_idx++] = formatted_value[i];
    }
    for (size_t i = end + 1; i < fmt_len; i++) {
        result[result_idx++] = format[i];
    }
    result[result_idx] = '\0';
    
    return result;
}

// ============================================================
// Format String Functions
// ============================================================
char* cm_format_string_1(const char* fmt, const char* arg1) {
    return cm_format_replace(fmt, arg1);
}

char* cm_format_string_2(const char* fmt, const char* arg1, const char* arg2) {
    char* temp = cm_format_replace(fmt, arg1);
    return cm_format_replace(temp, arg2);
}

char* cm_format_string_3(const char* fmt, const char* arg1, const char* arg2, const char* arg3) {
    char* temp1 = cm_format_replace(fmt, arg1);
    char* temp2 = cm_format_replace(temp1, arg2);
    return cm_format_replace(temp2, arg3);
}

char* cm_format_string_4(const char* fmt, const char* arg1, const char* arg2, const char* arg3, const char* arg4) {
    char* temp1 = cm_format_replace(fmt, arg1);
    char* temp2 = cm_format_replace(temp1, arg2);
    char* temp3 = cm_format_replace(temp2, arg3);
    return cm_format_replace(temp3, arg4);
}

char* cm_format_string(const char* fmt, ...) {
    return (char*)fmt;  // Simplified
}

// ============================================================
// String Compare (Cm runtime functions)
// ============================================================
int cm_strcmp(const char* s1, const char* s2) {
    if (!s1 && !s2) return 0;
    if (!s1) return -1;
    if (!s2) return 1;
    
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return (unsigned char)*s1 - (unsigned char)*s2;
}

int cm_strncmp(const char* s1, const char* s2, size_t n) {
    if (!s1 && !s2) return 0;
    if (!s1) return -1;
    if (!s2) return 1;
    
    while (n > 0 && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return (unsigned char)*s1 - (unsigned char)*s2;
}

// libc compatibility alias
int strcmp(const char* s1, const char* s2) {
    return cm_strcmp(s1, s2);
}

// ============================================================
// Array Methods (WASM)
// ============================================================

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
_Bool __builtin_array_includes_i64(int64_t* arr, int64_t size, int64_t value) {
    return __builtin_array_indexOf_i64(arr, size, value) >= 0;
}

_Bool __builtin_array_includes_i32(int32_t* arr, int64_t size, int32_t value) {
    return __builtin_array_indexOf_i32(arr, size, value) >= 0;
}

// some: いずれかの要素が条件を満たすか
_Bool __builtin_array_some_i64(int64_t* arr, int64_t size, _Bool (*predicate)(int64_t)) {
    if (!arr || !predicate) return 0;
    for (int64_t i = 0; i < size; i++) {
        if (predicate(arr[i])) return 1;
    }
    return 0;
}

_Bool __builtin_array_some_i32(int32_t* arr, int64_t size, _Bool (*predicate)(int32_t)) {
    if (!arr || !predicate) return 0;
    for (int64_t i = 0; i < size; i++) {
        if (predicate(arr[i])) return 1;
    }
    return 0;
}

// every: すべての要素が条件を満たすか
_Bool __builtin_array_every_i64(int64_t* arr, int64_t size, _Bool (*predicate)(int64_t)) {
    if (!arr || !predicate) return 1;
    for (int64_t i = 0; i < size; i++) {
        if (!predicate(arr[i])) return 0;
    }
    return 1;
}

_Bool __builtin_array_every_i32(int32_t* arr, int64_t size, _Bool (*predicate)(int32_t)) {
    if (!arr || !predicate) return 1;
    for (int64_t i = 0; i < size; i++) {
        if (!predicate(arr[i])) return 0;
    }
    return 1;
}

// findIndex: 条件を満たす最初の要素のインデックス
int64_t __builtin_array_findIndex_i64(int64_t* arr, int64_t size, _Bool (*predicate)(int64_t)) {
    if (!arr || !predicate) return -1;
    for (int64_t i = 0; i < size; i++) {
        if (predicate(arr[i])) return i;
    }
    return -1;
}

int64_t __builtin_array_findIndex_i32(int32_t* arr, int64_t size, _Bool (*predicate)(int32_t)) {
    if (!arr || !predicate) return -1;
    for (int64_t i = 0; i < size; i++) {
        if (predicate(arr[i])) return i;
    }
    return -1;
}

// sortBy: カスタム比較関数でソートしたコピーを返す
void* __builtin_array_sortBy_i32(int32_t* arr, int64_t size, int (*comparator)(int32_t, int32_t)) {
    CmSlice* slice = (CmSlice*)wasm_alloc(sizeof(CmSlice));
    if (!slice) return NULL;
    
    if (!arr || size <= 0 || !comparator) {
        slice->data = NULL;
        slice->len = 0;
        return slice;
    }
    
    int32_t* result = (int32_t*)wasm_alloc(size * sizeof(int32_t));
    if (!result) {
        return NULL;
    }
    
    // コピー
    for (int64_t i = 0; i < size; i++) {
        result[i] = arr[i];
    }
    
    // ソート（バブルソート）
    for (int64_t i = 0; i < size - 1; i++) {
        for (int64_t j = i + 1; j < size; j++) {
            if (comparator(result[i], result[j]) > 0) {
                int32_t tmp = result[i];
                result[i] = result[j];
                result[j] = tmp;
            }
        }
    }
    
    slice->data = result;
    slice->len = size;
    return slice;
}

void* __builtin_array_sortBy_i64(int64_t* arr, int64_t size, int (*comparator)(int64_t, int64_t)) {
    CmSlice* slice = (CmSlice*)wasm_alloc(sizeof(CmSlice));
    if (!slice) return NULL;
    
    if (!arr || size <= 0 || !comparator) {
        slice->data = NULL;
        slice->len = 0;
        return slice;
    }
    
    int64_t* result = (int64_t*)wasm_alloc(size * sizeof(int64_t));
    if (!result) {
        return NULL;
    }
    
    // コピー
    for (int64_t i = 0; i < size; i++) {
        result[i] = arr[i];
    }
    
    // ソート（バブルソート）
    for (int64_t i = 0; i < size - 1; i++) {
        for (int64_t j = i + 1; j < size; j++) {
            if (comparator(result[i], result[j]) > 0) {
                int64_t tmp = result[i];
                result[i] = result[j];
                result[j] = tmp;
            }
        }
    }
    
    slice->data = result;
    slice->len = size;
    return slice;
}

void* __builtin_array_sortBy(int32_t* arr, int64_t size, int (*comparator)(int32_t, int32_t)) {
    return __builtin_array_sortBy_i32(arr, size, comparator);
}

// forEach: 各要素に関数を適用
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

// ============================================================
// Array first/last/find Functions
// ============================================================

// first: 配列の最初の要素を返す
int32_t __builtin_array_first_i32(int32_t* arr, int64_t size) {
    if (!arr || size <= 0) return 0;
    return arr[0];
}

int64_t __builtin_array_first_i64(int64_t* arr, int64_t size) {
    if (!arr || size <= 0) return 0;
    return arr[0];
}

// last: 配列の最後の要素を返す
int32_t __builtin_array_last_i32(int32_t* arr, int64_t size) {
    if (!arr || size <= 0) return 0;
    return arr[size - 1];
}

int64_t __builtin_array_last_i64(int64_t* arr, int64_t size) {
    if (!arr || size <= 0) return 0;
    return arr[size - 1];
}

// find: 条件に合う最初の要素を返す
int32_t __builtin_array_find_i32(int32_t* arr, int64_t size, _Bool (*predicate)(int32_t)) {
    if (!arr || !predicate) return 0;
    for (int64_t i = 0; i < size; i++) {
        if (predicate(arr[i])) return arr[i];
    }
    return 0;
}

int64_t __builtin_array_find_i64(int64_t* arr, int64_t size, _Bool (*predicate)(int64_t)) {
    if (!arr || !predicate) return 0;
    for (int64_t i = 0; i < size; i++) {
        if (predicate(arr[i])) return arr[i];
    }
    return 0;
}

// ============================================================
// Array reverse/sort Functions (returning CmSlice)
// ============================================================

typedef struct {
    void* data;
    int64_t len;
    int64_t cap;
    int64_t elem_size;
} CmSlice_wasm;

// reverse: 配列を逆順にしたコピーを返す
void* __builtin_array_reverse_i32(int32_t* arr, int64_t size) {
    CmSlice_wasm* slice = (CmSlice_wasm*)wasm_alloc(sizeof(CmSlice_wasm));
    if (!slice) return NULL;
    
    if (!arr || size <= 0) {
        slice->data = NULL;
        slice->len = 0;
        slice->cap = 0;
        slice->elem_size = sizeof(int32_t);
        return slice;
    }
    
    int32_t* result = (int32_t*)wasm_alloc(size * sizeof(int32_t));
    if (!result) {
        return NULL;
    }
    for (int64_t i = 0; i < size; i++) {
        result[i] = arr[size - 1 - i];
    }
    
    slice->data = result;
    slice->len = size;
    slice->cap = size;
    slice->elem_size = sizeof(int32_t);
    return slice;
}

void* __builtin_array_reverse_i64(int64_t* arr, int64_t size) {
    CmSlice_wasm* slice = (CmSlice_wasm*)wasm_alloc(sizeof(CmSlice_wasm));
    if (!slice) return NULL;
    
    if (!arr || size <= 0) {
        slice->data = NULL;
        slice->len = 0;
        slice->cap = 0;
        slice->elem_size = sizeof(int64_t);
        return slice;
    }
    
    int64_t* result = (int64_t*)wasm_alloc(size * sizeof(int64_t));
    if (!result) {
        return NULL;
    }
    for (int64_t i = 0; i < size; i++) {
        result[i] = arr[size - 1 - i];
    }
    
    slice->data = result;
    slice->len = size;
    slice->cap = size;
    slice->elem_size = sizeof(int64_t);
    return slice;
}

void* __builtin_array_reverse(int32_t* arr, int64_t size) {
    return __builtin_array_reverse_i32(arr, size);
}

// sort: 配列をソートしたコピーを返す（単純なバブルソート）
static void wasm_sort_i32(int32_t* arr, int64_t size) {
    for (int64_t i = 0; i < size - 1; i++) {
        for (int64_t j = 0; j < size - i - 1; j++) {
            if (arr[j] > arr[j + 1]) {
                int32_t temp = arr[j];
                arr[j] = arr[j + 1];
                arr[j + 1] = temp;
            }
        }
    }
}

static void wasm_sort_i64(int64_t* arr, int64_t size) {
    for (int64_t i = 0; i < size - 1; i++) {
        for (int64_t j = 0; j < size - i - 1; j++) {
            if (arr[j] > arr[j + 1]) {
                int64_t temp = arr[j];
                arr[j] = arr[j + 1];
                arr[j + 1] = temp;
            }
        }
    }
}

void* __builtin_array_sort_i32(int32_t* arr, int64_t size) {
    CmSlice_wasm* slice = (CmSlice_wasm*)wasm_alloc(sizeof(CmSlice_wasm));
    if (!slice) return NULL;
    
    if (!arr || size <= 0) {
        slice->data = NULL;
        slice->len = 0;
        slice->cap = 0;
        slice->elem_size = sizeof(int32_t);
        return slice;
    }
    
    int32_t* result = (int32_t*)wasm_alloc(size * sizeof(int32_t));
    if (!result) {
        return NULL;
    }
    // コピー
    for (int64_t i = 0; i < size; i++) {
        result[i] = arr[i];
    }
    wasm_sort_i32(result, size);
    
    slice->data = result;
    slice->len = size;
    slice->cap = size;
    slice->elem_size = sizeof(int32_t);
    return slice;
}

void* __builtin_array_sort_i64(int64_t* arr, int64_t size) {
    CmSlice_wasm* slice = (CmSlice_wasm*)wasm_alloc(sizeof(CmSlice_wasm));
    if (!slice) return NULL;
    
    if (!arr || size <= 0) {
        slice->data = NULL;
        slice->len = 0;
        slice->cap = 0;
        slice->elem_size = sizeof(int64_t);
        return slice;
    }
    
    int64_t* result = (int64_t*)wasm_alloc(size * sizeof(int64_t));
    if (!result) {
        return NULL;
    }
    // コピー
    for (int64_t i = 0; i < size; i++) {
        result[i] = arr[i];
    }
    wasm_sort_i64(result, size);
    
    slice->data = result;
    slice->len = size;
    slice->cap = size;
    slice->elem_size = sizeof(int64_t);
    return slice;
}

void* __builtin_array_sort(int32_t* arr, int64_t size) {
    return __builtin_array_sort_i32(arr, size);
}
