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
// 引数付きアセンブリ
// ============================================================

// ポインタ1つを入力（%0 = ptr）
void cm_asm_ptr(const char* code, void* ptr) {
    if (code == NULL || ptr == NULL)
        return;

        // x86_64: movl $imm, (%0) 形式をサポート
#if defined(__x86_64__) || defined(__i386__)
    if (strstr(code, "movl $") && strstr(code, ", (%0)")) {
        // "movl $42, (%0)" -> ストア
        int value = 0;
        sscanf(code, "movl $%d", &value);
        __asm__ volatile("movl %1, (%0)" : : "r"(ptr), "r"(value) : "memory");
    } else if (strstr(code, "movl (%0)")) {
        // "movl (%0), %eax" -> ロード
        int32_t* p = (int32_t*)ptr;
        int32_t val = *p;
        __asm__ volatile("movl %0, %%eax" : : "r"(val) : "eax");
    } else if (strstr(code, "incl (%0)")) {
        __asm__ volatile("incl (%0)" : : "r"(ptr) : "memory");
    } else if (strstr(code, "decl (%0)")) {
        __asm__ volatile("decl (%0)" : : "r"(ptr) : "memory");
    }
#elif defined(__aarch64__)
    if (strstr(code, "str") && strstr(code, "[%0]")) {
        int value = 0;
        sscanf(code, "mov w1, #%d", &value);
        __asm__ volatile("str %w1, [%0]" : : "r"(ptr), "r"(value) : "memory");
    } else if (strstr(code, "ldr") && strstr(code, "[%0]")) {
        int32_t* p = (int32_t*)ptr;
        int32_t val = *p;
        __asm__ volatile("mov w0, %w0" : : "r"(val) : "w0");
    }
#else
    (void)code;
    (void)ptr;
#endif
}

// 値1つを入力（%0 = value）
void cm_asm_val(const char* code, int32_t value) {
    if (code == NULL)
        return;

#if defined(__x86_64__) || defined(__i386__)
    if (strstr(code, "addl") && strstr(code, "%0")) {
        __asm__ volatile("addl %0, %%eax" : : "r"(value) : "eax");
    } else if (strstr(code, "subl") && strstr(code, "%0")) {
        __asm__ volatile("subl %0, %%eax" : : "r"(value) : "eax");
    } else if (strstr(code, "movl") && strstr(code, "%0")) {
        __asm__ volatile("movl %0, %%eax" : : "r"(value) : "eax");
    }
#elif defined(__aarch64__)
    if (strstr(code, "add") && strstr(code, "%0")) {
        __asm__ volatile("add x0, x0, %0" : : "r"((int64_t)value) : "x0");
    } else if (strstr(code, "mov") && strstr(code, "%0")) {
        __asm__ volatile("mov w0, %w0" : : "r"(value) : "w0");
    }
#else
    (void)code;
    (void)value;
#endif
}

// ポインタと値を入力（%0 = ptr, %1 = value）
void cm_asm_ptr_val(const char* code, void* ptr, int32_t value) {
    if (code == NULL || ptr == NULL)
        return;

#if defined(__x86_64__) || defined(__i386__)
    if (strstr(code, "movl %1, (%0)")) {
        __asm__ volatile("movl %1, (%0)" : : "r"(ptr), "r"(value) : "memory");
    } else if (strstr(code, "addl %1, (%0)")) {
        __asm__ volatile("addl %1, (%0)" : : "r"(ptr), "r"(value) : "memory");
    } else if (strstr(code, "xchgl %1, (%0)")) {
        __asm__ volatile("xchgl %1, (%0)" : : "r"(ptr), "r"(value) : "memory");
    }
#elif defined(__aarch64__)
    if (strstr(code, "str %1, [%0]")) {
        __asm__ volatile("str %w1, [%0]" : : "r"(ptr), "r"(value) : "memory");
    }
#else
    // フォールバック: 通常のCでストア
    int32_t* p = (int32_t*)ptr;
    *p = value;
#endif
}

// ポインタから読み取って値を返す
int32_t cm_asm_ptr_ret(const char* code, void* ptr) {
    if (code == NULL || ptr == NULL)
        return 0;

#if defined(__x86_64__) || defined(__i386__)
    if (strstr(code, "movl (%0)")) {
        int32_t result;
        __asm__ volatile("movl (%1), %0" : "=r"(result) : "r"(ptr) : "memory");
        return result;
    }
#elif defined(__aarch64__)
    if (strstr(code, "ldr")) {
        int32_t result;
        __asm__ volatile("ldr %w0, [%1]" : "=r"(result) : "r"(ptr) : "memory");
        return result;
    }
#endif

    // フォールバック
    return *(int32_t*)ptr;
}

// ============================================================
// 基本命令
// ============================================================
void cm_asm_nop(void) {
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("nop");
#elif defined(__aarch64__)
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

// ============================================================
// 演算テスト用関数（インラインアセンブリで実装）
// ============================================================

// アセンブリによる加算
int32_t cm_asm_add(int32_t a, int32_t b) {
#if defined(__x86_64__) || defined(__i386__)
    int32_t result;
    __asm__ volatile("addl %2, %0" : "=r"(result) : "0"(a), "r"(b));
    return result;
#elif defined(__aarch64__)
    int32_t result;
    __asm__ volatile("add %w0, %w1, %w2" : "=r"(result) : "r"(a), "r"(b));
    return result;
#else
    return a + b;
#endif
}

// アセンブリによる乗算
int32_t cm_asm_mul(int32_t a, int32_t b) {
#if defined(__x86_64__) || defined(__i386__)
    int32_t result;
    __asm__ volatile("imull %2, %0" : "=r"(result) : "0"(a), "r"(b));
    return result;
#elif defined(__aarch64__)
    int32_t result;
    __asm__ volatile("mul %w0, %w1, %w2" : "=r"(result) : "r"(a), "r"(b));
    return result;
#else
    return a * b;
#endif
}

// RDTSCの下位32ビットを取得（タイムスタンプカウンタ）
int32_t cm_asm_rdtsc_low(void) {
#if defined(__x86_64__)
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return (int32_t)lo;
#elif defined(__i386__)
    uint32_t lo;
    __asm__ volatile("rdtsc" : "=a"(lo) : : "edx");
    return (int32_t)lo;
#elif defined(__aarch64__)
    // ARM64: CNTVCTを使用
    uint64_t cntpct;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(cntpct));
    return (int32_t)(cntpct & 0xFFFFFFFF);
#else
    return 0;
#endif
}
