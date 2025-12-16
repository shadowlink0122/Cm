/// @file format_impl.h
/// @brief 共通フォーマット実装（インライン関数）
/// プラットフォーム固有のメモリ割り当てを抽象化

#ifndef CM_FORMAT_IMPL_H
#define CM_FORMAT_IMPL_H

#include <stddef.h>
#include <stdint.h>

// ============================================================
// Platform-specific function declarations (to be implemented by each backend)
// ============================================================

/// メモリ割り当て（プラットフォーム固有）
void* cm_alloc(size_t size);

/// メモリ解放（プラットフォーム固有、WASMでは何もしない）
void cm_free(void* ptr);

/// 文字列長（プラットフォーム固有）
size_t cm_strlen(const char* str);

/// 文字列コピー（プラットフォーム固有）
void cm_strcpy(char* dest, const char* src);

// ============================================================
// Integer to String Conversion (Pure Algorithm)
// ============================================================

/// @brief 整数を文字列バッファに変換
/// @param value 変換する値
/// @param buffer 出力バッファ（最低32バイト必要）
/// @param len 出力された文字数
static inline void cm_int_to_buffer(int value, char* buffer, size_t* len) {
    int is_negative = 0;
    unsigned int uval;

    if (value < 0) {
        is_negative = 1;
        // INT_MIN の特別処理
        if (value == -2147483648) {
            const char* int_min = "-2147483648";
            size_t i = 0;
            while (int_min[i]) {
                buffer[i] = int_min[i];
                i++;
            }
            *len = i;
            return;
        }
        uval = (unsigned int)(-value);
    } else {
        uval = (unsigned int)value;
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

    *len = (size_t)j;
}

/// @brief 符号なし整数を文字列バッファに変換
static inline void cm_uint_to_buffer(unsigned int value, char* buffer, size_t* len) {
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

    *len = (size_t)j;
}

/// @brief 64bit整数を文字列バッファに変換
static inline void cm_int64_to_buffer(long long value, char* buffer, size_t* len) {
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

    *len = (size_t)j;
}

// ============================================================
// Hex/Binary/Octal Conversion
// ============================================================

/// @brief 整数を16進数文字列に変換（小文字）
static inline void cm_int_to_hex_buffer(long long value, char* buffer, size_t* len, int uppercase) {
    const char* hex_chars = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    unsigned long long uval = (unsigned long long)value;

    if (uval == 0) {
        buffer[0] = '0';
        buffer[1] = '\0';
        *len = 1;
        return;
    }

    char temp[32];
    int i = 0;
    while (uval > 0) {
        temp[i++] = hex_chars[uval % 16];
        uval /= 16;
    }

    int j = 0;
    while (i > 0) {
        buffer[j++] = temp[--i];
    }
    buffer[j] = '\0';
    *len = (size_t)j;
}

/// @brief 整数を2進数文字列に変換
static inline void cm_int_to_binary_buffer(long long value, char* buffer, size_t* len) {
    unsigned long long uval = (unsigned long long)value;

    if (uval == 0) {
        buffer[0] = '0';
        buffer[1] = '\0';
        *len = 1;
        return;
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
    *len = (size_t)j;
}

/// @brief 整数を8進数文字列に変換
static inline void cm_int_to_octal_buffer(long long value, char* buffer, size_t* len) {
    unsigned long long uval = (unsigned long long)value;

    if (uval == 0) {
        buffer[0] = '0';
        buffer[1] = '\0';
        *len = 1;
        return;
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
    *len = (size_t)j;
}

// ============================================================
// Double to String Conversion
// ============================================================

/// @brief 浮動小数点数を指数表記に変換
static inline void cm_double_to_scientific_buffer(double value, char* buffer, size_t* len,
                                                  int uppercase) {
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
        while (mantissa < 1.0 && mantissa > 0.0) {
            mantissa *= 10.0;
            exponent--;
        }
    }

    // 丸め
    mantissa += 0.0000005;
    if (mantissa >= 10.0) {
        mantissa /= 10.0;
        exponent++;
    }

    int mantissa_int = (int)mantissa;
    int mantissa_frac = (int)((mantissa - mantissa_int) * 1000000);

    int idx = 0;
    if (is_negative)
        buffer[idx++] = '-';
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
    *len = (size_t)idx;
}

// ============================================================
// Format Spec Parsing
// ============================================================

/// @brief フォーマット仕様を解析
/// @param spec_start ':' の次の文字へのポインタ
/// @param spec_end '}' の位置
/// @param out_align 出力：アライメント文字 ('<', '>', '^' or '\0')
/// @param out_fill 出力：フィル文字
/// @param out_width 出力：幅
/// @param out_type 出力：型指定子 ('x', 'X', 'b', 'o', 'e', 'E' or '\0')
/// @param out_precision 出力：精度（-1 if not specified）
static inline void cm_parse_format_spec(const char* spec_start, const char* spec_end,
                                        char* out_align, char* out_fill, int* out_width,
                                        char* out_type, int* out_precision) {
    *out_align = '\0';
    *out_fill = ' ';
    *out_width = 0;
    *out_type = '\0';
    *out_precision = -1;

    if (!spec_start || spec_start >= spec_end)
        return;

    const char* p = spec_start;

    // アライメントとフィル文字をチェック
    if (p < spec_end) {
        char c = *p;
        if (c == '<' || c == '>' || c == '^') {
            *out_align = c;
            p++;
        } else if (p + 1 < spec_end) {
            char next = *(p + 1);
            if (next == '<' || next == '>' || next == '^') {
                *out_fill = c;
                *out_align = next;
                p += 2;
            }
        }
    }

    // 幅を解析
    while (p < spec_end && *p >= '0' && *p <= '9') {
        *out_width = (*out_width) * 10 + (*p - '0');
        p++;
    }

    // 精度を解析 (.N)
    if (p < spec_end && *p == '.') {
        p++;
        *out_precision = 0;
        while (p < spec_end && *p >= '0' && *p <= '9') {
            *out_precision = (*out_precision) * 10 + (*p - '0');
            p++;
        }
    }

    // 型指定子を解析
    if (p < spec_end) {
        *out_type = *p;
    }
}

/// @brief プレースホルダーを検索
/// @param format フォーマット文字列
/// @param out_start 出力：'{' の位置
/// @param out_end 出力：'}' の位置
/// @return 見つかった場合は1、それ以外は0
static inline int cm_find_placeholder(const char* format, size_t len, size_t* out_start,
                                      size_t* out_end) {
    for (size_t i = 0; i < len; i++) {
        if (format[i] == '{') {
            // エスケープチェック
            if (i + 1 < len && format[i + 1] == '{') {
                i++;  // {{をスキップ
                continue;
            }
            *out_start = i;
            for (size_t j = i + 1; j < len; j++) {
                if (format[j] == '}') {
                    *out_end = j;
                    return 1;
                }
            }
        }
    }
    return 0;
}

// ============================================================
// Padding Helper
// ============================================================

/// @brief パディングを適用した文字列を作成
/// @param value 元の文字列
/// @param val_len 元の文字列の長さ
/// @param align アライメント文字
/// @param fill フィル文字
/// @param width 幅
/// @param result 出力バッファ（width + 1バイト以上必要）
static inline void cm_apply_padding(const char* value, size_t val_len, char align, char fill,
                                    int width, char* result) {
    if (width <= 0 || (size_t)width <= val_len || align == '\0') {
        // パディング不要
        for (size_t i = 0; i < val_len; i++) {
            result[i] = value[i];
        }
        result[val_len] = '\0';
        return;
    }

    size_t padding = (size_t)width - val_len;

    if (align == '<') {
        // 左揃え
        for (size_t i = 0; i < val_len; i++)
            result[i] = value[i];
        for (size_t i = val_len; i < (size_t)width; i++)
            result[i] = fill;
    } else if (align == '>') {
        // 右揃え
        for (size_t i = 0; i < padding; i++)
            result[i] = fill;
        for (size_t i = 0; i < val_len; i++)
            result[padding + i] = value[i];
    } else if (align == '^') {
        // 中央揃え
        size_t left_pad = padding / 2;
        for (size_t i = 0; i < left_pad; i++)
            result[i] = fill;
        for (size_t i = 0; i < val_len; i++)
            result[left_pad + i] = value[i];
        for (size_t i = left_pad + val_len; i < (size_t)width; i++)
            result[i] = fill;
    }
    result[width] = '\0';
}

#endif  // CM_FORMAT_IMPL_H
