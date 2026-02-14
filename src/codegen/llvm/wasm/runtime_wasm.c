// Cm Language Runtime Library for WASM
// This is the main runtime file that combines all runtime components for WASM
//
// Components are split into separate files for maintainability:
// - runtime_print.c  : WASI-based output functions
// - runtime_format.c : Formatting functions (allocation, conversion, formatting)
//
// This file includes both components and provides the WASI entry point

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

// Forward declaration of main function
int main();

// WASI imports
__attribute__((import_module("wasi_snapshot_preview1"), import_name("proc_exit"))) void
__wasi_proc_exit(int exit_code) __attribute__((noreturn));

__attribute__((import_module("wasi_snapshot_preview1"), import_name("fd_write"))) int
__wasi_fd_write_direct(int fd, const void* iovs, size_t iovs_len, size_t* nwritten);

// ============================================================
// Memory operations (memcpy, memcmp, memmove, memset)
// These must be defined before including runtime_slice.c
// ============================================================
void* memcpy(void* dest, const void* src, size_t n) {
    char* d = (char*)dest;
    const char* s = (const char*)src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dest;
}

int memcmp(const void* s1, const void* s2, size_t n) {
    const unsigned char* p1 = (const unsigned char*)s1;
    const unsigned char* p2 = (const unsigned char*)s2;
    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) {
            return (int)p1[i] - (int)p2[i];
        }
    }
    return 0;
}

void* memmove(void* dest, const void* src, size_t n) {
    char* d = (char*)dest;
    const char* s = (const char*)src;
    if (d < s) {
        for (size_t i = 0; i < n; i++) {
            d[i] = s[i];
        }
    } else {
        for (size_t i = n; i > 0; i--) {
            d[i - 1] = s[i - 1];
        }
    }
    return dest;
}

void* memset(void* s, int c, size_t n) {
    unsigned char* p = (unsigned char*)s;
    for (size_t i = 0; i < n; i++) {
        p[i] = (unsigned char)c;
    }
    return s;
}

// Include runtime components
#include "runtime_format.c"
#include "runtime_print.c"
#include "runtime_slice.c"

// ============================================================
// Legacy aliases for compatibility
// ============================================================
char* cm_concat_strings(const char* s1, const char* s2) {
    return cm_string_concat(s1, s2);
}

// ============================================================
// libc FFI compatibility functions
// ============================================================

// puts - output string with newline
int puts(const char* s) {
    if (!s)
        return -1;
    cm_println_string(s);
    return 0;
}

// strlen - 標準ライブラリ互換のシグネチャ
size_t strlen(const char* s) {
    if (!s)
        return 0;
    return (size_t)wasm_strlen(s);
}

// printf - simplified implementation
int printf(const char* format, ...) {
    if (!format)
        return -1;

    va_list args;
    va_start(args, format);

    int written = 0;
    const char* p = format;

    while (*p) {
        if (*p == '%' && *(p + 1)) {
            p++;
            switch (*p) {
                case 'd':
                case 'i': {
                    int val = va_arg(args, int);
                    char buffer[32];
                    size_t len;
                    wasm_int_to_str(val, buffer, &len);
                    wasm_write_stdout(buffer, len);
                    written += len;
                    break;
                }
                case 'u': {
                    unsigned int val = va_arg(args, unsigned int);
                    char buffer[32];
                    size_t len;
                    wasm_uint_to_str(val, buffer, &len);
                    wasm_write_stdout(buffer, len);
                    written += len;
                    break;
                }
                case 's': {
                    const char* s = va_arg(args, const char*);
                    if (s) {
                        size_t len = wasm_strlen(s);
                        wasm_write_stdout(s, len);
                        written += len;
                    }
                    break;
                }
                case 'c': {
                    char c = (char)va_arg(args, int);
                    wasm_write_stdout(&c, 1);
                    written++;
                    break;
                }
                case '%':
                    wasm_write_stdout("%", 1);
                    written++;
                    break;
                default:
                    // Unknown format, output as-is
                    wasm_write_stdout("%", 1);
                    wasm_write_stdout(p, 1);
                    written += 2;
                    break;
            }
        } else {
            wasm_write_stdout(p, 1);
            written++;
        }
        p++;
    }

    va_end(args);
    return written;
}

// ============================================================
// Memory allocation (wrapping wasm_alloc from runtime_format.c)
// ============================================================
void* malloc(size_t size) {
    return wasm_alloc(size);
}

void free(void* ptr) {
    // Simple allocator doesn't support free
    (void)ptr;
}

void* calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void* ptr = wasm_alloc(total);
    if (ptr) {
        char* p = (char*)ptr;
        for (size_t i = 0; i < total; i++) {
            p[i] = 0;
        }
    }
    return ptr;
}

void* realloc(void* ptr, size_t size) {
    // Simple implementation: allocate new, copy old data
    // Note: This doesn't actually free the old memory
    void* new_ptr = wasm_alloc(size);
    if (new_ptr && ptr) {
        // Copy minimum possible (we don't know old size)
        char* src = (char*)ptr;
        char* dst = (char*)new_ptr;
        for (size_t i = 0; i < size; i++) {
            dst[i] = src[i];
        }
    }
    return new_ptr;
}

// ============================================================
// WASI Entry Point
// ============================================================
void _start() {
    int exit_code = main();
    __wasi_proc_exit(exit_code);
}