// ============================================================
// Cmランタイム 配列HOF/検索ビルトインの共通コア（runtime-hof-common-source 第1段）
// native/wasmの二重実装を一本化した共有ソース。プラットフォーム差は以下のフックで注入する:
//   CM_RT_ALLOC(size)        確保（native=cm_alloc / wasm=wasm_alloc）
//   CM_RT_FREE(ptr)          解放（native=cm_dealloc / wasm=cm_free）
//   CM_RT_MEMCPY(dst,src,n)  メモリコピー
// ソートはcm_qsort（各ターゲットのプラットフォーム側定義）、文字列比較はstrcmp（wasmはformat.c内の自前定義）を使う。
// __builtin_array_*の追加・変更は本ファイルで行い、プラットフォーム側への個別実装は禁止する（scripts/check_builtin_signatures.pyが重複定義を検査する）
// ============================================================

// プラットフォーム側定義のソート・文字列比較（wasmは同一TU内定義、nativeはランタイム自前実装/libc）
void cm_qsort(void* base, size_t nmemb, size_t size, int (*compar)(const void*, const void*));
int strcmp(const char* s1, const char* s2);

// ============================================================
// 配列スライス: 新しい配列を作成して要素をコピー
// arr: ソース配列へのポインタ
// elem_size: 各要素のバイトサイズ
// arr_len: ソース配列の長さ
// start: 開始インデックス
// end: 終了インデックス（-1は最後まで）
// out_len: 出力配列の長さを格納するポインタ
void* __builtin_array_slice(void* arr, int64_t elem_size, int64_t arr_len, int64_t start,
                            int64_t end, int64_t* out_len) {
    // スライス構造体のインポート（slice.cと同じ形式）
    typedef struct {
        void* data;
        int64_t len;
        int64_t cap;
        int64_t elem_size;
    } CmSlice;

    if (!arr || elem_size <= 0 || arr_len <= 0) {
        if (out_len)
            *out_len = 0;
        // 空のスライスを返す
        CmSlice* slice = (CmSlice*)CM_RT_ALLOC(sizeof(CmSlice));
        if (slice) {
            slice->data = NULL;
            slice->len = 0;
            slice->cap = 0;
            slice->elem_size = elem_size;
        }
        return slice;
    }

    // Python風: 負のインデックス処理
    if (start < 0) {
        start = arr_len + start;
        if (start < 0)
            start = 0;
    }
    if (end < 0) {
        end = arr_len + end;
        if (end < 0)
            end = 0;
    }
    if (end > arr_len)
        end = arr_len;
    if (start >= end || start >= arr_len) {
        if (out_len)
            *out_len = 0;
        // 空のスライスを返す
        CmSlice* slice = (CmSlice*)CM_RT_ALLOC(sizeof(CmSlice));
        if (slice) {
            slice->data = NULL;
            slice->len = 0;
            slice->cap = 0;
            slice->elem_size = elem_size;
        }
        return slice;
    }

    int64_t slice_len = end - start;

    // CmSlice構造体を作成
    CmSlice* slice = (CmSlice*)CM_RT_ALLOC(sizeof(CmSlice));
    if (!slice) {
        if (out_len)
            *out_len = 0;
        return NULL;
    }

    slice->data = CM_RT_ALLOC((size_t)(slice_len * elem_size));
    if (!slice->data) {
        CM_RT_FREE(slice);
        if (out_len)
            *out_len = 0;
        return NULL;
    }

    CM_RT_MEMCPY(slice->data, (char*)arr + (start * elem_size), (size_t)(slice_len * elem_size));
    slice->len = slice_len;
    slice->cap = slice_len;
    slice->elem_size = elem_size;

    if (out_len)
        *out_len = slice_len;
    return slice;
}

// 整数配列スライス（便利関数）
int64_t* __builtin_array_slice_int(int64_t* arr, int64_t arr_len, int64_t start, int64_t end,
                                   int64_t* out_len) {
    return (int64_t*)__builtin_array_slice(arr, sizeof(int64_t), arr_len, start, end, out_len);
}

// i32配列スライス
int32_t* __builtin_array_slice_i32(int32_t* arr, int64_t arr_len, int64_t start, int64_t end,
                                   int64_t* out_len) {
    return (int32_t*)__builtin_array_slice(arr, sizeof(int32_t), arr_len, start, end, out_len);
}

// ============================================================
// Array Higher-Order Functions
// ============================================================

// スライス対応: サイズが負の場合、データ引数はCmSlice*であり展開する
// （HIRは可変長スライスのHOF呼び出しでサイズ-1を渡す）
typedef struct {
    void* data;
    int64_t len;
    int64_t cap;
    int64_t elem_size;
} CmHofSlice;

#define CM_HOF_UNWRAP(a, s)                    \
    do {                                       \
        if ((s) < 0) {                         \
            CmHofSlice* __cm_s = (CmHofSlice*)(a); \
            (a) = (void*)__cm_s->data;         \
            (s) = __cm_s->len;                 \
        }                                      \
    } while (0)

// forEach: 各要素に対して関数を実行
void __builtin_array_forEach_i64(int64_t* arr, int64_t size, void (*callback)(int64_t)) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !callback)
        return;
    for (int64_t i = 0; i < size; i++) {
        callback(arr[i]);
    }
}

void __builtin_array_forEach_i32(int32_t* arr, int64_t size, void (*callback)(int32_t)) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !callback)
        return;
    for (int64_t i = 0; i < size; i++) {
        callback(arr[i]);
    }
}

