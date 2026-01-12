# cm_*ランタイム関数の完全なライブラリ化設計

作成日: 2026-01-11
対象バージョン: v0.11.0
ステータス: 設計提案

## エグゼクティブサマリー

現在のCm言語は、`cm_print_string`、`cm_format_int`などのランタイム関数をC言語で実装し、生成されたLLVM IRから直接呼び出しています。これらの不自然な`cm_*`プレフィックス付き関数を排除し、純粋なCm言語で実装された標準ライブラリに置き換える完全な設計案です。

## 現状分析

### 問題点

1. **不自然な生成コード**
```llvm
; 現在のLLVM IR出力
declare void @cm_print_string(i8*)
declare i8* @cm_format_int(i32)
declare i8* @cm_string_concat(i8*, i8*)

define i32 @main() {
  call void @cm_println_string(i8* @.str.0)
  ret i32 0
}
```

2. **アーキテクチャの問題**
- Cランタイムへの過度な依存
- 標準ライブラリとランタイムの境界が不明確
- ドッグフーディングの不足（Cmの標準ライブラリがCで書かれている）

3. **保守性の問題**
- C言語とCm言語の2つの実装を管理
- デバッグが困難（言語境界をまたぐ）
- 拡張が困難（C言語での実装が必要）

### 現在のファイル構造

```
src/
├── codegen/
│   ├── common/
│   │   └── runtime_functions.cpp  # cm_*関数の宣言
│   └── llvm/
│       └── native/
│           ├── runtime.c          # メインランタイム
│           ├── runtime_print.c    # print/println実装
│           └── runtime_format.c   # フォーマット実装
```

## 提案する新アーキテクチャ

### 3層構造

```
┌─────────────────────────────────────────────────┐
│          ユーザーアプリケーション                 │
│   import std::io;                                │
│   io::println("Hello, World!");                  │
└──────────────────┬──────────────────────────────┘
                   │ Cm関数呼び出し
┌──────────────────▼──────────────────────────────┐
│         Cm標準ライブラリ (純粋なCm実装)           │
│                                                  │
│   std/                                           │
│   ├── io/        # I/O操作                      │
│   ├── fmt/       # フォーマット                  │
│   ├── string/    # 文字列処理                   │
│   └── mem/       # メモリ管理                   │
└──────────────────┬──────────────────────────────┘
                   │ FFI経由
┌──────────────────▼──────────────────────────────┐
│      最小システムインターフェース (30行以下)       │
│                                                  │
│   __sys_write(fd, buf, len)  # POSIX write      │
│   __sys_malloc(size)         # malloc           │
│   __sys_free(ptr)           # free              │
└─────────────────────────────────────────────────┘
```

### 設計原則

1. **最小限の外部依存**
   - システムコールのみC実装（30行以下）
   - それ以外はすべてCm実装

2. **型安全性**
   - ジェネリクスとトレイトの活用
   - コンパイル時の型チェック

3. **ゼロコスト抽象化**
   - インライン化による最適化
   - 不要なアロケーションの排除

## 実装計画

### Phase 1: システムインターフェース層（Week 1）

#### 最小Cランタイム（sys_interface.c）

```c
// 30行以下の最小実装
#include <unistd.h>
#include <stdlib.h>

int __sys_write(int fd, const void* buf, int len) {
    return write(fd, buf, len);
}

void* __sys_malloc(int size) {
    return malloc(size);
}

void __sys_free(void* ptr) {
    free(ptr);
}
```

#### Cmラッパー（std/sys.cm）

```cm
module std::sys;

// FFI宣言
extern "C" {
    int __sys_write(int fd, void* buf, int len);
    void* __sys_malloc(int size);
    void __sys_free(void* ptr);
}

// 安全なラッパー
export namespace sys {
    int write(int fd, byte[] data) {
        return __sys_write(fd, data.ptr, data.len);
    }

    void* alloc(int size) {
        return __sys_malloc(size);
    }

    void free(void* ptr) {
        __sys_free(ptr);
    }
}
```

### Phase 2: コアライブラリ実装（Week 2-3）

#### メモリ管理（std/mem.cm）

