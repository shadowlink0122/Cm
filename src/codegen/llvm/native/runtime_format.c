// Cm Language Runtime - Format Functions (LLVM Backend)
// String formatting and conversion implementations

// Define this to use our optimized mem functions instead of platform.h's
#define CM_HAVE_OPTIMIZED_MEM

#include "../../common/runtime_alloc.h"
#include "../../common/runtime_platform.h"

#ifndef CM_NO_STD
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#endif

#include <stdbool.h>
#include <stdint.h>
#include "../../common/runtime_common.h"

// ============================================================
// Low-level String Functions (no_std compatible)
// These can be used without libc in freestanding environments
// ============================================================

// 自前のstrlen実装 (O(n))
static inline size_t cm_strlen_impl(const char* str) {
    if (!str) return 0;
    const char* p = str;
    while (*p) p++;
    return (size_t)(p - str);
}

// 自前のstrcpy実装
static inline char* cm_strcpy_impl(char* dst, const char* src) {
    if (!dst) return NULL;
    if (!src) { dst[0] = '\0'; return dst; }
    char* d = dst;
    while ((*d++ = *src++));
    return dst;
}

// 自前のstrncpy実装
static inline char* cm_strncpy_impl(char* dst, const char* src, size_t n) {
    if (!dst) return NULL;
    char* d = dst;
    if (src) {
        while (n > 0 && *src) {
            *d++ = *src++;
            n--;
        }
    }
    while (n > 0) {
        *d++ = '\0';
        n--;
    }
    return dst;
}

// 自前のstrcat実装
static inline char* cm_strcat_impl(char* dst, const char* src) {
    if (!dst) return NULL;
    char* d = dst;
    while (*d) d++;
    if (src) {
        while ((*d++ = *src++));
    }
    return dst;
}

// 自前のstrcmp実装 (文字列比較)
// 戻り値: 0 = 等しい, <0 = str1 < str2, >0 = str1 > str2
int cm_strcmp(const char* str1, const char* str2) {
    if (!str1 && !str2) return 0;
    if (!str1) return -1;
    if (!str2) return 1;
    
    while (*str1 && (*str1 == *str2)) {
        str1++;
        str2++;
    }
    return (unsigned char)*str1 - (unsigned char)*str2;
}

// 自前のstrncmp実装
int cm_strncmp(const char* str1, const char* str2, size_t n) {
    if (!str1 && !str2) return 0;
    if (!str1) return -1;
    if (!str2) return 1;
    
    while (n > 0 && *str1 && (*str1 == *str2)) {
        str1++;
        str2++;
        n--;
    }
    if (n == 0) return 0;
    return (unsigned char)*str1 - (unsigned char)*str2;
}

// 自前のatoi実装
static inline int cm_atoi_impl(const char* str) {
    if (!str) return 0;
    int result = 0;
    int sign = 1;
    
    // 空白スキップ
    while (*str == ' ' || *str == '\t') str++;
    
    // 符号
    if (*str == '-') { sign = -1; str++; }
    else if (*str == '+') { str++; }
    
    // 数字
    while (*str >= '0' && *str <= '9') {
        result = result * 10 + (*str - '0');
        str++;
    }
    
    return sign * result;
}

// 自前のmemcpy実装 (最適化: 8バイトアラインメント)
void* cm_memcpy(void* dest, const void* src, size_t n) {
    if (!dest || !src) return dest;
    
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    
    // 8バイト単位でコピー（アラインメントが合っている場合）
    if (((uintptr_t)d & 7) == 0 && ((uintptr_t)s & 7) == 0) {
        while (n >= 8) {
            *(uint64_t*)d = *(const uint64_t*)s;
            d += 8;
            s += 8;
            n -= 8;
        }
    }
    
    // 残りをバイト単位でコピー
    while (n > 0) {
        *d++ = *s++;
        n--;
    }
    
    return dest;
}

// 自前のmemset実装
void* cm_memset(void* dest, int c, size_t n) {
    if (!dest) return dest;
    
    unsigned char* d = (unsigned char*)dest;
    unsigned char val = (unsigned char)c;
    
    // 8バイト単位で埋める
    if (n >= 8 && ((uintptr_t)d & 7) == 0) {
        uint64_t val64 = val;
        val64 |= val64 << 8;
        val64 |= val64 << 16;
        val64 |= val64 << 32;
        
        while (n >= 8) {
            *(uint64_t*)d = val64;
            d += 8;
            n -= 8;
        }
    }
    
    while (n > 0) {
        *d++ = val;
        n--;
    }
    
    return dest;
}

// 自前のmemmove実装（オーバーラップ対応）
void* cm_memmove(void* dest, const void* src, size_t n) {
    if (!dest || !src || n == 0) return dest;
    
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    
    if (d < s || d >= s + n) {
        // 前方コピー（オーバーラップなし、またはdestがsrcより前）
        return cm_memcpy(dest, src, n);
    } else {
        // 後方コピー（オーバーラップあり、destがsrcより後）
        d += n;
        s += n;
        while (n > 0) {
            *--d = *--s;
            n--;
        }
    }
    
    return dest;
}

// 自前のstrchr実装
char* cm_strchr(const char* str, int c) {
    if (!str) return NULL;
    
    while (*str) {
        if (*str == (char)c) return (char*)str;
        str++;
    }
    return (c == '\0') ? (char*)str : NULL;
}