// スカラ全幅対応（局所処理調査E系）: forEachのi8/i16/f32/f64変種
void __builtin_array_forEach_i8(int8_t* arr, int64_t size, void (*callback)(int8_t)) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !callback)
        return;
    for (int64_t i = 0; i < size; i++) {
        callback(arr[i]);
    }
}

void __builtin_array_forEach_i16(int16_t* arr, int64_t size, void (*callback)(int16_t)) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !callback)
        return;
    for (int64_t i = 0; i < size; i++) {
        callback(arr[i]);
    }
}

void __builtin_array_forEach_f32(float* arr, int64_t size, void (*callback)(float)) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !callback)
        return;
    for (int64_t i = 0; i < size; i++) {
        callback(arr[i]);
    }
}

void __builtin_array_forEach_f64(double* arr, int64_t size, void (*callback)(double)) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !callback)
        return;
    for (int64_t i = 0; i < size; i++) {
        callback(arr[i]);
    }
}

// reduce: 要素を畳み込み
int64_t __builtin_array_reduce_i64(int64_t* arr, int64_t size,
                                   int64_t (*callback)(int64_t, int64_t), int64_t init) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !callback)
        return init;
    int64_t acc = init;
    for (int64_t i = 0; i < size; i++) {
        acc = callback(acc, arr[i]);
    }
    return acc;
}

int32_t __builtin_array_reduce_i32(int32_t* arr, int64_t size,
                                   int32_t (*callback)(int32_t, int32_t), int32_t init) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !callback)
        return init;
    int32_t acc = init;
    for (int64_t i = 0; i < size; i++) {
        acc = callback(acc, arr[i]);
    }
    return acc;
}

// スカラ全幅対応（局所処理調査E系）: reduceのi8/i16/f32/f64変種（アキュムレータはi8/i16→int32_t、f32→float、f64→double）
int32_t __builtin_array_reduce_i8(int8_t* arr, int64_t size,
                                  int32_t (*callback)(int32_t, int8_t), int32_t init) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !callback)
        return init;
    int32_t acc = init;
    for (int64_t i = 0; i < size; i++) {
        acc = callback(acc, arr[i]);
    }
    return acc;
}

int32_t __builtin_array_reduce_i16(int16_t* arr, int64_t size,
                                   int32_t (*callback)(int32_t, int16_t), int32_t init) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !callback)
        return init;
    int32_t acc = init;
    for (int64_t i = 0; i < size; i++) {
        acc = callback(acc, arr[i]);
    }
    return acc;
}

float __builtin_array_reduce_f32(float* arr, int64_t size,
                                 float (*callback)(float, float), float init) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !callback)
        return init;
    float acc = init;
    for (int64_t i = 0; i < size; i++) {
        acc = callback(acc, arr[i]);
    }
    return acc;
}

double __builtin_array_reduce_f64(double* arr, int64_t size,
                                  double (*callback)(double, double), double init) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !callback)
        return init;
    double acc = init;
    for (int64_t i = 0; i < size; i++) {
        acc = callback(acc, arr[i]);
    }
    return acc;
}

// some: いずれかの要素が条件を満たすか
_Bool __builtin_array_some_i64(int64_t* arr, int64_t size, _Bool (*predicate)(int64_t)) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !predicate)
        return false;
    for (int64_t i = 0; i < size; i++) {
        if (predicate(arr[i]))
            return true;
    }
    return false;
}

_Bool __builtin_array_some_i32(int32_t* arr, int64_t size, _Bool (*predicate)(int32_t)) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !predicate)
        return false;
    for (int64_t i = 0; i < size; i++) {
        if (predicate(arr[i]))
            return true;
    }
    return false;
}

// スカラ全幅対応（局所処理調査E系）: someのi8/i16/f32/f64変種
_Bool __builtin_array_some_i8(int8_t* arr, int64_t size, _Bool (*predicate)(int8_t)) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !predicate)
        return false;
    for (int64_t i = 0; i < size; i++) {
        if (predicate(arr[i]))
            return true;
    }
    return false;
}

_Bool __builtin_array_some_i16(int16_t* arr, int64_t size, _Bool (*predicate)(int16_t)) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !predicate)
        return false;
    for (int64_t i = 0; i < size; i++) {
        if (predicate(arr[i]))
            return true;
    }
    return false;
}

_Bool __builtin_array_some_f32(float* arr, int64_t size, _Bool (*predicate)(float)) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !predicate)
        return false;
    for (int64_t i = 0; i < size; i++) {
        if (predicate(arr[i]))
            return true;
    }
    return false;
}

_Bool __builtin_array_some_f64(double* arr, int64_t size, _Bool (*predicate)(double)) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !predicate)
        return false;
    for (int64_t i = 0; i < size; i++) {
        if (predicate(arr[i]))
            return true;
    }
    return false;
}

// every: すべての要素が条件を満たすか
_Bool __builtin_array_every_i64(int64_t* arr, int64_t size, _Bool (*predicate)(int64_t)) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !predicate)
        return true;
    for (int64_t i = 0; i < size; i++) {
        if (!predicate(arr[i]))
            return false;
    }
    return true;
}

_Bool __builtin_array_every_i32(int32_t* arr, int64_t size, _Bool (*predicate)(int32_t)) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !predicate)
        return true;
    for (int64_t i = 0; i < size; i++) {
        if (!predicate(arr[i]))
            return false;
    }
    return true;
}

