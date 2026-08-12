// Cm Language Runtime Library for LLVM Backend
// This is the main runtime file that combines all runtime components
//
// Components are split into separate files for maintainability:
// - alloc.c   : Memory allocator abstraction
// - platform.c: Platform-specific I/O
// - print.c   : Output functions (cm_print_*, cm_println_*)
// - format.c  : Formatting functions (cm_format_*, cm_format_replace_*)
// - slice.c   : Slice (dynamic array) functions
// - file.c    : File I/O and stdin input functions
// - io.c      : Low-level POSIX I/O wrapper functions
// - time.c    : Monotonic clock (cm_now_ms for std::core::time)
//
// This file includes all components to create a single compilation unit

// Use optimized memory functions from format.c
#define CM_HAVE_OPTIMIZED_MEM

#include "../../../common/runtime/alloc.c"
#include "../../../common/runtime/file.c"
#include "asm.c"
#include "format.c"
#include "io.c"
#include "time.c"
#include "platform.c"
#include "print.c"
#include "slice.c"
