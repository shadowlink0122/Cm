// Cm Language Runtime Library for LLVM Backend
// This file provides runtime support functions for string formatting and printing

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>

// エスケープ処理: {{ -> {, }} -> }
char* cm_unescape_braces(const char* str) {
    if (!str) return NULL;
    
    size_t len = strlen(str);
    char* result = (char*)malloc(len + 1);
    if (!result) return NULL;
    
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (str[i] == '{' && i + 1 < len && str[i + 1] == '{') {
            result[j++] = '{';
            i++;  // skip next {
        } else if (str[i] == '}' && i + 1 < len && str[i + 1] == '}') {
            result[j++] = '}';
            i++;  // skip next }
        } else {
            result[j++] = str[i];
        }
    }
    result[j] = '\0';
    return result;
}

// エイリアス: フォーマット用
char* cm_format_unescape_braces(const char* str) {
    return cm_unescape_braces(str);
}

// 文字列出力
void cm_print_string(const char* str) {
    if (str) {
        printf("%s", str);
    }
}

void cm_println_string(const char* str) {
    if (str) {
        // エスケープ処理を適用
        char* unescaped = cm_unescape_braces(str);
        if (unescaped) {
            printf("%s\n", unescaped);
            free(unescaped);
        } else {
            printf("%s\n", str);
        }
    } else {
        printf("\n");
    }
}

// 整数出力
void cm_print_int(int value) {
    printf("%d", value);
}

void cm_println_int(int value) {
    printf("%d\n", value);
}

// ブール値出力（println用）
void cm_println_bool(char value) {
    printf("%s\n", value ? "true" : "false");
}

// 文字出力
void cm_print_char(char value) {
    printf("%c", value);
}

void cm_println_char(char value) {
    printf("%c\n", value);
}

// 符号なし整数出力
void cm_print_uint(unsigned int value) {
    printf("%u", value);
}

void cm_println_uint(unsigned int value) {
    printf("%u\n", value);
}

// 浮動小数点数出力
void cm_print_double(double value) {
    // Remove trailing zeros for integer-like values
    if (value == (int)value) {
        printf("%d", (int)value);
    } else {
        printf("%g", value);
    }
}

void cm_println_double(double value) {
    if (value == (int)value) {
        printf("%d\n", (int)value);
    } else {
        printf("%g\n", value);
    }
}

// ブール値出力（print用）
void cm_print_bool(char value) {
    printf("%s", value ? "true" : "false");
}

// 前方宣言（cm_println_format_typedで使用）
char* cm_format_int(int value);
char* cm_format_double(double value);
char* cm_format_bool(char value);
char* cm_format_char(char value);
char* cm_format_replace(const char* format, const char* value);

// フォーマット文字列のprintln（簡易版）
// formatは文字列、argcは追加引数の数、その後に実際の引数が続く
// 現在は整数、double、文字列のみをサポート
void cm_println_format(const char* format, int argc, ...) {
    if (!format) {
        printf("\n");
        return;
    }

    // 作業用バッファ（最大4KB）
    char buffer[4096];
    buffer[0] = '\0';

    va_list args;
    va_start(args, argc);

    const char* p = format;
    int arg_index = 0;
    int buf_pos = 0;

    while (*p && buf_pos < sizeof(buffer) - 1) {
        if (*p == '{' && *(p + 1) == '}') {
            // {} プレースホルダー発見
            if (arg_index < argc) {
                // 引数を取得して文字列に変換
                // 簡略化のため、すべての引数をintとして処理
                int value = va_arg(args, int);
                int written = snprintf(buffer + buf_pos, sizeof(buffer) - buf_pos, "%d", value);
                if (written > 0) {
                    buf_pos += written;
                }
                arg_index++;
            } else {
                // 引数が足りない場合はそのまま出力
                if (buf_pos + 2 < sizeof(buffer)) {
                    buffer[buf_pos++] = '{';
                    buffer[buf_pos++] = '}';
                }
            }
            p += 2; // "{}"をスキップ
        } else if (*p == '{' && *(p + 1) == '{') {
            // {{ -> { エスケープ
            buffer[buf_pos++] = '{';
            p += 2;
        } else if (*p == '}' && *(p + 1) == '}') {
            // }} -> } エスケープ
            buffer[buf_pos++] = '}';
            p += 2;
        } else {
            buffer[buf_pos++] = *p;
            p++;
        }
    }

    buffer[buf_pos] = '\0';
    printf("%s\n", buffer);

    va_end(args);
}

