// Cm Language Runtime - Native Platform Implementation
// Uses standard C library functions

#include "../common/runtime_platform.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void* cm_alloc(size_t size) {
    return malloc(size);
}

void cm_free(void* ptr) {
    free(ptr);
}

size_t cm_strlen(const char* str) {
    return str ? strlen(str) : 0;
}

char* cm_strcpy(char* dst, const char* src) {
    return strcpy(dst, src);
}

void cm_write_stdout(const char* str, size_t len) {
    fwrite(str, 1, len, stdout);
}

void cm_write_stderr(const char* str, size_t len) {
    fwrite(str, 1, len, stderr);
}
