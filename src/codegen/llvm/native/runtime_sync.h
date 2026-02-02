// runtime_sync.h - Cm Synchronization Primitives Runtime
// Atomic operations only (mutex/rwlock/channel are in std::sync via libc)

#ifndef CM_RUNTIME_SYNC_H
#define CM_RUNTIME_SYNC_H

#include <stddef.h>
#include <stdint.h>

// ============================================================
// Atomic Operations (LLVM intrinsics)
// ============================================================

// Atomic i64
int64_t cm_atomic_load_i64(int64_t* ptr);
void cm_atomic_store_i64(int64_t* ptr, int64_t value);
int64_t cm_atomic_fetch_add_i64(int64_t* ptr, int64_t value);
int64_t cm_atomic_fetch_sub_i64(int64_t* ptr, int64_t value);
int cm_atomic_compare_exchange_i64(int64_t* ptr, int64_t expected, int64_t desired);

// Atomic i32
int32_t cm_atomic_load_i32(int32_t* ptr);
void cm_atomic_store_i32(int32_t* ptr, int32_t value);
int32_t cm_atomic_fetch_add_i32(int32_t* ptr, int32_t value);

#endif  // CM_RUNTIME_SYNC_H
