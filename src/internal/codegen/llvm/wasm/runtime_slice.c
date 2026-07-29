// Cm Language Runtime - Slice Functions (WASM Backend)
// Dynamic array (slice) operations

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Forward declarations for WASM memory functions
void* wasm_alloc(size_t size);
void* realloc(void* ptr, size_t size);
void cm_free(void* ptr);
// memcpy and memcmp are defined in runtime_wasm.c before this file is included

// スライス構造体
#ifndef CM_SLICE_DEFINED
#define CM_SLICE_DEFINED
typedef struct {
    void* data;         // データポインタ
    int64_t len;        // 現在の要素数
    int64_t cap;        // 容量
    int64_t elem_size;  // 要素サイズ
} CmSlice;
#endif

// スライスを作成
void* cm_slice_new(int64_t elem_size, int64_t initial_cap) {
    CmSlice* slice = (CmSlice*)wasm_alloc(sizeof(CmSlice));
    if (!slice)
        return NULL;

    slice->elem_size = elem_size;
    slice->len = 0;
    slice->cap = initial_cap > 0 ? initial_cap : 4;
    slice->data = wasm_alloc(slice->cap * elem_size);

    return slice;
}

// スライスを解放
void cm_slice_free(void* slice_ptr) {
    if (!slice_ptr)
        return;
    CmSlice* slice = (CmSlice*)slice_ptr;
    if (slice->data) {
        cm_free(slice->data);
    }
    cm_free(slice);
}

// 長さを取得
int64_t cm_slice_len(void* slice_ptr) {
    if (!slice_ptr)
        return 0;
    CmSlice* slice = (CmSlice*)slice_ptr;
    return slice->len;
}

// 容量を取得
int64_t cm_slice_cap(void* slice_ptr) {
    if (!slice_ptr)
        return 0;
    CmSlice* slice = (CmSlice*)slice_ptr;
    return slice->cap;
}

// 容量を拡張
static void cm_slice_grow(CmSlice* slice) {
    int64_t new_cap = slice->cap * 2;
    if (new_cap < 4)
        new_cap = 4;

    // WASMの汎用realloc（runtime_wasm.c）は旧ブロックのサイズを知らず、
    // 新サイズ分だけ旧ブロックからコピーするため旧ブロック末尾を超えて読み出してしまう。
    // ここでは旧要素数ぶん（len*elem_size）だけを確実にコピーする（cap分の未使用領域は不要）。
    int64_t old_bytes = slice->len * slice->elem_size;
    void* new_data = wasm_alloc((uint64_t)(new_cap * slice->elem_size));
    if (new_data) {
        if (slice->data && old_bytes > 0) {
            char* src = (char*)slice->data;
            char* dst = (char*)new_data;
            for (int64_t i = 0; i < old_bytes; i++) {
                dst[i] = src[i];
            }
        }
        // H11: 旧データブロックをフリーリストへ返して再利用可能にする
        cm_free(slice->data);
        slice->data = new_data;
        slice->cap = new_cap;
    }
}

// i8要素をpush（char/bool用）
void cm_slice_push_i8(void* slice_ptr, int8_t value) {
    if (!slice_ptr)
        return;
    CmSlice* slice = (CmSlice*)slice_ptr;

    if (slice->len >= slice->cap) {
        cm_slice_grow(slice);
    }

    int8_t* data = (int8_t*)slice->data;
    data[slice->len] = value;
    slice->len++;
}

// i16要素をpush（short/ushort用）
void cm_slice_push_i16(void* slice_ptr, int16_t value) {
    if (!slice_ptr)
        return;
    CmSlice* slice = (CmSlice*)slice_ptr;

    if (slice->len >= slice->cap) {
        cm_slice_grow(slice);
    }

    int16_t* data = (int16_t*)slice->data;
    data[slice->len] = value;
    slice->len++;
}

// i32要素をpush
void cm_slice_push_i32(void* slice_ptr, int32_t value) {
    if (!slice_ptr)
        return;
    CmSlice* slice = (CmSlice*)slice_ptr;

    if (slice->len >= slice->cap) {
        cm_slice_grow(slice);
    }

    int32_t* data = (int32_t*)slice->data;
    data[slice->len] = value;
    slice->len++;
}

// i64要素をpush
void cm_slice_push_i64(void* slice_ptr, int64_t value) {
    if (!slice_ptr)
        return;
    CmSlice* slice = (CmSlice*)slice_ptr;

    if (slice->len >= slice->cap) {
        cm_slice_grow(slice);
    }

    int64_t* data = (int64_t*)slice->data;
    data[slice->len] = value;
    slice->len++;
}