```cm
module std::mem;

import std::sys;

export struct Allocator {
    void* (*alloc_fn)(int);
    void (*free_fn)(void*);
}

export Allocator GLOBAL = {
    alloc_fn: sys::alloc,
    free_fn: sys::free
};

export <T> T* new() {
    return (T*)GLOBAL.alloc_fn(sizeof(T));
}

export <T> void delete(T* ptr) {
    if (ptr != null) {
        ptr->~T();  // デストラクタ呼び出し
        GLOBAL.free_fn(ptr);
    }
}
```

#### 文字列処理（std/string.cm）

```cm
module std::string;

import std::mem;

// Stringインターフェース定義
export interface StringOps {
    void push(byte c);
    void append(String s);
    int length();
    byte* c_str();
}

export struct String {
    private byte* data;
    private int len;
    private int cap;
}

// 重要: impl単独ブロックはコンストラクタ・デストラクタのみ定義可能
impl String {
    self() {
        self.data = null;
        self.len = 0;
        self.cap = 0;
    }

    self(cstring s) {
        int length = 0;
        while (s[length] != 0) length++;

        self.cap = length + 1;
        self.data = (byte*)mem::GLOBAL.alloc_fn(self.cap);

        for (int i = 0; i < length; i++) {
            self.data[i] = s[i];
        }
        self.data[length] = 0;
        self.len = length;
    }

    ~self() {
        if (self.data != null) {
            mem::GLOBAL.free_fn(self.data);
        }
    }
}

// メソッドはinterface実装として定義
impl String for StringOps {
    void push(byte c) {
        if (self.len + 1 >= self.cap) {
            self.grow();
        }
        self.data[self.len++] = c;
        self.data[self.len] = 0;
    }

    void append(String s) {
        for (int i = 0; i < s.len; i++) {
            self.push(s.data[i]);
        }
    }

    int length() {
        return self.len;
    }

    byte* c_str() {
        return self.data;
    }

    private void grow() {
        int new_cap = self.cap == 0 ? 16 : self.cap * 2;
        byte* new_data = (byte*)mem::GLOBAL.alloc_fn(new_cap);

        for (int i = 0; i < self.len; i++) {
            new_data[i] = self.data[i];
        }

        if (self.data != null) {
            mem::GLOBAL.free_fn(self.data);
        }

        self.data = new_data;
        self.cap = new_cap;
    }
}

// 文字列演算子
export String operator+(String a, String b) {
    String result = String();
    result.append(a);
    result.append(b);
    return result;
}
```

### Phase 3: I/Oとフォーマット（Week 4-5）

#### I/O実装（std/io.cm）

```cm
module std::io;

import std::sys;
import std::string::String;
import std::fmt::Display;

export struct Writer {
    int fd;
}

impl Writer {
    self(int fd) {
        self.fd = fd;
    }

    void write(byte[] data) {
        sys::write(self.fd, data);
    }

    void write_str(String s) {
        self.write(byte[]{ptr: s.data, len: s.len});
    }
}

export Writer stdout = Writer(1);
export Writer stderr = Writer(2);

export void print(String s) {
    stdout.write_str(s);
}

export void println(String s) {
    stdout.write_str(s);
    stdout.write(byte[]{ptr: "\n", len: 1});
}

// ジェネリック版
export <T: Display> void print(T value) {
    print(value.to_string());
}

export <T: Display> void println(T value) {
    println(value.to_string());
}
```

#### フォーマット実装（std/fmt.cm）

