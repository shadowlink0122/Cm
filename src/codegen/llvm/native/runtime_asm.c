// runtime_asm.c - インラインアセンブリランタイム関数
// v0.13.0: std::asm モジュールのバックエンド

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// 前方宣言
void cm_asm_nop(void);
void cm_asm_pause(void);
void cm_asm_barrier(void);
void cm_asm_store_barrier(void);
void cm_asm_load_barrier(void);

// ============================================================
// 任意のインラインアセンブリ
// ============================================================
void cm_asm_inline(const char* code) {
    if (code == NULL)
        return;

    if (strcmp(code, "nop") == 0) {
        cm_asm_nop();
    } else if (strcmp(code, "pause") == 0 || strcmp(code, "yield") == 0) {
        cm_asm_pause();
    } else if (strcmp(code, "mfence") == 0 || strcmp(code, "dmb sy") == 0) {
        cm_asm_barrier();
    } else if (strcmp(code, "sfence") == 0 || strcmp(code, "dmb st") == 0) {
        cm_asm_store_barrier();
    } else if (strcmp(code, "lfence") == 0 || strcmp(code, "dmb ld") == 0) {
        cm_asm_load_barrier();
    }
}

void cm_asm_volatile(const char* code) {
    cm_asm_inline(code);
}

// ============================================================
// メモリ操作
// ============================================================
int32_t cm_asm_load_i32(int32_t* ptr) {
#if defined(__x86_64__) || defined(__i386__)
    int32_t result;
    __asm__ volatile("movl (%1), %0" : "=r"(result) : "r"(ptr) : "memory");
    return result;
#elif defined(__aarch64__)
    int32_t result;
    __asm__ volatile("ldr %w0, [%1]" : "=r"(result) : "r"(ptr) : "memory");
    return result;
#else
    return *ptr;
#endif
}

void cm_asm_store_i32(int32_t* ptr, int32_t value) {
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("movl %1, (%0)" : : "r"(ptr), "r"(value) : "memory");
#elif defined(__aarch64__)
    __asm__ volatile("str %w1, [%0]" : : "r"(ptr), "r"(value) : "memory");
#else
    *ptr = value;
#endif
}

// ============================================================
// アトミック操作
// ============================================================
int32_t cm_asm_atomic_load_i32(int32_t* ptr) {
#if defined(__x86_64__) || defined(__i386__)
    int32_t result;
    __asm__ volatile("movl (%1), %0" : "=r"(result) : "r"(ptr) : "memory");
    return result;
#elif defined(__aarch64__)
    int32_t result;
    __asm__ volatile("ldar %w0, [%1]" : "=r"(result) : "r"(ptr) : "memory");
    return result;
#else
    return __atomic_load_n(ptr, __ATOMIC_SEQ_CST);
#endif
}

void cm_asm_atomic_store_i32(int32_t* ptr, int32_t value) {
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("xchgl %1, (%0)" : : "r"(ptr), "r"(value) : "memory");
#elif defined(__aarch64__)
    __asm__ volatile("stlr %w1, [%0]" : : "r"(ptr), "r"(value) : "memory");
#else
    __atomic_store_n(ptr, value, __ATOMIC_SEQ_CST);
#endif
}

bool cm_asm_cas_i32(int32_t* ptr, int32_t expected, int32_t desired) {
#if defined(__x86_64__) || defined(__i386__)
    int32_t old_val = expected;
    __asm__ volatile("lock cmpxchgl %2, (%1)"
                     : "+a"(old_val)
                     : "r"(ptr), "r"(desired)
                     : "memory", "cc");
    return old_val == expected;
#elif defined(__aarch64__)
    int32_t old_val;
    int32_t tmp;
    __asm__ volatile(
        "1: ldaxr %w0, [%2]\n"
        "   cmp %w0, %w3\n"
        "   bne 2f\n"
        "   stlxr %w1, %w4, [%2]\n"
        "   cbnz %w1, 1b\n"
        "2:"
        : "=&r"(old_val), "=&r"(tmp)
        : "r"(ptr), "r"(expected), "r"(desired)
        : "memory", "cc");
    return old_val == expected;
#else
    return __atomic_compare_exchange_n(ptr, &expected, desired, false, __ATOMIC_SEQ_CST,
                                       __ATOMIC_SEQ_CST);
#endif
}

// ============================================================
// 基本命令
// ============================================================
void cm_asm_nop(void) {
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
    __asm__ volatile("nop");
#elif defined(__aarch64__) || defined(_M_ARM64)
    __asm__ volatile("nop");
#else
    (void)0;
#endif
}

void cm_asm_barrier(void) {
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("mfence" ::: "memory");
#elif defined(__aarch64__)
    __asm__ volatile("dmb sy" ::: "memory");
#else
    __sync_synchronize();
#endif
}

void cm_asm_store_barrier(void) {
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("sfence" ::: "memory");
#elif defined(__aarch64__)
    __asm__ volatile("dmb st" ::: "memory");
#else
    __sync_synchronize();
#endif
}

void cm_asm_load_barrier(void) {
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("lfence" ::: "memory");
#elif defined(__aarch64__)
    __asm__ volatile("dmb ld" ::: "memory");
#else
    __sync_synchronize();
#endif
}

void cm_asm_pause(void) {
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("pause");
#elif defined(__aarch64__)
    __asm__ volatile("yield");
#else
    (void)0;
#endif
}

// ============================================================
// アーキテクチャ検出
// ============================================================
bool cm_asm_is_x86(void) {
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
    return true;
#else
    return false;
#endif
}

bool cm_asm_is_arm64(void) {
#if defined(__aarch64__) || defined(_M_ARM64)
    return true;
#else
    return false;
#endif
}