// f64要素をpush
void cm_slice_push_f64(void* slice_ptr, double value) {
    if (!slice_ptr)
        return;
    CmSlice* slice = (CmSlice*)slice_ptr;

    if (slice->len >= slice->cap) {
        cm_slice_grow(slice);
    }

    double* data = (double*)slice->data;
    data[slice->len] = value;
    slice->len++;
}

// f32要素をpush（float用）
void cm_slice_push_f32(void* slice_ptr, float value) {
    if (!slice_ptr)
        return;
    CmSlice* slice = (CmSlice*)slice_ptr;

    if (slice->len >= slice->cap) {
        cm_slice_grow(slice);
    }

    float* data = (float*)slice->data;
    data[slice->len] = value;
    slice->len++;
}

// ポインタ要素をpush
// wasmではポインタは4バイトだがスライスのelem_sizeは8。sort/reverse/subslice/delete/equalなど
// elem_size駆動の処理とstrideを一致させるため、8バイトスロットへゼロ拡張して格納する。
void cm_slice_push_ptr(void* slice_ptr, void* value) {
    if (!slice_ptr)
        return;
    CmSlice* slice = (CmSlice*)slice_ptr;

    if (slice->len >= slice->cap) {
        cm_slice_grow(slice);
    }

    unsigned char* slot = (unsigned char*)slice->data + slice->len * slice->elem_size;
    *(uint64_t*)slot = (uint64_t)(uintptr_t)value;
    slice->len++;
}

// スライス要素をpush（スライス構造体をコピー）
void cm_slice_push_slice(void* slice_ptr, void* inner_slice_ptr) {
    if (!slice_ptr || !inner_slice_ptr)
        return;
    CmSlice* slice = (CmSlice*)slice_ptr;
    CmSlice* inner = (CmSlice*)inner_slice_ptr;

    if (slice->len >= slice->cap) {
        cm_slice_grow(slice);
    }

    // 内部スライス構造体をコピー
    CmSlice* data = (CmSlice*)slice->data;
    data[slice->len] = *inner;  // 構造体をコピー
    slice->len++;
}

// blob（可変サイズデータ）をpush（ユニオン型用）
// スライスのelem_sizeに従ってデータをコピー
void cm_slice_push_blob(void* slice_ptr, void* data_ptr) {
    if (!slice_ptr || !data_ptr)
        return;
    CmSlice* slice = (CmSlice*)slice_ptr;

    if (slice->len >= slice->cap) {
        cm_slice_grow(slice);
    }

    // elem_sizeを使用してデータをコピー
    char* dest = (char*)slice->data + (slice->len * slice->elem_size);
    memcpy(dest, data_ptr, (size_t)slice->elem_size);
    slice->len++;
}

// i8要素をpop（char/bool用）
int8_t cm_slice_pop_i8(void* slice_ptr) {
    if (!slice_ptr)
        return 0;
    CmSlice* slice = (CmSlice*)slice_ptr;

    if (slice->len == 0)
        return 0;

    slice->len--;
    int8_t* data = (int8_t*)slice->data;
    return data[slice->len];
}

// i16要素をpop（short/ushort用）
int16_t cm_slice_pop_i16(void* slice_ptr) {
    if (!slice_ptr)
        return 0;
    CmSlice* slice = (CmSlice*)slice_ptr;

    if (slice->len == 0)
        return 0;

    slice->len--;
    int16_t* data = (int16_t*)slice->data;
    return data[slice->len];
}

// i32要素をpop
int32_t cm_slice_pop_i32(void* slice_ptr) {
    if (!slice_ptr)
        return 0;
    CmSlice* slice = (CmSlice*)slice_ptr;

    if (slice->len == 0)
        return 0;

    slice->len--;
    int32_t* data = (int32_t*)slice->data;
    return data[slice->len];
}

// i64要素をpop
int64_t cm_slice_pop_i64(void* slice_ptr) {
    if (!slice_ptr)
        return 0;
    CmSlice* slice = (CmSlice*)slice_ptr;

    if (slice->len == 0)
        return 0;

    slice->len--;
    int64_t* data = (int64_t*)slice->data;
    return data[slice->len];
}

// f64要素をpop
double cm_slice_pop_f64(void* slice_ptr) {
    if (!slice_ptr)
        return 0.0;
    CmSlice* slice = (CmSlice*)slice_ptr;

    if (slice->len == 0)
        return 0.0;

    slice->len--;
    double* data = (double*)slice->data;
    return data[slice->len];
}

// f32要素をpop（float用）
float cm_slice_pop_f32(void* slice_ptr) {
    if (!slice_ptr)
        return 0.0f;
    CmSlice* slice = (CmSlice*)slice_ptr;

    if (slice->len == 0)
        return 0.0f;

    slice->len--;
    float* data = (float*)slice->data;
    return data[slice->len];
}

