// Cm Language Runtime - Platform Abstraction Layer
// Abstract interface for memory and string operations

#ifndef CM_RUNTIME_PLATFORM_H
#define CM_RUNTIME_PLATFORM_H

#include "runtime_alloc.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Memory Allocation
// ============================================================
// Memory allocation is now handled by runtime_alloc.h
// Use cm_alloc(), cm_dealloc(), cm_realloc() from there.
//
// For backwards compatibility, cm_free is aliased to cm_dealloc:
#define cm_free(ptr) cm_dealloc(ptr)

// ============================================================
// String Operations
// ============================================================
size_t cm_strlen(const char* str);
char* cm_strcpy(char* dst, const char* src);
char* cm_strncpy(char* dst, const char* src, size_t n);
char* cm_strcat(char* dst, const char* src);
int cm_strcmp(const char* s1, const char* s2);
int cm_strncmp(const char* s1, const char* s2, size_t n);
char* cm_strchr(const char* s, int c);
char* cm_strstr(const char* haystack, const char* needle);

// ============================================================
// I/O Operations
// ============================================================
void cm_write_stdout(const char* str, size_t len);
void cm_write_stderr(const char* str, size_t len);

// ============================================================
// Memory Operations
// ============================================================

// Note: Optimized versions with alignment are in runtime_format.c
// These are simple fallback implementations
#ifndef CM_HAVE_OPTIMIZED_MEM

static inline void* cm_memcpy(void* dst, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dst;
}

static inline void* cm_memset(void* dst, int value, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    for (size_t i = 0; i < n; i++) {
        d[i] = (unsigned char)value;
    }
    return dst;
}

static inline void* cm_memmove(void* dst, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;

    if (d < s || d >= s + n) {
        // Forward copy
        for (size_t i = 0; i < n; i++) {
            d[i] = s[i];
        }
    } else {
        // Backward copy (overlap case)
        for (size_t i = n; i > 0; i--) {
            d[i - 1] = s[i - 1];
        }
    }
    return dst;
}

#endif  // CM_HAVE_OPTIMIZED_MEM

// ============================================================
// String Utilities
// ============================================================

// Duplicate string (allocates new memory)
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
