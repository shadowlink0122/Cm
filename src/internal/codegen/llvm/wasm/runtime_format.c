// Cm Language Runtime - Format Functions (WASM Backend)
// String formatting and conversion implementations for WASM

#include <stddef.h>
#include <stdint.h>

// Boolean type
#ifndef __cplusplus
typedef int bool;
#define true 1
#define false 0
#endif

// ============================================================
// CmSlice Structure (used by array methods)
// ============================================================
#ifndef CM_SLICE_DEFINED
#define CM_SLICE_DEFINED
typedef struct {
    void* data;
    int64_t len;
    int64_t cap;
    int64_t elem_size;
} CmSlice;
#endif

// ============================================================
// String Length Function (defined here to avoid dependency issues)
// ============================================================
static size_t wasm_strlen(const char* str) {
    if (!str) return 0;
    size_t len = 0;
    while (str[len]) len++;
    return len;
}


// ============================================================
// 長さヘッダ付き文字列（H9第4段。native側と同一設計のSDS方式）
// ============================================================
#define CM_STR_MAGIC 0x434D5331u  /* "CMS1" */
#define CM_STR_MAGIC2 0x53315243u /* "S1RC" */

typedef struct {
    uint32_t magic;
    uint32_t byte_len;
    uint32_t magic2;
    uint32_t reserved;
} CmStrHdr;

void* wasm_alloc(size_t size);
void wasm_free(void* ptr);

static char* cm_str_alloc(size_t len) {
    char* raw = (char*)wasm_alloc(sizeof(CmStrHdr) + len + 1);
    if (!raw)
        return NULL;
    CmStrHdr* hdr = (CmStrHdr*)raw;
    hdr->magic = CM_STR_MAGIC;
    hdr->byte_len = (uint32_t)len;
    hdr->magic2 = CM_STR_MAGIC2;
    hdr->reserved = (uint32_t)sizeof(CmStrHdr);
    char* data = raw + sizeof(CmStrHdr);
    data[len] = 0;
    return data;
}

static const CmStrHdr* cm_str_hdr(const char* s) {
    if (!s)
        return NULL;
    // wasmの線形メモリは連続でヘッダ位置の前方読みが常に安全なため、
    // アドレス下限と4バイト整列（u32フィールド読み）のみ確認する
    if ((uintptr_t)s < sizeof(CmStrHdr) || (((uintptr_t)s & 3u) != 0))
        return NULL;
    const CmStrHdr* hdr = (const CmStrHdr*)(s - sizeof(CmStrHdr));
    if (hdr->magic != CM_STR_MAGIC || hdr->magic2 != CM_STR_MAGIC2)
        return NULL;
    if (s[hdr->byte_len] != 0)
        return NULL;
    return hdr;
}

size_t cm_string_byte_len(const char* s) {
    if (!s)
        return 0;
    const CmStrHdr* hdr = cm_str_hdr(s);
    if (hdr)
        return hdr->byte_len;
    size_t len = 0;
    while (s[len])
        len++;
    return len;
}

static char* cm_str_dup(const char* src) {
    size_t len = 0;
    if (src) {
        while (src[len])
            len++;
    }
    char* out = cm_str_alloc(len);
    if (out) {
        for (size_t i = 0; i < len; i++)
            out[i] = src[i];
    }
    return out;
}

char* cm_string_from_bytes(const void* data, int64_t len) {
    if (len < 0)
        len = 0;
    char* out = cm_str_alloc((size_t)len);
    if (out && len) {
        const char* src = (const char*)data;
        for (int64_t i = 0; i < len; i++)
            out[i] = src[i];
    }
    return out;
}

// Builtin string length function (used by Cm .len() method)
// Returns uint64_t to match the Cm type system
uint64_t __builtin_string_len(const char* str) {
    // ヘッダ付き文字列はO(1)、リテラル等はstrlenフォールバック（埋め込みNULも正しく数える）
    return (uint64_t)cm_string_byte_len(str);
}

// UTF-8コードポイント数を返す（H9第3段のlen()実体。継続バイト0b10xxxxxxを数えない）。
// 不正なUTF-8列でも各バイトの上位2ビット判定のみのため停止せず、バイト数以下の値を返す
uint64_t __builtin_string_codepoint_len(const char* str) {
    if (!str) {
        return 0;
    }
    const size_t blen = cm_string_byte_len(str);
    uint64_t count = 0;
    const unsigned char* p = (const unsigned char*)str;
    for (size_t i = 0; i < blen; i++) {
        if ((p[i] & 0xC0) != 0x80) {
            count++;
        }
    }
    return count;
}

// ============================================================
// Memory Allocator (segregated free list with linear-memory growth)
// ============================================================
// H11対応: 従来は解放不可能な単調バンプで、論理的に解放しても実メモリが返らず
// 長時間実行プログラムは総確保量に比例して線形メモリが増え続けた。
// 各割り当ての直前に8バイトヘッダ（サイズ+マジック）を置き、解放されたブロックを
// サイズクラス別フリーリストへ返却して再利用する。マジック検証により、ヒープ由来で
// ないポインタ（文字列リテラル・スタック等）のfreeは安全に無視する。
// 確保はフリーリスト→初期プール→memory.growの順で行い、生存中の割り当てを
// 上書きしない性質は従来どおり維持する。

// ヘッダ: [uint32_t 使用可能サイズ][uint32_t マジック]。本体は直後（8バイト整列）
#define WASM_HEAP_MAGIC 0xC3A110C8u
#define WASM_HEAP_FREED 0xF4EEF4EEu

// サイズクラス: 8, 16, 32, ..., 65536（2のべき乗、14クラス）。超過分はラージリスト
#define WASM_NUM_CLASSES 14
#define WASM_MAX_CLASS_SIZE 65536u

typedef struct WasmFreeBlock {
    struct WasmFreeBlock* next;
} WasmFreeBlock;

static WasmFreeBlock* free_lists[WASM_NUM_CLASSES];
static WasmFreeBlock* large_free_list;  // WASM_MAX_CLASS_SIZE超のブロック（先頭適合探索）

__attribute__((aligned(8))) static char
    memory_pool[65536];  // 初期プール（64KB）: 小さなプログラムはmemory.grow不要
static size_t pool_offset = 0;
static int use_grown_heap = 0;  // プール枯渇後は拡張領域から配る
static size_t grown_ptr = 0;  // 拡張領域の次アドレス（線形メモリ先頭からのバイトオフセット）

// サイズ（8整列済み）→サイズクラスindex。8→0, 16→1, ..., 65536→13
static int wasm_size_class(size_t size) {
    int cls = 0;
    size_t c = 8;
    while (c < size) {
        c <<= 1;
        cls++;
    }
    return cls;
}

// バンプ確保（ヘッダ込みの生バイト列を切り出す。フリーリストに無い場合のフォールバック）
static void* wasm_bump_alloc(size_t total) {
    // 高速パス: 初期プールに収まる間はプールから配る
    if (!use_grown_heap && pool_offset + total <= sizeof(memory_pool)) {
        void* ptr = &memory_pool[pool_offset];
        pool_offset += total;
        return ptr;
    }

    // プール枯渇: 以降は線形メモリを伸ばして拡張領域から配る（巻き戻さない）
    use_grown_heap = 1;
    if (grown_ptr == 0) {
        // 現在のメモリ末尾（=全静的データ・スタックの上）から開始する
        grown_ptr = (size_t)__builtin_wasm_memory_size(0) << 16;
    }
    size_t addr = grown_ptr;
    size_t end = addr + total;
    size_t mem_bytes = (size_t)__builtin_wasm_memory_size(0) << 16;
    if (end > mem_bytes) {
        size_t need = end - mem_bytes;
        size_t pages = (need + 65535u) >> 16;  // 64KBページ単位で切り上げ
        if ((size_t)__builtin_wasm_memory_grow(0, (int)pages) == (size_t)-1) {
            return 0;  // メモリ拡張失敗
        }
    }
    grown_ptr = end;
    return (void*)addr;
}

// Non-static for use in runtime_slice.c
void* wasm_alloc(size_t size) {
    // 8バイト境界に整列（誤整列アクセス回避）
    size = (size + 7u) & ~(size_t)7u;
    if (size == 0)
        size = 8;

    uint32_t* header;
    if (size <= WASM_MAX_CLASS_SIZE) {
        // サイズクラスへ切り上げ、フリーリストを先に探す
        int cls = wasm_size_class(size);
        size_t class_size = (size_t)8 << cls;
        WasmFreeBlock* block = free_lists[cls];
        if (block) {
            free_lists[cls] = block->next;
            header = (uint32_t*)((char*)block - 8);
            header[1] = WASM_HEAP_MAGIC;  // 再利用: FREED→MAGICへ戻す
            return (void*)block;
        }
        header = (uint32_t*)wasm_bump_alloc(class_size + 8);
        if (!header)
            return 0;
        header[0] = (uint32_t)class_size;
        header[1] = WASM_HEAP_MAGIC;
        return (void*)((char*)header + 8);
    }

    // ラージブロック: フリーリストを先頭適合で探索（サイズが足りる最初のブロックを再利用）
    WasmFreeBlock** prev = &large_free_list;
    for (WasmFreeBlock* block = large_free_list; block; block = block->next) {
        uint32_t* bh = (uint32_t*)((char*)block - 8);
        if ((size_t)bh[0] >= size) {
            *prev = block->next;
            bh[1] = WASM_HEAP_MAGIC;
            return (void*)block;
        }
        prev = &block->next;
    }
    header = (uint32_t*)wasm_bump_alloc(size + 8);
    if (!header)
        return 0;
    header[0] = (uint32_t)size;
    header[1] = WASM_HEAP_MAGIC;
    return (void*)((char*)header + 8);
}