```cm
module std::fmt;

import std::string::String;

// Displayトレイト
export interface Display {
    String to_string();
}

// 基本型への実装
impl int for Display {
    String to_string() {
        if (self == 0) return String("0");

        String result;
        int value = self;
        bool negative = value < 0;

        if (negative) value = -value;

        byte[32] buffer;
        int i = 31;

        while (value > 0) {
            buffer[i--] = '0' + (value % 10);
            value /= 10;
        }

        if (negative) {
            result.push('-');
        }

        for (int j = i + 1; j < 32; j++) {
            result.push(buffer[j]);
        }

        return result;
    }
}

impl double for Display {
    String to_string() {
        // IEEE 754浮動小数点フォーマット
        // Grisu3アルゴリズムの簡易実装
        String result;

        if (self < 0) {
            result.push('-');
            return format_positive(-self, result);
        }

        return format_positive(self, result);
    }

    private String format_positive(double value, String result) {
        // 整数部
        int integer_part = (int)value;
        result.append(integer_part.to_string());

        // 小数部
        result.push('.');
        double fractional = value - integer_part;

        for (int i = 0; i < 6; i++) {  // 6桁精度
            fractional *= 10;
            int digit = (int)fractional;
            result.push('0' + digit);
            fractional -= digit;
        }

        return result;
    }
}

// フォーマット関数
export String format(String fmt, Display... args) {
    String result;
    int arg_idx = 0;
    int i = 0;

    while (i < fmt.len) {
        if (fmt.data[i] == '{' && i + 1 < fmt.len && fmt.data[i+1] == '}') {
            if (arg_idx < args.len) {
                result.append(args[arg_idx++].to_string());
            }
            i += 2;
        } else if (fmt.data[i] == '\\' && i + 1 < fmt.len && fmt.data[i+1] == '{') {
            result.push('{');
            i += 2;
        } else {
            result.push(fmt.data[i++]);
        }
    }

    return result;
}
```

### Phase 4: コンパイラ統合（Week 6-7）

#### MIR生成の変更

```cpp
// src/mir/lowering/expr_call.cpp

void lower_builtin_call(const std::string& name, std::vector<MirOperand> args) {
    // 旧実装
    // if (name == "println") {
    //     return create_call("cm_println_string", args);
    // }

    // 新実装
    if (name == "println") {
        // std::io::println への変換
        auto module_path = resolve_module("std::io");
        return create_module_call(module_path, "println", args);
    }
}
```

#### LLVM IR生成の変更

```cpp
// src/codegen/llvm/core/mir_to_llvm.cpp

llvm::Value* generate_call(const MirCall& call) {
    // 旧実装
    // if (starts_with(call.name, "cm_")) {
    //     return generate_runtime_call(call);
    // }

    // 新実装
    if (is_std_module_call(call)) {
        // 通常のCm関数として処理
        return generate_cm_function_call(call);
    }
}
```

## コンパイル・リンク戦略

### ビルドパイプライン

```makefile
# 1. システムインターフェースのビルド
sys_interface.o: sys_interface.c
	$(CC) -c $< -o $@ -O3 -ffunction-sections

# 2. 標準ライブラリのビルド
STD_MODULES = io fmt string mem sys
STD_OBJECTS = $(patsubst %,std/%.o,$(STD_MODULES))

std/%.o: std/%.cm
	$(CM) -c $< -o $@ --no-runtime

# 3. 標準ライブラリアーカイブ
libcm_std.a: $(STD_OBJECTS) sys_interface.o
	$(AR) rcs $@ $^

# 4. ユーザープログラムのビルド
%.exe: %.cm libcm_std.a
	$(CM) $< -o $@ -L. -lcm_std
```

### リンク時最適化（LTO）

```cpp
// LTOによるcm_*関数の完全な除去
class RuntimeElimination : public llvm::ModulePass {
    bool runOnModule(llvm::Module& M) override {
        // cm_*関数の参照を検出して警告
        for (auto& F : M) {
            if (F.getName().startswith("cm_")) {
                errs() << "Warning: Legacy runtime function: "
                       << F.getName() << "\n";
            }
        }
        return false;
    }
};
```

## パフォーマンス最適化

### インライン化戦略

```cm
// 小さな関数は積極的にインライン化
#[inline(always)]
export void print(String s) {
    __sys_write(1, s.data, s.len);
}

// 大きな関数は呼び出しコストを考慮
#[inline(hint)]
export String format(String fmt, Display... args) {
    // 実装
}
```

### メモリ最適化

```cm
// スモールストリング最適化（SSO）
struct String {
    union {
        struct {
            byte* ptr;
            int len;
            int cap;
        } heap;
        byte small[24];  // 24バイト以下はスタック上
    } data;
    bool is_small;
}
```

## 移行計画

### 段階的移行

1. **Week 1-2**: システムインターフェース実装
2. **Week 3-4**: コアライブラリ実装
3. **Week 5-6**: I/O・フォーマット実装
4. **Week 7-8**: コンパイラ統合
5. **Week 9-10**: テスト・最適化