// 自前のstrstr実装 (Boyer-Moore-Horspoolアルゴリズムの簡易版)
char* cm_strstr(const char* haystack, const char* needle) {
    if (!haystack || !needle) return NULL;
    if (*needle == '\0') return (char*)haystack;
    
    size_t needle_len = cm_strlen_impl(needle);
    size_t haystack_len = cm_strlen_impl(haystack);
    
    if (needle_len > haystack_len) return NULL;
    
    // 単純な検索（短い文字列用）
    for (size_t i = 0; i <= haystack_len - needle_len; i++) {
        size_t j;
        for (j = 0; j < needle_len; j++) {
            if (haystack[i + j] != needle[j]) break;
        }
        if (j == needle_len) return (char*)(haystack + i);
    }
    
    return NULL;
}

// ============================================================
// Number to String Conversion (no_std compatible)
// ============================================================

// 整数を文字列に変換（バッファに書き込み、書き込んだ文字数を返す）
static inline int cm_itoa_buf(int value, char* buf, size_t bufsize) {
    if (!buf || bufsize == 0) return 0;
    
    char temp[16];
    int i = 0;
    bool negative = false;
    
    if (value < 0) {
        negative = true;
        // INT_MINの特別処理
        if (value == (-2147483647 - 1)) {
            const char* min_str = "-2147483648";
            size_t len = 11;
            if (len >= bufsize) len = bufsize - 1;
            for (size_t j = 0; j < len; j++) buf[j] = min_str[j];
            buf[len] = '\0';
            return (int)len;
        }
        value = -value;
    }
    
    // 逆順で数字を格納
    do {
        temp[i++] = '0' + (value % 10);
        value /= 10;
    } while (value > 0 && i < 15);
    
    // 結果をバッファに書き込み
    int pos = 0;
    if (negative && pos < (int)bufsize - 1) {
        buf[pos++] = '-';
    }
    while (i > 0 && pos < (int)bufsize - 1) {
        buf[pos++] = temp[--i];
    }
    buf[pos] = '\0';
    return pos;
}

// unsigned intを文字列に変換
static inline int cm_utoa_buf(unsigned int value, char* buf, size_t bufsize) {
    if (!buf || bufsize == 0) return 0;
    
    char temp[16];
    int i = 0;
    
    do {
        temp[i++] = '0' + (value % 10);
        value /= 10;
    } while (value > 0 && i < 15);
    
    int pos = 0;
    while (i > 0 && pos < (int)bufsize - 1) {
        buf[pos++] = temp[--i];
    }
    buf[pos] = '\0';
    return pos;
}

// long longを文字列に変換
static inline int cm_ltoa_buf(long long value, char* buf, size_t bufsize) {
    if (!buf || bufsize == 0) return 0;
    
    char temp[24];
    int i = 0;
    bool negative = false;
    
    if (value < 0) {
        negative = true;
        // LLONG_MINの特別処理
        if (value == (-9223372036854775807LL - 1)) {
            const char* min_str = "-9223372036854775808";
            size_t len = 20;
            if (len >= bufsize) len = bufsize - 1;
            for (size_t j = 0; j < len; j++) buf[j] = min_str[j];
            buf[len] = '\0';
            return (int)len;
        }
        value = -value;
    }
    
    do {
        temp[i++] = '0' + (value % 10);
        value /= 10;
    } while (value > 0 && i < 23);
    
    int pos = 0;
    if (negative && pos < (int)bufsize - 1) {
        buf[pos++] = '-';
    }
    while (i > 0 && pos < (int)bufsize - 1) {
        buf[pos++] = temp[--i];
    }
    buf[pos] = '\0';
    return pos;
}

// unsigned long longを文字列に変換
static inline int cm_ultoa_buf(unsigned long long value, char* buf, size_t bufsize) {
    if (!buf || bufsize == 0) return 0;
    
    char temp[24];
    int i = 0;
    
    do {
        temp[i++] = '0' + (value % 10);
        value /= 10;
    } while (value > 0 && i < 23);
    
    int pos = 0;
    while (i > 0 && pos < (int)bufsize - 1) {
        buf[pos++] = temp[--i];
    }
    buf[pos] = '\0';
    return pos;
}

// 16進数変換（小文字）
static inline int cm_itoa_hex_buf(unsigned long long value, char* buf, size_t bufsize, bool uppercase) {
    if (!buf || bufsize == 0) return 0;
    
    const char* digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    char temp[20];
    int i = 0;
    
    if (value == 0) {
        if (bufsize > 1) { buf[0] = '0'; buf[1] = '\0'; return 1; }
        buf[0] = '\0';
        return 0;
    }
    
    while (value > 0 && i < 19) {
        temp[i++] = digits[value & 0xF];
        value >>= 4;
    }
    
    int pos = 0;
    while (i > 0 && pos < (int)bufsize - 1) {
        buf[pos++] = temp[--i];
    }
    buf[pos] = '\0';
    return pos;
}

// 8進数変換
static inline int cm_itoa_oct_buf(unsigned long long value, char* buf, size_t bufsize) {
    if (!buf || bufsize == 0) return 0;
    
    char temp[24];
    int i = 0;
    
    if (value == 0) {
        if (bufsize > 1) { buf[0] = '0'; buf[1] = '\0'; return 1; }
        buf[0] = '\0';
        return 0;
    }
    
    while (value > 0 && i < 23) {
        temp[i++] = '0' + (value & 7);
        value >>= 3;
    }
    
    int pos = 0;
    while (i > 0 && pos < (int)bufsize - 1) {
        buf[pos++] = temp[--i];
    }
    buf[pos] = '\0';
    return pos;
}

// 10のべき乗を計算
static inline double cm_pow10(int n) {
    double result = 1.0;
    if (n > 0) {
        for (int i = 0; i < n; i++) result *= 10.0;
    } else {
        for (int i = 0; i < -n; i++) result /= 10.0;
    }
    return result;
}

