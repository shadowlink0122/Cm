// Cm Language Runtime - WASM Platform Implementation
// Uses static memory pool and WASI for I/O

#include "../common/runtime_platform.h"
#include <stdint.h>

// WASI imports
__attribute__((import_module("wasi_snapshot_preview1"), import_name("fd_write")))
int __wasi_fd_write(int fd, const void* iovs, size_t iovs_len, size_t* nwritten);

// Helper structure for WASI fd_write
typedef struct {
    const uint8_t* buf;
    size_t buf_len;
} ciovec_t;

// ============================================================
// Memory Allocator (Static Pool)
// ============================================================
static char memory_pool[65536];  // 64KB
static size_t pool_offset = 0;

void* cm_alloc(size_t size) {
    // Align to 8 bytes
    size = (size + 7) & ~7;
    
    if (pool_offset + size > sizeof(memory_pool)) {
        // Simple GC: reset pool when full
        pool_offset = 0;
    }
    void* ptr = &memory_pool[pool_offset];
    pool_offset += size;
    return ptr;
}

void cm_free(void* ptr) {
    // No-op for static pool allocator
    (void)ptr;
}

size_t cm_strlen(const char* str) {
    if (!str) return 0;
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

char* cm_strcpy(char* dst, const char* src) {
    char* ret = dst;
    while ((*dst++ = *src++));
    return ret;
}

void cm_write_stdout(const char* str, size_t len) {
    ciovec_t iov = {
        .buf = (const uint8_t*)str,
        .buf_len = len
    };
    size_t nwritten;
    __wasi_fd_write(1, &iov, 1, &nwritten);
}

void cm_write_stderr(const char* str, size_t len) {
    ciovec_t iov = {
        .buf = (const uint8_t*)str,
        .buf_len = len
    };
    size_t nwritten;
    __wasi_fd_write(2, &iov, 1, &nwritten);
}
