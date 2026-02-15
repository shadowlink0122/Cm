// sync_runtime.cpp - Cm 同期プリミティブ stdバッキング実装
// アトミック操作をC++ <atomic>で実装
// Mutex/RwLockはmod.cmからuse libcでpthread関数を直接呼び出し

#include <cstdint>

extern "C" {

// ============================================================
// Atomic Operations - i64
// ============================================================

int64_t cm_atomic_load_i64(int64_t* ptr) {
    return __atomic_load_n(ptr, __ATOMIC_SEQ_CST);
}

void cm_atomic_store_i64(int64_t* ptr, int64_t value) {
    __atomic_store_n(ptr, value, __ATOMIC_SEQ_CST);
}

int64_t cm_atomic_fetch_add_i64(int64_t* ptr, int64_t value) {
    return __atomic_fetch_add(ptr, value, __ATOMIC_SEQ_CST);
}

int64_t cm_atomic_fetch_sub_i64(int64_t* ptr, int64_t value) {
    return __atomic_fetch_sub(ptr, value, __ATOMIC_SEQ_CST);
}

int cm_atomic_compare_exchange_i64(int64_t* ptr, int64_t expected, int64_t desired) {
    return __atomic_compare_exchange_n(ptr, &expected, desired, false, __ATOMIC_SEQ_CST,
                                       __ATOMIC_SEQ_CST)
               ? 1
               : 0;
}

// ============================================================
// Atomic Operations - i32
// ============================================================

int32_t cm_atomic_load_i32(int32_t* ptr) {
    return __atomic_load_n(ptr, __ATOMIC_SEQ_CST);
}

void cm_atomic_store_i32(int32_t* ptr, int32_t value) {
    __atomic_store_n(ptr, value, __ATOMIC_SEQ_CST);
}

int32_t cm_atomic_fetch_add_i32(int32_t* ptr, int32_t value) {
    return __atomic_fetch_add(ptr, value, __ATOMIC_SEQ_CST);
}

int32_t cm_atomic_fetch_sub_i32(int32_t* ptr, int32_t value) {
    return __atomic_fetch_sub(ptr, value, __ATOMIC_SEQ_CST);
}

int cm_atomic_compare_exchange_i32(int32_t* ptr, int32_t* expected, int32_t desired) {
    return __atomic_compare_exchange_n(ptr, expected, desired, false, __ATOMIC_SEQ_CST,
                                       __ATOMIC_SEQ_CST)
               ? 1
               : 0;
}

// ============================================================
// Direct API（cm_プレフィックスなし）
// Cmモジュールからexport extern経由で直接呼び出し
// ラッパー関数不要のためJIT環境で安定動作
// ============================================================

// i32
int32_t atomic_load_i32(int32_t* ptr) {
    return __atomic_load_n(ptr, __ATOMIC_SEQ_CST);
}

void atomic_store_i32(int32_t* ptr, int32_t value) {
    __atomic_store_n(ptr, value, __ATOMIC_SEQ_CST);
}

int32_t atomic_fetch_add_i32(int32_t* ptr, int32_t value) {
    return __atomic_fetch_add(ptr, value, __ATOMIC_SEQ_CST);
}

int32_t atomic_fetch_sub_i32(int32_t* ptr, int32_t value) {
    return __atomic_fetch_sub(ptr, value, __ATOMIC_SEQ_CST);
}

int atomic_compare_exchange_i32(int32_t* ptr, int32_t* expected, int32_t desired) {
    return __atomic_compare_exchange_n(ptr, expected, desired, false, __ATOMIC_SEQ_CST,
                                       __ATOMIC_SEQ_CST)
               ? 1
               : 0;
}

// i64
int64_t atomic_load_i64(int64_t* ptr) {
    return __atomic_load_n(ptr, __ATOMIC_SEQ_CST);
}

void atomic_store_i64(int64_t* ptr, int64_t value) {
    __atomic_store_n(ptr, value, __ATOMIC_SEQ_CST);
}

int64_t atomic_fetch_add_i64(int64_t* ptr, int64_t value) {
    return __atomic_fetch_add(ptr, value, __ATOMIC_SEQ_CST);
}

int64_t atomic_fetch_sub_i64(int64_t* ptr, int64_t value) {
    return __atomic_fetch_sub(ptr, value, __ATOMIC_SEQ_CST);
}

int atomic_compare_exchange_i64(int64_t* ptr, int64_t* expected, int64_t desired) {
    return __atomic_compare_exchange_n(ptr, expected, desired, false, __ATOMIC_SEQ_CST,
                                       __ATOMIC_SEQ_CST)
               ? 1
               : 0;
}

}  // extern "C"