// スカラ全幅対応（局所処理調査E系）: everyのi8/i16/f32/f64変種
_Bool __builtin_array_every_i8(int8_t* arr, int64_t size, _Bool (*predicate)(int8_t)) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !predicate)
        return true;
    for (int64_t i = 0; i < size; i++) {
        if (!predicate(arr[i]))
            return false;
    }
    return true;
}

_Bool __builtin_array_every_i16(int16_t* arr, int64_t size, _Bool (*predicate)(int16_t)) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !predicate)
        return true;
    for (int64_t i = 0; i < size; i++) {
        if (!predicate(arr[i]))
            return false;
    }
    return true;
}

_Bool __builtin_array_every_f32(float* arr, int64_t size, _Bool (*predicate)(float)) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !predicate)
        return true;
    for (int64_t i = 0; i < size; i++) {
        if (!predicate(arr[i]))
            return false;
    }
    return true;
}

_Bool __builtin_array_every_f64(double* arr, int64_t size, _Bool (*predicate)(double)) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !predicate)
        return true;
    for (int64_t i = 0; i < size; i++) {
        if (!predicate(arr[i]))
            return false;
    }
    return true;
}

// findIndex: 条件を満たす最初の要素のインデックス
// 戻り幅はCmのint（i32）に統一する（LLVM宣言・closure変種・wasm実装と一致。従来のint64_tはABIの偶然で動作していた）
int32_t __builtin_array_findIndex_i64(int64_t* arr, int64_t size, _Bool (*predicate)(int64_t)) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !predicate)
        return -1;
    for (int64_t i = 0; i < size; i++) {
        if (predicate(arr[i]))
            return i;
    }
    return -1;
}

int32_t __builtin_array_findIndex_i32(int32_t* arr, int64_t size, _Bool (*predicate)(int32_t)) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !predicate)
        return -1;
    for (int64_t i = 0; i < size; i++) {
        if (predicate(arr[i]))
            return (int32_t)i;
    }
    return -1;
}

// スカラ全幅対応（局所処理調査E系）: findIndexのi8/i16/f32/f64変種
int32_t __builtin_array_findIndex_i8(int8_t* arr, int64_t size, _Bool (*predicate)(int8_t)) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !predicate)
        return -1;
    for (int64_t i = 0; i < size; i++) {
        if (predicate(arr[i]))
            return (int32_t)i;
    }
    return -1;
}

int32_t __builtin_array_findIndex_i16(int16_t* arr, int64_t size, _Bool (*predicate)(int16_t)) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !predicate)
        return -1;
    for (int64_t i = 0; i < size; i++) {
        if (predicate(arr[i]))
            return (int32_t)i;
    }
    return -1;
}

int32_t __builtin_array_findIndex_f32(float* arr, int64_t size, _Bool (*predicate)(float)) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !predicate)
        return -1;
    for (int64_t i = 0; i < size; i++) {
        if (predicate(arr[i]))
            return (int32_t)i;
    }
    return -1;
}

int32_t __builtin_array_findIndex_f64(double* arr, int64_t size, _Bool (*predicate)(double)) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !predicate)
        return -1;
    for (int64_t i = 0; i < size; i++) {
        if (predicate(arr[i]))
            return (int32_t)i;
    }
    return -1;
}

// ============================================================
// 高階関数のクロージャ版（C6拡張）
// キャプチャ環境ポインタ（コード生成側が構築するi64スロット配列）を
// コールバックの第一引数として受け取る。シグネチャはキャプチャ数に依存しない
// ============================================================

// 混合幅版: 64bitアキュムレータ×32bit要素（long acc×int[]のreduce。
// 要素幅だけで選ぶと(i32,i32)シグネチャでコールバックが呼ばれ、wasmはcall_indirectの型検査でトラップする）
int64_t __builtin_array_reduce_i32_acc64(int32_t* arr, int64_t size,
                                         int64_t (*callback)(int64_t, int32_t), int64_t init) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !callback)
        return init;
    int64_t acc = init;
    for (int64_t i = 0; i < size; i++) {
        acc = callback(acc, arr[i]);
    }
    return acc;
}

int32_t __builtin_array_reduce_i32_closure(int32_t* arr, int64_t size,
                                           int32_t (*callback)(void*, int32_t, int32_t),
                                           int32_t init, void* env) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !callback)
        return init;
    int32_t acc = init;
    for (int64_t i = 0; i < size; i++) {
        acc = callback(env, acc, arr[i]);
    }
    return acc;
}

int64_t __builtin_array_reduce_i32_acc64_closure(int32_t* arr, int64_t size,
                                                 int64_t (*callback)(void*, int64_t, int32_t),
                                                 int64_t init, void* env) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !callback)
        return init;
    int64_t acc = init;
    for (int64_t i = 0; i < size; i++) {
        acc = callback(env, acc, arr[i]);
    }
    return acc;
}

int64_t __builtin_array_reduce_i64_closure(int64_t* arr, int64_t size,
                                           int64_t (*callback)(void*, int64_t, int64_t),
                                           int64_t init, void* env) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !callback)
        return init;
    int64_t acc = init;
    for (int64_t i = 0; i < size; i++) {
        acc = callback(env, acc, arr[i]);
    }
    return acc;
}

