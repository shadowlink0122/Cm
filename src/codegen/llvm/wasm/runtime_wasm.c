// Cm Language Runtime Library for WASM
// This is the main runtime file that combines all runtime components for WASM
//
// Components are split into separate files for maintainability:
// - runtime_print.c  : WASI-based output functions
// - runtime_format.c : Formatting functions (allocation, conversion, formatting)
//
// This file includes both components and provides the WASI entry point

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

// Forward declaration of main function
int main();

// WASI imports
__attribute__((import_module("wasi_snapshot_preview1"), import_name("proc_exit")))
void __wasi_proc_exit(int exit_code) __attribute__((noreturn));

__attribute__((import_module("wasi_snapshot_preview1"), import_name("fd_write")))
int __wasi_fd_write_direct(int fd, const void* iovs, size_t iovs_len, size_t* nwritten);

// Include runtime components
#include "runtime_format.c"
#include "runtime_print.c"

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
    if (!s) return -1;
    cm_println_string(s);
    return 0;
}

// strlen - returns uint64_t to match Cm's usize type
uint64_t strlen(const char* s) {
    if (!s) return 0;
    return (uint64_t)wasm_strlen(s);
}

// printf - simplified implementation
int printf(const char* format, ...) {
    if (!format) return -1;
    
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
void* malloc(uint64_t size) {
    return wasm_alloc((size_t)size);
}

void free(void* ptr) {
    // Simple allocator doesn't support free
    (void)ptr;
}

void* calloc(uint64_t nmemb, uint64_t size) {
    size_t total = (size_t)(nmemb * size);
    void* ptr = wasm_alloc(total);
    if (ptr) {
        char* p = (char*)ptr;
        for (size_t i = 0; i < total; i++) {
            p[i] = 0;
        }
    }
    return ptr;
}

void* realloc(void* ptr, uint64_t size) {
    // Simple implementation: allocate new, copy old data
    // Note: This doesn't actually free the old memory
    void* new_ptr = wasm_alloc((size_t)size);
    if (new_ptr && ptr) {
        // Copy minimum possible (we don't know old size)
        char* src = (char*)ptr;
        char* dst = (char*)new_ptr;
        for (size_t i = 0; i < (size_t)size; i++) {
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