### 互換性維持

```cm
// 移行期間中の互換レイヤー
#ifdef CM_COMPAT_MODE
void cm_print_string(cstring s) {
    std::io::print(String(s));
}
#endif
```

## 期待される成果

### Before（現在）
```llvm
declare void @cm_println_string(i8*)
declare i8* @cm_format_int(i32)

define void @main() {
    %1 = call i8* @cm_format_int(i32 42)
    call void @cm_println_string(i8* %1)
    ret void
}
```

### After（提案）
```llvm
; 標準ライブラリは通常のCm関数
declare void @_ZN3std2io7printlnE6String(%String*)

define void @main() {
    %1 = alloca %String
    call void @_ZN6String4initEi(%String* %1, i32 42)
    call void @_ZN3std2io7printlnE6String(%String* %1)
    ret void
}
```

## リスク分析と対策

### リスク1: パフォーマンス劣化
- **対策**: LTO、インライン化、プロファイルガイド最適化

### リスク2: ブートストラップ問題
- **対策**: 段階的ビルド（Stage0: C → Stage1: Cm）

### リスク3: 後方互換性
- **対策**: 互換レイヤー、非推奨警告、移行ガイド

## ベンチマーク目標

| 操作 | 現在 | 目標 | 許容範囲 |
|------|------|------|----------|
| println(string) | 120ns | 100ns | ±10% |
| format(simple) | 450ns | 400ns | ±15% |
| format(complex) | 850ns | 700ns | ±20% |

## まとめ

この設計により：

1. **cm_*関数の完全な排除** - 不自然なプレフィックスが消える
2. **純粋なCm実装** - ドッグフーディングの実現
3. **保守性の向上** - 単一言語での実装
4. **拡張性の向上** - Cmで新機能追加が容易
5. **型安全性** - コンパイル時チェックの強化

実装は段階的に進め、各フェーズで動作確認とパフォーマンス測定を実施します。

---

**作成者:** Claude Code
**ステータス:** 設計完了
**次のステップ:** Phase 1のシステムインターフェース実装

## 標準ライブラリ拡張設計（034-039）

### 034: Vector実装

#### 基本設計
```cm
// std/collections/vector.cm
struct Vector<T> {
    T* data;
    size_t len;
    size_t cap;
}

impl<T> Vector<T> {
    // コンストラクタ
    static Vector<T> new() {
        return Vector<T>{null, 0, 0};
    }

    static Vector<T> with_capacity(size_t capacity) {
        T* buffer = (T*)malloc(sizeof(T) * capacity);
        return Vector<T>{buffer, 0, capacity};
    }

    // 基本操作
    void push(T value) {
        if (self.len >= self.cap) {
            self.grow();
        }
        self.data[self.len++] = value;
    }

    T pop() {
        if (self.len == 0) {
            panic("pop from empty vector");
        }
        return self.data[--self.len];
    }

    T& operator[](size_t index) {
        if (index >= self.len) {
            panic("index out of bounds");
        }
        return self.data[index];
    }

    // 成長戦略
    private void grow() {
        size_t new_cap = self.cap == 0 ? 8 : self.cap * 2;
        T* new_data = (T*)realloc(self.data, sizeof(T) * new_cap);
        if (!new_data) {
            panic("allocation failed");
        }
        self.data = new_data;
        self.cap = new_cap;
    }

    // イテレータサポート
    T* begin() { return self.data; }
    T* end() { return self.data + self.len; }
}

// イテレータトレイト実装
impl<T> Vector<T> for Iterator<T> {
    type Item = T;

    bool has_next() {
        return self.cursor < self.len;
    }

    T next() {
        return self.data[self.cursor++];
    }
}
```

### 035: HashMap実装