// 解放: ヘッダのマジックを検証し、該当サイズクラスのフリーリストへ返す。
// ヒープ由来でないポインタ（リテラル・スタック・二重解放）は安全に無視する
void wasm_free(void* ptr) {
    if (!ptr)
        return;
    uint32_t* header = (uint32_t*)((char*)ptr - 8);
    if (header[1] != WASM_HEAP_MAGIC)
        return;  // ヒープ由来でない、または二重解放
    header[1] = WASM_HEAP_FREED;
    size_t block_size = header[0];
    WasmFreeBlock* block = (WasmFreeBlock*)ptr;
    if (block_size <= WASM_MAX_CLASS_SIZE) {
        int cls = wasm_size_class(block_size);
        block->next = free_lists[cls];
        free_lists[cls] = block;
    } else {
        block->next = large_free_list;
        large_free_list = block;
    }
}

// ヒープ由来ブロックの使用可能サイズ（reallocの正確なコピーに使用。非ヒープは0）
size_t wasm_alloc_size(const void* ptr) {
    if (!ptr)
        return 0;
    const uint32_t* header = (const uint32_t*)((const char*)ptr - 8);
    if (header[1] != WASM_HEAP_MAGIC)
        return 0;
    return header[0];
}

// ============================================================
// String Builtin Functions
// ============================================================
char __builtin_string_charAt(const char* str, int64_t index) {
    if (!str || index < 0) return '\0';
    size_t len = wasm_strlen(str);
    if ((size_t)index >= len) return '\0';
    return str[index];
}

char __builtin_string_first(const char* str) {
    if (!str || str[0] == '\0') return '\0';
    return str[0];
}

char __builtin_string_last(const char* str) {
    if (!str || str[0] == '\0') return '\0';
    size_t len = wasm_strlen(str);
    return str[len - 1];
}

// コードポイント添字に対応するバイトオフセットを返す（native版と同一ロジック）
static size_t cm_cp_index_to_byte(const char* str, int64_t cp_index) {
    // バイト長境界で走査する（埋め込みNULを越えて正しく添字を解決する。H9第4段）
    const size_t blen = cm_string_byte_len(str);
    const unsigned char* p = (const unsigned char*)str;
    int64_t seen = 0;
    size_t i = 0;
    while (i < blen) {
        if ((p[i] & 0xC0) != 0x80) {
            if (seen == cp_index) {
                break;
            }
            seen++;
        }
        i++;
    }
    while (i < blen && (p[i] & 0xC0) == 0x80) {
        i++;
    }
    return i;
}

// substring/sliceの添字はコードポイント単位（H9第3段）。負添字のPython風意味論は維持
char* __builtin_string_substring(const char* str, int64_t start, int64_t end) {
    if (!str) {
        char* empty = cm_str_alloc(0);
        empty[0] = '\0';
        return empty;
    }
    int64_t cp_len = (int64_t)__builtin_string_codepoint_len(str);
    // Python風: 負の値は末尾からの位置
    if (start < 0) {
        start = cp_len + start;
        if (start < 0) start = 0;
    }
    if (end < 0) {
        end = cp_len + end + 1;  // -1 => cp_len
    }
    if (end > cp_len) end = cp_len;
    if (start >= end) {
        char* empty = cm_str_alloc(0);
        empty[0] = '\0';
        return empty;
    }
    size_t byte_start = cm_cp_index_to_byte(str, start);
    size_t byte_end = cm_cp_index_to_byte(str, end);
    size_t result_len = byte_end - byte_start;
    char* result = cm_str_alloc(result_len);
    for (size_t i = 0; i < result_len; i++) {
        result[i] = str[byte_start + i];
    }
    return result;
}

