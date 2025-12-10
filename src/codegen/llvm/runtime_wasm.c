// Cm Language Runtime Library for WASM
// This file provides WASM-specific runtime support functions

#include <stddef.h>
#include <stdint.h>

// Forward declaration of main function
int main();

// WASI imports - これらは外部から提供される必要がある
// wasm-ld向けのimportディレクティブを使用
__attribute__((import_module("wasi_snapshot_preview1"), import_name("fd_write")))
int __wasi_fd_write(int fd, const void* iovs, size_t iovs_len, size_t* nwritten);

__attribute__((import_module("wasi_snapshot_preview1"), import_name("proc_exit")))
void __wasi_proc_exit(int exit_code) __attribute__((noreturn));

// Helper structure for WASI fd_write
typedef struct {
    const uint8_t* buf;
    size_t buf_len;
} ciovec_t;

// 標準出力への書き込み
static void wasm_write_stdout(const char* str, size_t len) {
    ciovec_t iov = {
        .buf = (const uint8_t*)str,
        .buf_len = len
    };
    size_t nwritten;
    __wasi_fd_write(1, &iov, 1, &nwritten);  // fd=1 is stdout
}

// 文字列の長さを計算
static size_t wasm_strlen(const char* str) {
    size_t len = 0;
    while (str[len] != '\0') {
        len++;
    }
    return len;
}