// ポインタ要素をpop
void* cm_slice_pop_ptr(void* slice_ptr) {
    if (!slice_ptr)
        return NULL;
    CmSlice* slice = (CmSlice*)slice_ptr;

    if (slice->len == 0)
        return NULL;

    slice->len--;
    unsigned char* slot = (unsigned char*)slice->data + slice->len * slice->elem_size;
    return (void*)(uintptr_t) * (uint64_t*)slot;
}

// i8要素を取得（char/bool用）
int8_t cm_slice_get_i8(void* slice_ptr, int64_t index) {
    if (!slice_ptr)
        return 0;
    CmSlice* slice = (CmSlice*)slice_ptr;

    if (index < 0 || index >= slice->len)
        return 0;

    int8_t* data = (int8_t*)slice->data;
    return data[index];
}

// i16要素を取得（short/ushort用）
int16_t cm_slice_get_i16(void* slice_ptr, int64_t index) {
    if (!slice_ptr)
        return 0;
    CmSlice* slice = (CmSlice*)slice_ptr;

    if (index < 0 || index >= slice->len)
        return 0;

    int16_t* data = (int16_t*)slice->data;
    return data[index];
}

// i32要素を取得
int32_t cm_slice_get_i32(void* slice_ptr, int64_t index) {
    if (!slice_ptr)
        return 0;
    CmSlice* slice = (CmSlice*)slice_ptr;

    if (index < 0 || index >= slice->len)
        return 0;

    int32_t* data = (int32_t*)slice->data;
    return data[index];
}

// i64要素を取得
int64_t cm_slice_get_i64(void* slice_ptr, int64_t index) {
    if (!slice_ptr)
        return 0;
    CmSlice* slice = (CmSlice*)slice_ptr;

    if (index < 0 || index >= slice->len)
        return 0;

    int64_t* data = (int64_t*)slice->data;
    return data[index];
}

// f64要素を取得
double cm_slice_get_f64(void* slice_ptr, int64_t index) {
    if (!slice_ptr)
        return 0.0;
    CmSlice* slice = (CmSlice*)slice_ptr;

    if (index < 0 || index >= slice->len)
        return 0.0;

    double* data = (double*)slice->data;
    return data[index];
}

// f32要素を取得（float用）
float cm_slice_get_f32(void* slice_ptr, int64_t index) {
    if (!slice_ptr)
        return 0.0f;
    CmSlice* slice = (CmSlice*)slice_ptr;

    if (index < 0 || index >= slice->len)
        return 0.0f;

    float* data = (float*)slice->data;
    return data[index];
}

// ポインタ要素を取得
void* cm_slice_get_ptr(void* slice_ptr, int64_t index) {
    if (!slice_ptr)
        return NULL;
    CmSlice* slice = (CmSlice*)slice_ptr;

    if (index < 0 || index >= slice->len)
        return NULL;

    unsigned char* slot = (unsigned char*)slice->data + index * slice->elem_size;
    return (void*)(uintptr_t) * (uint64_t*)slot;
}

// 要素を削除
void cm_slice_delete(void* slice_ptr, int64_t index) {
    if (!slice_ptr)
        return;
    CmSlice* slice = (CmSlice*)slice_ptr;

    if (index < 0 || index >= slice->len)
        return;

    // index以降の要素を前にシフト (WASMではmemmoveの代わりに手動でコピー)
    char* data = (char*)slice->data;
    int64_t src_offset = (index + 1) * slice->elem_size;
    int64_t dst_offset = index * slice->elem_size;
    int64_t bytes = (slice->len - index - 1) * slice->elem_size;

    for (int64_t i = 0; i < bytes; i++) {
        data[dst_offset + i] = data[src_offset + i];
    }

    slice->len--;
}

// 全要素をクリア
void cm_slice_clear(void* slice_ptr) {
    if (!slice_ptr)
        return;
    CmSlice* slice = (CmSlice*)slice_ptr;
    slice->len = 0;
}

// ============================================================
// 配列高階関数 (map, filter)
// ============================================================

// 関数ポインタ型定義
typedef int32_t (*MapFnI32)(int32_t);
typedef int64_t (*MapFnI64)(int64_t);
typedef int8_t (*FilterFnI32)(int32_t);
typedef int8_t (*FilterFnI64)(int64_t);

// クロージャ用関数ポインタ型（キャプチャ環境ポインタを第一引数として受け取る。
// C6: キャプチャ数に依存しないシグネチャ。envはコード生成側が構築するi64スロット配列）
typedef int32_t (*MapFnI32Closure)(void*, int32_t);
typedef int64_t (*MapFnI64Closure)(void*, int64_t);
typedef int8_t (*FilterFnI32Closure)(void*, int32_t);
typedef int8_t (*FilterFnI64Closure)(void*, int64_t);

