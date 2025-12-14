// Cm Language Runtime Library for LLVM Backend
// This is the main runtime file that combines all runtime components
// 
// Components are split into separate files for maintainability:
// - runtime_print.c  : Output functions (cm_print_*, cm_println_*)
// - runtime_format.c : Formatting functions (cm_format_*, cm_format_replace_*)
//
// This file includes both components to create a single compilation unit

#include "runtime_print.c"
#include "runtime_format.c"