// 簡易double→文字列変換（丸め処理付き、%gスタイル）
static inline int cm_dtoa_buf(double value, char* buf, size_t bufsize, int precision) {
    if (!buf || bufsize == 0) return 0;
    
    int pos = 0;
    
    // NaN/Inf処理
    if (value != value) {  // NaN
        const char* s = "nan";
        while (*s && pos < (int)bufsize - 1) buf[pos++] = *s++;
        buf[pos] = '\0';
        return pos;
    }
    
    // 負の数
    bool negative = false;
    if (value < 0) {
        negative = true;
        value = -value;
    }
    
    // 無限大
    if (value > 1e308) {
        if (negative && pos < (int)bufsize - 1) buf[pos++] = '-';
        const char* s = "inf";
        while (*s && pos < (int)bufsize - 1) buf[pos++] = *s++;
        buf[pos] = '\0';
        return pos;
    }
    
    // 符号を出力
    if (negative && pos < (int)bufsize - 1) {
        buf[pos++] = '-';
    }
    
    // 0の特別処理
    if (value < 1e-15) {
        buf[pos++] = '0';
        if (precision > 0) {
            buf[pos++] = '.';
            for (int i = 0; i < precision && pos < (int)bufsize - 1; i++) {
                buf[pos++] = '0';
            }
        }
        buf[pos] = '\0';
        return pos;
    }
    
    int frac_precision;
    
    if (precision > 0) {
        // precision > 0: 小数点以下N桁を明示指定
        frac_precision = precision;
        
        // 丸めを適用
        double rounding = 0.5 * cm_pow10(-frac_precision);
        value += rounding;
    } else {
        // precision < 0: %gスタイル（有効数字6桁）
        int significant_digits = 6;
        
        // 整数部の桁数を計算
        int int_digits = 0;
        double temp = value;
        while (temp >= 1.0) {
            temp /= 10.0;
            int_digits++;
        }
        if (int_digits == 0) int_digits = 1;
        
        // 小数点以下の精度を計算
        frac_precision = significant_digits - int_digits;
        if (frac_precision < 0) frac_precision = 0;
        
        // 丸めを適用
        if (frac_precision > 0) {
            double rounding = 0.5 * cm_pow10(-frac_precision);
            value += rounding;
        } else {
            double scale = cm_pow10(int_digits - significant_digits);
            value = (long long)(value / scale + 0.5) * scale;
        }
    }
    
    // 整数部
    long long int_part = (long long)value;
    double frac_part = value - (double)int_part;
    
    // 整数部を変換
    char int_buf[24];
    int int_len = cm_ltoa_buf(int_part, int_buf, sizeof(int_buf));
    for (int i = 0; i < int_len && pos < (int)bufsize - 1; i++) {
        buf[pos++] = int_buf[i];
    }
    
    // 小数部
    if (frac_precision > 0) {
        // precision > 0 の場合は必ず小数点を出力
        // precision < 0 の場合は小数部がある場合のみ出力
        bool has_frac = (precision > 0) || (frac_part >= 1e-10);
        
        if (has_frac) {
            if (pos < (int)bufsize - 1) buf[pos++] = '.';
            
            char frac_buf[20];
            int frac_pos = 0;
            int last_nonzero = -1;
            
            for (int i = 0; i < frac_precision && frac_pos < 19; i++) {
                frac_part *= 10;
                int digit = (int)(frac_part + 1e-9);  // 微小誤差補正
                if (digit > 9) digit = 9;
                frac_buf[frac_pos++] = '0' + digit;
                if (digit != 0) last_nonzero = frac_pos;
                frac_part -= digit;
            }
            
            // precision指定時は末尾0を保持、そうでなければ除去
            int frac_len;
            if (precision > 0) {
                frac_len = frac_pos;
            } else {
                frac_len = (last_nonzero > 0) ? last_nonzero : 0;
            }
            
            // 末尾が0のみなら小数点を削除
            if (frac_len == 0 && pos > 0 && buf[pos-1] == '.') {
                pos--;
            } else {
                for (int i = 0; i < frac_len && pos < (int)bufsize - 1; i++) {
                    buf[pos++] = frac_buf[i];
                }
            }
        }
    }
    
    buf[pos] = '\0';
    return pos;
}

// 指数表記変換
static inline int cm_dtoa_exp_buf(double value, char* buf, size_t bufsize, bool uppercase) {
    if (!buf || bufsize == 0) return 0;
    
    int pos = 0;
    int exponent = 0;
    
    // 負の数
    if (value < 0) {
        if (pos < (int)bufsize - 1) buf[pos++] = '-';
        value = -value;
    }
    
    // 0の場合
    if (value == 0) {
        const char* s = uppercase ? "0E+00" : "0e+00";
        while (*s && pos < (int)bufsize - 1) buf[pos++] = *s++;
        buf[pos] = '\0';
        return pos;
    }
    
    // 指数を計算して正規化
    while (value >= 10.0) { value /= 10.0; exponent++; }
    while (value < 1.0 && value > 0) { value *= 10.0; exponent--; }
    
    // 仮数部（6桁）
    pos += cm_dtoa_buf(value, buf + pos, bufsize - pos, 6);
    
    // 指数部
    if (pos < (int)bufsize - 1) buf[pos++] = uppercase ? 'E' : 'e';
    if (pos < (int)bufsize - 1) buf[pos++] = (exponent >= 0) ? '+' : '-';
    if (exponent < 0) exponent = -exponent;
    
    // 指数を2桁で出力
    if (exponent < 10) {
        if (pos < (int)bufsize - 1) buf[pos++] = '0';
    }
    char exp_buf[8];
    int exp_len = cm_itoa_buf(exponent, exp_buf, sizeof(exp_buf));
    for (int i = 0; i < exp_len && pos < (int)bufsize - 1; i++) {
        buf[pos++] = exp_buf[i];
    }
    
    buf[pos] = '\0';
    return pos;
}