// map: i32配列に関数を適用し、新しいスライスを返す
void* __builtin_array_map(void* arr_ptr, int64_t size, void* fn_ptr) {
    if (size < 0) {
        CmSlice* __cm_s = (CmSlice*)arr_ptr;
        arr_ptr = __cm_s->data;
        size = __cm_s->len;
    }
    if (!arr_ptr || !fn_ptr || size <= 0)
        return NULL;

    MapFnI32 fn = (MapFnI32)fn_ptr;
    int32_t* arr = (int32_t*)arr_ptr;

    // 新しいスライスを作成
    CmSlice* result = (CmSlice*)cm_slice_new(sizeof(int32_t), size);
    if (!result)
        return NULL;

    // 各要素に関数を適用
    int32_t* result_data = (int32_t*)result->data;
    for (int64_t i = 0; i < size; i++) {
        result_data[i] = fn(arr[i]);
    }
    result->len = size;

    return result;
}

// map (クロージャ版): キャプチャ値付きで関数を適用
void* __builtin_array_map_closure(void* arr_ptr, int64_t size, void* fn_ptr, void* env) {
    if (size < 0) {
        CmSlice* __cm_s = (CmSlice*)arr_ptr;
        arr_ptr = __cm_s->data;
        size = __cm_s->len;
    }
    if (!arr_ptr || !fn_ptr || size <= 0)
        return NULL;

    MapFnI32Closure fn = (MapFnI32Closure)fn_ptr;
    int32_t* arr = (int32_t*)arr_ptr;

    CmSlice* result = (CmSlice*)cm_slice_new(sizeof(int32_t), size);
    if (!result)
        return NULL;

    int32_t* result_data = (int32_t*)result->data;
    for (int64_t i = 0; i < size; i++) {
        result_data[i] = fn(env, arr[i]);
    }
    result->len = size;

    return result;
}

// map: i64配列に関数を適用
void* __builtin_array_map_i64(void* arr_ptr, int64_t size, void* fn_ptr) {
    if (size < 0) {
        CmSlice* __cm_s = (CmSlice*)arr_ptr;
        arr_ptr = __cm_s->data;
        size = __cm_s->len;
    }
    if (!arr_ptr || !fn_ptr || size <= 0)
        return NULL;

    MapFnI64 fn = (MapFnI64)fn_ptr;
    int64_t* arr = (int64_t*)arr_ptr;

    CmSlice* result = (CmSlice*)cm_slice_new(sizeof(int64_t), size);
    if (!result)
        return NULL;

    int64_t* result_data = (int64_t*)result->data;
    for (int64_t i = 0; i < size; i++) {
        result_data[i] = fn(arr[i]);
    }
    result->len = size;

    return result;
}

// map_i64 (クロージャ版)
void* __builtin_array_map_i64_closure(void* arr_ptr, int64_t size, void* fn_ptr, void* env) {
    if (size < 0) {
        CmSlice* __cm_s = (CmSlice*)arr_ptr;
        arr_ptr = __cm_s->data;
        size = __cm_s->len;
    }
    if (!arr_ptr || !fn_ptr || size <= 0)
        return NULL;

    MapFnI64Closure fn = (MapFnI64Closure)fn_ptr;
    int64_t* arr = (int64_t*)arr_ptr;

    CmSlice* result = (CmSlice*)cm_slice_new(sizeof(int64_t), size);
    if (!result)
        return NULL;

    int64_t* result_data = (int64_t*)result->data;
    for (int64_t i = 0; i < size; i++) {
        result_data[i] = fn(env, arr[i]);
    }
    result->len = size;

    return result;
}

// filter: i32配列から条件を満たす要素を抽出
void* __builtin_array_filter(void* arr_ptr, int64_t size, void* fn_ptr) {
    if (size < 0) {
        CmSlice* __cm_s = (CmSlice*)arr_ptr;
        arr_ptr = __cm_s->data;
        size = __cm_s->len;
    }
    if (!arr_ptr || !fn_ptr || size <= 0)
        return NULL;

    FilterFnI32 fn = (FilterFnI32)fn_ptr;
    int32_t* arr = (int32_t*)arr_ptr;

    // 新しいスライスを作成（最大でsize個の要素）
    CmSlice* result = (CmSlice*)cm_slice_new(sizeof(int32_t), size);
    if (!result)
        return NULL;

    // 条件を満たす要素のみをコピー
    int32_t* result_data = (int32_t*)result->data;
    int64_t count = 0;
    for (int64_t i = 0; i < size; i++) {
        if (fn(arr[i])) {
            result_data[count++] = arr[i];
        }
    }
    result->len = count;

    return result;
}