// スカラ全幅対応（局所処理調査E系）: reduceクロージャ版のi8/i16/f32/f64変種
int32_t __builtin_array_reduce_i8_closure(int8_t* arr, int64_t size,
                                          int32_t (*callback)(void*, int32_t, int8_t),
                                          int32_t init, void* env) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !callback)
        return init;
    int32_t acc = init;
    for (int64_t i = 0; i < size; i++) {
        acc = callback(env, acc, arr[i]);
    }
    return acc;
}

int32_t __builtin_array_reduce_i16_closure(int16_t* arr, int64_t size,
                                           int32_t (*callback)(void*, int32_t, int16_t),
                                           int32_t init, void* env) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !callback)
        return init;
    int32_t acc = init;
    for (int64_t i = 0; i < size; i++) {
        acc = callback(env, acc, arr[i]);
    }
    return acc;
}

float __builtin_array_reduce_f32_closure(float* arr, int64_t size,
                                         float (*callback)(void*, float, float),
                                         float init, void* env) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !callback)
        return init;
    float acc = init;
    for (int64_t i = 0; i < size; i++) {
        acc = callback(env, acc, arr[i]);
    }
    return acc;
}

double __builtin_array_reduce_f64_closure(double* arr, int64_t size,
                                          double (*callback)(void*, double, double),
                                          double init, void* env) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !callback)
        return init;
    double acc = init;
    for (int64_t i = 0; i < size; i++) {
        acc = callback(env, acc, arr[i]);
    }
    return acc;
}

void __builtin_array_forEach_i32_closure(int32_t* arr, int64_t size,
                                         void (*callback)(void*, int32_t), void* env) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !callback)
        return;
    for (int64_t i = 0; i < size; i++) {
        callback(env, arr[i]);
    }
}

void __builtin_array_forEach_i64_closure(int64_t* arr, int64_t size,
                                         void (*callback)(void*, int64_t), void* env) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !callback)
        return;
    for (int64_t i = 0; i < size; i++) {
        callback(env, arr[i]);
    }
}

// スカラ全幅対応（局所処理調査E系）: forEachクロージャ版のi8/i16/f32/f64変種
void __builtin_array_forEach_i8_closure(int8_t* arr, int64_t size,
                                        void (*callback)(void*, int8_t), void* env) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !callback)
        return;
    for (int64_t i = 0; i < size; i++) {
        callback(env, arr[i]);
    }
}

void __builtin_array_forEach_i16_closure(int16_t* arr, int64_t size,
                                         void (*callback)(void*, int16_t), void* env) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !callback)
        return;
    for (int64_t i = 0; i < size; i++) {
        callback(env, arr[i]);
    }
}

void __builtin_array_forEach_f32_closure(float* arr, int64_t size,
                                         void (*callback)(void*, float), void* env) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !callback)
        return;
    for (int64_t i = 0; i < size; i++) {
        callback(env, arr[i]);
    }
}

void __builtin_array_forEach_f64_closure(double* arr, int64_t size,
                                         void (*callback)(void*, double), void* env) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !callback)
        return;
    for (int64_t i = 0; i < size; i++) {
        callback(env, arr[i]);
    }
}

_Bool __builtin_array_some_i32_closure(int32_t* arr, int64_t size,
                                       _Bool (*predicate)(void*, int32_t), void* env) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !predicate)
        return 0;
    for (int64_t i = 0; i < size; i++) {
        if (predicate(env, arr[i]))
            return 1;
    }
    return 0;
}

_Bool __builtin_array_some_i64_closure(int64_t* arr, int64_t size,
                                       _Bool (*predicate)(void*, int64_t), void* env) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !predicate)
        return 0;
    for (int64_t i = 0; i < size; i++) {
        if (predicate(env, arr[i]))
            return 1;
    }
    return 0;
}

// スカラ全幅対応（局所処理調査E系）: someクロージャ版のi8/i16/f32/f64変種
_Bool __builtin_array_some_i8_closure(int8_t* arr, int64_t size,
                                      _Bool (*predicate)(void*, int8_t), void* env) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !predicate)
        return 0;
    for (int64_t i = 0; i < size; i++) {
        if (predicate(env, arr[i]))
            return 1;
    }
    return 0;
}

_Bool __builtin_array_some_i16_closure(int16_t* arr, int64_t size,
                                       _Bool (*predicate)(void*, int16_t), void* env) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !predicate)
        return 0;
    for (int64_t i = 0; i < size; i++) {
        if (predicate(env, arr[i]))
            return 1;
    }
    return 0;
}

_Bool __builtin_array_some_f32_closure(float* arr, int64_t size,
                                       _Bool (*predicate)(void*, float), void* env) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !predicate)
        return 0;
    for (int64_t i = 0; i < size; i++) {
        if (predicate(env, arr[i]))
            return 1;
    }
    return 0;
}

_Bool __builtin_array_some_f64_closure(double* arr, int64_t size,
                                       _Bool (*predicate)(void*, double), void* env) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !predicate)
        return 0;
    for (int64_t i = 0; i < size; i++) {
        if (predicate(env, arr[i]))
            return 1;
    }
    return 0;
}

_Bool __builtin_array_every_i32_closure(int32_t* arr, int64_t size,
                                        _Bool (*predicate)(void*, int32_t), void* env) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !predicate)
        return 1;
    for (int64_t i = 0; i < size; i++) {
        if (!predicate(env, arr[i]))
            return 0;
    }
    return 1;
}

_Bool __builtin_array_every_i64_closure(int64_t* arr, int64_t size,
                                        _Bool (*predicate)(void*, int64_t), void* env) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !predicate)
        return 1;
    for (int64_t i = 0; i < size; i++) {
        if (!predicate(env, arr[i]))
            return 0;
    }
    return 1;
}

