// Cm Language Runtime - Slice Functions (LLVM Backend)
// 実装本体はnative/wasm共通コア（common/runtime_slice_core.inc）へ一本化した（runtime-builtin-registry 第4段）。
// 本ファイルはnativeのプラットフォームフック（アロケータ・メモリ移動・拡張）の定義のみを持つ

#include "../../common/runtime_alloc.h"

#include <stdint.h>

#ifndef CM_NO_STD
#include <stdio.h>
#endif

// Forward declaration for cm_memcpy
void* cm_memcpy(void* dest, const void* src, size_t n);

// Forward declaration for cm_memmove
void* cm_memmove(void* dest, const void* src, size_t n);

// nativeのプラットフォームフック（確保・解放はアロケータラッパ経由=set_allocator対応。M14）
#define CM_RT_ALLOC(size) cm_alloc(size)
#define CM_RT_FREE(ptr) cm_dealloc(ptr)
#define CM_RT_MEMCPY(dst, src, n) cm_memcpy((dst), (src), (n))
#define CM_RT_MEMMOVE(dst, src, n) cm_memmove((dst), (src), (n))

// 拡張はreallocで実現（old_bytesはrealloc側が旧ブロックサイズを知っているため未使用）
static inline void* cm_rt_grow_native(void* old_ptr, int64_t old_bytes, int64_t new_bytes) {
    (void)old_bytes;
    return cm_realloc(old_ptr, new_bytes);
}
#define CM_RT_GROW(old_ptr, old_bytes, new_bytes) \
    cm_rt_grow_native((old_ptr), (old_bytes), (new_bytes))

#include "../../common/runtime_slice_core.inc"