// 複数の型に対応したフォーマット文字列のprintln（拡張版）
// 各引数には型タグが必要（0=int, 1=double, 2=string, 3=bool, 4=char）
void cm_println_format_typed(const char* format, int argc, ...) {
    if (!format) {
        printf("\n");
        return;
    }

    va_list args;
    va_start(args, argc);

    char* result = (char*)malloc(strlen(format) + 1);
    if (!result) {
        printf("%s\n", format);
        va_end(args);
        return;
    }
    strcpy(result, format);

    for (int i = 0; i < argc; i++) {
        int type_tag = va_arg(args, int);
        char* formatted = NULL;

        switch (type_tag) {
            case 0: { // int
                int val = va_arg(args, int);
                formatted = cm_format_int(val);
                break;
            }
            case 1: { // double
                double val = va_arg(args, double);
                formatted = cm_format_double(val);
                break;
            }
            case 2: { // string
                const char* val = va_arg(args, const char*);
                formatted = (char*)malloc(strlen(val) + 1);
                if (formatted) strcpy(formatted, val);
                break;
            }
            case 3: { // bool
                int val = va_arg(args, int);
                formatted = cm_format_bool((char)val);
                break;
            }
            case 4: { // char
                int val = va_arg(args, int);
                formatted = cm_format_char((char)val);
                break;
            }
        }

        if (formatted) {
            char* new_result = cm_format_replace(result, formatted);
            free(formatted);
            free(result);
            result = new_result;
            if (!result) {
                printf("\n");
                va_end(args);
                return;
            }
        }
    }

    // エスケープ処理を適用
    char* unescaped = cm_unescape_braces(result);
    if (unescaped) {
        printf("%s\n", unescaped);
        free(unescaped);
    } else {
        printf("%s\n", result);
    }

    free(result);
    va_end(args);
}

// 文字を文字列に変換
char* cm_format_char(char value) {
    char* buffer = (char*)malloc(2);
    if (buffer) {
        buffer[0] = value;
        buffer[1] = '\0';
    }
    return buffer;
}

// ブール値を文字列に変換
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

// 整数を文字列に変換
char* cm_format_int(int value) {
    char* buffer = (char*)malloc(32);
    if (buffer) {
        snprintf(buffer, 32, "%d", value);
    }
    return buffer;
}

// 符号なし整数を文字列に変換
char* cm_format_uint(unsigned int value) {
    char* buffer = (char*)malloc(32);
    if (buffer) {
        snprintf(buffer, 32, "%u", value);
    }
    return buffer;
}

// 整数を16進数（小文字）に変換
char* cm_format_int_hex(int value) {
    char* buffer = (char*)malloc(32);
    if (buffer) {
        snprintf(buffer, 32, "%x", value);
    }
    return buffer;
}

// 整数を16進数（大文字）に変換
char* cm_format_int_HEX(int value) {
    char* buffer = (char*)malloc(32);
    if (buffer) {
        snprintf(buffer, 32, "%X", value);
    }
    return buffer;
}