// filter (クロージャ版)
void* __builtin_array_filter_closure(void* arr_ptr, int64_t size, void* fn_ptr, void* env) {
    if (size < 0) {
        CmSlice* __cm_s = (CmSlice*)arr_ptr;
        arr_ptr = __cm_s->data;
        size = __cm_s->len;
    }
    if (!arr_ptr || !fn_ptr || size <= 0)
        return NULL;

    FilterFnI32Closure fn = (FilterFnI32Closure)fn_ptr;
    int32_t* arr = (int32_t*)arr_ptr;

    CmSlice* result = (CmSlice*)cm_slice_new(sizeof(int32_t), size);
    if (!result)
        return NULL;

    int32_t* result_data = (int32_t*)result->data;
    int64_t count = 0;
    for (int64_t i = 0; i < size; i++) {
        if (fn(env, arr[i])) {
            result_data[count++] = arr[i];
        }
    }
    result->len = count;

    return result;
}

// filter: i64配列から条件を満たす要素を抽出
void* __builtin_array_filter_i64(void* arr_ptr, int64_t size, void* fn_ptr) {
    if (size < 0) {
        CmSlice* __cm_s = (CmSlice*)arr_ptr;
        arr_ptr = __cm_s->data;
        size = __cm_s->len;
    }
    if (!arr_ptr || !fn_ptr || size <= 0)
        return NULL;

    FilterFnI64 fn = (FilterFnI64)fn_ptr;
    int64_t* arr = (int64_t*)arr_ptr;

    CmSlice* result = (CmSlice*)cm_slice_new(sizeof(int64_t), size);
    if (!result)
        return NULL;

    int64_t* result_data = (int64_t*)result->data;
    int64_t count = 0;
    for (int64_t i = 0; i < size; i++) {
        if (fn(arr[i])) {
            result_data[count++] = arr[i];
        }
    }
    result->len = count;

    return result;
}

// filter_i64 (クロージャ版)
void* __builtin_array_filter_i64_closure(void* arr_ptr, int64_t size, void* fn_ptr, void* env) {
    if (size < 0) {
        CmSlice* __cm_s = (CmSlice*)arr_ptr;
        arr_ptr = __cm_s->data;
        size = __cm_s->len;
    }
    if (!arr_ptr || !fn_ptr || size <= 0)
        return NULL;

    FilterFnI64Closure fn = (FilterFnI64Closure)fn_ptr;
    int64_t* arr = (int64_t*)arr_ptr;

    CmSlice* result = (CmSlice*)cm_slice_new(sizeof(int64_t), size);
    if (!result)
        return NULL;

    int64_t* result_data = (int64_t*)result->data;
    int64_t count = 0;
    for (int64_t i = 0; i < size; i++) {
        if (fn(env, arr[i])) {
            result_data[count++] = arr[i];
        }
    }
    result->len = count;

    return result;
}

// ============================================================
// Slice first/last Functions
// ============================================================

// スライスの最初の要素を取得
int32_t cm_slice_first_i32(void* slice_ptr) {
    if (!slice_ptr)
        return 0;
    CmSlice* slice = (CmSlice*)slice_ptr;
    if (slice->len <= 0 || !slice->data)
        return 0;
    int32_t* data = (int32_t*)slice->data;
    return data[0];
}

int64_t cm_slice_first_i64(void* slice_ptr) {
    if (!slice_ptr)
        return 0;
    CmSlice* slice = (CmSlice*)slice_ptr;
    if (slice->len <= 0 || !slice->data)
        return 0;
    int64_t* data = (int64_t*)slice->data;
    return data[0];
}

// スライスの最後の要素を取得
int32_t cm_slice_last_i32(void* slice_ptr) {
    if (!slice_ptr)
        return 0;
    CmSlice* slice = (CmSlice*)slice_ptr;
    if (slice->len <= 0 || !slice->data)
        return 0;
    int32_t* data = (int32_t*)slice->data;
    return data[slice->len - 1];
}

int64_t cm_slice_last_i64(void* slice_ptr) {
    if (!slice_ptr)
        return 0;
    CmSlice* slice = (CmSlice*)slice_ptr;
    if (slice->len <= 0 || !slice->data)
        return 0;
    int64_t* data = (int64_t*)slice->data;
    return data[slice->len - 1];
}

// ============================================================
// Generic Slice Element Access (for multidimensional arrays)
// ============================================================

// 汎用的な要素へのポインタを取得（elem_sizeを使用）
void* cm_slice_get_element_ptr(void* slice_ptr, int64_t index) {
    if (!slice_ptr)
        return NULL;
    CmSlice* slice = (CmSlice*)slice_ptr;
    if (index < 0 || index >= slice->len || !slice->data)
        return NULL;

    // elem_sizeを使用して正しいオフセットを計算
    return (char*)slice->data + (index * slice->elem_size);
}