// ============================================================
// QuickSort Implementation (no_std compatible)
// ============================================================

static void cm_swap(void* a, void* b, size_t size) {
    unsigned char* pa = (unsigned char*)a;
    unsigned char* pb = (unsigned char*)b;
    for (size_t i = 0; i < size; i++) {
        unsigned char tmp = pa[i];
        pa[i] = pb[i];
        pb[i] = tmp;
    }
}

static void cm_qsort_impl(void* base, size_t nmemb, size_t size, 
                          int (*compar)(const void*, const void*)) {
    if (nmemb <= 1) return;
    
    unsigned char* arr = (unsigned char*)base;
    
    // 挿入ソート（小さい配列用）
    if (nmemb <= 16) {
        for (size_t i = 1; i < nmemb; i++) {
            size_t j = i;
            while (j > 0 && compar(arr + (j-1)*size, arr + j*size) > 0) {
                cm_swap(arr + (j-1)*size, arr + j*size, size);
                j--;
            }
        }
        return;
    }
    
    // クイックソート
    // ピボットは中央値を選択
    size_t mid = nmemb / 2;
    if (compar(arr, arr + mid*size) > 0) cm_swap(arr, arr + mid*size, size);
    if (compar(arr, arr + (nmemb-1)*size) > 0) cm_swap(arr, arr + (nmemb-1)*size, size);
    if (compar(arr + mid*size, arr + (nmemb-1)*size) > 0) cm_swap(arr + mid*size, arr + (nmemb-1)*size, size);
    
    // ピボットを末尾-1に移動
    cm_swap(arr + mid*size, arr + (nmemb-2)*size, size);
    void* pivot = arr + (nmemb-2)*size;
    
    size_t i = 0;
    size_t j = nmemb - 2;
    
    while (1) {
        while (compar(arr + (++i)*size, pivot) < 0);
        while (j > 0 && compar(arr + (--j)*size, pivot) > 0);
        if (i >= j) break;
        cm_swap(arr + i*size, arr + j*size, size);
    }
    
    // ピボットを正しい位置に
    cm_swap(arr + i*size, arr + (nmemb-2)*size, size);
    
    // 再帰
    cm_qsort_impl(arr, i, size, compar);
    cm_qsort_impl(arr + (i+1)*size, nmemb - i - 1, size, compar);
}

// 公開qsort関数
void cm_qsort(void* base, size_t nmemb, size_t size,
              int (*compar)(const void*, const void*)) {
    if (!base || nmemb <= 1 || size == 0 || !compar) return;
    cm_qsort_impl(base, nmemb, size, compar);
}

// ============================================================
// String Builtin Functions
// ============================================================
size_t __builtin_string_len(const char* str) {
    return str ? cm_strlen_impl(str) : 0;
}

char __builtin_string_charAt(const char* str, int64_t index) {
    if (!str || index < 0) return '\0';
    size_t len = cm_strlen_impl(str);
    if ((size_t)index >= len) return '\0';
    return str[index];
}

char __builtin_string_first(const char* str) {
    if (!str || str[0] == '\0') return '\0';
    return str[0];
}

char __builtin_string_last(const char* str) {
    if (!str || str[0] == '\0') return '\0';
    size_t len = cm_strlen_impl(str);
    return str[len - 1];
}

char* __builtin_string_substring(const char* str, int64_t start, int64_t end) {
    if (!str) return NULL;
    size_t len = cm_strlen_impl(str);
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
        char* empty = (char*)cm_alloc(1);
        if (empty) empty[0] = '\0';
        return empty;
    }
    size_t result_len = (size_t)(end - start);
    char* result = (char*)cm_alloc(result_len + 1);
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
    size_t len = cm_strlen_impl(str);
    char* result = (char*)cm_alloc(len + 1);
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
    size_t len = cm_strlen_impl(str);
    char* result = (char*)cm_alloc(len + 1);
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
    size_t len = cm_strlen_impl(str);
    size_t start = 0, end = len;
    while (start < len && (str[start] == ' ' || str[start] == '\t' || 
           str[start] == '\n' || str[start] == '\r')) start++;
    while (end > start && (str[end-1] == ' ' || str[end-1] == '\t' || 
           str[end-1] == '\n' || str[end-1] == '\r')) end--;
    size_t result_len = end - start;
    char* result = (char*)cm_alloc(result_len + 1);
    if (!result) return NULL;
    memcpy(result, str + start, result_len);
    result[result_len] = '\0';
    return result;
}

bool __builtin_string_startsWith(const char* str, const char* prefix) {
    if (!str || !prefix) return false;
    size_t str_len = cm_strlen_impl(str);
    size_t prefix_len = strlen(prefix);
    if (prefix_len > str_len) return false;
    return strncmp(str, prefix, prefix_len) == 0;
}

