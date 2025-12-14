/// @file format_api.h
/// @brief 共通フォーマットAPI宣言
/// 各バックエンドで実装されるフォーマット関数の宣言

#ifndef CM_FORMAT_API_H
#define CM_FORMAT_API_H

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Escape Processing
// ============================================================

/// @brief {{と}}のエスケープを解除
char* cm_unescape_braces(const char* str);
char* cm_format_unescape_braces(const char* str);

// ============================================================
// Type to String Conversion
// ============================================================

char* cm_format_int(int value);
char* cm_format_uint(unsigned int value);
char* cm_format_double(double value);
char* cm_format_double_precision(double value, int precision);
char* cm_format_bool(char value);
char* cm_format_char(char value);

// ============================================================
// Integer Format Variants
// ============================================================

char* cm_format_int_hex(long long value);
char* cm_format_int_HEX(long long value);
char* cm_format_int_binary(long long value);
char* cm_format_int_octal(long long value);

// ============================================================
// Double Format Variants
// ============================================================

char* cm_format_double_exp(double value);
char* cm_format_double_EXP(double value);
char* cm_format_double_scientific(double value, int uppercase);

// ============================================================
// String Utilities
// ============================================================

char* cm_string_concat(const char* left, const char* right);
char* cm_int_to_string(int value);
char* cm_uint_to_string(unsigned int value);
char* cm_char_to_string(char value);
char* cm_bool_to_string(char value);
char* cm_double_to_string(double value);

// ============================================================
// Format Replace Functions
// ============================================================

char* cm_format_replace(const char* format, const char* value);
char* cm_format_replace_int(const char* format, int value);
char* cm_format_replace_uint(const char* format, unsigned int value);
char* cm_format_replace_double(const char* format, double value);
char* cm_format_replace_string(const char* format, const char* value);

// ============================================================
// Format String Functions
// ============================================================

char* cm_format_string_1(const char* fmt, const char* arg1);
char* cm_format_string_2(const char* fmt, const char* arg1, const char* arg2);
char* cm_format_string_3(const char* fmt, const char* arg1, const char* arg2, const char* arg3);
char* cm_format_string_4(const char* fmt, const char* arg1, const char* arg2, const char* arg3,
                         const char* arg4);

// ============================================================
// Panic
// ============================================================

void __cm_panic(const char* message);

#ifdef __cplusplus
}
#endif

#endif  // CM_FORMAT_API_H