// 整数を2進数に変換
char* cm_format_int_binary(int value) {
    char* buffer = (char*)malloc(33);  // 最大32ビット + null終端
    if (!buffer) return NULL;

    int index = 31;
    buffer[32] = '\0';

    // 0の特殊ケース
    if (value == 0) {
        strcpy(buffer, "0");
        return buffer;
    }

    // 正の値として扱う（unsigned にキャスト）
    unsigned int uval = (unsigned int)value;

    // 最下位ビットから処理
    while (uval > 0 && index >= 0) {
        buffer[index--] = (uval & 1) ? '1' : '0';
        uval >>= 1;
    }

    // 結果を先頭に移動
    char* result = (char*)malloc(33 - index);
    if (result) {
        strcpy(result, buffer + index + 1);
    }
    free(buffer);
    return result;
}

// 整数を8進数に変換
char* cm_format_int_octal(int value) {
    char* buffer = (char*)malloc(32);
    if (buffer) {
        snprintf(buffer, 32, "%o", value);
    }
    return buffer;
}

// 浮動小数点数を文字列に変換
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

// 浮動小数点数を指定精度で文字列に変換
char* cm_format_double_precision(double value, int precision) {
    char* buffer = (char*)malloc(64);
    if (buffer) {
        snprintf(buffer, 64, "%.*f", precision, value);
    }
    return buffer;
}

// 浮動小数点数を指数表記（小文字）で文字列に変換
char* cm_format_double_exp(double value) {
    char* buffer = (char*)malloc(64);
    if (buffer) {
        snprintf(buffer, 64, "%e", value);
    }
    return buffer;
}

// 浮動小数点数を指数表記（大文字）で文字列に変換
char* cm_format_double_EXP(double value) {
    char* buffer = (char*)malloc(64);
    if (buffer) {
        snprintf(buffer, 64, "%E", value);
    }
    return buffer;
}

// 文字列連結
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

// フォーマット文字列の{}を値で置き換える
char* cm_format_replace(const char* format, const char* value) {
    if (!format) return NULL;
    if (!value) value = "";

    // 最初の{を探す
    const char* start = strchr(format, '{');
    if (!start) {
        // プレースホルダーがない場合はそのまま返す
        char* result = (char*)malloc(strlen(format) + 1);
        if (result) strcpy(result, format);
        return result;
    }

    // 対応する}を探す
    const char* end = strchr(start, '}');
    if (!end) {
        // 閉じ括弧がない場合はそのまま返す
        char* result = (char*)malloc(strlen(format) + 1);
        if (result) strcpy(result, format);
        return result;
    }

    // プレースホルダーの長さ（{}を含む）
    size_t placeholder_len = end - start + 1;

    // 結果のサイズを計算
    size_t formatLen = strlen(format);
    size_t valueLen = strlen(value);
    size_t resultLen = formatLen - placeholder_len + valueLen + 1;

    char* result = (char*)malloc(resultLen);
    if (!result) return NULL;

    // プレースホルダーの前の部分をコピー
    size_t prefixLen = start - format;
    strncpy(result, format, prefixLen);
    result[prefixLen] = '\0';

    // 値を追加
    strcat(result, value);

    // プレースホルダーの後の部分を追加
    strcat(result, end + 1);

    return result;
}

