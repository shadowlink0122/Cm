// Cm Language Runtime - Slice Functions (LLVM Backend)
// Dynamic array (slice) operations

#include "../../common/runtime_alloc.h"

#include <stdint.h>

#ifndef CM_NO_STD
#include <stdio.h>
#endif

// Forward declaration for cm_qsort (defined in runtime_format.c)
void cm_qsort(void* base, size_t nmemb, size_t size, int (*compar)(const void*, const void*));

// Forward declaration for cm_memcpy
void* cm_memcpy(void* dest, const void* src, size_t n);

// Forward declaration for cm_memmove
void* cm_memmove(void* dest, const void* src, size_t n);

// スライス構造体
typedef struct {
    void* data;         // データポインタ
    int64_t len;        // 現在の要素数
    int64_t cap;        // 容量
    int64_t elem_size;  // 要素サイズ
} CmSlice;

// スライスを作成
void* cm_slice_new(int64_t elem_size, int64_t initial_cap) {
    CmSlice* slice = (CmSlice*)cm_alloc(sizeof(CmSlice));
    if (!slice)
        return NULL;

    slice->elem_size = elem_size;
    slice->len = 0;
    slice->cap = initial_cap > 0 ? initial_cap : 4;
    slice->data = cm_alloc(slice->cap * elem_size);

    return slice;
}