// コードポイント添字indexのUnicodeスカラ値を返す（H9第3段）。範囲外・不正列は0
uint32_t __builtin_string_codepoint_at(const char* str, int64_t index) {
    if (!str || index < 0)
        return 0;
    size_t off = cm_cp_index_to_byte(str, index);
    const unsigned char* p = (const unsigned char*)str + off;
    if (!*p)
        return 0;
    unsigned char b0 = *p;
    if (b0 < 0x80) {
        return b0;
    }
    if ((b0 & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
        return ((uint32_t)(b0 & 0x1F) << 6) | (uint32_t)(p[1] & 0x3F);
    }
    if ((b0 & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
        return ((uint32_t)(b0 & 0x0F) << 12) | ((uint32_t)(p[1] & 0x3F) << 6) |
               (uint32_t)(p[2] & 0x3F);
    }
    if ((b0 & 0xF8) == 0xF0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 &&
        (p[3] & 0xC0) == 0x80) {
        return ((uint32_t)(b0 & 0x07) << 18) | ((uint32_t)(p[1] & 0x3F) << 12) |
               ((uint32_t)(p[2] & 0x3F) << 6) | (uint32_t)(p[3] & 0x3F);
    }
    return 0;
}

// Simple strstr implementation for WASM
static const char* wasm_strstr(const char* haystack, const char* needle) {
    if (!haystack || !needle) return 0;
    if (!*needle) return haystack;
    for (; *haystack; haystack++) {
        const char* h = haystack;
        const char* n = needle;
        while (*n && *h == *n) { h++; n++; }
        if (!*n) return haystack;
    }
    return 0;
}

// indexOfの戻り値はコードポイント添字（H9。従来はバイトオフセットでjs=UTF-16単位と不一致だった）。未検出は-1
int64_t __builtin_string_indexOf(const char* str, const char* substr) {
    if (!str || !substr) return -1;
    const char* pos = wasm_strstr(str, substr);
    if (!pos) return -1;
    int64_t cp_index = 0;
    for (const unsigned char* p = (const unsigned char*)str; p < (const unsigned char*)pos; p++) {
        if ((*p & 0xC0) != 0x80) {
            cp_index++;
        }
    }
    return cp_index;
}

char* __builtin_string_toUpperCase(const char* str) {
    if (!str) {
        char* empty = cm_str_alloc(0);
        empty[0] = '\0';
        return empty;
    }
    size_t len = wasm_strlen(str);
    char* result = cm_str_alloc(len);
    for (size_t i = 0; i < len; i++) {
        char c = str[i];
        if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
        result[i] = c;
    }
    result[len] = '\0';
    return result;
}

char* __builtin_string_toLowerCase(const char* str) {
    if (!str) {
        char* empty = cm_str_alloc(0);
        empty[0] = '\0';
        return empty;
    }
    size_t len = wasm_strlen(str);
    char* result = cm_str_alloc(len);
    for (size_t i = 0; i < len; i++) {
        char c = str[i];
        if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
        result[i] = c;
    }
    result[len] = '\0';
    return result;
}

char* __builtin_string_trim(const char* str) {
    if (!str) {
        char* empty = cm_str_alloc(0);
        empty[0] = '\0';
        return empty;
    }
    size_t len = wasm_strlen(str);
    size_t start = 0, end = len;
    while (start < len && (str[start] == ' ' || str[start] == '\t' || 
           str[start] == '\n' || str[start] == '\r')) start++;
    while (end > start && (str[end-1] == ' ' || str[end-1] == '\t' || 
           str[end-1] == '\n' || str[end-1] == '\r')) end--;
    size_t result_len = end - start;
    char* result = cm_str_alloc(result_len);
    for (size_t i = 0; i < result_len; i++) {
        result[i] = str[start + i];
    }
    result[result_len] = '\0';
    return result;
}

bool __builtin_string_startsWith(const char* str, const char* prefix) {
    if (!str || !prefix) return false;
    while (*prefix) {
        if (*str != *prefix) return false;
        str++;
        prefix++;
    }
    return true;
}

bool __builtin_string_endsWith(const char* str, const char* suffix) {
    if (!str || !suffix) return false;
    size_t str_len = wasm_strlen(str);
    size_t suffix_len = wasm_strlen(suffix);
    if (suffix_len > str_len) return false;
    const char* str_end = str + str_len - suffix_len;
    while (*suffix) {
        if (*str_end != *suffix) return false;
        str_end++;
        suffix++;
    }
    return true;
}

bool __builtin_string_includes(const char* str, const char* substr) {
    if (!str || !substr) return false;
    return wasm_strstr(str, substr) != 0;
}

char* __builtin_string_repeat(const char* str, int64_t count) {
    if (!str || count <= 0) {
        char* empty = cm_str_alloc(0);
        empty[0] = '\0';
        return empty;
    }
    size_t len = wasm_strlen(str);
    size_t total_len = len * (size_t)count;
    char* result = cm_str_alloc(total_len);
    for (int64_t i = 0; i < count; i++) {
        for (size_t j = 0; j < len; j++) {
            result[i * len + j] = str[j];
        }
    }
    result[total_len] = '\0';
    return result;
}

char* __builtin_string_replace(const char* str, const char* from, const char* to) {
    if (!str) {
        char* empty = cm_str_alloc(0);
        empty[0] = '\0';
        return empty;
    }
    if (!from || !to) {
        size_t len = wasm_strlen(str);
        char* copy = cm_str_alloc(len);
        for (size_t i = 0; i <= len; i++) copy[i] = str[i];
        return copy;
    }
    const char* pos = wasm_strstr(str, from);
    if (!pos) {
        size_t len = wasm_strlen(str);
        char* copy = cm_str_alloc(len);
        for (size_t i = 0; i <= len; i++) copy[i] = str[i];
        return copy;
    }
    size_t str_len = wasm_strlen(str);
    size_t from_len = wasm_strlen(from);
    size_t to_len = wasm_strlen(to);
    size_t result_len = str_len - from_len + to_len;
    char* result = cm_str_alloc(result_len);
    size_t prefix_len = (size_t)(pos - str);
    for (size_t i = 0; i < prefix_len; i++) result[i] = str[i];
    for (size_t i = 0; i < to_len; i++) result[prefix_len + i] = to[i];
    const char* rest = pos + from_len;
    size_t rest_len = wasm_strlen(rest);
    for (size_t i = 0; i <= rest_len; i++) result[prefix_len + to_len + i] = rest[i];
    return result;
}

// ============================================================
// Array Slice Functions
// ============================================================
void* __builtin_array_slice(void* arr, int64_t elem_size, int64_t arr_len, 
                            int64_t start, int64_t end, int64_t* out_len) {
    // スライス構造体（runtime_slice.cと同じ形式）
    typedef struct {
        void* data;
        int64_t len;
        int64_t cap;
        int64_t elem_size;
    } CmSlice;
    
    if (!arr || elem_size <= 0 || arr_len <= 0) {
        if (out_len) *out_len = 0;
        CmSlice* slice = (CmSlice*)wasm_alloc(sizeof(CmSlice));
        if (slice) {
            slice->data = 0;
            slice->len = 0;
            slice->cap = 0;
            slice->elem_size = elem_size;
        }
        return slice;
    }
    
    // Python風: 負のインデックス処理
    if (start < 0) {
        start = arr_len + start;
        if (start < 0) start = 0;
    }
    if (end < 0) {
        end = arr_len + end;
        if (end < 0) end = 0;
    }
    if (end > arr_len) end = arr_len;
    if (start >= end || start >= arr_len) {
        if (out_len) *out_len = 0;
        CmSlice* slice = (CmSlice*)wasm_alloc(sizeof(CmSlice));
        if (slice) {
            slice->data = 0;
            slice->len = 0;
            slice->cap = 0;
            slice->elem_size = elem_size;
        }
        return slice;
    }
    
    int64_t slice_len = end - start;
    
    // CmSlice構造体を作成
    CmSlice* slice = (CmSlice*)wasm_alloc(sizeof(CmSlice));
    if (!slice) {
        if (out_len) *out_len = 0;
        return 0;
    }
    
    slice->data = wasm_alloc((size_t)(slice_len * elem_size));
    if (!slice->data) {
        if (out_len) *out_len = 0;
        return 0;
    }
    
    // memcpyの代わりにバイト単位でコピー
    char* src = (char*)arr + (start * elem_size);
    char* dst = (char*)slice->data;
    for (int64_t i = 0; i < slice_len * elem_size; i++) {
        dst[i] = src[i];
    }
    
    slice->len = slice_len;
    slice->cap = slice_len;
    slice->elem_size = elem_size;
    
    if (out_len) *out_len = slice_len;
    return slice;
}

int64_t* __builtin_array_slice_int(int64_t* arr, int64_t arr_len,
                                   int64_t start, int64_t end, int64_t* out_len) {
    return (int64_t*)__builtin_array_slice(arr, sizeof(int64_t), arr_len, start, end, out_len);
}

int32_t* __builtin_array_slice_i32(int32_t* arr, int64_t arr_len,
                                   int64_t start, int64_t end, int64_t* out_len) {
    return (int32_t*)__builtin_array_slice(arr, sizeof(int32_t), arr_len, start, end, out_len);
}

// ============================================================
// Integer to String Conversion
// ============================================================
static void wasm_int_to_str(int value, char* buffer, size_t* len) {
    int is_negative = 0;
    unsigned int uval;

    if (value < 0) {
        is_negative = 1;
        if (value == -2147483648) {
            const char* int_min = "-2147483648";
            int i = 0;
            while (int_min[i]) {
                buffer[i] = int_min[i];
                i++;
            }
            *len = i;
            return;
        }
        uval = -value;
    } else {
        uval = value;
    }

    char temp[32];
    int i = 0;
    do {
        temp[i++] = '0' + (uval % 10);
        uval /= 10;
    } while (uval > 0);

    int j = 0;
    if (is_negative) {
        buffer[j++] = '-';
    }

    while (i > 0) {
        buffer[j++] = temp[--i];
    }

    *len = j;
}

static void wasm_uint_to_str(unsigned int value, char* buffer, size_t* len) {
    char temp[32];
    int i = 0;
    do {
        temp[i++] = '0' + (value % 10);
        value /= 10;
    } while (value > 0);

    int j = 0;
    while (i > 0) {
        buffer[j++] = temp[--i];
    }

    *len = j;
}

static void wasm_int64_to_str(long long value, char* buffer, size_t* len) {
    int is_negative = 0;
    unsigned long long uval;

    if (value < 0) {
        is_negative = 1;
        uval = (unsigned long long)(-value);
    } else {
        uval = (unsigned long long)value;
    }

    char temp[32];
    int i = 0;
    do {
        temp[i++] = '0' + (uval % 10);
        uval /= 10;
    } while (uval > 0);

    int j = 0;
    if (is_negative) {
        buffer[j++] = '-';
    }

    while (i > 0) {
        buffer[j++] = temp[--i];
    }

    *len = j;
}

// ============================================================
// Escape Processing
// ============================================================
char* cm_unescape_braces(const char* str) {
    if (!str) return 0;

    size_t len = wasm_strlen(str);
    char* result = cm_str_alloc(len);

    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (str[i] == '{' && i + 1 < len && str[i + 1] == '{') {
            result[j++] = '{';
            i++;
        } else if (str[i] == '}' && i + 1 < len && str[i + 1] == '}') {
            result[j++] = '}';
            i++;
        } else {
            result[j++] = str[i];
        }
    }
    result[j] = '\0';
    return result;
}

char* cm_format_unescape_braces(const char* str) {
    return cm_unescape_braces(str);
}

// ============================================================
// Type to String Conversion
// ============================================================
char* cm_format_int(int value) {
    char* buffer = (char*)wasm_alloc(32);
    size_t len;
    wasm_int_to_str(value, buffer, &len);
    buffer[len] = '\0';
    return buffer;
}

char* cm_format_uint(unsigned int value) {
    char* buffer = (char*)wasm_alloc(32);
    size_t len;
    wasm_uint_to_str(value, buffer, &len);
    buffer[len] = '\0';
    return buffer;
}

char* cm_format_long(long long value) {
    char* buffer = (char*)wasm_alloc(32);
    size_t len = 0;

    if (value == 0) {
        buffer[0] = '0';
        buffer[1] = '\0';
        return buffer;
    }

    int is_negative = value < 0;
    unsigned long long abs_value = is_negative ? -value : value;

    // 数値を文字列に変換（逆順）
    char temp[32];
    int temp_len = 0;
    while (abs_value > 0) {
        temp[temp_len++] = '0' + (abs_value % 10);
        abs_value /= 10;
    }

    // 負の符号を追加
    if (is_negative) {
        buffer[len++] = '-';
    }

    // 逆順を正順に
    for (int i = temp_len - 1; i >= 0; i--) {
        buffer[len++] = temp[i];
    }

    buffer[len] = '\0';
    return buffer;
}

char* cm_format_ulong(unsigned long long value) {
    char* buffer = (char*)wasm_alloc(32);
    size_t len = 0;

    if (value == 0) {
        buffer[0] = '0';
        buffer[1] = '\0';
        return buffer;
    }

    // 数値を文字列に変換（逆順）
    char temp[32];
    int temp_len = 0;
    while (value > 0) {
        temp[temp_len++] = '0' + (value % 10);
        value /= 10;
    }

    // 逆順を正順に
    for (int i = temp_len - 1; i >= 0; i--) {
        buffer[len++] = temp[i];
    }

    buffer[len] = '\0';
    return buffer;
}

char* cm_format_bool(char value) {
    if (value) {
        char* buffer = (char*)wasm_alloc(5);
        buffer[0] = 't'; buffer[1] = 'r'; buffer[2] = 'u'; buffer[3] = 'e'; buffer[4] = '\0';
        return buffer;
    } else {
        char* buffer = (char*)wasm_alloc(6);
        buffer[0] = 'f'; buffer[1] = 'a'; buffer[2] = 'l'; buffer[3] = 's'; buffer[4] = 'e'; buffer[5] = '\0';
        return buffer;
    }
}

char* cm_format_char(char value) {
    char* buffer = (char*)wasm_alloc(2);
    buffer[0] = value;
    buffer[1] = '\0';
    return buffer;
}

// ============================================================
// double→最短round-trip文字列（M8: 5桁固定を撤廃し全バックエンドの出力を統一）
// ============================================================
// libc（snprintf/strtod）が無いため、桁列生成と復元検証を自前実装する。
// JSのNumber→String規則と整合: 小数点位置nが(-6, 21]は10進表記、それ以外は指数表記。

// 10^n をdoubleで返す（1e0..1e22は正確、超過分は分割乗算）
static double wasm_pow10d(int n) {
    static const double exact[23] = {1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,
                                     1e8,  1e9,  1e10, 1e11, 1e12, 1e13, 1e14, 1e15,
                                     1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};
    if (n >= 0) {
        double r = 1.0;
        while (n > 22) {
            r *= 1e22;
            n -= 22;
        }
        return r * exact[n];
    }
    double r = 1.0;
    while (n < -22) {
        r /= 1e22;
        n += 22;
    }
    return r / exact[-n];
}

// valueの10進指数（floor(log10(value))）を求める（value > 0 前提）
static int wasm_exp10_of(double value) {
    int e = 0;
    // 粗い範囲寄せ（大きなステップで反復回数を抑える）
    while (value >= 1e22) {
        value /= 1e22;
        e += 22;
    }
    while (value < 1e-22) {
        value *= 1e22;
        e -= 22;
    }
    while (value >= 10.0) {
        value /= 10.0;
        e += 1;
    }
    while (value < 1.0) {
        value *= 10.0;
        e -= 1;
    }
    return e;
}

// 有効桁p（1..17）でvalueを桁列へ変換し、10進指数を返す（value > 0 前提）
static void wasm_dtoa_prec(double value, int p, char* digits, int* exp10_out) {
    int e = wasm_exp10_of(value);
    // 仮数を [10^(p-1), 10^p) のu64へスケーリング（四捨五入）
    double scaled = value / wasm_pow10d(e - p + 1);
    unsigned long long m = (unsigned long long)(scaled + 0.5);
    unsigned long long lo = 1;
    for (int i = 1; i < p; ++i)
        lo *= 10ull;
    unsigned long long hi = lo * 10ull;
    if (m >= hi) {
        m /= 10ull;
        e += 1;
    }
    if (p > 1 && m < lo) {
        m = lo;  // スケーリング誤差の下振れ補正（round-trip検証で棄却される）
    }
    for (int i = p - 1; i >= 0; --i) {
        digits[i] = (char)('0' + (int)(m % 10ull));
        m /= 10ull;
    }
    digits[p] = 0;
    *exp10_out = e;
}

// 桁列+指数をdoubleへ戻す（round-trip検証用）
static double wasm_atod_digits(const char* digits, int nd, int exp10) {
    unsigned long long m = 0;
    for (int i = 0; i < nd; ++i)
        m = m * 10ull + (unsigned long long)(digits[i] - '0');
    int k = exp10 - nd + 1;
    double md = (double)m;
    if (k >= 0)
        return md * wasm_pow10d(k);
    // 負の指数は除算の方が正確（10^|k|が正確な範囲で正しく丸む）
    return md / wasm_pow10d(-k);
}

// double値のビット表現比較（round-trip判定。==はNaNや-0.0で不適切）
static int wasm_double_bits_equal(double a, double b) {
    union {
        double d;
        unsigned long long u;
    } ua, ub;
    ua.d = a;
    ub.d = b;
    return ua.u == ub.u;
}

char* cm_format_double(double value) {
    char* buffer = (char*)wasm_alloc(64);

    // NaN/Inf/0（従来トークンを維持）
    if (value != value) {
        buffer[0] = 'n';
        buffer[1] = 'a';
        buffer[2] = 'n';
        buffer[3] = 0;
        return buffer;
    }
    int is_negative = 0;
    if (value < 0) {
        is_negative = 1;
        value = -value;
    }
    if (value > 1.7976931348623157e308) {
        size_t len = 0;
        if (is_negative)
            buffer[len++] = '-';
        buffer[len++] = 'i';
        buffer[len++] = 'n';
        buffer[len++] = 'f';
        buffer[len] = 0;
        return buffer;
    }
    if (value == 0.0) {
        buffer[0] = '0';
        buffer[1] = 0;
        return buffer;
    }

    // round-tripする最小桁数の桁列を選ぶ。
    // float(32bit)から拡張された値はfloat精度でのround-tripを採用する（native実装と同一規則）
    char digits[24];
    int exp10 = 0;
    int nd = 17;
    int found = 0;
    if ((double)(float)value == value) {
        for (int p = 1; p <= 9; ++p) {
            wasm_dtoa_prec(value, p, digits, &exp10);
            if ((double)(float)wasm_atod_digits(digits, p, exp10) == value) {
                nd = p;
                found = 1;
                break;
            }
        }
    }
    if (!found) {
        for (int p = 1; p <= 17; ++p) {
            wasm_dtoa_prec(value, p, digits, &exp10);
            if (wasm_double_bits_equal(wasm_atod_digits(digits, p, exp10), value)) {
                nd = p;
                found = 1;
                break;
            }
        }
    }
    if (!found) {
        wasm_dtoa_prec(value, 17, digits, &exp10);
        nd = 17;
    }
    // 末尾の0を除去
    while (nd > 1 && digits[nd - 1] == '0')
        nd--;

    // JS互換の整形: n = 小数点の位置
    int n = exp10 + 1;
    size_t len = 0;
    if (is_negative)
        buffer[len++] = '-';

    if (n >= 1 && n <= 21) {
        if (nd <= n) {
            for (int i = 0; i < nd; ++i)
                buffer[len++] = digits[i];
            for (int i = 0; i < n - nd; ++i)
                buffer[len++] = '0';
        } else {
            for (int i = 0; i < n; ++i)
                buffer[len++] = digits[i];
            buffer[len++] = '.';
            for (int i = n; i < nd; ++i)
                buffer[len++] = digits[i];
        }
    } else if (n <= 0 && n > -6) {
        buffer[len++] = '0';
        buffer[len++] = '.';
        for (int i = 0; i < -n; ++i)
            buffer[len++] = '0';
        for (int i = 0; i < nd; ++i)
            buffer[len++] = digits[i];
    } else {
        buffer[len++] = digits[0];
        if (nd > 1) {
            buffer[len++] = '.';
            for (int i = 1; i < nd; ++i)
                buffer[len++] = digits[i];
        }
        buffer[len++] = 'e';
        int ev = n - 1;
        if (ev >= 0) {
            buffer[len++] = '+';
        } else {
            buffer[len++] = '-';
            ev = -ev;
        }
        char eb[8];
        int el = 0;
        if (ev == 0)
            eb[el++] = '0';
        while (ev > 0) {
            eb[el++] = (char)('0' + ev % 10);
            ev /= 10;
        }
        while (el > 0)
            buffer[len++] = eb[--el];
    }
    buffer[len] = 0;
    return buffer;
}

// 明示stringキャスト用（M8: 従来wasmに実体が無くリンク切れの危険があった）
char* cm_double_to_string(double value) {
    return cm_format_double(value);
}

char* cm_format_double_precision(double value, int precision) {
    char* buffer = (char*)wasm_alloc(64);
    
    double round_adj = 0.5;
    for (int i = 0; i < precision; i++) {
        round_adj /= 10.0;
    }
    if (value >= 0) {
        value += round_adj;
    } else {
        value -= round_adj;
    }
    
    int int_part = (int)value;
    double frac_part = value - int_part;
    if (frac_part < 0) frac_part = -frac_part;

    size_t len;
    wasm_int_to_str(int_part, buffer, &len);
    buffer[len++] = '.';

    for (int i = 0; i < precision; i++) {
        frac_part *= 10;
        int digit = (int)frac_part % 10;
        buffer[len++] = '0' + digit;
    }
    buffer[len] = '\0';

    return buffer;
}

// ============================================================
// Integer Format Variants
// ============================================================
char* cm_format_int_hex(long long value) {
    char* buffer = (char*)wasm_alloc(32);
    unsigned long long uval = (unsigned long long)value;

    char hex_chars[] = "0123456789abcdef";
    char temp[32];
    int i = 0;

    if (uval == 0) {
        buffer[0] = '0';
        buffer[1] = '\0';
        return buffer;
    }

    while (uval > 0) {
        temp[i++] = hex_chars[uval % 16];
        uval /= 16;
    }

    // WASMでは線形メモリオフセットなので0xプレフィックスは付けない
    int j = 0;
    while (i > 0) {
        buffer[j++] = temp[--i];
    }
    buffer[j] = '\0';

    return buffer;
}

char* cm_format_int_HEX(long long value) {
    char* buffer = (char*)wasm_alloc(32);
    unsigned long long uval = (unsigned long long)value;

    char hex_chars[] = "0123456789ABCDEF";
    char temp[32];
    int i = 0;

    if (uval == 0) {
        buffer[0] = '0';
        buffer[1] = '\0';
        return buffer;
    }

    while (uval > 0) {
        temp[i++] = hex_chars[uval % 16];
        uval /= 16;
    }

    // WASMでは線形メモリオフセットなので0xプレフィックスは付けない
    int j = 0;
    while (i > 0) {
        buffer[j++] = temp[--i];
    }
    buffer[j] = '\0';

    return buffer;
}

char* cm_format_int_binary(long long value) {
    char* buffer = (char*)wasm_alloc(65);
    unsigned long long uval = (unsigned long long)value;

    if (uval == 0) {
        buffer[0] = '0';
        buffer[1] = '\0';
        return buffer;
    }

    char temp[64];
    int i = 0;

    while (uval > 0) {
        temp[i++] = (uval % 2) ? '1' : '0';
        uval /= 2;
    }

    int j = 0;
    while (i > 0) {
        buffer[j++] = temp[--i];
    }
    buffer[j] = '\0';

    return buffer;
}

char* cm_format_int_octal(long long value) {
    char* buffer = (char*)wasm_alloc(32);
    unsigned long long uval = (unsigned long long)value;

    if (uval == 0) {
        buffer[0] = '0';
        buffer[1] = '\0';
        return buffer;
    }

    char temp[32];
    int i = 0;

    while (uval > 0) {
        temp[i++] = '0' + (uval % 8);
        uval /= 8;
    }

    int j = 0;
    while (i > 0) {
        buffer[j++] = temp[--i];
    }
    buffer[j] = '\0';

    return buffer;
}

// ============================================================
// Double Format Variants
// ============================================================
char* cm_format_double_scientific(double value, int uppercase) {
    char* buffer = (char*)wasm_alloc(32);

    int is_negative = 0;
    if (value < 0) {
        is_negative = 1;
        value = -value;
    }

    int exponent = 0;
    double mantissa = value;

    if (value != 0.0) {
        while (mantissa >= 10.0) {
            mantissa /= 10.0;
            exponent++;
        }
        while (mantissa < 1.0) {
            mantissa *= 10.0;
            exponent--;
        }
    }

    mantissa += 0.0000005;
    if (mantissa >= 10.0) {
        mantissa /= 10.0;
        exponent++;
    }

    int mantissa_int = (int)mantissa;
    int mantissa_frac = (int)((mantissa - mantissa_int) * 1000000);

    int idx = 0;
    if (is_negative) buffer[idx++] = '-';
    buffer[idx++] = '0' + mantissa_int;
    buffer[idx++] = '.';

    int divisor = 100000;
    for (int i = 0; i < 6; i++) {
        buffer[idx++] = '0' + ((mantissa_frac / divisor) % 10);
        divisor /= 10;
    }

    buffer[idx++] = uppercase ? 'E' : 'e';
    if (exponent < 0) {
        buffer[idx++] = '-';
        exponent = -exponent;
    } else {
        buffer[idx++] = '+';
    }

    buffer[idx++] = '0' + (exponent / 10);
    buffer[idx++] = '0' + (exponent % 10);
    buffer[idx] = '\0';

    return buffer;
}

char* cm_format_double_exp(double value) {
    return cm_format_double_scientific(value, 0);
}

char* cm_format_double_EXP(double value) {
    return cm_format_double_scientific(value, 1);
}

// ============================================================
// String Utilities
// ============================================================
// 連結チェーン最適化（H9第5段）: a+b+c(+d)を1回の確保で連結する
char* cm_string_concat3(const char* a, const char* b, const char* c) {
    if (!a) a = "";
    if (!b) b = "";
    if (!c) c = "";
    size_t la = cm_string_byte_len(a);
    size_t lb = cm_string_byte_len(b);
    size_t lc = cm_string_byte_len(c);
    char* result = cm_str_alloc(la + lb + lc);
    for (size_t i = 0; i < la; i++) result[i] = a[i];
    for (size_t i = 0; i < lb; i++) result[la + i] = b[i];
    for (size_t i = 0; i < lc; i++) result[la + lb + i] = c[i];
    result[la + lb + lc] = '\0';
    return result;
}

char* cm_string_concat4(const char* a, const char* b, const char* c, const char* d) {
    if (!a) a = "";
    if (!b) b = "";
    if (!c) c = "";
    if (!d) d = "";
    size_t la = cm_string_byte_len(a);
    size_t lb = cm_string_byte_len(b);
    size_t lc = cm_string_byte_len(c);
    size_t ld = cm_string_byte_len(d);
    char* result = cm_str_alloc(la + lb + lc + ld);
    for (size_t i = 0; i < la; i++) result[i] = a[i];
    for (size_t i = 0; i < lb; i++) result[la + i] = b[i];
    for (size_t i = 0; i < lc; i++) result[la + lb + i] = c[i];
    for (size_t i = 0; i < ld; i++) result[la + lb + lc + i] = d[i];
    result[la + lb + lc + ld] = '\0';
    return result;
}

char* cm_string_concat(const char* left, const char* right) {
    if (!left) left = "";
    if (!right) right = "";

    size_t len1 = cm_string_byte_len(left);
    size_t len2 = cm_string_byte_len(right);
    char* result = cm_str_alloc(len1 + len2);

    for (size_t i = 0; i < len1; i++) {
        result[i] = left[i];
    }
    for (size_t i = 0; i < len2; i++) {
        result[len1 + i] = right[i];
    }

    return result;
}

// 文一時文字列の解放（C12 dropパス）。cm_string_concat・cm_*_to_string等が返した無名一時の解放に使う。NULLは無視する
void cm_string_free(char* str) {
    if (str) {
        // ヘッダ付き文字列は確保起点（ヘッダ先頭）を解放する（H9第4段）
        if (cm_str_hdr(str)) {
            // 解放前にマジックを消去し、ブロック再利用時の残留ヘッダ誤認を防ぐ
            CmStrHdr* hdr = (CmStrHdr*)(str - sizeof(CmStrHdr));
            char* raw = str - hdr->reserved;
            hdr->magic = 0;
            hdr->magic2 = 0;
            wasm_free(raw);
        } else {
            wasm_free(str);
        }
    }
}

// ============================================================
// StringBuilder（H9第1段）: 容量倍増の可変バッファへ償却O(1)で追記する。
// ループ連結のO(n²)（毎回strlen+全コピー）をO(n)へ置き換える（native版と同一シグネチャ）
// ============================================================

typedef struct {
    char* data;
    size_t len;
    size_t cap;
} CmStringBuilder;

int64_t cm_sb_create(void) {
    CmStringBuilder* sb = (CmStringBuilder*)wasm_alloc(sizeof(CmStringBuilder));
    if (!sb) {
        return 0;
    }
    sb->cap = 16;
    sb->len = 0;
    sb->data = (char*)wasm_alloc(sb->cap);
    if (!sb->data) {
        wasm_free(sb);
        return 0;
    }
    return (int64_t)(intptr_t)sb;
}

void cm_sb_append(int64_t handle, const char* s) {
    CmStringBuilder* sb = (CmStringBuilder*)(intptr_t)handle;
    if (!sb || !s) {
        return;
    }
    size_t add = wasm_strlen(s);
    if (add == 0) {
        return;
    }
    if (sb->len + add > sb->cap) {
        size_t new_cap = sb->cap;
        while (sb->len + add > new_cap) {
            new_cap *= 2;
        }
        char* new_data = (char*)wasm_alloc(new_cap);
        if (!new_data) {
            return;
        }
        for (size_t i = 0; i < sb->len; i++) {
            new_data[i] = sb->data[i];
        }
        wasm_free(sb->data);
        sb->data = new_data;
        sb->cap = new_cap;
    }
    for (size_t i = 0; i < add; i++) {
        sb->data[sb->len + i] = s[i];
    }
    sb->len += add;
}

// 現在の内容をNUL終端の新規バッファで返す（呼び出し側所有。builderは継続使用可能）
char* cm_sb_to_string(int64_t handle) {
    CmStringBuilder* sb = (CmStringBuilder*)(intptr_t)handle;
    if (!sb) {
        char* empty = cm_str_alloc(0);
        if (empty) {
            empty[0] = '\0';
        }
        return empty;
    }
    char* result = cm_str_alloc(sb->len);
    if (!result) {
        return NULL;
    }
    for (size_t i = 0; i < sb->len; i++) {
        result[i] = sb->data[i];
    }
    result[sb->len] = '\0';
    return result;
}

int64_t cm_sb_len(int64_t handle) {
    CmStringBuilder* sb = (CmStringBuilder*)(intptr_t)handle;
    return sb ? (int64_t)sb->len : 0;
}

// 内容を空にする（容量は維持し、再利用時の再確保を避ける）
void cm_sb_clear(int64_t handle) {
    CmStringBuilder* sb = (CmStringBuilder*)(intptr_t)handle;
    if (sb) {
        sb->len = 0;
    }
}

void cm_sb_destroy(int64_t handle) {
    CmStringBuilder* sb = (CmStringBuilder*)(intptr_t)handle;
    if (sb) {
        if (sb->data) {
            wasm_free(sb->data);
        }
        wasm_free(sb);
    }
}

char* cm_int_to_string(int value) {
    return cm_format_int(value);
}

char* cm_uint_to_string(unsigned int value) {
    return cm_format_uint(value);
}

char* cm_long_to_string(int64_t value) {
    return cm_format_long(value);
}

char* cm_ulong_to_string(uint64_t value) {
    return cm_format_ulong(value);
}

char* cm_char_to_string(char value) {
    return cm_format_char(value);
}

char* cm_bool_to_string(char value) {
    return cm_format_bool(value);
}

// ============================================================
// Format Spec Extraction
// ============================================================
// プレースホルダーからフォーマット指定子を抽出
// 例: "{name:x}" -> 'x', "{value:b}" -> 'b', "{foo}" -> '\0'
static char extract_format_spec(const char* format, size_t start, size_t end) {
    // start は '{' の位置、end は '}' の位置
    for (size_t i = start + 1; i < end; i++) {
        if (format[i] == ':' && i + 1 < end) {
            return format[i + 1];  // ':' の次の文字を返す
        }
    }
    return '\0';  // フォーマット指定子なし
}

// ============================================================
// Format Replace Functions
// ============================================================
char* cm_format_replace(const char* format, const char* value) {
    if (!format) return 0;
    if (!value) value = "";

    size_t fmt_len = wasm_strlen(format);

    size_t start = 0;
    int found = 0;
    for (size_t i = 0; i < fmt_len; i++) {
        if (format[i] == '{') {
            start = i;
            found = 1;
            break;
        }
    }

    if (!found) {
        char* result = cm_str_alloc(fmt_len);
        for (size_t i = 0; i < fmt_len; i++) {
            result[i] = format[i];
        }
        result[fmt_len] = '\0';
        return result;
    }

    size_t end = start;
    for (size_t i = start + 1; i < fmt_len; i++) {
        if (format[i] == '}') {
            end = i;
            break;
        }
    }

    if (end == start) {
        char* result = cm_str_alloc(fmt_len);
        for (size_t i = 0; i < fmt_len; i++) {
            result[i] = format[i];
        }
        result[fmt_len] = '\0';
        return result;
    }

    size_t placeholder_len = end - start + 1;
    size_t val_len = wasm_strlen(value);
    size_t result_len = fmt_len - placeholder_len + val_len + 1;
    char* result = (char*)wasm_alloc(result_len);

    size_t result_idx = 0;
    for (size_t i = 0; i < start; i++) {
        result[result_idx++] = format[i];
    }

    for (size_t i = 0; i < val_len; i++) {
        result[result_idx++] = value[i];
    }

    for (size_t i = end + 1; i < fmt_len; i++) {
        result[result_idx++] = format[i];
    }
    result[result_idx] = '\0';

    return result;
}

char* cm_format_replace_int(const char* format, int value) {
    if (!format) return 0;

    size_t fmt_len = wasm_strlen(format);

    // プレースホルダーを探す
    size_t start = 0;
    int found = 0;
    for (size_t i = 0; i < fmt_len; i++) {
        if (format[i] == '{') {
            start = i;
            found = 1;
            break;
        }
    }

    if (!found) {
        char* result = cm_str_alloc(fmt_len);
        for (size_t i = 0; i < fmt_len; i++) {
            result[i] = format[i];
        }
        result[fmt_len] = '\0';
        return result;
    }

    size_t end = start;
    for (size_t i = start + 1; i < fmt_len; i++) {
        if (format[i] == '}') {
            end = i;
            break;
        }
    }

    // フォーマット指定子を抽出
    char spec = extract_format_spec(format, start, end);
    
    // アライメントとパディングを解析
    char align = '\0';
    char fill_char = ' ';
    int width = 0;
    
    if (end > start + 1 && format[start + 1] == ':') {
        size_t spec_start = start + 2;
        
        // {:0>5} のようなパターンを検出
        if (spec_start < end) {
            char c = format[spec_start];
            if (c == '<' || c == '>' || c == '^') {
                align = c;
                spec_start++;
            } else if (spec_start + 1 < end && 
                       (format[spec_start + 1] == '<' || 
                        format[spec_start + 1] == '>' || 
                        format[spec_start + 1] == '^')) {
                fill_char = c;
                align = format[spec_start + 1];
                spec_start += 2;
            }
        }
        
        // 幅を解析
        while (spec_start < end && format[spec_start] >= '0' && format[spec_start] <= '9') {
            width = width * 10 + (format[spec_start] - '0');
            spec_start++;
        }
    }
    
    // 指定子に応じた値の文字列化
    char* value_str;
    switch (spec) {
        case 'x':
            value_str = cm_format_int_hex((long long)(unsigned int)value);  // 整数には0xプレフィックスなし
            break;
        case 'X':
            value_str = cm_format_int_HEX((long long)(unsigned int)value);  // 整数には0xプレフィックスなし
            break;
        case 'b':
            value_str = cm_format_int_binary((long long)(unsigned int)value);
            break;
        case 'o':
            value_str = cm_format_int_octal((long long)(unsigned int)value);
            break;
        default:
            value_str = cm_format_int(value);
            break;
    }
    
    // パディングを適用
    size_t val_len = wasm_strlen(value_str);
    char* formatted_value;
    
    if (width > 0 && (size_t)width > val_len && align != '\0') {
        formatted_value = cm_str_alloc(width);
        size_t padding = width - val_len;
        
        if (align == '<') {
            for (size_t i = 0; i < val_len; i++) formatted_value[i] = value_str[i];
            for (size_t i = val_len; i < (size_t)width; i++) formatted_value[i] = fill_char;
        } else if (align == '>') {
            for (size_t i = 0; i < padding; i++) formatted_value[i] = fill_char;
            for (size_t i = 0; i < val_len; i++) formatted_value[padding + i] = value_str[i];
        } else if (align == '^') {
            size_t left_pad = padding / 2;
            for (size_t i = 0; i < left_pad; i++) formatted_value[i] = fill_char;
            for (size_t i = 0; i < val_len; i++) formatted_value[left_pad + i] = value_str[i];
            for (size_t i = left_pad + val_len; i < (size_t)width; i++) formatted_value[i] = fill_char;
        }
        formatted_value[width] = '\0';
    } else {
        formatted_value = value_str;
    }
    
    // 置換を実行
    size_t placeholder_len = end - start + 1;
    size_t formatted_len = wasm_strlen(formatted_value);
    size_t result_len = fmt_len - placeholder_len + formatted_len + 1;
    char* result = (char*)wasm_alloc(result_len);
    
    size_t result_idx = 0;
    for (size_t i = 0; i < start; i++) {
        result[result_idx++] = format[i];
    }
    for (size_t i = 0; i < formatted_len; i++) {
        result[result_idx++] = formatted_value[i];
    }
    for (size_t i = end + 1; i < fmt_len; i++) {
        result[result_idx++] = format[i];
    }
    result[result_idx] = '\0';
    
    return result;
}

char* cm_format_replace_uint(const char* format, unsigned int value) {
    if (!format) return 0;

    size_t fmt_len = wasm_strlen(format);

    // プレースホルダーを探す
    size_t start = 0;
    int found = 0;
    for (size_t i = 0; i < fmt_len; i++) {
        if (format[i] == '{') {
            start = i;
            found = 1;
            break;
        }
    }

    if (!found) {
        char* result = cm_str_alloc(fmt_len);
        for (size_t i = 0; i < fmt_len; i++) {
            result[i] = format[i];
        }
        result[fmt_len] = '\0';
        return result;
    }

    size_t end = start;
    for (size_t i = start + 1; i < fmt_len; i++) {
        if (format[i] == '}') {
            end = i;
            break;
        }
    }

    // フォーマット指定子を抽出
    char spec = extract_format_spec(format, start, end);
    
    // 指定子に応じた値の文字列化
    char* value_str;
    switch (spec) {
        case 'x':
            value_str = cm_format_int_hex((long long)value);  // 整数には0xプレフィックスなし
            break;
        case 'X':
            value_str = cm_format_int_HEX((long long)value);  // 整数には0xプレフィックスなし
            break;
        case 'b':
            value_str = cm_format_int_binary((long long)value);
            break;
        case 'o':
            value_str = cm_format_int_octal((long long)value);
            break;
        default:
            value_str = cm_format_uint(value);
            break;
    }
    
    return cm_format_replace(format, value_str);
}

// ポインタ専用のフォーマット関数（デフォルトで10進数表示）
char* cm_format_replace_ptr(const char* format, long long value) {
    if (!format) return 0;

    size_t fmt_len = wasm_strlen(format);

    // プレースホルダーを探す
    size_t start = 0;
    int found = 0;
    for (size_t i = 0; i < fmt_len; i++) {
        if (format[i] == '{') {
            start = i;
            found = 1;
            break;
        }
    }

    if (!found) {
        char* result = cm_str_alloc(fmt_len);
        for (size_t i = 0; i < fmt_len; i++) {
            result[i] = format[i];
        }
        result[fmt_len] = '\0';
        return result;
    }

    size_t end = start;
    for (size_t i = start + 1; i < fmt_len; i++) {
        if (format[i] == '}') {
            end = i;
            break;
        }
    }

    // フォーマット指定子を抽出
    char spec = extract_format_spec(format, start, end);

    // 指定子に応じた値の文字列化
    char* value_str;
    if (spec == 'x') {
        // 小文字16進数（明示的指定時は0xプレフィックス付き）
        char* hex_str = cm_format_int_hex(value);
        size_t hex_len = wasm_strlen(hex_str);
        value_str = (char*)wasm_alloc(hex_len + 3);  // "0x" + hex + '\0'
        value_str[0] = '0';
        value_str[1] = 'x';
        for (size_t i = 0; i <= hex_len; i++) {
            value_str[i + 2] = hex_str[i];
        }
    } else if (spec == 'X') {
        // 大文字16進数（明示的指定時は0xプレフィックス付き）
        char* hex_str = cm_format_int_HEX(value);
        size_t hex_len = wasm_strlen(hex_str);
        value_str = (char*)wasm_alloc(hex_len + 3);  // "0x" + hex + '\0'
        value_str[0] = '0';
        value_str[1] = 'x';
        for (size_t i = 0; i <= hex_len; i++) {
            value_str[i + 2] = hex_str[i];
        }
    } else if (spec == 'b') {
        value_str = cm_format_int_binary(value);
    } else if (spec == 'o') {
        value_str = cm_format_int_octal(value);
    } else {
        // デフォルトは10進数
        value_str = cm_format_long(value);
    }

    return cm_format_replace(format, value_str);
}

char* cm_format_replace_long(const char* format, long long value) {
    if (!format) return 0;

    size_t fmt_len = wasm_strlen(format);

    // プレースホルダーを探す
    size_t start = 0;
    int found = 0;
    for (size_t i = 0; i < fmt_len; i++) {
        if (format[i] == '{') {
            start = i;
            found = 1;
            break;
        }
    }

    if (!found) {
        char* result = cm_str_alloc(fmt_len);
        for (size_t i = 0; i < fmt_len; i++) {
            result[i] = format[i];
        }
        result[fmt_len] = '\0';
        return result;
    }

    size_t end = start;
    for (size_t i = start + 1; i < fmt_len; i++) {
        if (format[i] == '}') {
            end = i;
            break;
        }
    }

    // フォーマット指定子を抽出
    char spec = extract_format_spec(format, start, end);

    // 指定子に応じた値の文字列化
    char* value_str;
    switch (spec) {
        case 'x':
            // 小文字16進数（整数には0xプレフィックスなし。native/jsと表記を統一）
            value_str = cm_format_int_hex(value);
            break;
        case 'X':
            value_str = cm_format_int_HEX(value);
            break;
        case 'b':
            value_str = cm_format_int_binary(value);
            break;
        case 'o':
            value_str = cm_format_int_octal(value);
            break;
        default:
            value_str = cm_format_long(value);
            break;
    }

    return cm_format_replace(format, value_str);
}

char* cm_format_replace_ulong(const char* format, unsigned long long value) {
    if (!format) return 0;

    char* value_str = cm_format_ulong(value);
    return cm_format_replace(format, value_str);
}

char* cm_format_replace_double(const char* format, double value) {
    if (!format) return 0;

    size_t fmt_len = wasm_strlen(format);

    // プレースホルダーを探す
    size_t start = 0;
    int found = 0;
    for (size_t i = 0; i < fmt_len; i++) {
        if (format[i] == '{') {
            start = i;
            found = 1;
            break;
        }
    }

    if (!found) {
        char* result = cm_str_alloc(fmt_len);
        for (size_t i = 0; i < fmt_len; i++) {
            result[i] = format[i];
        }
        result[fmt_len] = '\0';
        return result;
    }

    size_t end = start;
    for (size_t i = start + 1; i < fmt_len; i++) {
        if (format[i] == '}') {
            end = i;
            break;
        }
    }

    // フォーマット指定子を抽出（精度も含む）
    // 例: ":e", ":E", ":.2" など
    char spec = '\0';
    int precision = -1;
    
    for (size_t i = start + 1; i < end; i++) {
        if (format[i] == ':') {
            // ':' の後を解析
            size_t j = i + 1;
            // 精度指定があるか確認 (例: .2)
            if (j < end && format[j] == '.') {
                j++;
                precision = 0;
                while (j < end && format[j] >= '0' && format[j] <= '9') {
                    precision = precision * 10 + (format[j] - '0');
                    j++;
                }
            }
            // 型指定子があるか確認
            if (j < end) {
                spec = format[j];
            }
            break;
        }
    }
    
    // 指定子に応じた値の文字列化
    char* value_str;
    switch (spec) {
        case 'e':
            value_str = cm_format_double_exp(value);
            break;
        case 'E':
            value_str = cm_format_double_EXP(value);
            break;
        default:
            if (precision >= 0) {
                value_str = cm_format_double_precision(value, precision);
            } else {
                value_str = cm_format_double(value);
            }
            break;
    }
    
    return cm_format_replace(format, value_str);
}

char* cm_format_replace_string(const char* format, const char* value) {
    if (!format) return 0;
    if (!value) value = "";
    
    size_t fmt_len = wasm_strlen(format);
    
    // プレースホルダーを探す
    size_t start = 0;
    int found = 0;
    for (size_t i = 0; i < fmt_len; i++) {
        if (format[i] == '{') {
            start = i;
            found = 1;
            break;
        }
    }
    
    if (!found) {
        return cm_format_replace(format, value);
    }
    
    size_t end = start;
    for (size_t i = start + 1; i < fmt_len; i++) {
        if (format[i] == '}') {
            end = i;
            break;
        }
    }
    
    // フォーマット指定子を解析 {:align width}
    size_t placeholder_len = end - start + 1;
    char align = '\0';
    int width = 0;
    char fill_char = ' ';
    
    // 指定子を解析
    if (end > start + 1 && format[start + 1] == ':') {
        size_t spec_start = start + 2;
        
        // アライメントとフィル文字をチェック
        if (spec_start < end) {
            // {:<width}, {:>width}, {:^width} または {:0>width}
            char c = format[spec_start];
            if (c == '<' || c == '>' || c == '^') {
                align = c;
                spec_start++;
            } else if (spec_start + 1 < end && 
                       (format[spec_start + 1] == '<' || 
                        format[spec_start + 1] == '>' || 
                        format[spec_start + 1] == '^')) {
                fill_char = c;
                align = format[spec_start + 1];
                spec_start += 2;
            }
        }
        
        // 幅を解析
        while (spec_start < end && format[spec_start] >= '0' && format[spec_start] <= '9') {
            width = width * 10 + (format[spec_start] - '0');
            spec_start++;
        }
    }
    
    // 幅指定がある場合
    size_t val_len = wasm_strlen(value);
    char* formatted_value;
    
    if (width > 0 && (size_t)width > val_len && align != '\0') {
        formatted_value = cm_str_alloc(width);
        size_t padding = width - val_len;
        
        if (align == '<') {
            // 左揃え
            for (size_t i = 0; i < val_len; i++) formatted_value[i] = value[i];
            for (size_t i = val_len; i < (size_t)width; i++) formatted_value[i] = fill_char;
        } else if (align == '>') {
            // 右揃え
            for (size_t i = 0; i < padding; i++) formatted_value[i] = fill_char;
            for (size_t i = 0; i < val_len; i++) formatted_value[padding + i] = value[i];
        } else if (align == '^') {
            // 中央揃え
            size_t left_pad = padding / 2;
            for (size_t i = 0; i < left_pad; i++) formatted_value[i] = fill_char;
            for (size_t i = 0; i < val_len; i++) formatted_value[left_pad + i] = value[i];
            for (size_t i = left_pad + val_len; i < (size_t)width; i++) formatted_value[i] = fill_char;
        }
        formatted_value[width] = '\0';
    } else {
        formatted_value = cm_str_alloc(val_len);
        for (size_t i = 0; i < val_len; i++) formatted_value[i] = value[i];
        formatted_value[val_len] = '\0';
    }
    
    // 置換を実行
    size_t formatted_len = wasm_strlen(formatted_value);
    size_t result_len = fmt_len - placeholder_len + formatted_len + 1;
    char* result = (char*)wasm_alloc(result_len);
    
    size_t result_idx = 0;
    for (size_t i = 0; i < start; i++) {
        result[result_idx++] = format[i];
    }
    for (size_t i = 0; i < formatted_len; i++) {
        result[result_idx++] = formatted_value[i];
    }
    for (size_t i = end + 1; i < fmt_len; i++) {
        result[result_idx++] = format[i];
    }
    result[result_idx] = '\0';
    
    return result;
}

// ============================================================
// Format String Functions
// ============================================================
char* cm_format_string_1(const char* fmt, const char* arg1) {
    return cm_format_replace(fmt, arg1);
}

char* cm_format_string_2(const char* fmt, const char* arg1, const char* arg2) {
    char* temp = cm_format_replace(fmt, arg1);
    return cm_format_replace(temp, arg2);
}

char* cm_format_string_3(const char* fmt, const char* arg1, const char* arg2, const char* arg3) {
    char* temp1 = cm_format_replace(fmt, arg1);
    char* temp2 = cm_format_replace(temp1, arg2);
    return cm_format_replace(temp2, arg3);
}

char* cm_format_string_4(const char* fmt, const char* arg1, const char* arg2, const char* arg3, const char* arg4) {
    char* temp1 = cm_format_replace(fmt, arg1);
    char* temp2 = cm_format_replace(temp1, arg2);
    char* temp3 = cm_format_replace(temp2, arg3);
    return cm_format_replace(temp3, arg4);
}

// wasmでは可変長引数版は未使用（コード生成はcm_format_string_1〜4を使う）。リンク完全性のためのスタブだが、シグネチャはレジストリ宣言（fmt, argc, ...）と一致させる
char* cm_format_string(const char* fmt, int num_args, ...) {
    (void)num_args;
    return (char*)fmt;  // Simplified
}

// ============================================================
// String Compare (Cm runtime functions)
// ============================================================
int cm_strcmp(const char* s1, const char* s2) {
    if (!s1 && !s2) return 0;
    if (!s1) return -1;
    if (!s2) return 1;
    
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return (unsigned char)*s1 - (unsigned char)*s2;
}

int cm_strncmp(const char* s1, const char* s2, size_t n) {
    if (!s1 && !s2) return 0;
    if (!s1) return -1;
    if (!s2) return 1;
    
    while (n > 0 && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return (unsigned char)*s1 - (unsigned char)*s2;
}

// libc compatibility alias
int strcmp(const char* s1, const char* s2) {
    return cm_strcmp(s1, s2);
}

// ============================================================
// Array Methods (WASM)
// ============================================================

// スライス対応: サイズが負の場合、データ引数はCmSlice*であり展開する
// （HIRは可変長スライスのHOF呼び出しでサイズ-1を渡す）
typedef struct {
    void* data;
    int64_t len;
    int64_t cap;
    int64_t elem_size;
} CmHofSlice;

#define CM_HOF_UNWRAP(a, s)                        \
    do {                                           \
        if ((s) < 0) {                             \
            CmHofSlice* __cm_s = (CmHofSlice*)(a); \
            (a) = (void*)__cm_s->data;             \
            (s) = __cm_s->len;                     \
        }                                          \
    } while (0)

// indexOf: 要素の位置を検索
int32_t __builtin_array_indexOf_i64(int64_t* arr, int64_t size, int64_t value) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr) return -1;
    for (int64_t i = 0; i < size; i++) {
        if (arr[i] == value) return (int32_t)i;
    }
    return -1;
}