// スカラ全幅対応（局所処理調査E系）: everyクロージャ版のi8/i16/f32/f64変種
_Bool __builtin_array_every_i8_closure(int8_t* arr, int64_t size,
                                       _Bool (*predicate)(void*, int8_t), void* env) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !predicate)
        return 1;
    for (int64_t i = 0; i < size; i++) {
        if (!predicate(env, arr[i]))
            return 0;
    }
    return 1;
}

_Bool __builtin_array_every_i16_closure(int16_t* arr, int64_t size,
                                        _Bool (*predicate)(void*, int16_t), void* env) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !predicate)
        return 1;
    for (int64_t i = 0; i < size; i++) {
        if (!predicate(env, arr[i]))
            return 0;
    }
    return 1;
}

_Bool __builtin_array_every_f32_closure(float* arr, int64_t size,
                                        _Bool (*predicate)(void*, float), void* env) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !predicate)
        return 1;
    for (int64_t i = 0; i < size; i++) {
        if (!predicate(env, arr[i]))
            return 0;
    }
    return 1;
}

_Bool __builtin_array_every_f64_closure(double* arr, int64_t size,
                                        _Bool (*predicate)(void*, double), void* env) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !predicate)
        return 1;
    for (int64_t i = 0; i < size; i++) {
        if (!predicate(env, arr[i]))
            return 0;
    }
    return 1;
}

int32_t __builtin_array_findIndex_i32_closure(int32_t* arr, int64_t size,
                                              _Bool (*predicate)(void*, int32_t), void* env) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !predicate)
        return -1;
    for (int64_t i = 0; i < size; i++) {
        if (predicate(env, arr[i]))
            return (int32_t)i;
    }
    return -1;
}

int32_t __builtin_array_findIndex_i64_closure(int64_t* arr, int64_t size,
                                              _Bool (*predicate)(void*, int64_t), void* env) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !predicate)
        return -1;
    for (int64_t i = 0; i < size; i++) {
        if (predicate(env, arr[i]))
            return (int32_t)i;
    }
    return -1;
}

// スカラ全幅対応（局所処理調査E系）: findIndexクロージャ版のi8/i16/f32/f64変種
int32_t __builtin_array_findIndex_i8_closure(int8_t* arr, int64_t size,
                                             _Bool (*predicate)(void*, int8_t), void* env) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !predicate)
        return -1;
    for (int64_t i = 0; i < size; i++) {
        if (predicate(env, arr[i]))
            return (int32_t)i;
    }
    return -1;
}

int32_t __builtin_array_findIndex_i16_closure(int16_t* arr, int64_t size,
                                              _Bool (*predicate)(void*, int16_t), void* env) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !predicate)
        return -1;
    for (int64_t i = 0; i < size; i++) {
        if (predicate(env, arr[i]))
            return (int32_t)i;
    }
    return -1;
}

int32_t __builtin_array_findIndex_f32_closure(float* arr, int64_t size,
                                              _Bool (*predicate)(void*, float), void* env) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !predicate)
        return -1;
    for (int64_t i = 0; i < size; i++) {
        if (predicate(env, arr[i]))
            return (int32_t)i;
    }
    return -1;
}

int32_t __builtin_array_findIndex_f64_closure(double* arr, int64_t size,
                                              _Bool (*predicate)(void*, double), void* env) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !predicate)
        return -1;
    for (int64_t i = 0; i < size; i++) {
        if (predicate(env, arr[i]))
            return (int32_t)i;
    }
    return -1;
}

// first: 配列の最初の要素を返す
int32_t __builtin_array_first_i32(int32_t* arr, int64_t size) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || size <= 0)
        return 0;
    return arr[0];
}

int64_t __builtin_array_first_i64(int64_t* arr, int64_t size) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || size <= 0)
        return 0;
    return arr[0];
}

// スカラ全幅対応（局所処理調査E系）: firstのi8/i16/f32/f64変種（空・NULLは0/0.0を返す）
int8_t __builtin_array_first_i8(int8_t* arr, int64_t size) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || size <= 0)
        return 0;
    return arr[0];
}

int16_t __builtin_array_first_i16(int16_t* arr, int64_t size) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || size <= 0)
        return 0;
    return arr[0];
}

float __builtin_array_first_f32(float* arr, int64_t size) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || size <= 0)
        return 0.0f;
    return arr[0];
}

double __builtin_array_first_f64(double* arr, int64_t size) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || size <= 0)
        return 0.0;
    return arr[0];
}

// last: 配列の最後の要素を返す
int32_t __builtin_array_last_i32(int32_t* arr, int64_t size) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || size <= 0)
        return 0;
    return arr[size - 1];
}

int64_t __builtin_array_last_i64(int64_t* arr, int64_t size) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || size <= 0)
        return 0;
    return arr[size - 1];
}

// スカラ全幅対応（局所処理調査E系）: lastのi8/i16/f32/f64変種（空・NULLは0/0.0を返す）
int8_t __builtin_array_last_i8(int8_t* arr, int64_t size) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || size <= 0)
        return 0;
    return arr[size - 1];
}

int16_t __builtin_array_last_i16(int16_t* arr, int64_t size) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || size <= 0)
        return 0;
    return arr[size - 1];
}

