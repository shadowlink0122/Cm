// Cm Language Runtime - Native Platform Implementation
// Uses standard C library functions

#include "../../common/runtime_platform.h"

#ifndef CM_NO_STD
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#endif

// ============================================================
// String Operations
// ============================================================

#ifndef CM_NO_STD

size_t cm_strlen(const char* str) {
    return str ? strlen(str) : 0;
}

char* cm_strcpy(char* dst, const char* src) {
    return strcpy(dst, src);
}

char* cm_strncpy(char* dst, const char* src, size_t n) {
    return strncpy(dst, src, n);
}

char* cm_strcat(char* dst, const char* src) {
    return strcat(dst, src);
}

// Note: cm_strcmp and cm_strncmp are implemented in runtime_format.c
// as no_std-compatible versions

#endif  // !CM_NO_STD

// ============================================================
// I/O Operations
// ============================================================

#ifndef CM_NO_STD

void cm_write_stdout(const char* str, size_t len) {
    fwrite(str, 1, len, stdout);
}

void cm_write_stderr(const char* str, size_t len) {
    fwrite(str, 1, len, stderr);
}

#endif  // !CM_NO_STD
