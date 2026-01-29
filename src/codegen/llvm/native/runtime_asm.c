// runtime_asm.c - インラインアセンブリランタイム関数
// v0.13.0: std::asm モジュールのバックエンド

#include <stdbool.h>
#include <string.h>

// ============================================================
// 前方宣言
// ============================================================
void cm_asm_nop(void);
void cm_asm_pause(void);
void cm_asm_barrier(void);
void cm_asm_store_barrier(void);
void cm_asm_load_barrier(void);

// ============================================================
// 任意のインラインアセンブリ
// Note: ランタイムでの動的アセンブリ実行は制限あり
// 主にコンパイル時インライン展開用のプレースホルダー
// ============================================================
void cm_asm_inline(const char* code) {
    // ランタイムでの文字列ベースのアセンブリ実行は
    // セキュリティとポータビリティの理由で制限
    // インタプリタモードでは以下の基本命令のみサポート:
    if (code == NULL)
        return;

    // 基本的な命令パターンマッチング
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
    // その他の命令はインタプリタモードでは無視
}

void cm_asm_volatile(const char* code) {
    cm_asm_inline(code);
}

// ============================================================
// NOP命令
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