float __builtin_array_last_f32(float* arr, int64_t size) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || size <= 0)
        return 0.0f;
    return arr[size - 1];
}

double __builtin_array_last_f64(double* arr, int64_t size) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || size <= 0)
        return 0.0;
    return arr[size - 1];
}

// find: 条件に合う最初の要素を返す（見つからなければデフォルト値）
int32_t __builtin_array_find_i32(int32_t* arr, int64_t size, _Bool (*predicate)(int32_t)) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !predicate)
        return 0;
    for (int64_t i = 0; i < size; i++) {
        if (predicate(arr[i]))
            return arr[i];
    }
    return 0;
}

int64_t __builtin_array_find_i64(int64_t* arr, int64_t size, _Bool (*predicate)(int64_t)) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !predicate)
        return 0;
    for (int64_t i = 0; i < size; i++) {
        if (predicate(arr[i]))
            return arr[i];
    }
    return 0;
}

// スカラ全幅対応（局所処理調査E系）: findのi8/i16/f32/f64変種（不一致は0/0.0を返す）
int8_t __builtin_array_find_i8(int8_t* arr, int64_t size, _Bool (*predicate)(int8_t)) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !predicate)
        return 0;
    for (int64_t i = 0; i < size; i++) {
        if (predicate(arr[i]))
            return arr[i];
    }
    return 0;
}

int16_t __builtin_array_find_i16(int16_t* arr, int64_t size, _Bool (*predicate)(int16_t)) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !predicate)
        return 0;
    for (int64_t i = 0; i < size; i++) {
        if (predicate(arr[i]))
            return arr[i];
    }
    return 0;
}

float __builtin_array_find_f32(float* arr, int64_t size, _Bool (*predicate)(float)) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !predicate)
        return 0.0f;
    for (int64_t i = 0; i < size; i++) {
        if (predicate(arr[i]))
            return arr[i];
    }
    return 0.0f;
}

double __builtin_array_find_f64(double* arr, int64_t size, _Bool (*predicate)(double)) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !predicate)
        return 0.0;
    for (int64_t i = 0; i < size; i++) {
        if (predicate(arr[i]))
            return arr[i];
    }
    return 0.0;
}

// reverse: 配列を逆順にしたコピーを返す（CmSlice*を返す）
typedef struct {
    void* data;
    int64_t len;
    int64_t cap;
    int64_t elem_size;
} CmSlice_fmt;

// sortBy: カスタム比較関数でソートしたコピーを返す
void* __builtin_array_sortBy_i32(int32_t* arr, int64_t size, int (*comparator)(int32_t, int32_t)) {
    CM_HOF_UNWRAP(arr, size);
    CmSlice_fmt* slice = (CmSlice_fmt*)CM_RT_ALLOC(sizeof(CmSlice_fmt));
    if (!slice)
        return NULL;

    if (!arr || size <= 0 || !comparator) {
        slice->data = NULL;
        slice->len = 0;
        slice->cap = 0;
        slice->elem_size = sizeof(int32_t);
        return slice;
    }

    int32_t* result = (int32_t*)CM_RT_ALLOC(size * sizeof(int32_t));
    if (!result) {
        CM_RT_FREE(slice);
        return NULL;
    }

    // コピー
    for (int64_t i = 0; i < size; i++) {
        result[i] = arr[i];
    }

    // ソート（バブルソート）
    for (int64_t i = 0; i < size - 1; i++) {
        for (int64_t j = i + 1; j < size; j++) {
            if (comparator(result[i], result[j]) > 0) {
                int32_t tmp = result[i];
                result[i] = result[j];
                result[j] = tmp;
            }
        }
    }

    slice->data = result;
    slice->len = size;
    slice->cap = size;
    slice->elem_size = sizeof(int32_t);
    return slice;
}

void* __builtin_array_sortBy_i64(int64_t* arr, int64_t size, int (*comparator)(int64_t, int64_t)) {
    CM_HOF_UNWRAP(arr, size);
    CmSlice_fmt* slice = (CmSlice_fmt*)CM_RT_ALLOC(sizeof(CmSlice_fmt));
    if (!slice)
        return NULL;

    if (!arr || size <= 0 || !comparator) {
        slice->data = NULL;
        slice->len = 0;
        slice->cap = 0;
        slice->elem_size = sizeof(int64_t);
        return slice;
    }

    int64_t* result = (int64_t*)CM_RT_ALLOC(size * sizeof(int64_t));
    if (!result) {
        CM_RT_FREE(slice);
        return NULL;
    }

    // コピー
    for (int64_t i = 0; i < size; i++) {
        result[i] = arr[i];
    }

    // ソート（バブルソート）
    for (int64_t i = 0; i < size - 1; i++) {
        for (int64_t j = i + 1; j < size; j++) {
            if (comparator(result[i], result[j]) > 0) {
                int64_t tmp = result[i];
                result[i] = result[j];
                result[j] = tmp;
            }
        }
    }

    slice->data = result;
    slice->len = size;
    slice->cap = size;
    slice->elem_size = sizeof(int64_t);
    return slice;
}

