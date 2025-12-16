// Cm Language Runtime - Print Functions (LLVM Backend)
// Platform-specific output implementations

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../common/runtime_common.h"

// Forward declaration
char* cm_unescape_braces(const char* str);

// ============================================================
// String Output
// ============================================================
void cm_print_string(const char* str) {
    if (str) {
        printf("%s", str);
    }
}

void cm_println_string(const char* str) {
    if (str) {
        char* unescaped = cm_unescape_braces(str);
        if (unescaped) {
            printf("%s\n", unescaped);
            free(unescaped);
        } else {
            printf("%s\n", str);
        }
    } else {
        printf("\n");
    }
}

// ============================================================
// Integer Output
// ============================================================
void cm_print_int(int value) {
    printf("%d", value);
}

void cm_println_int(int value) {
    printf("%d\n", value);
}

void cm_print_uint(unsigned int value) {
    printf("%u", value);
}

void cm_println_uint(unsigned int value) {
    printf("%u\n", value);
}

// ============================================================
// Floating Point Output
// ============================================================
void cm_print_double(double value) {
    if (value == (int)value) {
        printf("%d", (int)value);
    } else {
        printf("%g", value);
    }
}

void cm_println_double(double value) {
    if (value == (int)value) {
        printf("%d\n", (int)value);
    } else {
        printf("%g\n", value);
    }
}

// ============================================================
// Boolean Output
// ============================================================
void cm_print_bool(char value) {
    printf("%s", value ? "true" : "false");
}

void cm_println_bool(char value) {
    printf("%s\n", value ? "true" : "false");
}

// ============================================================
// Character Output
// ============================================================
void cm_print_char(char value) {
    printf("%c", value);
}

void cm_println_char(char value) {
    printf("%c\n", value);
}