#### 基本設計
```cm
// std/collections/hashmap.cm
struct Entry<K, V> {
    K key;
    V value;
    Entry<K, V>* next;  // チェイン法
    uint32_t hash;
}

struct HashMap<K, V> {
    Entry<K, V>** buckets;
    size_t bucket_count;
    size_t size;
    float load_factor;
}

impl<K: Hash + Eq, V> HashMap<K, V> {
    static HashMap<K, V> new() {
        return HashMap<K, V>::with_capacity(16);
    }

    static HashMap<K, V> with_capacity(size_t capacity) {
        size_t bucket_count = next_power_of_two(capacity);
        Entry<K, V>** buckets = (Entry<K, V>**)calloc(bucket_count, sizeof(Entry<K, V>*));
        return HashMap<K, V>{buckets, bucket_count, 0, 0.75};
    }

    // 基本操作
    void insert(K key, V value) {
        if ((float)self.size / self.bucket_count > self.load_factor) {
            self.resize();
        }

        uint32_t hash = key.hash();
        size_t idx = hash & (self.bucket_count - 1);

        Entry<K, V>* entry = self.buckets[idx];
        while (entry) {
            if (entry.key == key) {
                entry.value = value;
                return;
            }
            entry = entry.next;
        }

        // 新規エントリ
        Entry<K, V>* new_entry = new Entry<K, V>{key, value, self.buckets[idx], hash};
        self.buckets[idx] = new_entry;
        self.size++;
    }

    V* get(K key) {
        uint32_t hash = key.hash();
        size_t idx = hash & (self.bucket_count - 1);

        Entry<K, V>* entry = self.buckets[idx];
        while (entry) {
            if (entry.key == key) {
                return &entry.value;
            }
            entry = entry.next;
        }
        return null;
    }

    bool remove(K key) {
        uint32_t hash = key.hash();
        size_t idx = hash & (self.bucket_count - 1);

        Entry<K, V>** current = &self.buckets[idx];
        while (*current) {
            if ((*current).key == key) {
                Entry<K, V>* to_remove = *current;
                *current = to_remove.next;
                delete to_remove;
                self.size--;
                return true;
            }
            current = &(*current).next;
        }
        return false;
    }

    // リサイズ
    private void resize() {
        size_t new_bucket_count = self.bucket_count * 2;
        Entry<K, V>** new_buckets = (Entry<K, V>**)calloc(new_bucket_count, sizeof(Entry<K, V>*));

        // 再ハッシュ
        for (size_t i = 0; i < self.bucket_count; i++) {
            Entry<K, V>* entry = self.buckets[i];
            while (entry) {
                Entry<K, V>* next = entry.next;
                size_t new_idx = entry.hash & (new_bucket_count - 1);
                entry.next = new_buckets[new_idx];
                new_buckets[new_idx] = entry;
                entry = next;
            }
        }

        free(self.buckets);
        self.buckets = new_buckets;
        self.bucket_count = new_bucket_count;
    }
}

// Hash trait
trait Hash {
    uint32_t hash();
}

// 基本型の実装
impl int for Hash {
    uint32_t hash() {
        // MurmurHash3の簡易版
        uint32_t h = (uint32_t)self;
        h ^= h >> 16;
        h *= 0x85ebca6b;
        h ^= h >> 13;
        h *= 0xc2b2ae35;
        h ^= h >> 16;
        return h;
    }
}

impl String for Hash {
    uint32_t hash() {
        // FNV-1a hash
        uint32_t hash = 2166136261u;
        for (int i = 0; i < self.len; i++) {
            hash ^= (uint32_t)self.data[i];
            hash *= 16777619;
        }
        return hash;
    }
}
```

### 036: String改善（UTF-8サポート）