// スカラ全幅対応（局所処理調査E系）: sortByのi8/i16/f32/f64変種（CmSlice_fmt確保+コピー+バブルソート、elem_sizeはsizeof(T)）
#define CM_DEFINE_SORTBY(suffix, ctype)                                                        \
    void* __builtin_array_sortBy_##suffix(ctype* arr, int64_t size,                            \
                                          int (*comparator)(ctype, ctype)) {                   \
        CM_HOF_UNWRAP(arr, size);                                                              \
        CmSlice_fmt* slice = (CmSlice_fmt*)CM_RT_ALLOC(sizeof(CmSlice_fmt));                   \
        if (!slice)                                                                            \
            return NULL;                                                                       \
        if (!arr || size <= 0 || !comparator) {                                                \
            slice->data = NULL;                                                                \
            slice->len = 0;                                                                    \
            slice->cap = 0;                                                                    \
            slice->elem_size = sizeof(ctype);                                                  \
            return slice;                                                                      \
        }                                                                                      \
        ctype* result = (ctype*)CM_RT_ALLOC(size * sizeof(ctype));                             \
        if (!result) {                                                                         \
            CM_RT_FREE(slice);                                                                 \
            return NULL;                                                                       \
        }                                                                                      \
        for (int64_t i = 0; i < size; i++) {                                                   \
            result[i] = arr[i];                                                                \
        }                                                                                      \
        for (int64_t i = 0; i < size - 1; i++) {                                               \
            for (int64_t j = i + 1; j < size; j++) {                                           \
                if (comparator(result[i], result[j]) > 0) {                                    \
                    ctype tmp = result[i];                                                     \
                    result[i] = result[j];                                                     \
                    result[j] = tmp;                                                           \
                }                                                                              \
            }                                                                                  \
        }                                                                                      \
        slice->data = result;                                                                  \
        slice->len = size;                                                                     \
        slice->cap = size;                                                                     \
        slice->elem_size = sizeof(ctype);                                                      \
        return slice;                                                                          \
    }

CM_DEFINE_SORTBY(i8, int8_t)
CM_DEFINE_SORTBY(i16, int16_t)
CM_DEFINE_SORTBY(f32, float)
CM_DEFINE_SORTBY(f64, double)
#undef CM_DEFINE_SORTBY

// 汎用sortBy（エイリアス）
void* __builtin_array_sortBy(int32_t* arr, int64_t size, int (*comparator)(int32_t, int32_t)) {
    CM_HOF_UNWRAP(arr, size);
    return __builtin_array_sortBy_i32(arr, size, comparator);
}

void* __builtin_array_reverse_i32(int32_t* arr, int64_t size) {
    CM_HOF_UNWRAP(arr, size);
    CmSlice_fmt* slice = (CmSlice_fmt*)CM_RT_ALLOC(sizeof(CmSlice_fmt));
    if (!slice)
        return NULL;

    if (!arr || size <= 0) {
        slice->data = NULL;
        slice->len = 0;
        slice->cap = 0;
        slice->elem_size = sizeof(int32_t);
        return slice;
    }

    int32_t* result = (int32_t*)CM_RT_ALLOC(size * sizeof(int32_t));
    if (!result) {
        CM_RT_FREE(slice);
        return NULL;
    }
    for (int64_t i = 0; i < size; i++) {
        result[i] = arr[size - 1 - i];
    }

    slice->data = result;
    slice->len = size;
    slice->cap = size;
    slice->elem_size = sizeof(int32_t);
    return slice;
}

void* __builtin_array_reverse_i64(int64_t* arr, int64_t size) {
    CM_HOF_UNWRAP(arr, size);
    CmSlice_fmt* slice = (CmSlice_fmt*)CM_RT_ALLOC(sizeof(CmSlice_fmt));
    if (!slice)
        return NULL;

    if (!arr || size <= 0) {
        slice->data = NULL;
        slice->len = 0;
        slice->cap = 0;
        slice->elem_size = sizeof(int64_t);
        return slice;
    }

    int64_t* result = (int64_t*)CM_RT_ALLOC(size * sizeof(int64_t));
    if (!result) {
        CM_RT_FREE(slice);
        return NULL;
    }
    for (int64_t i = 0; i < size; i++) {
        result[i] = arr[size - 1 - i];
    }

    slice->data = result;
    slice->len = size;
    slice->cap = size;
    slice->elem_size = sizeof(int64_t);
    return slice;
}

// デフォルト版（i32と同じ）
void* __builtin_array_reverse(int32_t* arr, int64_t size) {
    CM_HOF_UNWRAP(arr, size);
    return __builtin_array_reverse_i32(arr, size);
}

// sort: 配列をソートしたコピーを返す（CmSlice*を返す）
static int compare_i32(const void* a, const void* b) {
    int32_t ia = *(const int32_t*)a;
    int32_t ib = *(const int32_t*)b;
    return (ia > ib) - (ia < ib);
}

static int compare_i64(const void* a, const void* b) {
    int64_t ia = *(const int64_t*)a;
    int64_t ib = *(const int64_t*)b;
    return (ia > ib) - (ia < ib);
}

void* __builtin_array_sort_i32(int32_t* arr, int64_t size) {
    CM_HOF_UNWRAP(arr, size);
    CmSlice_fmt* slice = (CmSlice_fmt*)CM_RT_ALLOC(sizeof(CmSlice_fmt));
    if (!slice)
        return NULL;

    if (!arr || size <= 0) {
        slice->data = NULL;
        slice->len = 0;
        slice->cap = 0;
        slice->elem_size = sizeof(int32_t);
        return slice;
    }

    int32_t* result = (int32_t*)CM_RT_ALLOC(size * sizeof(int32_t));
    if (!result) {
        CM_RT_FREE(slice);
        return NULL;
    }
    CM_RT_MEMCPY(result, arr, size * sizeof(int32_t));
    cm_qsort(result, size, sizeof(int32_t), compare_i32);

    slice->data = result;
    slice->len = size;
    slice->cap = size;
    slice->elem_size = sizeof(int32_t);
    return slice;
}