// 整数を文字列に変換
static void wasm_int_to_str(int value, char* buffer, size_t* len) {
    int is_negative = 0;
    unsigned int uval;

    if (value < 0) {
        is_negative = 1;
        // INT_MINの場合、-valueがオーバーフローするので特別処理
        if (value == -2147483648) {
            // "-2147483648"を直接設定
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

    // 数字を逆順で格納
    char temp[32];
    int i = 0;
    do {
        temp[i++] = '0' + (uval % 10);
        uval /= 10;
    } while (uval > 0);

    // 符号を追加
    int j = 0;
    if (is_negative) {
        buffer[j++] = '-';
    }

    // 逆順を正しい順序に
    while (i > 0) {
        buffer[j++] = temp[--i];
    }

    *len = j;
}

// 符号なし整数を文字列に変換
static void wasm_uint_to_str(unsigned int value, char* buffer, size_t* len) {
    // 数字を逆順で格納
    char temp[32];
    int i = 0;
    do {
        temp[i++] = '0' + (value % 10);
        value /= 10;
    } while (value > 0);

    // 逆順を正しい順序に
    int j = 0;
    while (i > 0) {
        buffer[j++] = temp[--i];
    }

    *len = j;
}

// Forward declaration
static void* wasm_alloc(size_t size);

// エスケープ処理: {{ -> {, }} -> }
char* cm_unescape_braces(const char* str) {
    if (!str) return 0;

    size_t len = wasm_strlen(str);
    char* result = (char*)wasm_alloc(len + 1);

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

// 文字列出力
void cm_print_string(const char* str) {
    if (str) {
        size_t len = wasm_strlen(str);
        wasm_write_stdout(str, len);
    }
}

void cm_println_string(const char* str) {
    if (str) {
        // エスケープ処理を適用
        char* unescaped = cm_unescape_braces(str);
        if (unescaped) {
            size_t len = wasm_strlen(unescaped);
            wasm_write_stdout(unescaped, len);
        } else {
            size_t len = wasm_strlen(str);
            wasm_write_stdout(str, len);
        }
    }
    wasm_write_stdout("\n", 1);
}

// 整数出力
void cm_print_int(int value) {
    char buffer[32];
    size_t len;
    wasm_int_to_str(value, buffer, &len);
    wasm_write_stdout(buffer, len);
}

void cm_println_int(int value) {
    char buffer[32];
    size_t len;
    wasm_int_to_str(value, buffer, &len);
    wasm_write_stdout(buffer, len);
    wasm_write_stdout("\n", 1);
}

// 符号なし整数出力
void cm_print_uint(unsigned int value) {
    char buffer[32];
    size_t len;
    wasm_uint_to_str(value, buffer, &len);
    wasm_write_stdout(buffer, len);
}

void cm_println_uint(unsigned int value) {
    char buffer[32];
    size_t len;
    wasm_uint_to_str(value, buffer, &len);
    wasm_write_stdout(buffer, len);
    wasm_write_stdout("\n", 1);
}

// 浮動小数点数出力（簡略版）
void cm_print_double(double value) {
    // 簡略化: 整数部分のみ出力
    int int_value = (int)value;
    cm_print_int(int_value);
}

void cm_println_double(double value) {
    int int_value = (int)value;
    cm_println_int(int_value);
}

// ブール値出力
void cm_print_bool(char value, char with_newline) {
    if (value) {
        wasm_write_stdout("true", 4);
    } else {
        wasm_write_stdout("false", 5);
    }
    if (with_newline) {
        wasm_write_stdout("\n", 1);
    }
}


// 簡易メモリアロケータ（静的バッファプール）
static char memory_pool[65536];  // 64KB
static size_t pool_offset = 0;

static void* wasm_alloc(size_t size) {
    if (pool_offset + size > sizeof(memory_pool)) {
        // メモリ不足 - プールをリセット（簡易GC）
        pool_offset = 0;
    }
    void* ptr = &memory_pool[pool_offset];
    pool_offset += size;
    return ptr;
}

// 文字列連結
char* cm_concat_strings(const char* s1, const char* s2) {
    if (!s1) s1 = "";
    if (!s2) s2 = "";

    size_t len1 = wasm_strlen(s1);
    size_t len2 = wasm_strlen(s2);
    char* result = (char*)wasm_alloc(len1 + len2 + 1);

    // s1をコピー
    for (size_t i = 0; i < len1; i++) {
        result[i] = s1[i];
    }
    // s2をコピー
    for (size_t i = 0; i < len2; i++) {
        result[len1 + i] = s2[i];
    }
    result[len1 + len2] = '\0';

    return result;
}

// cm_string_concat - 文字列連結（エイリアス）
char* cm_string_concat(const char* left, const char* right) {
    return cm_concat_strings(left, right);
}

// 整数を文字列に変換（連結用）
char* cm_int_to_string(int value) {
    char* buffer = (char*)wasm_alloc(32);
    size_t len;
    wasm_int_to_str(value, buffer, &len);
    buffer[len] = '\0';
    return buffer;
}

// cm_format_int - 整数をフォーマット
char* cm_format_int(int value) {
    char* buffer = (char*)wasm_alloc(32);
    size_t len;
    wasm_int_to_str(value, buffer, &len);
    buffer[len] = '\0';
    return buffer;
}

// cm_format_uint - 符号なし整数をフォーマット
char* cm_format_uint(unsigned int value) {
    char* buffer = (char*)wasm_alloc(32);
    size_t len;
    wasm_uint_to_str(value, buffer, &len);
    buffer[len] = '\0';
    return buffer;
}

// boolを文字列に変換
char* cm_bool_to_string(char value) {
    return value ? "true" : "false";
}

// cm_format_bool - boolをフォーマット
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

// charを文字列に変換
char* cm_char_to_string(char value) {
    char* buffer = (char*)wasm_alloc(2);
    buffer[0] = value;
    buffer[1] = '\0';
    return buffer;
}

// cm_format_char - charをフォーマット
char* cm_format_char(char value) {
    char* buffer = (char*)wasm_alloc(2);
    buffer[0] = value;
    buffer[1] = '\0';
    return buffer;
}

// cm_format_double_precision - 精度付き浮動小数点数をフォーマット
char* cm_format_double_precision(double value, int precision) {
    char* buffer = (char*)wasm_alloc(64);
    int int_part = (int)value;
    double frac_part = value - int_part;
    if (frac_part < 0) frac_part = -frac_part;

    // 整数部分を文字列化
    size_t len;
    wasm_int_to_str(int_part, buffer, &len);

    // 小数点を追加
    buffer[len++] = '.';

    // 小数部分を指定精度まで表示
    for (int i = 0; i < precision; i++) {
        frac_part *= 10;
        int digit = (int)frac_part % 10;
        buffer[len++] = '0' + digit;
    }
    buffer[len] = '\0';

    return buffer;
}

// cm_format_double - 浮動小数点数をフォーマット（簡易版）
char* cm_format_double(double value) {
    char* buffer = (char*)wasm_alloc(64);
    int int_part = (int)value;
    double frac_part = value - int_part;
    if (frac_part < 0) frac_part = -frac_part;

    // 整数部分を文字列化
    size_t len;
    wasm_int_to_str(int_part, buffer, &len);

    // 小数点を追加
    buffer[len++] = '.';

    // 小数部分を表示（最大5桁、末尾の0を削除）
    int frac_int = (int)(frac_part * 100000);

    // 末尾の0を数える
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

    // 有効桁数を出力（最低2桁、最大5桁）
    int num_digits = 5 - trailing_zeros;
    if (num_digits < 2) num_digits = 2;
    if (num_digits > 5) num_digits = 5;

    // 桁を出力
    int divisor = 10000;
    for (int i = 0; i < num_digits; i++) {
        buffer[len++] = '0' + ((frac_int / divisor) % 10);
        divisor /= 10;
    }

    buffer[len] = '\0';

    return buffer;
}

// 整数を16進数（小文字）に変換
char* cm_format_int_hex(long value) {
    char* buffer = (char*)wasm_alloc(32);
    unsigned long uval = (unsigned long)value;

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

    int j = 0;
    while (i > 0) {
        buffer[j++] = temp[--i];
    }
    buffer[j] = '\0';

    return buffer;
}

// 整数を16進数（大文字）に変換
char* cm_format_int_HEX(long value) {
    char* buffer = (char*)wasm_alloc(32);
    unsigned long uval = (unsigned long)value;

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

    int j = 0;
    while (i > 0) {
        buffer[j++] = temp[--i];
    }
    buffer[j] = '\0';

    return buffer;
}

// 整数を2進数に変換
char* cm_format_int_binary(long value) {
    char* buffer = (char*)wasm_alloc(65);  // 最大64ビット + null終端
    unsigned long uval = (unsigned long)value;

    if (uval == 0) {
        buffer[0] = '0';
        buffer[1] = '\0';
        return buffer;
    }

    char temp[32];
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

// 整数を8進数に変換
char* cm_format_int_octal(long value) {
    char* buffer = (char*)wasm_alloc(32);
    unsigned long uval = (unsigned long)value;

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

// フォーマット文字列（簡易版 - 1引数のみ対応）
char* cm_format_string_1(const char* fmt, const char* arg1) {
    if (!fmt) return "";

    size_t fmt_len = wasm_strlen(fmt);
    char* result = (char*)wasm_alloc(fmt_len + wasm_strlen(arg1) + 1);
    size_t result_idx = 0;

    for (size_t i = 0; i < fmt_len; i++) {
        if (fmt[i] == '{' && i + 1 < fmt_len && fmt[i + 1] == '}') {
            // {}をarg1で置換
            size_t arg_len = wasm_strlen(arg1);
            for (size_t j = 0; j < arg_len; j++) {
                result[result_idx++] = arg1[j];
            }
            i++; // '}'をスキップ
        } else {
            result[result_idx++] = fmt[i];
        }
    }
    result[result_idx] = '\0';

    return result;
}

// フォーマット文字列（2引数版）
char* cm_format_string_2(const char* fmt, const char* arg1, const char* arg2) {
    if (!fmt) return "";

    size_t fmt_len = wasm_strlen(fmt);
    size_t arg1_len = arg1 ? wasm_strlen(arg1) : 0;
    size_t arg2_len = arg2 ? wasm_strlen(arg2) : 0;

    char* result = (char*)wasm_alloc(fmt_len + arg1_len + arg2_len + 1);
    size_t result_idx = 0;
    int arg_num = 0;

    for (size_t i = 0; i < fmt_len; i++) {
        if (fmt[i] == '{' && i + 1 < fmt_len && fmt[i + 1] == '}') {
            const char* arg = 0;
            size_t arg_len = 0;

            if (arg_num == 0 && arg1) {
                arg = arg1;
                arg_len = arg1_len;
            } else if (arg_num == 1 && arg2) {
                arg = arg2;
                arg_len = arg2_len;
            }

            if (arg) {
                for (size_t j = 0; j < arg_len; j++) {
                    result[result_idx++] = arg[j];
                }
            }

            i++; // '}'をスキップ
            arg_num++;
        } else {
            result[result_idx++] = fmt[i];
        }
    }
    result[result_idx] = '\0';

    return result;
}

// フォーマット文字列（3引数版）
char* cm_format_string_3(const char* fmt, const char* arg1, const char* arg2, const char* arg3) {
    if (!fmt) return "";

    size_t fmt_len = wasm_strlen(fmt);
    size_t arg1_len = arg1 ? wasm_strlen(arg1) : 0;
    size_t arg2_len = arg2 ? wasm_strlen(arg2) : 0;
    size_t arg3_len = arg3 ? wasm_strlen(arg3) : 0;

    // 十分なバッファサイズを確保
    char* result = (char*)wasm_alloc(fmt_len + arg1_len + arg2_len + arg3_len + 1);
    size_t result_idx = 0;
    int arg_num = 0;

    for (size_t i = 0; i < fmt_len; i++) {
        if (fmt[i] == '{' && i + 1 < fmt_len && fmt[i + 1] == '}') {
            const char* arg = 0;
            size_t arg_len = 0;

            if (arg_num == 0 && arg1) {
                arg = arg1;
                arg_len = arg1_len;
            } else if (arg_num == 1 && arg2) {
                arg = arg2;
                arg_len = arg2_len;
            } else if (arg_num == 2 && arg3) {
                arg = arg3;
                arg_len = arg3_len;
            }

            if (arg) {
                for (size_t j = 0; j < arg_len; j++) {
                    result[result_idx++] = arg[j];
                }
            }

            i++; // '}'をスキップ
            arg_num++;
        } else {
            result[result_idx++] = fmt[i];
        }
    }
    result[result_idx] = '\0';

    return result;
}

// フォーマット文字列（4引数版）
char* cm_format_string_4(const char* fmt, const char* arg1, const char* arg2, const char* arg3, const char* arg4) {
    if (!fmt) return "";

    size_t fmt_len = wasm_strlen(fmt);
    size_t arg1_len = arg1 ? wasm_strlen(arg1) : 0;
    size_t arg2_len = arg2 ? wasm_strlen(arg2) : 0;
    size_t arg3_len = arg3 ? wasm_strlen(arg3) : 0;
    size_t arg4_len = arg4 ? wasm_strlen(arg4) : 0;

    // 十分なバッファサイズを確保
    char* result = (char*)wasm_alloc(fmt_len + arg1_len + arg2_len + arg3_len + arg4_len + 1);
    size_t result_idx = 0;
    int arg_num = 0;

    for (size_t i = 0; i < fmt_len; i++) {
        if (fmt[i] == '{' && i + 1 < fmt_len && fmt[i + 1] == '}') {
            const char* arg = 0;
            size_t arg_len = 0;

            if (arg_num == 0 && arg1) {
                arg = arg1;
                arg_len = arg1_len;
            } else if (arg_num == 1 && arg2) {
                arg = arg2;
                arg_len = arg2_len;
            } else if (arg_num == 2 && arg3) {
                arg = arg3;
                arg_len = arg3_len;
            } else if (arg_num == 3 && arg4) {
                arg = arg4;
                arg_len = arg4_len;
            }

            if (arg) {
                for (size_t j = 0; j < arg_len; j++) {
                    result[result_idx++] = arg[j];
                }
            }

            i++; // '}'をスキップ
            arg_num++;
        } else {
            result[result_idx++] = fmt[i];
        }
    }
    result[result_idx] = '\0';

    return result;
}

// 汎用フォーマット（可変長引数 - 暫定対応）
char* cm_format_string(const char* fmt, ...) {
    // 簡略化: フォーマットなしで返す
    return (char*)fmt;
}

// フォーマット文字列の{}を値で置き換える
char* cm_format_replace(const char* format, const char* value) {
    if (!format) return 0;
    if (!value) value = "";

    size_t fmt_len = wasm_strlen(format);
    size_t val_len = wasm_strlen(value);

    // {}の個数をカウント
    int placeholder_count = 0;
    for (size_t i = 0; i < fmt_len - 1; i++) {
        if (format[i] == '{' && format[i + 1] == '}') {
            placeholder_count++;
            i++;  // Skip the }
        }
    }

    // 結果のサイズを計算
    size_t result_len = fmt_len - (2 * placeholder_count) + (val_len * placeholder_count) + 1;
    char* result = (char*)wasm_alloc(result_len);

    size_t result_idx = 0;
    for (size_t i = 0; i < fmt_len; i++) {
        if (i < fmt_len - 1 && format[i] == '{' && format[i + 1] == '}') {
            // {}を値で置換
            for (size_t j = 0; j < val_len; j++) {
                result[result_idx++] = value[j];
            }
            i++;  // Skip }
        } else {
            result[result_idx++] = format[i];
        }
    }
    result[result_idx] = '\0';

    return result;
}

// 整数用のフォーマット付き置き換え
char* cm_format_replace_int(const char* format, int value) {
    if (!format) return 0;

    size_t fmt_len = wasm_strlen(format);

    // フォーマット指定子を探す {:...}
    for (size_t i = 0; i < fmt_len - 1; i++) {
        if (format[i] == '{' && format[i + 1] == ':') {
            // フォーマット指定子の終わりを探す
            size_t spec_start = i + 2;
            size_t spec_end = spec_start;
            while (spec_end < fmt_len && format[spec_end] != '}') {
                spec_end++;
            }

            if (spec_end < fmt_len) {
                // フォーマット指定子を解析
                size_t spec_len = spec_end - spec_start;
                char* formatted_value = 0;

                if (spec_len == 1 && format[spec_start] == 'x') {
                    formatted_value = cm_format_int_hex(value);
                } else if (spec_len == 1 && format[spec_start] == 'X') {
                    formatted_value = cm_format_int_HEX(value);
                } else if (spec_len == 1 && format[spec_start] == 'b') {
                    formatted_value = cm_format_int_binary(value);
                } else if (spec_len == 1 && format[spec_start] == 'o') {
                    formatted_value = cm_format_int_octal(value);
                } else if (spec_len >= 2 && format[spec_start] == '0' && format[spec_start + 1] == '>') {
                    // ゼロパディング（右揃え）
                    int width = 0;
                    for (size_t j = spec_start + 2; j < spec_end; j++) {
                        width = width * 10 + (format[j] - '0');
                    }
                    char* int_str = cm_format_int(value);
                    int val_len = wasm_strlen(int_str);

                    formatted_value = (char*)wasm_alloc(width + 1);
                    int padding = width - val_len;
                    if (padding > 0) {
                        for (int j = 0; j < padding; j++) {
                            formatted_value[j] = '0';
                        }
                        for (int j = 0; j < val_len; j++) {
                            formatted_value[padding + j] = int_str[j];
                        }
                    } else {
                        for (int j = 0; j < val_len; j++) {
                            formatted_value[j] = int_str[j];
                        }
                    }
                    formatted_value[width > val_len ? width : val_len] = '\0';
                } else {
                    // デフォルト：10進数
                    formatted_value = cm_format_int(value);
                }

                // フォーマット文字列を作成（{:...}を{}に置き換える）
                char* new_format = (char*)wasm_alloc(fmt_len);
                size_t new_idx = 0;
                for (size_t j = 0; j < i; j++) {
                    new_format[new_idx++] = format[j];
                }
                new_format[new_idx++] = '{';
                new_format[new_idx++] = '}';
                for (size_t j = spec_end + 1; j < fmt_len; j++) {
                    new_format[new_idx++] = format[j];
                }
                new_format[new_idx] = '\0';

                return cm_format_replace(new_format, formatted_value);
            }
        }
    }

    // フォーマット指定子がない場合、デフォルトの10進数
    char* value_str = cm_format_int(value);
    return cm_format_replace(format, value_str);
}

// 符号なし整数用のフォーマット付き置き換え
char* cm_format_replace_uint(const char* format, unsigned int value) {
    if (!format) return 0;

    // 符号なし整数を文字列に変換
    char* value_str = cm_format_uint(value);
    char* result = cm_format_replace(format, value_str);
    return result;
}

// 浮動小数点を科学表記法でフォーマット
char* cm_format_double_scientific(double value, int uppercase) {
    char* buffer = (char*)wasm_alloc(32);

    // 符号処理
    int is_negative = 0;
    if (value < 0) {
        is_negative = 1;
        value = -value;
    }

    // 指数を計算
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

    // 仮数部を文字列に変換（小数点以下6桁）
    int mantissa_int = (int)mantissa;
    int mantissa_frac = (int)((mantissa - mantissa_int) * 1000000);

    int idx = 0;
    if (is_negative) buffer[idx++] = '-';
    buffer[idx++] = '0' + mantissa_int;
    buffer[idx++] = '.';

    // 小数部分（6桁）
    for (int i = 5; i >= 0; i--) {
        int digit = mantissa_frac / 1;
        for (int j = 0; j < i; j++) digit /= 10;
        buffer[idx++] = '0' + (digit % 10);
    }

    // 指数部
    buffer[idx++] = uppercase ? 'E' : 'e';
    if (exponent < 0) {
        buffer[idx++] = '-';
        exponent = -exponent;
    } else {
        buffer[idx++] = '+';
    }

    // 指数を2桁で出力
    buffer[idx++] = '0' + (exponent / 10);
    buffer[idx++] = '0' + (exponent % 10);
    buffer[idx] = '\0';

    return buffer;
}

// 浮動小数点数用のフォーマット付き置き換え
char* cm_format_replace_double(const char* format, double value) {
    if (!format) return 0;

    size_t fmt_len = wasm_strlen(format);

    // フォーマット指定子を探す {:...}
    for (size_t i = 0; i < fmt_len - 1; i++) {
        if (format[i] == '{' && format[i + 1] == ':') {
            // フォーマット指定子の終わりを探す
            size_t spec_start = i + 2;
            size_t spec_end = spec_start;
            while (spec_end < fmt_len && format[spec_end] != '}') {
                spec_end++;
            }

            if (spec_end < fmt_len) {
                // フォーマット指定子を解析
                size_t spec_len = spec_end - spec_start;
                char* formatted_value = 0;

                if (spec_len >= 2 && format[spec_start] == '.' && format[spec_start + 1] >= '0' && format[spec_start + 1] <= '9') {
                    // 精度指定 {:.N}
                    int precision = format[spec_start + 1] - '0';
                    formatted_value = cm_format_double_precision(value, precision);
                } else if (spec_len == 1 && format[spec_start] == 'e') {
                    // 科学表記法 (小文字)
                    formatted_value = cm_format_double_scientific(value, 0);
                } else if (spec_len == 1 && format[spec_start] == 'E') {
                    // 科学表記法 (大文字)
                    formatted_value = cm_format_double_scientific(value, 1);
                } else {
                    // デフォルト：通常の浮動小数点
                    formatted_value = cm_format_double(value);
                }

                // フォーマット文字列を作成（{:...}を{}に置き換える）
                char* new_format = (char*)wasm_alloc(fmt_len);
                size_t new_idx = 0;
                for (size_t j = 0; j < i; j++) {
                    new_format[new_idx++] = format[j];
                }
                new_format[new_idx++] = '{';
                new_format[new_idx++] = '}';
                for (size_t j = spec_end + 1; j < fmt_len; j++) {
                    new_format[new_idx++] = format[j];
                }
                new_format[new_idx] = '\0';

                return cm_format_replace(new_format, formatted_value);
            }
        }
    }

    // フォーマット指定子がない場合、デフォルトのフォーマット
    char* value_str = cm_format_double(value);
    return cm_format_replace(format, value_str);
}

// 文字列用のフォーマット付き置き換え
char* cm_format_replace_string(const char* format, const char* value) {
    if (!format) return 0;

    size_t fmt_len = wasm_strlen(format);

    // フォーマット指定子を探す {:...}
    for (size_t i = 0; i < fmt_len - 1; i++) {
        if (format[i] == '{' && format[i + 1] == ':') {
            // フォーマット指定子の終わりを探す
            size_t spec_start = i + 2;
            size_t spec_end = spec_start;
            while (spec_end < fmt_len && format[spec_end] != '}') {
                spec_end++;
            }

            if (spec_end < fmt_len) {
                // フォーマット指定子を解析
                size_t spec_len = spec_end - spec_start;
                char* formatted_value = 0;

                if (spec_len >= 2) {
                    char align_char = format[spec_start];
                    int width = 0;
                    size_t width_start = spec_start + 1;

                    // アライメント文字をチェック
                    if (align_char == '<' || align_char == '>' || align_char == '^') {
                        // 幅を解析
                        for (size_t j = width_start; j < spec_end; j++) {
                            if (format[j] >= '0' && format[j] <= '9') {
                                width = width * 10 + (format[j] - '0');
                            }
                        }

                        // 文字列の長さを取得
                        size_t val_len = wasm_strlen(value);

                        if (width > val_len) {
                            formatted_value = (char*)wasm_alloc(width + 1);
                            int padding = width - val_len;

                            if (align_char == '<') {
                                // 左揃え
                                for (size_t j = 0; j < val_len; j++) {
                                    formatted_value[j] = value[j];
                                }
                                for (int j = 0; j < padding; j++) {
                                    formatted_value[val_len + j] = ' ';
                                }
                            } else if (align_char == '>') {
                                // 右揃え
                                for (int j = 0; j < padding; j++) {
                                    formatted_value[j] = ' ';
                                }
                                for (size_t j = 0; j < val_len; j++) {
                                    formatted_value[padding + j] = value[j];
                                }
                            } else if (align_char == '^') {
                                // 中央揃え
                                int left_padding = padding / 2;
                                int right_padding = padding - left_padding;

                                for (int j = 0; j < left_padding; j++) {
                                    formatted_value[j] = ' ';
                                }
                                for (size_t j = 0; j < val_len; j++) {
                                    formatted_value[left_padding + j] = value[j];
                                }
                                for (int j = 0; j < right_padding; j++) {
                                    formatted_value[left_padding + val_len + j] = ' ';
                                }
                            }

                            formatted_value[width] = '\0';
                        } else {
                            // 幅が文字列長より小さい場合はそのまま
                            formatted_value = (char*)value;
                        }
                    } else {
                        // アライメント指定なし
                        formatted_value = (char*)value;
                    }
                } else {
                    // 指定子が短い場合はそのまま
                    formatted_value = (char*)value;
                }

                // フォーマット文字列を作成（{:...}を{}に置き換える）
                char* new_format = (char*)wasm_alloc(fmt_len);
                size_t new_idx = 0;
                for (size_t j = 0; j < i; j++) {
                    new_format[new_idx++] = format[j];
                }
                new_format[new_idx++] = '{';
                new_format[new_idx++] = '}';
                for (size_t j = spec_end + 1; j < fmt_len; j++) {
                    new_format[new_idx++] = format[j];
                }
                new_format[new_idx] = '\0';

                return cm_format_replace(new_format, formatted_value);
            }
        }
    }

    // フォーマット指定子がない場合はそのまま
    return cm_format_replace(format, value);
}

// WASI entry point
void _start() {
    // Call the main function
    int exit_code = main();
    // Exit with the code from main
    __wasi_proc_exit(exit_code);
}