// 整数用のフォーマット付き置き換え
char* cm_format_replace_int(const char* format, int value) {
    if (!format) return NULL;

    // 最初の{を探す
    const char* start = strchr(format, '{');
    if (!start) {
        char* result = (char*)malloc(strlen(format) + 1);
        if (result) strcpy(result, format);
        return result;
    }

    // 対応する}を探す
    const char* end = strchr(start, '}');
    if (!end) {
        char* result = (char*)malloc(strlen(format) + 1);
        if (result) strcpy(result, format);
        return result;
    }

    // フォーマット指定子を抽出
    char specifier[32] = {0};
    size_t spec_len = end - start - 1;
    if (spec_len > 0 && spec_len < sizeof(specifier)) {
        strncpy(specifier, start + 1, spec_len);
    }

    // フォーマット指定子を解析
    char* formatted_value = NULL;
    if (strcmp(specifier, ":x") == 0) {
        formatted_value = cm_format_int_hex(value);
    } else if (strcmp(specifier, ":X") == 0) {
        formatted_value = cm_format_int_HEX(value);
    } else if (strcmp(specifier, ":b") == 0) {
        formatted_value = cm_format_int_binary(value);
    } else if (strcmp(specifier, ":o") == 0) {
        formatted_value = cm_format_int_octal(value);
    } else if (strncmp(specifier, ":<", 2) == 0 || strncmp(specifier, ":>", 2) == 0 || strncmp(specifier, ":^", 2) == 0) {
        // アライメント指定
        char align = specifier[1];
        int width = atoi(specifier + 2);
        char* int_str = cm_format_int(value);
        int val_len = strlen(int_str);

        if (width <= val_len) {
            formatted_value = int_str;
        } else {
            formatted_value = (char*)malloc(width + 1);
            if (formatted_value) {
                int padding = width - val_len;
                if (align == '<') {
                    // 左揃え
                    strcpy(formatted_value, int_str);
                    for (int i = val_len; i < width; i++) formatted_value[i] = ' ';
                } else if (align == '>') {
                    // 右揃え
                    for (int i = 0; i < padding; i++) formatted_value[i] = ' ';
                    strcpy(formatted_value + padding, int_str);
                } else if (align == '^') {
                    // 中央揃え
                    int left_pad = padding / 2;
                    int right_pad = padding - left_pad;
                    for (int i = 0; i < left_pad; i++) formatted_value[i] = ' ';
                    strcpy(formatted_value + left_pad, int_str);
                    for (int i = left_pad + val_len; i < width; i++) formatted_value[i] = ' ';
                }
                formatted_value[width] = '\0';
            }
            free(int_str);
        }
    } else if (strncmp(specifier, ":0>", 3) == 0) {
        // ゼロパディング（右揃え）
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
        // デフォルト：10進数
        formatted_value = cm_format_int(value);
    }

    if (!formatted_value) return NULL;

    char* result = cm_format_replace(format, formatted_value);
    free(formatted_value);
    return result;
}

// 符号なし整数用のフォーマット付き置き換え
char* cm_format_replace_uint(const char* format, unsigned int value) {
    if (!format) return NULL;

    // 最初の{を探す
    const char* start = strchr(format, '{');
    if (!start) {
        char* result = (char*)malloc(strlen(format) + 1);
        if (result) strcpy(result, format);
        return result;
    }

    // 対応する}を探す
    const char* end = strchr(start, '}');
    if (!end) {
        char* result = (char*)malloc(strlen(format) + 1);
        if (result) strcpy(result, format);
        return result;
    }

    // デフォルト：10進数
    char* formatted_value = cm_format_uint(value);

    if (!formatted_value) return NULL;

    char* result = cm_format_replace(format, formatted_value);
    free(formatted_value);
    return result;
}

// 浮動小数点数用のフォーマット付き置き換え
char* cm_format_replace_double(const char* format, double value) {
    if (!format) return NULL;

    // 最初の{を探す
    const char* start = strchr(format, '{');
    if (!start) {
        char* result = (char*)malloc(strlen(format) + 1);
        if (result) strcpy(result, format);
        return result;
    }

    // 対応する}を探す
    const char* end = strchr(start, '}');
    if (!end) {
        char* result = (char*)malloc(strlen(format) + 1);
        if (result) strcpy(result, format);
        return result;
    }

    // フォーマット指定子を抽出
    char specifier[32] = {0};
    size_t spec_len = end - start - 1;
    if (spec_len > 0 && spec_len < sizeof(specifier)) {
        strncpy(specifier, start + 1, spec_len);
    }

    // フォーマット指定子を解析
    char* formatted_value = NULL;
    if (strcmp(specifier, ":e") == 0) {
        formatted_value = cm_format_double_exp(value);
    } else if (strcmp(specifier, ":E") == 0) {
        formatted_value = cm_format_double_EXP(value);
    } else if (specifier[0] == ':' && specifier[1] == '.') {
        // 精度指定 (例: :.2, :.4)
        int precision = atoi(specifier + 2);
        formatted_value = cm_format_double_precision(value, precision);
    } else {
        // デフォルト
        formatted_value = cm_format_double(value);
    }

    if (!formatted_value) return NULL;

    char* result = cm_format_replace(format, formatted_value);
    free(formatted_value);
    return result;
}