// 多次元スライスの内側の配列をラップするスライスを作成
// 内側スライスヘッダへの参照を返す（コピーしない）。
// 添字レシーバ（rows[0].push(x)等）の変異を格納中のヘッダへ反映するために使う（H10第3段）。
// 返したポインタは外側スライスのdataバッファ内を指すため、外側のpush/growで無効化される。
// 取得直後のメソッド呼び出しにのみ使用し、保持しないこと
void* cm_slice_get_subslice_ref(void* slice_ptr, int64_t index) {
    if (!slice_ptr)
        return NULL;
    CmSlice* slice = (CmSlice*)slice_ptr;
    if (index < 0 || index >= slice->len || !slice->data)
        return NULL;
    CmSlice* slice_array = (CmSlice*)slice->data;
    return &slice_array[index];
}

void* cm_slice_get_subslice(void* slice_ptr, int64_t index) {
    if (!slice_ptr)
        return NULL;
    CmSlice* slice = (CmSlice*)slice_ptr;
    if (index < 0 || index >= slice->len || !slice->data)
        return NULL;

    // 要素へのポインタを取得
    // 多次元スライスでは、要素はCmSlice構造体（値）として格納されている
    // elem_size は sizeof(CmSlice) であり、data は CmSlice の配列へのポインタ
    CmSlice* slice_array = (CmSlice*)slice->data;
    CmSlice* elem_ptr = &slice_array[index];

    // 内側のスライスのコピーを作成
    // これにより、元のスライスが破壊されても安全
    // WASM環境ではバンプアロケータを使用
    extern void* wasm_alloc(size_t size);
    CmSlice* new_slice = (CmSlice*)wasm_alloc(sizeof(CmSlice));
    if (!new_slice)
        return NULL;

    new_slice->data = elem_ptr->data;
    new_slice->len = elem_ptr->len;
    new_slice->cap = elem_ptr->cap;
    new_slice->elem_size = elem_ptr->elem_size;

    return new_slice;
}

// 汎用的な最初の要素へのポインタを取得
void* cm_slice_first_ptr(void* slice_ptr) {
    if (!slice_ptr)
        return NULL;
    CmSlice* slice = (CmSlice*)slice_ptr;
    if (slice->len <= 0 || !slice->data)
        return NULL;
    return slice->data;
}

// 汎用的な最後の要素へのポインタを取得
void* cm_slice_last_ptr(void* slice_ptr) {
    if (!slice_ptr)
        return NULL;
    CmSlice* slice = (CmSlice*)slice_ptr;
    if (slice->len <= 0 || !slice->data)
        return NULL;

    return (char*)slice->data + ((slice->len - 1) * slice->elem_size);
}

// 要素サイズを取得
int64_t cm_slice_elem_size(void* slice_ptr) {
    if (!slice_ptr)
        return 0;
    CmSlice* slice = (CmSlice*)slice_ptr;
    return slice->elem_size;
}

// ============================================================
// Slice reverse/sort Functions
// ============================================================

// スライスを逆順にしたコピーを返す
void* cm_slice_reverse(void* slice_ptr) {
    if (!slice_ptr)
        return NULL;
    CmSlice* slice = (CmSlice*)slice_ptr;

    CmSlice* result = (CmSlice*)wasm_alloc(sizeof(CmSlice));
    if (!result)
        return NULL;

    if (slice->len <= 0 || !slice->data) {
        result->data = NULL;
        result->len = 0;
        result->cap = 0;
        result->elem_size = slice->elem_size;
        return result;
    }

    result->data = wasm_alloc(slice->len * slice->elem_size);
    if (!result->data) {
        cm_free(result);
        return NULL;
    }

    // 逆順にコピー
    for (int64_t i = 0; i < slice->len; i++) {
        char* src = (char*)slice->data + ((slice->len - 1 - i) * slice->elem_size);
        char* dst = (char*)result->data + (i * slice->elem_size);
        memcpy(dst, src, slice->elem_size);
    }

    result->len = slice->len;
    result->cap = slice->len;
    result->elem_size = slice->elem_size;
    return result;
}

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

// 要素型ごとの比較関数（符号・浮動小数・文字列を正しく区別する）
#define CM_DEFINE_CMP(suffix, ctype)                                \
    static int cm_slice_cmp_##suffix(const void* a, const void* b) { \
        ctype x = *(const ctype*)a;                                \
        ctype y = *(const ctype*)b;                                \
        return (x > y) - (x < y);                                  \
    }