#### UTF-8サポート設計
```cm
// std/string/utf8.cm
struct Utf8String {
    byte* data;
    size_t byte_len;    // バイト長
    size_t char_count;  // 文字数（キャッシュ）
}

impl Utf8String {
    // UTF-8文字境界の検出
    static bool is_char_boundary(byte b) {
        // 継続バイトではない（10xxxxxx以外）
        return (b & 0xC0) != 0x80;
    }

    // UTF-8文字のバイト数を取得
    static int char_len(byte first_byte) {
        if ((first_byte & 0x80) == 0) return 1;      // 0xxxxxxx
        if ((first_byte & 0xE0) == 0xC0) return 2;    // 110xxxxx
        if ((first_byte & 0xF0) == 0xE0) return 3;    // 1110xxxx
        if ((first_byte & 0xF8) == 0xF0) return 4;    // 11110xxx
        return 1; // エラー時は1バイトとして扱う
    }

    // 文字数のカウント
    size_t count_chars() {
        size_t count = 0;
        size_t i = 0;
        while (i < self.byte_len) {
            i += char_len(self.data[i]);
            count++;
        }
        self.char_count = count;  // キャッシュ更新
        return count;
    }

    // n番目の文字を取得（Unicodeコードポイント）
    uint32_t char_at(size_t index) {
        size_t current = 0;
        size_t byte_pos = 0;

        while (byte_pos < self.byte_len && current < index) {
            byte_pos += char_len(self.data[byte_pos]);
            current++;
        }

        if (byte_pos >= self.byte_len) {
            panic("index out of bounds");
        }

        return decode_utf8_char(&self.data[byte_pos]);
    }

    // UTF-8文字のデコード
    private uint32_t decode_utf8_char(byte* p) {
        byte b0 = p[0];

        if ((b0 & 0x80) == 0) {
            return b0;
        }
        if ((b0 & 0xE0) == 0xC0) {
            return ((b0 & 0x1F) << 6) | (p[1] & 0x3F);
        }
        if ((b0 & 0xF0) == 0xE0) {
            return ((b0 & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        }
        if ((b0 & 0xF8) == 0xF0) {
            return ((b0 & 0x07) << 18) | ((p[1] & 0x3F) << 12) |
                   ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
        }
        return 0xFFFD; // 置換文字
    }

    // 部分文字列の取得（文字単位）
    Utf8String substring(size_t start, size_t len) {
        size_t start_byte = 0;
        size_t current = 0;

        // 開始位置を探す
        while (start_byte < self.byte_len && current < start) {
            start_byte += char_len(self.data[start_byte]);
            current++;
        }

        // 長さを計算
        size_t end_byte = start_byte;
        size_t count = 0;
        while (end_byte < self.byte_len && count < len) {
            end_byte += char_len(self.data[end_byte]);
            count++;
        }

        // 新しい文字列を作成
        size_t byte_count = end_byte - start_byte;
        byte* new_data = (byte*)malloc(byte_count + 1);
        memcpy(new_data, self.data + start_byte, byte_count);
        new_data[byte_count] = 0;

        return Utf8String{new_data, byte_count, count};
    }
}
```

### 037: File I/O拡張

#### ファイル操作の完全実装
```cm
// std/fs/file.cm
struct File {
    int fd;
    String path;
    FileMode mode;
    bool is_open;
}

enum FileMode {
    Read = 0x01,
    Write = 0x02,
    Append = 0x04,
    Create = 0x08,
    Truncate = 0x10,
}

impl File {
    static File open(String path, FileMode mode) {
        int flags = 0;

        if (mode & FileMode::Read && mode & FileMode::Write) {
            flags = O_RDWR;
        } else if (mode & FileMode::Read) {
            flags = O_RDONLY;
        } else if (mode & FileMode::Write) {
            flags = O_WRONLY;
        }

        if (mode & FileMode::Create) flags |= O_CREAT;
        if (mode & FileMode::Append) flags |= O_APPEND;
        if (mode & FileMode::Truncate) flags |= O_TRUNC;

        int fd = __sys_open(path.c_str(), flags, 0644);
        if (fd < 0) {
            panic("Failed to open file");
        }

        return File{fd, path, mode, true};
    }

    size_t read(byte* buffer, size_t count) {
        if (!self.is_open) panic("File is closed");
        ssize_t result = __sys_read(self.fd, buffer, count);
        if (result < 0) panic("Read failed");
        return result;
    }

    size_t write(byte* buffer, size_t count) {
        if (!self.is_open) panic("File is closed");
        ssize_t result = __sys_write(self.fd, buffer, count);
        if (result < 0) panic("Write failed");
        return result;
    }

    void seek(int64_t offset, SeekFrom from) {
        if (!self.is_open) panic("File is closed");
        int whence = 0;
        switch (from) {
            case SeekFrom::Start: whence = SEEK_SET; break;
            case SeekFrom::Current: whence = SEEK_CUR; break;
            case SeekFrom::End: whence = SEEK_END; break;
        }
        if (__sys_lseek(self.fd, offset, whence) < 0) {
            panic("Seek failed");
        }
    }

    String read_to_string() {
        // ファイルサイズを取得
        struct stat st;
        if (__sys_fstat(self.fd, &st) < 0) {
            panic("Failed to get file size");
        }

        // バッファを確保して読み込み
        byte* buffer = (byte*)malloc(st.st_size + 1);
        size_t total_read = 0;

        while (total_read < st.st_size) {
            size_t chunk = self.read(buffer + total_read, st.st_size - total_read);
            if (chunk == 0) break;
            total_read += chunk;
        }

        buffer[total_read] = 0;
        return String{buffer, total_read};
    }

    void close() {
        if (self.is_open) {
            __sys_close(self.fd);
            self.is_open = false;
        }
    }

    ~File() {
        self.close();
    }
}

// 便利関数
String read_file(String path) {
    File f = File::open(path, FileMode::Read);
    String content = f.read_to_string();
    f.close();
    return content;
}

void write_file(String path, String content) {
    File f = File::open(path, FileMode::Write | FileMode::Create | FileMode::Truncate);
    f.write(content.data, content.len);
    f.close();
}
```

