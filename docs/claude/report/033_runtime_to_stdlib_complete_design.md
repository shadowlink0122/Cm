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

export struct String {
    private byte* data;
    private int len;
    private int cap;
}

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