int32_t __builtin_array_indexOf_i32(int32_t* arr, int64_t size, int32_t value) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr) return -1;
    for (int64_t i = 0; i < size; i++) {
        if (arr[i] == value) return (int32_t)i;
    }
    return -1;
}

// includes: 要素が含まれているか
_Bool __builtin_array_includes_i64(int64_t* arr, int64_t size, int64_t value) {
    return __builtin_array_indexOf_i64(arr, size, value) >= 0;
}

_Bool __builtin_array_includes_i32(int32_t* arr, int64_t size, int32_t value) {
    return __builtin_array_indexOf_i32(arr, size, value) >= 0;
}

// some: いずれかの要素が条件を満たすか
_Bool __builtin_array_some_i64(int64_t* arr, int64_t size, _Bool (*predicate)(int64_t)) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !predicate) return 0;
    for (int64_t i = 0; i < size; i++) {
        if (predicate(arr[i])) return 1;
    }
    return 0;
}

_Bool __builtin_array_some_i32(int32_t* arr, int64_t size, _Bool (*predicate)(int32_t)) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !predicate) return 0;
    for (int64_t i = 0; i < size; i++) {
        if (predicate(arr[i])) return 1;
    }
    return 0;
}

// every: すべての要素が条件を満たすか
_Bool __builtin_array_every_i64(int64_t* arr, int64_t size, _Bool (*predicate)(int64_t)) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !predicate) return 1;
    for (int64_t i = 0; i < size; i++) {
        if (!predicate(arr[i])) return 0;
    }
    return 1;
}

