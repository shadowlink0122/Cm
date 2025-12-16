// Cm Language Runtime Common Header
// Shared declarations for LLVM and WASM backends

#ifndef CM_RUNTIME_COMMON_H
#define CM_RUNTIME_COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Print Functions
// ============================================================
void cm_print_string(const char* str);
void cm_println_string(const char* str);
void cm_print_int(int value);
void cm_println_int(int value);
void cm_print_uint(unsigned int value);
void cm_println_uint(unsigned int value);
void cm_print_double(double value);
void cm_println_double(double value);
void cm_print_bool(char value);
void cm_println_bool(char value);
void cm_print_char(char value);
void cm_println_char(char value);

// ============================================================
// Format Functions - Type to String
// ============================================================
char* cm_format_int(int value);
char* cm_format_uint(unsigned int value);
char* cm_format_double(double value);
char* cm_format_double_precision(double value, int precision);
char* cm_format_bool(char value);
char* cm_format_char(char value);

// Integer format variants
char* cm_format_int_hex(long long value);
char* cm_format_int_HEX(long long value);
char* cm_format_int_binary(long long value);
char* cm_format_int_octal(long long value);

// Double format variants
char* cm_format_double_exp(double value);  // lowercase 'e'
char* cm_format_double_EXP(double value);  // uppercase 'E'
char* cm_format_double_scientific(double value, int uppercase);

// ============================================================
// Format Replace Functions
// ============================================================
char* cm_format_replace(const char* format, const char* value);
char* cm_format_replace_int(const char* format, int value);
char* cm_format_replace_uint(const char* format, unsigned int value);
char* cm_format_replace_double(const char* format, double value);
char* cm_format_replace_string(const char* format, const char* value);

// ============================================================
// String Functions
// ============================================================
char* cm_string_concat(const char* left, const char* right);
char* cm_unescape_braces(const char* str);
char* cm_format_unescape_braces(const char* str);

// Type to string conversion
char* cm_int_to_string(int value);
char* cm_uint_to_string(unsigned int value);
char* cm_char_to_string(char value);
char* cm_bool_to_string(char value);
char* cm_double_to_string(double value);

// Format string (variadic)
char* cm_format_string(const char* format, int num_args, ...);
char* cm_format_string_1(const char* fmt, const char* arg1);
char* cm_format_string_2(const char* fmt, const char* arg1, const char* arg2);
char* cm_format_string_3(const char* fmt, const char* arg1, const char* arg2, const char* arg3);
char* cm_format_string_4(const char* fmt, const char* arg1, const char* arg2, const char* arg3,
                         const char* arg4);

// ============================================================
// Panic / Error Handling
// ============================================================
void __cm_panic(const char* message);

#ifdef __cplusplus
}
#endif

#endif  // CM_RUNTIME_COMMON_H