void* __builtin_array_sort_i64(int64_t* arr, int64_t size) {
    CM_HOF_UNWRAP(arr, size);
    CmSlice_fmt* slice = (CmSlice_fmt*)CM_RT_ALLOC(sizeof(CmSlice_fmt));
    if (!slice)
        return NULL;

    if (!arr || size <= 0) {
        slice->data = NULL;
        slice->len = 0;
        slice->cap = 0;
        slice->elem_size = sizeof(int64_t);
        return slice;
    }

    int64_t* result = (int64_t*)CM_RT_ALLOC(size * sizeof(int64_t));
    if (!result) {
        CM_RT_FREE(slice);
        return NULL;
    }
    CM_RT_MEMCPY(result, arr, size * sizeof(int64_t));
    cm_qsort(result, size, sizeof(int64_t), compare_i64);

    slice->data = result;
    slice->len = size;
    slice->cap = size;
    slice->elem_size = sizeof(int64_t);
    return slice;
}

// デフォルト版（i32と同じ）
void* __builtin_array_sort(int32_t* arr, int64_t size) {
    CM_HOF_UNWRAP(arr, size);
    return __builtin_array_sort_i32(arr, size);
}

// indexOf: 要素の位置を検索
int32_t __builtin_array_indexOf_i64(int64_t* arr, int64_t size, int64_t value) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr)
        return -1;
    for (int64_t i = 0; i < size; i++) {
        if (arr[i] == value)
            return (int32_t)i;
    }
    return -1;
}

int32_t __builtin_array_indexOf_i32(int32_t* arr, int64_t size, int32_t value) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr)
        return -1;
    for (int64_t i = 0; i < size; i++) {
        if (arr[i] == value)
            return (int32_t)i;
    }
    return -1;
}

// includes: 要素が含まれているか
_Bool __builtin_array_includes_i64(int64_t* arr, int64_t size, int64_t value) {
    CM_HOF_UNWRAP(arr, size);
    return __builtin_array_indexOf_i64(arr, size, value) >= 0;
}

_Bool __builtin_array_includes_i32(int32_t* arr, int64_t size, int32_t value) {
    CM_HOF_UNWRAP(arr, size);
    return __builtin_array_indexOf_i32(arr, size, value) >= 0;
}

int32_t __builtin_array_indexOf_i8(int8_t* arr, int64_t size, int8_t value) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr)
        return -1;
    for (int64_t i = 0; i < size; i++) {
        if (arr[i] == value) {
            return (int32_t)i;
        }
    }
    return -1;
}

_Bool __builtin_array_includes_i8(int8_t* arr, int64_t size, int8_t value) {
    return __builtin_array_indexOf_i8(arr, size, value) >= 0;
}

int32_t __builtin_array_indexOf_i16(int16_t* arr, int64_t size, int16_t value) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr)
        return -1;
    for (int64_t i = 0; i < size; i++) {
        if (arr[i] == value) {
            return (int32_t)i;
        }
    }
    return -1;
}

_Bool __builtin_array_includes_i16(int16_t* arr, int64_t size, int16_t value) {
    return __builtin_array_indexOf_i16(arr, size, value) >= 0;
}

int32_t __builtin_array_indexOf_f32(float* arr, int64_t size, float value) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr)
        return -1;
    for (int64_t i = 0; i < size; i++) {
        if (arr[i] == value) {
            return (int32_t)i;
        }
    }
    return -1;
}

_Bool __builtin_array_includes_f32(float* arr, int64_t size, float value) {
    return __builtin_array_indexOf_f32(arr, size, value) >= 0;
}

int32_t __builtin_array_indexOf_f64(double* arr, int64_t size, double value) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr)
        return -1;
    for (int64_t i = 0; i < size; i++) {
        if (arr[i] == value) {
            return (int32_t)i;
        }
    }
    return -1;
}

_Bool __builtin_array_includes_f64(double* arr, int64_t size, double value) {
    return __builtin_array_indexOf_f64(arr, size, value) >= 0;
}

// 文字列要素はポインタ値でなく内容（strcmp）で比較する（Z1: ポインタ切り詰め比較でjit/native/wasmが分裂していた）。
// スライスのポインタ要素ストライドはヘッダのelem_sizeに従う（wasm32ではMIR側の格納ストライドとCのchar**幅が一致しないため必須）
int32_t __builtin_array_indexOf_str(char** arr, int64_t size, const char* value) {
    int64_t stride = (int64_t)sizeof(char*);
    if (size < 0) {
        CmHofSlice* __cm_s = (CmHofSlice*)arr;
        arr = (char**)__cm_s->data;
        size = __cm_s->len;
        if (__cm_s->elem_size > 0) {
            stride = __cm_s->elem_size;
        }
    }
    if (!arr)
        return -1;
    for (int64_t i = 0; i < size; i++) {
        char* elem = *(char**)((char*)arr + i * stride);
        if (elem == value) {
            return (int32_t)i;
        }
        if (elem && value && strcmp(elem, value) == 0) {
            return (int32_t)i;
        }
    }
    return -1;
}

_Bool __builtin_array_includes_str(char** arr, int64_t size, const char* value) {
    return __builtin_array_indexOf_str(arr, size, value) >= 0;
}