bool __builtin_string_endsWith(const char* str, const char* suffix) {
    if (!str || !suffix) return false;
    size_t str_len = cm_strlen_impl(str);
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
        char* empty = (char*)cm_alloc(1);
        if (empty) empty[0] = '\0';
        return empty;
    }
    size_t len = cm_strlen_impl(str);
    size_t total_len = len * (size_t)count;
    char* result = (char*)cm_alloc(total_len + 1);
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
        char* copy = (char*)cm_alloc(cm_strlen_impl(str) + 1);
        if (copy) strcpy(copy, str);
        return copy;
    }
    const char* pos = strstr(str, from);
    if (!pos) {
        char* copy = (char*)cm_alloc(cm_strlen_impl(str) + 1);
        if (copy) strcpy(copy, str);
        return copy;
    }
    size_t str_len = cm_strlen_impl(str);
    size_t from_len = strlen(from);
    size_t to_len = strlen(to);
    size_t result_len = str_len - from_len + to_len;
    char* result = (char*)cm_alloc(result_len + 1);
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
    // スライス構造体のインポート（runtime_slice.cと同じ形式）
    typedef struct {
        void* data;
        int64_t len;
        int64_t cap;
        int64_t elem_size;
    } CmSlice;
    
    if (!arr || elem_size <= 0 || arr_len <= 0) {
        if (out_len) *out_len = 0;
        // 空のスライスを返す
        CmSlice* slice = (CmSlice*)cm_alloc(sizeof(CmSlice));
        if (slice) {
            slice->data = NULL;
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
        // 空のスライスを返す
        CmSlice* slice = (CmSlice*)cm_alloc(sizeof(CmSlice));
        if (slice) {
            slice->data = NULL;
            slice->len = 0;
            slice->cap = 0;
            slice->elem_size = elem_size;
        }
        return slice;
    }
    
    int64_t slice_len = end - start;
    
    // CmSlice構造体を作成
    CmSlice* slice = (CmSlice*)cm_alloc(sizeof(CmSlice));
    if (!slice) {
        if (out_len) *out_len = 0;
        return NULL;
    }
    
    slice->data = cm_alloc((size_t)(slice_len * elem_size));
    if (!slice->data) {
        cm_dealloc(slice);
        if (out_len) *out_len = 0;
        return NULL;
    }
    
    memcpy(slice->data, (char*)arr + (start * elem_size), (size_t)(slice_len * elem_size));
    slice->len = slice_len;
    slice->cap = slice_len;
    slice->elem_size = elem_size;
    
    if (out_len) *out_len = slice_len;
    return slice;
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

// find: 条件に合う最初の要素を返す（見つからなければデフォルト値）
int32_t __builtin_array_find_i32(int32_t* arr, int64_t size, bool (*predicate)(int32_t)) {
    if (!arr || !predicate) return 0;
    for (int64_t i = 0; i < size; i++) {
        if (predicate(arr[i])) return arr[i];
    }
    return 0;
}

int64_t __builtin_array_find_i64(int64_t* arr, int64_t size, bool (*predicate)(int64_t)) {
    if (!arr || !predicate) return 0;
    for (int64_t i = 0; i < size; i++) {
        if (predicate(arr[i])) return arr[i];
    }
    return 0;
}

// reverse: 配列を逆順にしたコピーを返す（CmSlice*を返す）
typedef struct {
    void* data;
    int64_t len;
    int64_t cap;
    int64_t elem_size;
} CmSlice_fmt;

// sortBy: カスタム比較関数でソートしたコピーを返す
void* __builtin_array_sortBy_i32(int32_t* arr, int64_t size, int (*comparator)(int32_t, int32_t)) {
    CmSlice_fmt* slice = (CmSlice_fmt*)cm_alloc(sizeof(CmSlice_fmt));
    if (!slice) return NULL;
    
    if (!arr || size <= 0 || !comparator) {
        slice->data = NULL;
        slice->len = 0;
        slice->cap = 0;
        slice->elem_size = sizeof(int32_t);
        return slice;
    }
    
    int32_t* result = (int32_t*)cm_alloc(size * sizeof(int32_t));
    if (!result) {
        cm_dealloc(slice);
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
    slice->cap = size;
    slice->elem_size = sizeof(int32_t);
    return slice;
}

void* __builtin_array_sortBy_i64(int64_t* arr, int64_t size, int (*comparator)(int64_t, int64_t)) {
    CmSlice_fmt* slice = (CmSlice_fmt*)cm_alloc(sizeof(CmSlice_fmt));
    if (!slice) return NULL;
    
    if (!arr || size <= 0 || !comparator) {
        slice->data = NULL;
        slice->len = 0;
        slice->cap = 0;
        slice->elem_size = sizeof(int64_t);
        return slice;
    }
    
    int64_t* result = (int64_t*)cm_alloc(size * sizeof(int64_t));
    if (!result) {
        cm_dealloc(slice);
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
    slice->cap = size;
    slice->elem_size = sizeof(int64_t);
    return slice;
}

// 汎用sortBy（エイリアス）
void* __builtin_array_sortBy(int32_t* arr, int64_t size, int (*comparator)(int32_t, int32_t)) {
    return __builtin_array_sortBy_i32(arr, size, comparator);
}

void* __builtin_array_reverse_i32(int32_t* arr, int64_t size) {
    CmSlice_fmt* slice = (CmSlice_fmt*)cm_alloc(sizeof(CmSlice_fmt));
    if (!slice) return NULL;
    
    if (!arr || size <= 0) {
        slice->data = NULL;
        slice->len = 0;
        slice->cap = 0;
        slice->elem_size = sizeof(int32_t);
        return slice;
    }
    
    int32_t* result = (int32_t*)cm_alloc(size * sizeof(int32_t));
    if (!result) {
        cm_dealloc(slice);
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
    CmSlice_fmt* slice = (CmSlice_fmt*)cm_alloc(sizeof(CmSlice_fmt));
    if (!slice) return NULL;
    
    if (!arr || size <= 0) {
        slice->data = NULL;
        slice->len = 0;
        slice->cap = 0;
        slice->elem_size = sizeof(int64_t);
        return slice;
    }
    
    int64_t* result = (int64_t*)cm_alloc(size * sizeof(int64_t));
    if (!result) {
        cm_dealloc(slice);
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

// デフォルト版（i32と同じ）
void* __builtin_array_reverse(int32_t* arr, int64_t size) {
    return __builtin_array_reverse_i32(arr, size);
}

// sort: 配列をソートしたコピーを返す（CmSlice*を返す）
static int compare_i32(const void* a, const void* b) {
    int32_t ia = *(const int32_t*)a;
    int32_t ib = *(const int32_t*)b;
    return (ia > ib) - (ia < ib);
}

static int compare_i64(const void* a, const void* b) {
    int64_t ia = *(const int64_t*)a;
    int64_t ib = *(const int64_t*)b;
    return (ia > ib) - (ia < ib);
}

void* __builtin_array_sort_i32(int32_t* arr, int64_t size) {
    CmSlice_fmt* slice = (CmSlice_fmt*)cm_alloc(sizeof(CmSlice_fmt));
    if (!slice) return NULL;
    
    if (!arr || size <= 0) {
        slice->data = NULL;
        slice->len = 0;
        slice->cap = 0;
        slice->elem_size = sizeof(int32_t);
        return slice;
    }
    
    int32_t* result = (int32_t*)cm_alloc(size * sizeof(int32_t));
    if (!result) {
        cm_dealloc(slice);
        return NULL;
    }
    memcpy(result, arr, size * sizeof(int32_t));
    qsort(result, size, sizeof(int32_t), compare_i32);
    
    slice->data = result;
    slice->len = size;
    slice->cap = size;
    slice->elem_size = sizeof(int32_t);
    return slice;
}

void* __builtin_array_sort_i64(int64_t* arr, int64_t size) {
    CmSlice_fmt* slice = (CmSlice_fmt*)cm_alloc(sizeof(CmSlice_fmt));
    if (!slice) return NULL;
    
    if (!arr || size <= 0) {
        slice->data = NULL;
        slice->len = 0;
        slice->cap = 0;
        slice->elem_size = sizeof(int64_t);
        return slice;
    }
    
    int64_t* result = (int64_t*)cm_alloc(size * sizeof(int64_t));
    if (!result) {
        cm_dealloc(slice);
        return NULL;
    }
    memcpy(result, arr, size * sizeof(int64_t));
    qsort(result, size, sizeof(int64_t), compare_i64);
    
    slice->data = result;
    slice->len = size;
    slice->cap = size;
    slice->elem_size = sizeof(int64_t);
    return slice;
}

// デフォルト版（i32と同じ）
void* __builtin_array_sort(int32_t* arr, int64_t size) {
    return __builtin_array_sort_i32(arr, size);
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
    
    size_t len = cm_strlen_impl(str);
    char* result = (char*)cm_alloc(len + 1);
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
// Type to String Conversion (no_std compatible)
// ============================================================
char* cm_format_int(int value) {
    char* buffer = (char*)cm_alloc(32);
    if (buffer) {
        cm_itoa_buf(value, buffer, 32);
    }
    return buffer;
}

char* cm_format_uint(unsigned int value) {
    char* buffer = (char*)cm_alloc(32);
    if (buffer) {
        cm_utoa_buf(value, buffer, 32);
    }
    return buffer;
}

char* cm_format_long(long long value) {
    char* buffer = (char*)cm_alloc(32);
    if (buffer) {
        cm_ltoa_buf(value, buffer, 32);
    }
    return buffer;
}

char* cm_format_ulong(unsigned long long value) {
    char* buffer = (char*)cm_alloc(32);
    if (buffer) {
        cm_ultoa_buf(value, buffer, 32);
    }
    return buffer;
}

char* cm_format_double(double value) {
    char* buffer = (char*)cm_alloc(64);
    if (buffer) {
        if (value == (long long)value && value > -1e15 && value < 1e15) {
            cm_ltoa_buf((long long)value, buffer, 64);
        } else {
            cm_dtoa_buf(value, buffer, 64, -1);  // デフォルト精度
        }
    }
    return buffer;
}

char* cm_format_double_precision(double value, int precision) {
    char* buffer = (char*)cm_alloc(64);
    if (buffer) {
        cm_dtoa_buf(value, buffer, 64, precision);
    }
    return buffer;
}

char* cm_format_bool(char value) {
    if (value) {
        char* buffer = (char*)cm_alloc(5);
        if (buffer) cm_strcpy_impl(buffer, "true");
        return buffer;
    } else {
        char* buffer = (char*)cm_alloc(6);
        if (buffer) cm_strcpy_impl(buffer, "false");
        return buffer;
    }
}

char* cm_format_char(char value) {
    char* buffer = (char*)cm_alloc(2);
    if (buffer) {
        buffer[0] = value;
        buffer[1] = '\0';
    }
    return buffer;
}

// ============================================================
// Integer Format Variants (no_std compatible)
// ============================================================
char* cm_format_int_hex(long long value) {
    char* buffer = (char*)cm_alloc(32);
    if (buffer) {
        cm_itoa_hex_buf((unsigned long long)value, buffer, 32, false);
    }
    return buffer;
}

char* cm_format_int_HEX(long long value) {
    char* buffer = (char*)cm_alloc(32);
    if (buffer) {
        cm_itoa_hex_buf((unsigned long long)value, buffer, 32, true);
    }
    return buffer;
}

char* cm_format_int_binary(long long value) {
    char* buffer = (char*)cm_alloc(65);
    if (!buffer) return NULL;

    if (value == 0) {
        cm_strcpy_impl(buffer, "0");
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
    char* buffer = (char*)cm_alloc(32);
    if (buffer) {
        cm_itoa_oct_buf((unsigned long long)value, buffer, 32);
    }
    return buffer;
}

// ============================================================
// Double Format Variants (no_std compatible)
// ============================================================
char* cm_format_double_exp(double value) {
    char* buffer = (char*)cm_alloc(64);
    if (buffer) {
        cm_dtoa_exp_buf(value, buffer, 64, false);
    }
    return buffer;
}

char* cm_format_double_EXP(double value) {
    char* buffer = (char*)cm_alloc(64);
    if (buffer) {
        cm_dtoa_exp_buf(value, buffer, 64, true);
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
    char* result = (char*)cm_alloc(len1 + len2 + 1);

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
    char* buffer = (char*)cm_alloc(64);
    if (buffer) {
        if (value == (long long)value && value > -1e15 && value < 1e15) {
            cm_ltoa_buf((long long)value, buffer, 64);
        } else {
            cm_dtoa_buf(value, buffer, 64, -1);
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

    const char* start = cm_strchr(format, '{');
    if (!start) {
        char* result = (char*)cm_alloc(cm_strlen_impl(format) + 1);
        if (result) cm_strcpy_impl(result, format);
        return result;
    }

    const char* end = cm_strchr(start, '}');
    if (!end) {
        char* result = (char*)cm_alloc(cm_strlen_impl(format) + 1);
        if (result) cm_strcpy_impl(result, format);
        return result;
    }

    size_t placeholder_len = end - start + 1;
    size_t formatLen = cm_strlen_impl(format);
    size_t valueLen = strlen(value);
    size_t resultLen = formatLen - placeholder_len + valueLen + 1;

    char* result = (char*)cm_alloc(resultLen);
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
        char* result = (char*)cm_alloc(strlen(format) + 1);
        if (result) strcpy(result, format);
        return result;
    }

    const char* end = strchr(start, '}');
    if (!end) {
        char* result = (char*)cm_alloc(strlen(format) + 1);
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
            formatted_value = (char*)cm_alloc(width + 1);
            if (formatted_value) {
                int padding = width - val_len;
                for (int i = 0; i < padding; i++) formatted_value[i] = '0';
                strcpy(formatted_value + padding, int_str);
            }
            cm_dealloc(int_str);
        }
    } else {
        formatted_value = cm_format_int(value);
    }

    if (!formatted_value) return NULL;

    char* result = cm_format_replace(format, formatted_value);
    cm_dealloc(formatted_value);
    return result;
}

char* cm_format_replace_uint(const char* format, unsigned int value) {
    if (!format) return NULL;

    char* formatted_value = cm_format_uint(value);
    if (!formatted_value) return NULL;

    char* result = cm_format_replace(format, formatted_value);
    cm_dealloc(formatted_value);
    return result;
}

// ポインタ専用のフォーマット関数（デフォルトで10進数表示）
char* cm_format_replace_ptr(const char* format, long long value) {
    if (!format) return NULL;

    const char* start = strchr(format, '{');
    if (!start) {
        char* result = (char*)cm_alloc(strlen(format) + 1);
        if (result) strcpy(result, format);
        return result;
    }

    const char* end = strchr(start, '}');
    if (!end) {
        char* result = (char*)cm_alloc(strlen(format) + 1);
        if (result) cm_strcpy_impl(result, format);
        return result;
    }

    char specifier[32] = {0};
    size_t spec_len = end - start - 1;
    if (spec_len > 0 && spec_len < sizeof(specifier)) {
        cm_strncpy_impl(specifier, start + 1, spec_len);
    }

    char* formatted_value = NULL;
    if (cm_strcmp(specifier, ":x") == 0) {
        // 小文字16進数（ポインタなので0xプレフィックス付き）
        char* hex_str = cm_format_int_hex(value);
        if (hex_str) {
            size_t hex_len = cm_strlen_impl(hex_str);
            formatted_value = (char*)cm_alloc(hex_len + 3);
            if (formatted_value) {
                formatted_value[0] = '0';
                formatted_value[1] = 'x';
                cm_strcpy_impl(formatted_value + 2, hex_str);
            }
            cm_dealloc(hex_str);
        }
    } else if (cm_strcmp(specifier, ":X") == 0) {
        // 大文字16進数（ポインタなので0xプレフィックス付き）
        char* hex_str = cm_format_int_HEX(value);
        if (hex_str) {
            size_t hex_len = cm_strlen_impl(hex_str);
            formatted_value = (char*)cm_alloc(hex_len + 3);
            if (formatted_value) {
                formatted_value[0] = '0';
                formatted_value[1] = 'x';
                cm_strcpy_impl(formatted_value + 2, hex_str);
            }
            cm_dealloc(hex_str);
        }
    } else if (cm_strcmp(specifier, ":b") == 0) {
        formatted_value = cm_format_int_binary(value);
    } else if (cm_strcmp(specifier, ":o") == 0) {
        formatted_value = cm_format_int_octal(value);
    } else {
        // デフォルトは10進数
        formatted_value = cm_format_long(value);
    }

    if (!formatted_value) return NULL;

    char* result = cm_format_replace(format, formatted_value);
    cm_dealloc(formatted_value);
    return result;
}

char* cm_format_replace_long(const char* format, long long value) {
    if (!format) return NULL;

    const char* start = cm_strchr(format, '{');
    if (!start) {
        char* result = (char*)cm_alloc(cm_strlen_impl(format) + 1);
        if (result) cm_strcpy_impl(result, format);
        return result;
    }

    const char* end = cm_strchr(start, '}');
    if (!end) {
        char* result = (char*)cm_alloc(cm_strlen_impl(format) + 1);
        if (result) cm_strcpy_impl(result, format);
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
        char* long_str = cm_format_long(value);
        int val_len = strlen(long_str);

        if (width <= val_len) {
            formatted_value = long_str;
        } else {
            formatted_value = (char*)cm_alloc(width + 1);
            if (formatted_value) {
                int padding = width - val_len;
                for (int i = 0; i < padding; i++) formatted_value[i] = '0';
                strcpy(formatted_value + padding, long_str);
            }
            cm_dealloc(long_str);
        }
    } else {
        formatted_value = cm_format_long(value);
    }

    if (!formatted_value) return NULL;

    char* result = cm_format_replace(format, formatted_value);
    cm_dealloc(formatted_value);
    return result;
}

char* cm_format_replace_ulong(const char* format, unsigned long long value) {
    if (!format) return NULL;

    char* formatted_value = cm_format_ulong(value);
    if (!formatted_value) return NULL;

    char* result = cm_format_replace(format, formatted_value);
    cm_dealloc(formatted_value);
    return result;
}

// アドレス表示用の専用関数 - &変数名:アドレス値 の形式で表示
char* cm_format_address(const char* var_name, void* address) {
    // 変数名が空の場合は単純にアドレス値のみを返す
    if (!var_name || cm_strlen_impl(var_name) == 0) {
        char* buffer = (char*)cm_alloc(32);
        if (buffer) {
            cm_ltoa_buf((long long)(uintptr_t)address, buffer, 32);
        }
        return buffer;
    }

    // &変数名:アドレス値 の形式でフォーマット
    size_t name_len = cm_strlen_impl(var_name);
    char* buffer = (char*)cm_alloc(name_len + 32);
    if (buffer) {
        buffer[0] = '&';
        cm_strcpy_impl(buffer + 1, var_name);
        buffer[name_len + 1] = ':';
        cm_ltoa_buf((long long)(uintptr_t)address, buffer + name_len + 2, 30);
    }
    return buffer;
}

// 文字列補間用のアドレス置換関数
char* cm_format_replace_address(const char* format, const char* var_name, void* address) {
    if (!format) return NULL;

    char* formatted_address = cm_format_address(var_name, address);
    if (!formatted_address) return NULL;

    char* result = cm_format_replace(format, formatted_address);
    cm_dealloc(formatted_address);
    return result;
}

char* cm_format_replace_double(const char* format, double value) {
    if (!format) return NULL;

    const char* start = cm_strchr(format, '{');
    if (!start) {
        char* result = (char*)cm_alloc(cm_strlen_impl(format) + 1);
        if (result) cm_strcpy_impl(result, format);
        return result;
    }

    const char* end = cm_strchr(start, '}');
    if (!end) {
        char* result = (char*)cm_alloc(cm_strlen_impl(format) + 1);
        if (result) cm_strcpy_impl(result, format);
        return result;
    }

    char specifier[32] = {0};
    size_t spec_len = end - start - 1;
    if (spec_len > 0 && spec_len < sizeof(specifier)) {
        cm_strncpy_impl(specifier, start + 1, spec_len);
    }

    char* formatted_value = NULL;
    if (cm_strcmp(specifier, ":e") == 0) {
        formatted_value = cm_format_double_exp(value);
    } else if (cm_strcmp(specifier, ":E") == 0) {
        formatted_value = cm_format_double_EXP(value);
    } else if (specifier[0] == ':' && specifier[1] == '.') {
        int precision = atoi(specifier + 2);
        formatted_value = cm_format_double_precision(value, precision);
    } else {
        formatted_value = cm_format_double(value);
    }

    if (!formatted_value) return NULL;

    char* result = cm_format_replace(format, formatted_value);
    cm_dealloc(formatted_value);
    return result;
}

char* cm_format_replace_string(const char* format, const char* value) {
    if (!format) return NULL;
    if (!value) value = "";

    const char* start = strchr(format, '{');
    if (!start) {
        char* result = (char*)cm_alloc(strlen(format) + 1);
        if (result) strcpy(result, format);
        return result;
    }

    const char* end = strchr(start, '}');
    if (!end) {
        char* result = (char*)cm_alloc(strlen(format) + 1);
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
            formatted_value = (char*)cm_alloc(val_len + 1);
            if (formatted_value) strcpy(formatted_value, value);
        } else {
            formatted_value = (char*)cm_alloc(width + 1);
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
        formatted_value = (char*)cm_alloc(strlen(value) + 1);
        if (formatted_value) strcpy(formatted_value, value);
    }

    if (!formatted_value) return NULL;

    char* result = cm_format_replace(format, formatted_value);
    cm_dealloc(formatted_value);
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
        result = (char*)cm_alloc(1);
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
    cm_dealloc(temp);
    return result;
}

char* cm_format_string_3(const char* fmt, const char* arg1, const char* arg2, const char* arg3) {
    char* temp1 = cm_format_replace(fmt, arg1);
    char* temp2 = cm_format_replace(temp1, arg2);
    char* result = cm_format_replace(temp2, arg3);
    cm_dealloc(temp1);
    cm_dealloc(temp2);
    return result;
}

char* cm_format_string_4(const char* fmt, const char* arg1, const char* arg2, const char* arg3, const char* arg4) {
    char* temp1 = cm_format_replace(fmt, arg1);
    char* temp2 = cm_format_replace(temp1, arg2);
    char* temp3 = cm_format_replace(temp2, arg3);
    char* result = cm_format_replace(temp3, arg4);
    cm_dealloc(temp1);
    cm_dealloc(temp2);
    cm_dealloc(temp3);
    return result;
}

// ============================================================
// Panic
// ============================================================
void __cm_panic(const char* message) {
#ifdef CM_NO_STD
    // no_std: 無限ループで停止
    (void)message;
    while (1) {
        // Halt
    }
#else
    // 標準モード: stderr出力してabort
    if (message) {
        const char* prefix = "panic: ";
        size_t prefix_len = 7;
        size_t msg_len = cm_strlen_impl(message);
        cm_write_stderr(prefix, prefix_len);
        cm_write_stderr(message, msg_len);
        cm_write_stderr("\n", 1);
    }
    // abort()の代わりに__builtin_trapを使用（より移植性が高い）
    __builtin_trap();
#endif
}