CM_DEFINE_CMP(i8, int8_t)
CM_DEFINE_CMP(u8, uint8_t)
CM_DEFINE_CMP(i16, int16_t)
CM_DEFINE_CMP(u16, uint16_t)
CM_DEFINE_CMP(i32, int32_t)
CM_DEFINE_CMP(u32, uint32_t)
CM_DEFINE_CMP(i64, int64_t)
CM_DEFINE_CMP(u64, uint64_t)
CM_DEFINE_CMP(f32, float)
CM_DEFINE_CMP(f64, double)
#undef CM_DEFINE_CMP

// 文字列（char*）要素の比較（cm_strcmpに依存せず自前でバイト比較する）
static int cm_slice_cmp_str(const void* a, const void* b) {
    const char* sa = *(const char* const*)a;
    const char* sb = *(const char* const*)b;
    if (sa == sb)
        return 0;
    if (!sa)
        return -1;
    if (!sb)
        return 1;
    while (*sa && (*sa == *sb)) {
        sa++;
        sb++;
    }
    return (int)(unsigned char)*sa - (int)(unsigned char)*sb;
}

// 与えられた比較関数でソートしたコピーを返す共通処理
static void* cm_slice_sort_with(void* slice_ptr, int (*cmp)(const void*, const void*)) {
    if (!slice_ptr)
        return NULL;
    CmSlice* slice = (CmSlice*)slice_ptr;

    CmSlice* result = (CmSlice*)wasm_alloc(sizeof(CmSlice));
    if (!result)
        return NULL;

    if (slice->len <= 0 || !slice->data) {
        result->data = NULL;
        result->len = 0;
        result->cap = 0;
        result->elem_size = slice->elem_size;
        return result;
    }

    result->data = wasm_alloc(slice->len * slice->elem_size);
    if (!result->data) {
        cm_free(result);
        return NULL;
    }

    // データをコピーしてからソート（要素サイズは実際のelem_sizeに従う）
    memcpy(result->data, slice->data, slice->len * slice->elem_size);
    cm_qsort(result->data, slice->len, slice->elem_size, cmp);

    result->len = slice->len;
    result->cap = slice->len;
    result->elem_size = slice->elem_size;
    return result;
}

// 要素型別のソートラッパー（呼び出し側が要素型サフィックスで選択）
#define CM_DEFINE_SORT(suffix)                                  \
    void* cm_slice_sort_##suffix(void* slice_ptr) {            \
        return cm_slice_sort_with(slice_ptr, cm_slice_cmp_##suffix); \
    }

CM_DEFINE_SORT(i8)
CM_DEFINE_SORT(u8)
CM_DEFINE_SORT(i16)
CM_DEFINE_SORT(u16)
CM_DEFINE_SORT(i32)
CM_DEFINE_SORT(u32)
CM_DEFINE_SORT(i64)
CM_DEFINE_SORT(u64)
CM_DEFINE_SORT(f32)
CM_DEFINE_SORT(f64)
CM_DEFINE_SORT(str)
#undef CM_DEFINE_SORT

// 後方互換: 型情報なしのcm_slice_sortはint32として扱う
void* cm_slice_sort(void* slice_ptr) {
    return cm_slice_sort_with(slice_ptr, cm_slice_cmp_i32);
}

// 固定サイズ配列からスライスを作成
void* cm_array_to_slice(void* array_ptr, int64_t len, int64_t elem_size) {
    CmSlice* result = (CmSlice*)wasm_alloc(sizeof(CmSlice));
    if (!result)
        return NULL;

    if (!array_ptr || len <= 0) {
        result->data = NULL;
        result->len = 0;
        result->cap = 0;
        result->elem_size = elem_size;
        return result;
    }

    result->data = wasm_alloc(len * elem_size);
    if (!result->data) {
        cm_free(result);
        return NULL;
    }

    memcpy(result->data, array_ptr, len * elem_size);
    result->len = len;
    result->cap = len;
    result->elem_size = elem_size;
    return result;
}

// 2次元配列→2次元スライスに変換
void* cm_array2d_to_slice2d(void* array_ptr, int64_t outer_len, int64_t inner_len,
                            int64_t inner_elem_size) {
    CmSlice* result = (CmSlice*)wasm_alloc(sizeof(CmSlice));
    if (!result)
        return NULL;

    if (!array_ptr || outer_len <= 0) {
        result->data = NULL;
        result->len = 0;
        result->cap = 0;
        result->elem_size = sizeof(CmSlice);
        return result;
    }

    // 各行をスライスに変換して格納
    CmSlice* inner_slices = (CmSlice*)wasm_alloc(outer_len * sizeof(CmSlice));
    if (!inner_slices) {
        cm_free(result);
        return NULL;
    }

    int64_t row_size = inner_len * inner_elem_size;
    char* src = (char*)array_ptr;

    for (int64_t i = 0; i < outer_len; i++) {
        inner_slices[i].data = wasm_alloc(row_size);
        if (!inner_slices[i].data) {
            for (int64_t j = 0; j < i; j++) {
                cm_free(inner_slices[j].data);
            }
            cm_free(inner_slices);
            cm_free(result);
            return NULL;
        }
        memcpy(inner_slices[i].data, src + (i * row_size), row_size);
        inner_slices[i].len = inner_len;
        inner_slices[i].cap = inner_len;
        inner_slices[i].elem_size = inner_elem_size;
    }

    result->data = inner_slices;
    result->len = outer_len;
    result->cap = outer_len;
    result->elem_size = sizeof(CmSlice);
    return result;
}