// 文字列用のフォーマット付き置き換え
char* cm_format_replace_string(const char* format, const char* value) {
    if (!format) return NULL;
    if (!value) value = "";

    // 最初の{を探す
    const char* start = strchr(format, '{');
    if (!start) {
        char* result = (char*)malloc(strlen(format) + 1);
        if (result) strcpy(result, format);
        return result;
    }

    // 対応する}を探す
    const char* end = strchr(start, '}');
    if (!end) {
        char* result = (char*)malloc(strlen(format) + 1);
        if (result) strcpy(result, format);
        return result;
    }

    // フォーマット指定子を抽出
    char specifier[32] = {0};
    size_t spec_len = end - start - 1;
    if (spec_len > 0 && spec_len < sizeof(specifier)) {
        strncpy(specifier, start + 1, spec_len);
    }

    // フォーマット指定子を解析
    char* formatted_value = NULL;
    if (strncmp(specifier, ":<", 2) == 0 || strncmp(specifier, ":>", 2) == 0 || strncmp(specifier, ":^", 2) == 0) {
        // アライメント指定
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
                    // 左揃え
                    strcpy(formatted_value, value);
                    for (int i = val_len; i < width; i++) formatted_value[i] = ' ';
                } else if (align == '>') {
                    // 右揃え
                    for (int i = 0; i < padding; i++) formatted_value[i] = ' ';
                    strcpy(formatted_value + padding, value);
                } else if (align == '^') {
                    // 中央揃え
                    int left_pad = padding / 2;
                    for (int i = 0; i < left_pad; i++) formatted_value[i] = ' ';
                    strcpy(formatted_value + left_pad, value);
                    for (int i = left_pad + val_len; i < width; i++) formatted_value[i] = ' ';
                }
                formatted_value[width] = '\0';
            }
        }
    } else {
        // デフォルト：そのまま
        formatted_value = (char*)malloc(strlen(value) + 1);
        if (formatted_value) strcpy(formatted_value, value);
    }

    if (!formatted_value) return NULL;

    char* result = cm_format_replace(format, formatted_value);
    free(formatted_value);
    return result;
}

// パニック処理
void __cm_panic(const char* message) {
    fprintf(stderr, "panic: %s\n", message);
    abort();
}
// ============================================================
// 型変換関数（文字列連結用）
// ============================================================

// int -> string
char* cm_int_to_string(int value) {
    char* buffer = (char*)malloc(32);
    if (buffer) {
        snprintf(buffer, 32, "%d", value);
    }
    return buffer;
}

// unsigned int -> string
char* cm_uint_to_string(unsigned int value) {
    char* buffer = (char*)malloc(32);
    if (buffer) {
        snprintf(buffer, 32, "%u", value);
    }
    return buffer;
}

// char -> string
char* cm_char_to_string(char value) {
    char* buffer = (char*)malloc(2);
    if (buffer) {
        buffer[0] = value;
        buffer[1] = '\0';
    }
    return buffer;
}

// bool -> string
char* cm_bool_to_string(bool value) {
    const char* str = value ? "true" : "false";
    char* buffer = (char*)malloc(strlen(str) + 1);
    if (buffer) {
        strcpy(buffer, str);
    }
    return buffer;
}

// double -> string
char* cm_double_to_string(double value) {
    char* buffer = (char*)malloc(64);
    if (buffer) {
        // 整数値の場合は小数点なしで表示
        if (value == (long long)value) {
            snprintf(buffer, 64, "%lld", (long long)value);
        } else {
            snprintf(buffer, 64, "%g", value);
        }
    }
    return buffer;
}