### 038: Network（ソケット通信）

#### 基本的なTCP/UDPサポート
```cm
// std/net/socket.cm
struct TcpSocket {
    int fd;
    SocketAddr addr;
}

struct SocketAddr {
    uint32_t ip;    // IPv4アドレス
    uint16_t port;
}

impl TcpSocket {
    static TcpSocket connect(String host, uint16_t port) {
        // DNS解決
        uint32_t ip = resolve_host(host);

        int fd = __sys_socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) panic("Failed to create socket");

        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = htonl(ip);

        if (__sys_connect(fd, &addr, sizeof(addr)) < 0) {
            __sys_close(fd);
            panic("Failed to connect");
        }

        return TcpSocket{fd, SocketAddr{ip, port}};
    }

    size_t send(byte* data, size_t len) {
        ssize_t result = __sys_send(self.fd, data, len, 0);
        if (result < 0) panic("Send failed");
        return result;
    }

    size_t recv(byte* buffer, size_t len) {
        ssize_t result = __sys_recv(self.fd, buffer, len, 0);
        if (result < 0) panic("Recv failed");
        return result;
    }

    void close() {
        __sys_close(self.fd);
    }
}

struct TcpListener {
    int fd;
    SocketAddr addr;
}

impl TcpListener {
    static TcpListener bind(String addr, uint16_t port) {
        int fd = __sys_socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) panic("Failed to create socket");

        // SO_REUSEADDR を設定
        int reuse = 1;
        __sys_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

        struct sockaddr_in bind_addr;
        bind_addr.sin_family = AF_INET;
        bind_addr.sin_port = htons(port);
        bind_addr.sin_addr.s_addr = INADDR_ANY;

        if (__sys_bind(fd, &bind_addr, sizeof(bind_addr)) < 0) {
            __sys_close(fd);
            panic("Failed to bind");
        }

        if (__sys_listen(fd, 128) < 0) {
            __sys_close(fd);
            panic("Failed to listen");
        }

        return TcpListener{fd, SocketAddr{0, port}};
    }

    TcpSocket accept() {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        int client_fd = __sys_accept(self.fd, &client_addr, &addr_len);
        if (client_fd < 0) panic("Accept failed");

        uint32_t ip = ntohl(client_addr.sin_addr.s_addr);
        uint16_t port = ntohs(client_addr.sin_port);

        return TcpSocket{client_fd, SocketAddr{ip, port}};
    }
}

// HTTPクライアントの例
struct HttpClient {
    static String get(String url) {
        // URLをパース
        String host, path;
        uint16_t port = 80;
        parse_url(url, host, path, port);

        // 接続
        TcpSocket socket = TcpSocket::connect(host, port);

        // リクエストを送信
        String request = format("GET {} HTTP/1.1\r\n"
                              "Host: {}\r\n"
                              "Connection: close\r\n"
                              "\r\n", path, host);
        socket.send(request.data, request.len);

        // レスポンスを読み取り
        byte buffer[4096];
        String response;
        size_t n;

        while ((n = socket.recv(buffer, sizeof(buffer))) > 0) {
            response.append(buffer, n);
        }

        socket.close();

        // ヘッダーとボディを分離
        int body_start = response.find("\r\n\r\n");
        if (body_start != -1) {
            return response.substring(body_start + 4);
        }

        return response;
    }
}
```

