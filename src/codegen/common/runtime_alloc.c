// Cm Language Runtime - Allocator Implementation
// Default allocator using standard C library

#include "runtime_alloc.h"

#ifndef CM_NO_STD
#include <stdlib.h>
#endif

// ============================================================
// Default Allocator Implementation (standard mode only)
// ============================================================

#ifndef CM_NO_STD

void* __cm_default_alloc(size_t size) {
    return malloc(size);
}

void __cm_default_dealloc(void* ptr) {
    free(ptr);
}

void* __cm_default_realloc(void* ptr, size_t new_size) {
    return realloc(ptr, new_size);
}

#endif  // !CM_NO_STD

// ============================================================
// Global Allocator State
// ============================================================

// Default allocator instance
static CmAllocator cm_default_alloc_instance = {
    .alloc     = CM_DEFAULT_ALLOC,
    .dealloc   = CM_DEFAULT_DEALLOC,
    .realloc   = CM_DEFAULT_REALLOC,
    .user_data = NULL
};

// Current global allocator (starts as default)
static CmAllocator* cm_current_allocator = &cm_default_alloc_instance;

// ============================================================
// Global Allocator API
// ============================================================

CmAllocator* cm_get_allocator(void) {
    return cm_current_allocator;
}

CmAllocator* cm_set_allocator(CmAllocator* allocator) {
    CmAllocator* previous = cm_current_allocator;
    if (allocator) {
        cm_current_allocator = allocator;
    }
    return previous;
}

void cm_reset_allocator(void) {
    cm_current_allocator = &cm_default_alloc_instance;
}

// ============================================================
// Legacy API Compatibility
// ============================================================
// These functions maintain compatibility with existing code
// that uses cm_alloc/cm_free directly as function calls

// Note: The inline versions in the header are preferred,
// but these provide linkable symbols for external code.

#ifdef CM_PROVIDE_LEGACY_SYMBOLS

void* cm_alloc_impl(size_t size) {
    return cm_get_allocator()->alloc(size);
}

void cm_dealloc_impl(void* ptr) {
    if (ptr) {
        cm_get_allocator()->dealloc(ptr);
    }
}

void* cm_realloc_impl(void* ptr, size_t new_size) {
    return cm_get_allocator()->realloc(ptr, new_size);
}

#endif  // CM_PROVIDE_LEGACY_SYMBOLS