_Bool __builtin_array_every_i32(int32_t* arr, int64_t size, _Bool (*predicate)(int32_t)) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !predicate) return 1;
    for (int64_t i = 0; i < size; i++) {
        if (!predicate(arr[i])) return 0;
    }
    return 1;
}

// findIndex: 条件を満たす最初の要素のインデックス
int32_t __builtin_array_findIndex_i64(int64_t* arr, int64_t size, _Bool (*predicate)(int64_t)) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !predicate) return -1;
    for (int64_t i = 0; i < size; i++) {
        if (predicate(arr[i])) return (int32_t)i;
    }
    return -1;
}

int32_t __builtin_array_findIndex_i32(int32_t* arr, int64_t size, _Bool (*predicate)(int32_t)) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !predicate) return -1;
    for (int64_t i = 0; i < size; i++) {
        if (predicate(arr[i])) return (int32_t)i;
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

// sortBy: カスタム比較関数でソートしたコピーを返す
void* __builtin_array_sortBy_i32(int32_t* arr, int64_t size, int (*comparator)(int32_t, int32_t)) {
    CM_HOF_UNWRAP(arr, size);
    CmSlice* slice = (CmSlice*)wasm_alloc(sizeof(CmSlice));
    if (!slice) return NULL;
    
    if (!arr || size <= 0 || !comparator) {
        slice->data = NULL;
        slice->len = 0;
        return slice;
    }
    
    int32_t* result = (int32_t*)wasm_alloc(size * sizeof(int32_t));
    if (!result) {
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
    return slice;
}

void* __builtin_array_sortBy_i64(int64_t* arr, int64_t size, int (*comparator)(int64_t, int64_t)) {
    CM_HOF_UNWRAP(arr, size);
    CmSlice* slice = (CmSlice*)wasm_alloc(sizeof(CmSlice));
    if (!slice) return NULL;
    
    if (!arr || size <= 0 || !comparator) {
        slice->data = NULL;
        slice->len = 0;
        return slice;
    }
    
    int64_t* result = (int64_t*)wasm_alloc(size * sizeof(int64_t));
    if (!result) {
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
    return slice;
}

void* __builtin_array_sortBy(int32_t* arr, int64_t size, int (*comparator)(int32_t, int32_t)) {
    return __builtin_array_sortBy_i32(arr, size, comparator);
}

// forEach: 各要素に関数を適用
void __builtin_array_forEach_i64(int64_t* arr, int64_t size, void (*callback)(int64_t)) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !callback) return;
    for (int64_t i = 0; i < size; i++) {
        callback(arr[i]);
    }
}

void __builtin_array_forEach_i32(int32_t* arr, int64_t size, void (*callback)(int32_t)) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !callback) return;
    for (int64_t i = 0; i < size; i++) {
        callback(arr[i]);
    }
}