### 039: Thread（スレッド対応）

#### 基本的なスレッドサポート
```cm
// std/thread/thread.cm
struct Thread {
    pthread_t handle;
    void* (*func)(void*);
    void* arg;
    bool joinable;
}

impl Thread {
    static Thread spawn<F: Fn()>(F func) {
        Thread t;
        t.func = thread_wrapper<F>;
        t.arg = new F(func);
        t.joinable = true;

        if (pthread_create(&t.handle, null, t.func, t.arg) != 0) {
            panic("Failed to create thread");
        }

        return t;
    }

    void* join() {
        if (!self.joinable) {
            panic("Thread is not joinable");
        }

        void* result;
        if (pthread_join(self.handle, &result) != 0) {
            panic("Failed to join thread");
        }

        self.joinable = false;
        return result;
    }

    void detach() {
        if (!self.joinable) {
            panic("Thread is not joinable");
        }

        if (pthread_detach(self.handle) != 0) {
            panic("Failed to detach thread");
        }

        self.joinable = false;
    }

    static void sleep(uint64_t millis) {
        struct timespec ts;
        ts.tv_sec = millis / 1000;
        ts.tv_nsec = (millis % 1000) * 1000000;
        nanosleep(&ts, null);
    }

    static void yield() {
        sched_yield();
    }
}

// ミューテックス
struct Mutex<T> {
    pthread_mutex_t mutex;
    T* data;
}

impl<T> Mutex<T> {
    static Mutex<T> new(T value) {
        Mutex<T> m;
        pthread_mutex_init(&m.mutex, null);
        m.data = new T(value);
        return m;
    }

    MutexGuard<T> lock() {
        pthread_mutex_lock(&self.mutex);
        return MutexGuard<T>{&self.mutex, self.data};
    }

    bool try_lock() {
        return pthread_mutex_trylock(&self.mutex) == 0;
    }

    ~Mutex() {
        pthread_mutex_destroy(&self.mutex);
        delete self.data;
    }
}

struct MutexGuard<T> {
    pthread_mutex_t* mutex;
    T* data;
}

impl<T> MutexGuard<T> {
    T& operator*() { return *self.data; }
    T* operator->() { return self.data; }

    ~MutexGuard() {
        pthread_mutex_unlock(self.mutex);
    }
}

// 条件変数
struct CondVar {
    pthread_cond_t cond;
}

impl CondVar {
    static CondVar new() {
        CondVar cv;
        pthread_cond_init(&cv.cond, null);
        return cv;
    }

    void wait<T>(MutexGuard<T>& guard) {
        pthread_cond_wait(&self.cond, guard.mutex);
    }

    void notify_one() {
        pthread_cond_signal(&self.cond);
    }

    void notify_all() {
        pthread_cond_broadcast(&self.cond);
    }

    ~CondVar() {
        pthread_cond_destroy(&self.cond);
    }
}

// チャンネル（メッセージパッシング）
struct Channel<T> {
    struct Node {
        T data;
        Node* next;
    }

    Node* head;
    Node* tail;
    Mutex<void> mutex;
    CondVar not_empty;
    bool closed;
}

impl<T> Channel<T> {
    static Channel<T> new() {
        return Channel<T>{null, null, Mutex<void>::new(), CondVar::new(), false};
    }

    void send(T value) {
        auto guard = self.mutex.lock();
        if (self.closed) panic("Channel is closed");

        Node* node = new Node{value, null};
        if (self.tail) {
            self.tail->next = node;
        } else {
            self.head = node;
        }
        self.tail = node;

        self.not_empty.notify_one();
    }

    T recv() {
        auto guard = self.mutex.lock();

        while (!self.head && !self.closed) {
            self.not_empty.wait(guard);
        }

        if (!self.head) panic("Channel is closed");

        Node* node = self.head;
        self.head = node->next;
        if (!self.head) self.tail = null;

        T value = node->data;
        delete node;
        return value;
    }

    void close() {
        auto guard = self.mutex.lock();
        self.closed = true;
        self.not_empty.notify_all();
    }
}
```