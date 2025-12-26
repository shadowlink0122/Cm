// Cm Language Runtime - Print Functions (LLVM Backend)
// Platform-specific output implementations

#include "../../common/runtime_alloc.h"
#include "../../common/runtime_platform.h"
#include <stdint.h>

#ifndef CM_NO_STD
#include <stdio.h>
#endif

#include "../../common/runtime_common.h"

// Forward declarations
char* cm_unescape_braces(const char* str);
char* cm_format_int(int value);
char* cm_format_uint(unsigned int value);
char* cm_format_long(long long value);
char* cm_format_ulong(unsigned long long value);
char* cm_format_double(double value);

// 内部ヘルパー: 文字列を標準出力に書き込む
static inline void cm_print_str(const char* str) {
    if (!str) return;
#ifdef CM_NO_STD
    // no_std: 出力は無効化（将来的にカスタムコールバックを実装可能）
    (void)str;
#else
    size_t len = 0;
    const char* p = str;
    while (*p++) len++;
    cm_write_stdout(str, len);
#endif
}

// ============================================================
// String Output
// ============================================================
void cm_print_string(const char* str) {
#ifdef CM_NO_STD
    (void)str;
#else
    if (str) {
        cm_print_str(str);
    }
#endif
}

void cm_println_string(const char* str) {
#ifdef CM_NO_STD
    (void)str;
#else
    if (str) {
        char* unescaped = cm_unescape_braces(str);
        if (unescaped) {
            cm_print_str(unescaped);
            cm_write_stdout("\n", 1);
            cm_dealloc(unescaped);
        } else {
            cm_print_str(str);
            cm_write_stdout("\n", 1);
        }
    } else {
        cm_write_stdout("\n", 1);
    }
#endif
}

// ============================================================
// Integer Output
// ============================================================
void cm_print_int(int value) {
#ifdef CM_NO_STD
    (void)value;
#else
    char* str = cm_format_int(value);
    if (str) {
        cm_print_str(str);
        cm_dealloc(str);
    }
#endif
}

void cm_println_int(int value) {
#ifdef CM_NO_STD
    (void)value;
#else
    char* str = cm_format_int(value);
    if (str) {
        cm_print_str(str);
        cm_write_stdout("\n", 1);
        cm_dealloc(str);
    }
#endif
}

void cm_print_uint(unsigned int value) {
#ifdef CM_NO_STD
    (void)value;
#else
    char* str = cm_format_uint(value);
    if (str) {
        cm_print_str(str);
        cm_dealloc(str);
    }
#endif
}

void cm_println_uint(unsigned int value) {
#ifdef CM_NO_STD
    (void)value;
#else
    char* str = cm_format_uint(value);
    if (str) {
        cm_print_str(str);
        cm_write_stdout("\n", 1);
        cm_dealloc(str);
    }
#endif
}

void cm_print_long(int64_t value) {
#ifdef CM_NO_STD
    (void)value;
#else
    char* str = cm_format_long((long long)value);
    if (str) {
        cm_print_str(str);
        cm_dealloc(str);
    }
#endif
}

void cm_println_long(int64_t value) {
#ifdef CM_NO_STD
    (void)value;
#else
    char* str = cm_format_long((long long)value);
    if (str) {
        cm_print_str(str);
        cm_write_stdout("\n", 1);
        cm_dealloc(str);
    }
#endif
}

void cm_print_ulong(uint64_t value) {
#ifdef CM_NO_STD
    (void)value;
#else
    char* str = cm_format_ulong((unsigned long long)value);
    if (str) {
        cm_print_str(str);
        cm_dealloc(str);
    }
#endif
}

void cm_println_ulong(uint64_t value) {
#ifdef CM_NO_STD
    (void)value;
#else
    char* str = cm_format_ulong((unsigned long long)value);
    if (str) {
        cm_print_str(str);
        cm_write_stdout("\n", 1);
        cm_dealloc(str);
    }
#endif
}

// ============================================================
// Floating Point Output
// ============================================================
void cm_print_double(double value) {
#ifdef CM_NO_STD
    (void)value;
#else
    char* str = cm_format_double(value);
    if (str) {
        cm_print_str(str);
        cm_dealloc(str);
    }
#endif
}

void cm_println_double(double value) {
#ifdef CM_NO_STD
    (void)value;
#else
    char* str = cm_format_double(value);
    if (str) {
        cm_print_str(str);
        cm_write_stdout("\n", 1);
        cm_dealloc(str);
    }
#endif
}

void cm_print_float(float value) {
#ifdef CM_NO_STD
    (void)value;
#else
    cm_print_double((double)value);
#endif
}

void cm_println_float(float value) {
#ifdef CM_NO_STD
    (void)value;
#else
    cm_println_double((double)value);
#endif
}

// ============================================================
// Boolean Output
// ============================================================
void cm_print_bool(char value) {
#ifdef CM_NO_STD
    (void)value;
#else
    cm_print_str(value ? "true" : "false");
#endif
}

void cm_println_bool(char value) {
#ifdef CM_NO_STD
    (void)value;
#else
    cm_print_str(value ? "true" : "false");
    cm_write_stdout("\n", 1);
#endif
}

// ============================================================
// Character Output
// ============================================================
void cm_print_char(char value) {
#ifdef CM_NO_STD
    (void)value;
#else
    cm_write_stdout(&value, 1);
#endif
}

void cm_println_char(char value) {
#ifdef CM_NO_STD
    (void)value;
#else
    cm_write_stdout(&value, 1);
    cm_write_stdout("\n", 1);
#endif
}