// reduce: 要素を畳み込み
int64_t __builtin_array_reduce_i64(int64_t* arr, int64_t size, 
                                   int64_t (*callback)(int64_t, int64_t), int64_t init) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !callback) return init;
    int64_t acc = init;
    for (int64_t i = 0; i < size; i++) {
        acc = callback(acc, arr[i]);
    }
    return acc;
}

int32_t __builtin_array_reduce_i32(int32_t* arr, int64_t size,
                                   int32_t (*callback)(int32_t, int32_t), int32_t init) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !callback) return init;
    int32_t acc = init;
    for (int64_t i = 0; i < size; i++) {
        acc = callback(acc, arr[i]);
    }
    return acc;
}

// ============================================================
// Array first/last/find Functions
// ============================================================

// first: 配列の最初の要素を返す
int32_t __builtin_array_first_i32(int32_t* arr, int64_t size) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || size <= 0) return 0;
    return arr[0];
}

int64_t __builtin_array_first_i64(int64_t* arr, int64_t size) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || size <= 0) return 0;
    return arr[0];
}

// last: 配列の最後の要素を返す
int32_t __builtin_array_last_i32(int32_t* arr, int64_t size) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || size <= 0) return 0;
    return arr[size - 1];
}

int64_t __builtin_array_last_i64(int64_t* arr, int64_t size) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || size <= 0) return 0;
    return arr[size - 1];
}

