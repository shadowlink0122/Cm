// Cm Language Runtime - Platform Abstraction Layer
// Abstract interface for memory and string operations

#ifndef CM_RUNTIME_PLATFORM_H
#define CM_RUNTIME_PLATFORM_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Platform-specific implementations must define:
// - cm_alloc(size_t size) - allocate memory
// - cm_free(void* ptr) - free memory (may be no-op for WASM)
// - cm_strlen(const char* str) - string length
// - cm_strcpy(char* dst, const char* src) - string copy
// - cm_write_stdout(const char* str, size_t len) - write to stdout

// These are defined in platform-specific files:
// - Native: runtime_platform_native.c (uses malloc/free/strlen/printf)
// - WASM: runtime_platform_wasm.c (uses static pool/wasi)

void* cm_alloc(size_t size);
void cm_free(void* ptr);
size_t cm_strlen(const char* str);
char* cm_strcpy(char* dst, const char* src);
void cm_write_stdout(const char* str, size_t len);
void cm_write_stderr(const char* str, size_t len);

// Helper: copy n bytes
static inline void* cm_memcpy(void* dst, const void* src, size_t n) {
    char* d = (char*)dst;
    const char* s = (const char*)src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dst;
}

// Helper: set n bytes to value
static inline void* cm_memset(void* dst, int value, size_t n) {
    char* d = (char*)dst;
    for (size_t i = 0; i < n; i++) {
        d[i] = (char)value;
    }
    return dst;
}

// Helper: allocate and copy string
static inline char* cm_strdup(const char* str) {
    if (!str)
        return NULL;
    size_t len = cm_strlen(str);
    char* result = (char*)cm_alloc(len + 1);
    if (result) {
        cm_strcpy(result, str);
    }
    return result;
}

#ifdef __cplusplus
}
#endif

#endif  // CM_RUNTIME_PLATFORM_H