// スライスを解放
void cm_slice_free(void* slice_ptr) {
    if (!slice_ptr)
        return;
    CmSlice* slice = (CmSlice*)slice_ptr;
    if (slice->data) {
        cm_dealloc(slice->data);
    }
    cm_dealloc(slice);
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

    void* new_data = cm_realloc(slice->data, new_cap * slice->elem_size);
    if (new_data) {
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

// ポインタ要素をpush
void cm_slice_push_ptr(void* slice_ptr, void* value) {
    if (!slice_ptr)
        return;
    CmSlice* slice = (CmSlice*)slice_ptr;

    if (slice->len >= slice->cap) {
        cm_slice_grow(slice);
    }

    void** data = (void**)slice->data;
    data[slice->len] = value;
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

// ポインタ要素をpop
void* cm_slice_pop_ptr(void* slice_ptr) {
    if (!slice_ptr)
        return NULL;
    CmSlice* slice = (CmSlice*)slice_ptr;

    if (slice->len == 0)
        return NULL;

    slice->len--;
    void** data = (void**)slice->data;
    return data[slice->len];
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

// ポインタ要素を取得
void* cm_slice_get_ptr(void* slice_ptr, int64_t index) {
    if (!slice_ptr)
        return NULL;
    CmSlice* slice = (CmSlice*)slice_ptr;

    if (index < 0 || index >= slice->len)
        return NULL;

    void** data = (void**)slice->data;
    return data[index];
}

// 要素を削除
void cm_slice_delete(void* slice_ptr, int64_t index) {
    if (!slice_ptr)
        return;
    CmSlice* slice = (CmSlice*)slice_ptr;

    if (index < 0 || index >= slice->len)
        return;

    // index以降の要素を前にシフト
    char* data = (char*)slice->data;
    memmove(data + index * slice->elem_size, data + (index + 1) * slice->elem_size,
            (slice->len - index - 1) * slice->elem_size);

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

// クロージャ用関数ポインタ型（キャプチャ値を第一引数として受け取る）
typedef int32_t (*MapFnI32Closure)(int64_t, int32_t);
typedef int64_t (*MapFnI64Closure)(int64_t, int64_t);
typedef int8_t (*FilterFnI32Closure)(int64_t, int32_t);
typedef int8_t (*FilterFnI64Closure)(int64_t, int64_t);

// map: i32配列に関数を適用し、新しいスライスを返す
void* __builtin_array_map(void* arr_ptr, int64_t size, void* fn_ptr) {
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
void* __builtin_array_map_closure(void* arr_ptr, int64_t size, void* fn_ptr, int64_t capture) {
    if (!arr_ptr || !fn_ptr || size <= 0)
        return NULL;

    MapFnI32Closure fn = (MapFnI32Closure)fn_ptr;
    int32_t* arr = (int32_t*)arr_ptr;

    CmSlice* result = (CmSlice*)cm_slice_new(sizeof(int32_t), size);
    if (!result)
        return NULL;

    int32_t* result_data = (int32_t*)result->data;
    for (int64_t i = 0; i < size; i++) {
        result_data[i] = fn(capture, arr[i]);
    }
    result->len = size;

    return result;
}

// map: i64配列に関数を適用
void* __builtin_array_map_i64(void* arr_ptr, int64_t size, void* fn_ptr) {
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
void* __builtin_array_map_i64_closure(void* arr_ptr, int64_t size, void* fn_ptr, int64_t capture) {
    if (!arr_ptr || !fn_ptr || size <= 0)
        return NULL;

    MapFnI64Closure fn = (MapFnI64Closure)fn_ptr;
    int64_t* arr = (int64_t*)arr_ptr;

    CmSlice* result = (CmSlice*)cm_slice_new(sizeof(int64_t), size);
    if (!result)
        return NULL;

    int64_t* result_data = (int64_t*)result->data;
    for (int64_t i = 0; i < size; i++) {
        result_data[i] = fn(capture, arr[i]);
    }
    result->len = size;

    return result;
}

// filter: i32配列から条件を満たす要素を抽出
void* __builtin_array_filter(void* arr_ptr, int64_t size, void* fn_ptr) {
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
void* __builtin_array_filter_closure(void* arr_ptr, int64_t size, void* fn_ptr, int64_t capture) {
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
        if (fn(capture, arr[i])) {
            result_data[count++] = arr[i];
        }
    }
    result->len = count;

    return result;
}

// filter: i64配列から条件を満たす要素を抽出
void* __builtin_array_filter_i64(void* arr_ptr, int64_t size, void* fn_ptr) {
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
void* __builtin_array_filter_i64_closure(void* arr_ptr, int64_t size, void* fn_ptr,
                                         int64_t capture) {
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
        if (fn(capture, arr[i])) {
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
// 内側の配列がCmSlice*の場合、それを返す
// そうでない場合（固定配列）、一時的なラッパーを作成
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
    CmSlice* new_slice = (CmSlice*)cm_current_allocator->alloc(sizeof(CmSlice));
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

    CmSlice* result = (CmSlice*)cm_alloc(sizeof(CmSlice));
    if (!result)
        return NULL;

    if (slice->len <= 0 || !slice->data) {
        result->data = NULL;
        result->len = 0;
        result->cap = 0;
        result->elem_size = slice->elem_size;
        return result;
    }

    result->data = cm_alloc(slice->len * slice->elem_size);
    if (!result->data) {
        cm_dealloc(result);
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

// スライスをソートしたコピーを返す（int32用）
static int cm_compare_i32(const void* a, const void* b) {
    int32_t ia = *(const int32_t*)a;
    int32_t ib = *(const int32_t*)b;
    return (ia > ib) - (ia < ib);
}

void* cm_slice_sort(void* slice_ptr) {
    if (!slice_ptr)
        return NULL;
    CmSlice* slice = (CmSlice*)slice_ptr;

    CmSlice* result = (CmSlice*)cm_alloc(sizeof(CmSlice));
    if (!result)
        return NULL;

    if (slice->len <= 0 || !slice->data) {
        result->data = NULL;
        result->len = 0;
        result->cap = 0;
        result->elem_size = slice->elem_size;
        return result;
    }

    result->data = cm_alloc(slice->len * slice->elem_size);
    if (!result->data) {
        cm_dealloc(result);
        return NULL;
    }

    // データをコピー
    memcpy(result->data, slice->data, slice->len * slice->elem_size);

    // ソート（現在はint32のみ対応）
    if (slice->elem_size == sizeof(int32_t)) {
        qsort(result->data, slice->len, slice->elem_size, cm_compare_i32);
    }

    result->len = slice->len;
    result->cap = slice->len;
    result->elem_size = slice->elem_size;
    return result;
}

// 固定サイズ配列からスライスを作成
void* cm_array_to_slice(void* array_ptr, int64_t len, int64_t elem_size) {
    CmSlice* result = (CmSlice*)cm_alloc(sizeof(CmSlice));
    if (!result)
        return NULL;

    if (!array_ptr || len <= 0) {
        result->data = NULL;
        result->len = 0;
        result->cap = 0;
        result->elem_size = elem_size;
        return result;
    }

    result->data = cm_alloc(len * elem_size);
    if (!result->data) {
        cm_dealloc(result);
        return NULL;
    }

    memcpy(result->data, array_ptr, len * elem_size);
    result->len = len;
    result->cap = len;
    result->elem_size = elem_size;
    return result;
}

// 2次元配列→2次元スライスに変換
// outer_len: 外側の配列長（行数）
// inner_len: 内側の配列長（列数）
// inner_elem_size: 内側要素のサイズ
void* cm_array2d_to_slice2d(void* array_ptr, int64_t outer_len, int64_t inner_len,
                            int64_t inner_elem_size) {
    CmSlice* result = (CmSlice*)cm_alloc(sizeof(CmSlice));
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
    CmSlice* inner_slices = (CmSlice*)cm_alloc(outer_len * sizeof(CmSlice));
    if (!inner_slices) {
        cm_dealloc(result);
        return NULL;
    }

    int64_t row_size = inner_len * inner_elem_size;
    char* src = (char*)array_ptr;

    for (int64_t i = 0; i < outer_len; i++) {
        inner_slices[i].data = cm_alloc(row_size);
        if (!inner_slices[i].data) {
            // ロールバック
            for (int64_t j = 0; j < i; j++) {
                cm_dealloc(inner_slices[j].data);
            }
            cm_dealloc(inner_slices);
            cm_dealloc(result);
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

    CmSlice* result = (CmSlice*)cm_alloc(sizeof(CmSlice));
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
    result->data = cm_alloc(new_len * slice->elem_size);
    if (!result->data) {
        cm_dealloc(result);
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
