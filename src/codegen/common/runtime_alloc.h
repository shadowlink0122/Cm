// Cm Language Runtime - Allocator Abstraction
// Provides pluggable memory allocator for no_std support
//
// Usage:
//   - Default: Uses malloc/free (or platform-specific allocator)
//   - Custom: Call cm_set_allocator() with custom CmAllocator
//   - no_std: Define CM_NO_STD and implement __cm_heap_* functions

#ifndef CM_RUNTIME_ALLOC_H
#define CM_RUNTIME_ALLOC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Allocator Interface
// ============================================================

/// Allocator function signatures
typedef void* (*CmAllocFn)(size_t size);
typedef void (*CmDeallocFn)(void* ptr);
typedef void* (*CmReallocFn)(void* ptr, size_t new_size);

/// Allocator structure - holds function pointers for memory operations
typedef struct CmAllocator {
    CmAllocFn alloc;      // Allocate memory
    CmDeallocFn dealloc;  // Free memory
    CmReallocFn realloc;  // Resize allocation
    void* user_data;      // Optional user context (for arena allocators, etc.)
} CmAllocator;

// ============================================================
// Default Allocator Setup
// ============================================================

#ifdef CM_NO_STD
// no_std mode: User must provide these implementations
extern void* __cm_heap_alloc(size_t size);
extern void __cm_heap_free(void* ptr);
extern void* __cm_heap_realloc(void* ptr, size_t new_size);

#define CM_DEFAULT_ALLOC __cm_heap_alloc
#define CM_DEFAULT_DEALLOC __cm_heap_free
#define CM_DEFAULT_REALLOC __cm_heap_realloc
#else
// Standard mode: Forward declarations (implemented in runtime_alloc.c)
void* __cm_default_alloc(size_t size);
void __cm_default_dealloc(void* ptr);
void* __cm_default_realloc(void* ptr, size_t new_size);

#define CM_DEFAULT_ALLOC __cm_default_alloc
#define CM_DEFAULT_DEALLOC __cm_default_dealloc
#define CM_DEFAULT_REALLOC __cm_default_realloc
#endif

// ============================================================
// Global Allocator
// ============================================================

/// Get the current global allocator
CmAllocator* cm_get_allocator(void);

/// Set a custom global allocator (returns previous allocator)
CmAllocator* cm_set_allocator(CmAllocator* allocator);

/// Reset to default allocator
void cm_reset_allocator(void);

// ============================================================
// Allocation API (uses global allocator)
// ============================================================

/// Allocate memory
static inline void* cm_alloc(size_t size) {
    return cm_get_allocator()->alloc(size);
}

/// Free memory
static inline void cm_dealloc(void* ptr) {
    if (ptr) {
        cm_get_allocator()->dealloc(ptr);
    }
}

/// Reallocate memory
static inline void* cm_realloc(void* ptr, size_t new_size) {
    return cm_get_allocator()->realloc(ptr, new_size);
}

/// Allocate zeroed memory
static inline void* cm_alloc_zeroed(size_t size) {
    void* ptr = cm_alloc(size);
    if (ptr) {
        // Zero-initialize
        uint8_t* p = (uint8_t*)ptr;
        for (size_t i = 0; i < size; i++) {
            p[i] = 0;
        }
    }
    return ptr;
}

/// Allocate array (size * count, with overflow check)
static inline void* cm_alloc_array(size_t elem_size, size_t count) {
    // Overflow check
    if (count > 0 && elem_size > SIZE_MAX / count) {
        return NULL;
    }
    return cm_alloc(elem_size * count);
}

// ============================================================
// Convenience Macros
// ============================================================

/// Allocate typed memory: CM_NEW(MyStruct)
#define CM_NEW(type) ((type*)cm_alloc(sizeof(type)))

/// Allocate typed array: CM_NEW_ARRAY(int, 10)
#define CM_NEW_ARRAY(type, count) ((type*)cm_alloc_array(sizeof(type), (count)))

/// Free and nullify: CM_DELETE(ptr)
#define CM_DELETE(ptr)   \
    do {                 \
        cm_dealloc(ptr); \
        (ptr) = NULL;    \
    } while (0)

// ============================================================
// Allocator Utilities
// ============================================================

/// Create a default allocator instance
static inline CmAllocator cm_default_allocator(void) {
    CmAllocator alloc = {.alloc = CM_DEFAULT_ALLOC,
                         .dealloc = CM_DEFAULT_DEALLOC,
                         .realloc = CM_DEFAULT_REALLOC,
                         .user_data = NULL};
    return alloc;
}

/// Create a custom allocator
static inline CmAllocator cm_create_allocator(CmAllocFn alloc_fn, CmDeallocFn dealloc_fn,
                                              CmReallocFn realloc_fn, void* user_data) {
    CmAllocator alloc = {
        .alloc = alloc_fn, .dealloc = dealloc_fn, .realloc = realloc_fn, .user_data = user_data};
    return alloc;
}

#ifdef __cplusplus
}
#endif

#endif  // CM_RUNTIME_ALLOC_H