// スライスのサブスライスを作成
void* cm_slice_subslice(void* slice_ptr, int64_t start, int64_t end) {
    if (!slice_ptr)
        return NULL;
    CmSlice* slice = (CmSlice*)slice_ptr;

    CmSlice* result = (CmSlice*)wasm_alloc(sizeof(CmSlice));
    if (!result)
        return NULL;

    int64_t len = slice->len;

    // 負のインデックスの処理
    if (start < 0)
        start = len + start;
    if (start < 0)
        start = 0;

    // endが-1なら最後まで
    if (end < 0)
        end = len + end + 1;
    if (end > len)
        end = len;

    if (start >= end || start >= len) {
        result->data = NULL;
        result->len = 0;
        result->cap = 0;
        result->elem_size = slice->elem_size;
        return result;
    }

    int64_t new_len = end - start;
    result->data = wasm_alloc(new_len * slice->elem_size);
    if (!result->data) {
        cm_free(result);
        return NULL;
    }

    memcpy(result->data, (char*)slice->data + start * slice->elem_size, new_len * slice->elem_size);
    result->len = new_len;
    result->cap = new_len;
    result->elem_size = slice->elem_size;
    return result;
}

// 固定配列の等値比較
bool cm_array_equal(void* lhs, void* rhs, int64_t lhs_len, int64_t rhs_len, int64_t elem_size) {
    if (lhs_len != rhs_len)
        return false;
    if (!lhs || !rhs)
        return lhs == rhs;
    return memcmp(lhs, rhs, lhs_len * elem_size) == 0;
}

// 動的スライスの等値比較
bool cm_slice_equal(void* lhs_ptr, void* rhs_ptr) {
    if (!lhs_ptr || !rhs_ptr)
        return lhs_ptr == rhs_ptr;

    CmSlice* lhs = (CmSlice*)lhs_ptr;
    CmSlice* rhs = (CmSlice*)rhs_ptr;

    if (lhs->len != rhs->len)
        return false;
    if (lhs->elem_size != rhs->elem_size)
        return false;
    if (!lhs->data || !rhs->data)
        return lhs->data == rhs->data;

    return memcmp(lhs->data, rhs->data, lhs->len * lhs->elem_size) == 0;
}

// 文字列のコードポイント列をuint(i32)スライスで返す（H9のchars()。native版と同一ロジック）
void* __builtin_string_chars(const char* str) {
    int64_t n = 0;
    if (str) {
        for (const unsigned char* p = (const unsigned char*)str; *p; p++) {
            if ((*p & 0xC0) != 0x80) {
                n++;
            }
        }
    }
    CmSlice* slice = (CmSlice*)cm_slice_new(sizeof(int32_t), n > 0 ? n : 1);
    if (!slice) {
        return NULL;
    }
    int32_t* out = (int32_t*)slice->data;
    int64_t idx = 0;
    if (str) {
        const unsigned char* p = (const unsigned char*)str;
        while (*p && idx < n) {
            unsigned char b0 = *p;
            uint32_t cp = 0;
            int adv = 1;
            if (b0 < 0x80) {
                cp = b0;
            } else if ((b0 & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
                cp = ((uint32_t)(b0 & 0x1F) << 6) | (uint32_t)(p[1] & 0x3F);
                adv = 2;
            } else if ((b0 & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
                cp = ((uint32_t)(b0 & 0x0F) << 12) | ((uint32_t)(p[1] & 0x3F) << 6) |
                     (uint32_t)(p[2] & 0x3F);
                adv = 3;
            } else if ((b0 & 0xF8) == 0xF0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 &&
                       (p[3] & 0xC0) == 0x80) {
                cp = ((uint32_t)(b0 & 0x07) << 18) | ((uint32_t)(p[1] & 0x3F) << 12) |
                     ((uint32_t)(p[2] & 0x3F) << 6) | (uint32_t)(p[3] & 0x3F);
                adv = 4;
            }
            out[idx++] = (int32_t)cp;
            p += adv;
        }
    }
    slice->len = idx;
    return slice;
}
