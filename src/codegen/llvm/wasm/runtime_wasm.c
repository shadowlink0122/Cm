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

// Forward declaration of main function
int main();

// WASI imports
__attribute__((import_module("wasi_snapshot_preview1"), import_name("proc_exit")))
void __wasi_proc_exit(int exit_code) __attribute__((noreturn));

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
// WASI Entry Point
// ============================================================
void _start() {
    int exit_code = main();
    __wasi_proc_exit(exit_code);
}