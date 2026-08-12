// Cm Language Runtime - Slice Functions (WASM Backend)
// 実装本体はnative/wasm共通コア（common/runtime/slice_core.inc）へ一本化した（runtime-builtin-registry 第4段）。
// 本ファイルはwasmのプラットフォームフック（バンプアロケータ・手動メモリ移動・alloc+copy+free拡張）とwasm専用のqsort実装のみを持つ

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Forward declarations for WASM memory functions
void* wasm_alloc(size_t size);
void cm_free(void* ptr);
// memcpy and memcmp are defined in core.c before this file is included

// wasmのプラットフォームフック
#define CM_RT_ALLOC(size) wasm_alloc(size)
#define CM_RT_FREE(ptr) cm_free(ptr)
#define CM_RT_MEMCPY(dst, src, n) memcpy((dst), (src), (size_t)(n))

// 重なり許容の移動（wasmはlibcのmemmoveが無いため後方コピーで実装）
static inline void* cm_rt_memmove_wasm(void* dst, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    if (d < s) {
        for (size_t i = 0; i < n; i++) {
            d[i] = s[i];
        }
    } else if (d > s) {
        for (size_t i = n; i > 0; i--) {
            d[i - 1] = s[i - 1];
        }
    }
    return dst;
}
#define CM_RT_MEMMOVE(dst, src, n) cm_rt_memmove_wasm((dst), (src), (size_t)(n))

// 拡張はalloc+copy+free（WASMの汎用reallocは旧ブロックサイズを知らず旧ブロック末尾を超えて読むため、
// 有効バイト数old_bytes=len*elem_sizeだけを確実にコピーする。H11: 旧ブロックはフリーリストへ返す）
static inline void* cm_rt_grow_wasm(void* old_ptr, int64_t old_bytes, int64_t new_bytes) {
    void* new_data = wasm_alloc((size_t)new_bytes);
    if (!new_data) {
        return NULL;
    }
    if (old_ptr && old_bytes > 0) {
        memcpy(new_data, old_ptr, (size_t)old_bytes);
    }
    cm_free(old_ptr);
    return new_data;
}
#define CM_RT_GROW(old_ptr, old_bytes, new_bytes) \
    cm_rt_grow_wasm((old_ptr), (old_bytes), (new_bytes))

// wasm専用のqsort実装（libc非依存。共通コアのcm_slice_sort_withが使用する）
// 自前のクイックソート実装（WASM用）
static void cm_qsort_swap(char* a, char* b, size_t size) {
    for (size_t i = 0; i < size; i++) {
        char tmp = a[i];
        a[i] = b[i];
        b[i] = tmp;
    }
}

static void cm_qsort_impl(char* base, size_t nmemb, size_t size,
                          int (*compar)(const void*, const void*)) {
    if (nmemb <= 1)
        return;

    // 挿入ソートを小さな配列に使用
    if (nmemb <= 10) {
        for (size_t i = 1; i < nmemb; i++) {
            for (size_t j = i; j > 0; j--) {
                if (compar(base + (j - 1) * size, base + j * size) > 0) {
                    cm_qsort_swap(base + (j - 1) * size, base + j * size, size);
                } else {
                    break;
                }
            }
        }
        return;
    }

    // ピボットは中央の要素
    size_t pivot_idx = nmemb / 2;
    cm_qsort_swap(base + pivot_idx * size, base + (nmemb - 1) * size, size);

    size_t store_idx = 0;
    for (size_t i = 0; i < nmemb - 1; i++) {
        if (compar(base + i * size, base + (nmemb - 1) * size) < 0) {
            cm_qsort_swap(base + i * size, base + store_idx * size, size);
            store_idx++;
        }
    }
    cm_qsort_swap(base + store_idx * size, base + (nmemb - 1) * size, size);

    if (store_idx > 0) {
        cm_qsort_impl(base, store_idx, size, compar);
    }
    if (store_idx + 1 < nmemb) {
        cm_qsort_impl(base + (store_idx + 1) * size, nmemb - store_idx - 1, size, compar);
    }
}

void cm_qsort(void* base, size_t nmemb, size_t size, int (*compar)(const void*, const void*)) {
    if (!base || nmemb <= 1 || size == 0 || !compar)
        return;
    cm_qsort_impl((char*)base, nmemb, size, compar);
}

#include "../../../common/runtime/slice_core.inc"
