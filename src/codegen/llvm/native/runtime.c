// Cm Language Runtime Library for LLVM Backend
// This is the main runtime file that combines all runtime components
// 
// Components are split into separate files for maintainability:
// - runtime_alloc.c   : Memory allocator abstraction
// - runtime_platform.c: Platform-specific I/O
// - runtime_print.c   : Output functions (cm_print_*, cm_println_*)
// - runtime_format.c  : Formatting functions (cm_format_*, cm_format_replace_*)
// - runtime_slice.c   : Slice (dynamic array) functions
//
// This file includes all components to create a single compilation unit

// Use optimized memory functions from runtime_format.c
#define CM_HAVE_OPTIMIZED_MEM

#include "../../common/runtime_alloc.c"
#include "runtime_platform.c"
#include "runtime_format.c"
#include "runtime_print.c"
#include "runtime_slice.c"