// find: 条件に合う最初の要素を返す
int32_t __builtin_array_find_i32(int32_t* arr, int64_t size, _Bool (*predicate)(int32_t)) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !predicate) return 0;
    for (int64_t i = 0; i < size; i++) {
        if (predicate(arr[i])) return arr[i];
    }
    return 0;
}

int64_t __builtin_array_find_i64(int64_t* arr, int64_t size, _Bool (*predicate)(int64_t)) {
    CM_HOF_UNWRAP(arr, size);
    if (!arr || !predicate) return 0;
    for (int64_t i = 0; i < size; i++) {
        if (predicate(arr[i])) return arr[i];
    }
    return 0;
}

// ============================================================
// Array reverse/sort Functions (returning CmSlice)
// ============================================================

typedef struct {
    void* data;
    int64_t len;
    int64_t cap;
    int64_t elem_size;
} CmSlice_wasm;

// reverse: 配列を逆順にしたコピーを返す
void* __builtin_array_reverse_i32(int32_t* arr, int64_t size) {
    CM_HOF_UNWRAP(arr, size);
    CmSlice_wasm* slice = (CmSlice_wasm*)wasm_alloc(sizeof(CmSlice_wasm));
    if (!slice) return NULL;
    
    if (!arr || size <= 0) {
        slice->data = NULL;
        slice->len = 0;
        slice->cap = 0;
        slice->elem_size = sizeof(int32_t);
        return slice;
    }
    
    int32_t* result = (int32_t*)wasm_alloc(size * sizeof(int32_t));
    if (!result) {
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
    CmSlice_wasm* slice = (CmSlice_wasm*)wasm_alloc(sizeof(CmSlice_wasm));
    if (!slice) return NULL;
    
    if (!arr || size <= 0) {
        slice->data = NULL;
        slice->len = 0;
        slice->cap = 0;
        slice->elem_size = sizeof(int64_t);
        return slice;
    }
    
    int64_t* result = (int64_t*)wasm_alloc(size * sizeof(int64_t));
    if (!result) {
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

void* __builtin_array_reverse(int32_t* arr, int64_t size) {
    return __builtin_array_reverse_i32(arr, size);
}

// sort: 配列をソートしたコピーを返す（単純なバブルソート）
static void wasm_sort_i32(int32_t* arr, int64_t size) {
    for (int64_t i = 0; i < size - 1; i++) {
        for (int64_t j = 0; j < size - i - 1; j++) {
            if (arr[j] > arr[j + 1]) {
                int32_t temp = arr[j];
                arr[j] = arr[j + 1];
                arr[j + 1] = temp;
            }
        }
    }
}

static void wasm_sort_i64(int64_t* arr, int64_t size) {
    for (int64_t i = 0; i < size - 1; i++) {
        for (int64_t j = 0; j < size - i - 1; j++) {
            if (arr[j] > arr[j + 1]) {
                int64_t temp = arr[j];
                arr[j] = arr[j + 1];
                arr[j + 1] = temp;
            }
        }
    }
}

void* __builtin_array_sort_i32(int32_t* arr, int64_t size) {
    CM_HOF_UNWRAP(arr, size);
    CmSlice_wasm* slice = (CmSlice_wasm*)wasm_alloc(sizeof(CmSlice_wasm));
    if (!slice) return NULL;
    
    if (!arr || size <= 0) {
        slice->data = NULL;
        slice->len = 0;
        slice->cap = 0;
        slice->elem_size = sizeof(int32_t);
        return slice;
    }
    
    int32_t* result = (int32_t*)wasm_alloc(size * sizeof(int32_t));
    if (!result) {
        return NULL;
    }
    // コピー
    for (int64_t i = 0; i < size; i++) {
        result[i] = arr[i];
    }
    wasm_sort_i32(result, size);
    
    slice->data = result;
    slice->len = size;
    slice->cap = size;
    slice->elem_size = sizeof(int32_t);
    return slice;
}

void* __builtin_array_sort_i64(int64_t* arr, int64_t size) {
    CM_HOF_UNWRAP(arr, size);
    CmSlice_wasm* slice = (CmSlice_wasm*)wasm_alloc(sizeof(CmSlice_wasm));
    if (!slice) return NULL;
    
    if (!arr || size <= 0) {
        slice->data = NULL;
        slice->len = 0;
        slice->cap = 0;
        slice->elem_size = sizeof(int64_t);
        return slice;
    }
    
    int64_t* result = (int64_t*)wasm_alloc(size * sizeof(int64_t));
    if (!result) {
        return NULL;
    }
    // コピー
    for (int64_t i = 0; i < size; i++) {
        result[i] = arr[i];
    }
    wasm_sort_i64(result, size);
    
    slice->data = result;
    slice->len = size;
    slice->cap = size;
    slice->elem_size = sizeof(int64_t);
    return slice;
}

void* __builtin_array_sort(int32_t* arr, int64_t size) {
    return __builtin_array_sort_i32(arr, size);
}


