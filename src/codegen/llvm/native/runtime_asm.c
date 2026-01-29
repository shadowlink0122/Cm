// runtime_asm.c - インラインアセンブリランタイム関数
// v0.13.0: std::asm モジュールのバックエンド

#include <stdbool.h>

// ============================================================
// NOP命令
// ============================================================
void cm_asm_nop(void) {
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
    __asm__ volatile("nop");
#elif defined(__aarch64__) || defined(_M_ARM64)
    __asm__ volatile("nop");
#else
    // フォールバック: 何もしない
    (void)0;
#endif
}

// ============================================================
// メモリバリア
// ============================================================
void cm_asm_barrier(void) {
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("mfence" ::: "memory");
#elif defined(__aarch64__)
    __asm__ volatile("dmb sy" ::: "memory");
#elif defined(_MSC_VER)
    _ReadWriteBarrier();
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

// ============================================================
// CPU一時停止
// ============================================================
void cm_asm_pause(void) {
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("pause");
#elif defined(__aarch64__)
    __asm__ volatile("yield");
#else
    // フォールバック: 何もしない
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
