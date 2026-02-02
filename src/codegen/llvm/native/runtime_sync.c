// runtime_sync.c - Cm Atomic Operations Runtime
// Only atomic operations (everything else is in std::sync via libc)

#include "runtime_sync.h"

#include <stdatomic.h>

// ============================================================
// Atomic Operations Implementation
// ============================================================

int64_t cm_atomic_load_i64(int64_t* ptr) {
    return atomic_load((_Atomic int64_t*)ptr);
}

void cm_atomic_store_i64(int64_t* ptr, int64_t value) {
    atomic_store((_Atomic int64_t*)ptr, value);
}

int64_t cm_atomic_fetch_add_i64(int64_t* ptr, int64_t value) {
    return atomic_fetch_add((_Atomic int64_t*)ptr, value);
}

int64_t cm_atomic_fetch_sub_i64(int64_t* ptr, int64_t value) {
    return atomic_fetch_sub((_Atomic int64_t*)ptr, value);
}

int cm_atomic_compare_exchange_i64(int64_t* ptr, int64_t expected, int64_t desired) {
    return atomic_compare_exchange_strong((_Atomic int64_t*)ptr, &expected, desired) ? 1 : 0;
}

int32_t cm_atomic_load_i32(int32_t* ptr) {
    return atomic_load((_Atomic int32_t*)ptr);
}

void cm_atomic_store_i32(int32_t* ptr, int32_t value) {
    atomic_store((_Atomic int32_t*)ptr, value);
}

int32_t cm_atomic_fetch_add_i32(int32_t* ptr, int32_t value) {
    return atomic_fetch_add((_Atomic int32_t*)ptr, value);
}
