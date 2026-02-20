// Cm Language Runtime - Print Functions (WASM Backend)
// WASI-based output implementations
// Note: This file is included from runtime_wasm.c AFTER runtime_format.c

#include <stddef.h>
#include <stdint.h>

// WASI imports
__attribute__((import_module("wasi_snapshot_preview1"), import_name("fd_write")))
int __wasi_fd_write(int fd, const void* iovs, size_t iovs_len, size_t* nwritten);

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
    __wasi_fd_write(1, &iov, 1, &nwritten);
}

// wasm_strlen, wasm_int_to_str, wasm_uint_to_str are defined in runtime_format.c

// Forward declarations
char* cm_unescape_braces(const char* str);

// ============================================================
// String Output
// ============================================================
void cm_print_string(const char* str) {
    if (str) {
        size_t len = wasm_strlen(str);
        wasm_write_stdout(str, len);
    }
}

void cm_println_string(const char* str) {
    if (str) {
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

// ============================================================
// Integer Output
// ============================================================
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

// ============================================================
// 64-bit Integer Output (long / ulong)
// ============================================================
// wasm_int64_to_str はruntime_format.c内で定義されている（staticなので再宣言）
static void wasm_ulong_to_str(unsigned long long value, char* buffer, size_t* len) {
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

void cm_print_long(long long value) {
    char buffer[32];
    size_t len;
    wasm_int64_to_str(value, buffer, &len);
    wasm_write_stdout(buffer, len);
}

void cm_println_long(long long value) {
    char buffer[32];
    size_t len;
    wasm_int64_to_str(value, buffer, &len);
    wasm_write_stdout(buffer, len);
    wasm_write_stdout("\n", 1);
}

void cm_print_ulong(unsigned long long value) {
    char buffer[32];
    size_t len;
    wasm_ulong_to_str(value, buffer, &len);
    wasm_write_stdout(buffer, len);
}

void cm_println_ulong(unsigned long long value) {
    char buffer[32];
    size_t len;
    wasm_ulong_to_str(value, buffer, &len);
    wasm_write_stdout(buffer, len);
    wasm_write_stdout("\n", 1);
}

// ============================================================
// Floating Point Output (simplified)
// ============================================================
void cm_print_double(double value) {
    int int_value = (int)value;
    cm_print_int(int_value);
}

void cm_println_double(double value) {
    int int_value = (int)value;
    cm_println_int(int_value);
}

void cm_print_float(float value) {
    int int_value = (int)value;
    cm_print_int(int_value);
}

void cm_println_float(float value) {
    int int_value = (int)value;
    cm_println_int(int_value);
}

// ============================================================
// Boolean Output
// ============================================================
void cm_print_bool(char value) {
    if (value) {
        wasm_write_stdout("true", 4);
    } else {
        wasm_write_stdout("false", 5);
    }
}

void cm_println_bool(char value) {
    if (value) {
        wasm_write_stdout("true", 4);
    } else {
        wasm_write_stdout("false", 5);
    }
    wasm_write_stdout("\n", 1);
}

// ============================================================
// Character Output
// ============================================================
void cm_print_char(char value) {
    wasm_write_stdout(&value, 1);
}

void cm_println_char(char value) {
    wasm_write_stdout(&value, 1);
    wasm_write_stdout("\n", 1);
}
