# 標準ライブラリ実装詳細とリンク戦略

作成日: 2026-01-11
対象バージョン: v0.11.0
関連文書: 030_runtime_to_stdlib_migration.md

## 実装アプローチの詳細

### 1. Intrinsics層の実装

#### 1.1 最小限のCランタイム

```c
// src/runtime/minimal/syscalls.c
// 最小限のシステムコールラッパーのみ提供

#include <unistd.h>
#include <stdlib.h>

// ファイルI/O
int __cm_syscall_write(int fd, const void* buf, int count) {
    return write(fd, buf, count);
}

int __cm_syscall_read(int fd, void* buf, int count) {
    return read(fd, buf, count);
}

// メモリ管理
void* __cm_syscall_malloc(int size) {
    return malloc(size);
}

void __cm_syscall_free(void* ptr) {
    free(ptr);
}

// パニック（最低限）
void __cm_syscall_panic(const char* msg) {
    write(2, msg, strlen(msg));
    write(2, "\n", 1);
    abort();
}
```

#### 1.2 Cmでのintrinsicsモジュール

```cm
// std/intrinsics/mod.cm
module std::intrinsics;

// Cランタイムの最小限関数を宣言
extern "C" {
    int __cm_syscall_write(int fd, void* buf, int count);
    int __cm_syscall_read(int fd, void* buf, int count);
    void* __cm_syscall_malloc(int size);
    void __cm_syscall_free(void* ptr);
    void __cm_syscall_panic(cstring msg);
}

// Cm向けの安全なラッパー
export struct FileDescriptor {
    int fd;
}

impl FileDescriptor {
    self(int fd) {
        self.fd = fd;
    }

    int write(byte[] data) {
        if (data.ptr == null || data.len <= 0) {
            return 0;
        }
        return __cm_syscall_write(self.fd, data.ptr, data.len);
    }

    int read(byte[] buffer) {
        if (buffer.ptr == null || buffer.len <= 0) {
            return 0;
        }
        return __cm_syscall_read(self.fd, buffer.ptr, buffer.len);
    }
}

// 標準ファイルディスクリプタ
export FileDescriptor STDIN = FileDescriptor(0);
export FileDescriptor STDOUT = FileDescriptor(1);
export FileDescriptor STDERR = FileDescriptor(2);
```

### 2. メモリ管理層

```cm
// std/mem/allocator.cm
module std::mem;

import std::intrinsics;

// アロケータインターフェース
export interface Allocator {
    void* alloc(int size);
    void dealloc(void* ptr);
    void* realloc(void* ptr, int new_size);
}

// デフォルトアロケータ
export struct SystemAllocator {}

// 注意: impl単独ブロックはコンストラクタ・デストラクタのみ定義可能
// 通常のメソッドはinterfaceとimpl forで定義する必要がある

impl SystemAllocator for Allocator {
    void* alloc(int size) {
        if (size <= 0) return null;

        void* ptr = intrinsics::__cm_syscall_malloc(size);
        if (ptr == null) {
            intrinsics::__cm_syscall_panic("Out of memory");
        }
        return ptr;
    }

    void dealloc(void* ptr) {
        if (ptr != null) {
            intrinsics::__cm_syscall_free(ptr);
        }
    }

    void* realloc(void* ptr, int new_size) {
        if (new_size <= 0) {
            self.dealloc(ptr);
            return null;
        }

        if (ptr == null) {
            return self.alloc(new_size);
        }

        // 単純な実装：新規割り当て＋コピー
        void* new_ptr = self.alloc(new_size);
        // memcpy相当の処理
        self.dealloc(ptr);
        return new_ptr;
    }
}

// グローバルアロケータ
export SystemAllocator GLOBAL_ALLOCATOR;

// 便利関数
export <T> T* alloc() {
    return (T*)GLOBAL_ALLOCATOR.alloc(sizeof(T));
}

export <T> T[] alloc_array(int count) {
    T* ptr = (T*)GLOBAL_ALLOCATOR.alloc(sizeof(T) * count);
    return T[]{ptr: ptr, len: count};
}

export void free(void* ptr) {
    GLOBAL_ALLOCATOR.dealloc(ptr);
}
```

### 3. 文字列処理層

```cm
// std/string/string.cm
module std::string;

import std::mem;

export struct String {
    private char* data;
    private int length;
    private int capacity;
}

impl String {
    // コンストラクタ
    self() {
        self.data = null;
        self.length = 0;
        self.capacity = 0;
    }

    self(cstring s) {
        int len = 0;
        while (s[len] != '\0') {
            len++;
        }

        self.capacity = len + 1;
        self.data = (char*)mem::GLOBAL_ALLOCATOR.alloc(self.capacity);

        for (int i = 0; i < len; i++) {
            self.data[i] = s[i];
        }
        self.data[len] = '\0';
        self.length = len;
    }

    // デストラクタ
    ~self() {
        if (self.data != null) {
            mem::free(self.data);
        }
    }

    // メソッド
    void push(char c) {
        if (self.length + 1 >= self.capacity) {
            self.grow();
        }

        self.data[self.length] = c;
        self.length++;
        self.data[self.length] = '\0';
    }

    void append(String other) {
        for (int i = 0; i < other.length; i++) {
            self.push(other.data[i]);
        }
    }

    cstring as_cstring() {
        return (cstring)self.data;
    }

    byte[] as_bytes() {
        return byte[]{
            ptr: (byte*)self.data,
            len: self.length
        };
    }

    private void grow() {
        int new_capacity = self.capacity == 0 ? 16 : self.capacity * 2;
        char* new_data = (char*)mem::GLOBAL_ALLOCATOR.alloc(new_capacity);

        if (self.data != null) {
            for (int i = 0; i < self.length; i++) {
                new_data[i] = self.data[i];
            }
            mem::free(self.data);
        }

        self.data = new_data;
        self.capacity = new_capacity;
    }
}
```

## リンク戦略

### 1. ビルドプロセス

```makefile
# 標準ライブラリのビルド
STD_SOURCES = $(wildcard std/**/*.cm)
STD_OBJECTS = $(STD_SOURCES:.cm=.o)

# Stage 1: 最小Cランタイムのビルド
minimal_runtime.o: src/runtime/minimal/syscalls.c
	$(CC) -c $< -o $@ -O2 -fPIC

# Stage 2: 標準ライブラリのビルド
libcm_std.a: $(STD_OBJECTS) minimal_runtime.o
	$(AR) rcs $@ $^

# Stage 3: ユーザープログラムのビルド
%.exe: %.cm libcm_std.a
	$(CM_COMPILER) $< -L. -lcm_std -o $@
```

### 2. リンク方式の選択

#### 2.1 静的リンク（推奨）
```bash
# 標準ライブラリを静的ライブラリとしてビルド
cm build --stdlib-static

# 利点：
# - 単一実行ファイル
# - デプロイが簡単
# - LTO最適化が効く
```

#### 2.2 動的リンク（オプション）
```bash
# 標準ライブラリを共有ライブラリとしてビルド
cm build --stdlib-shared

# 利点：
# - メモリ効率
# - ライブラリ更新が容易
# - 実行ファイルサイズが小さい
```

### 3. モジュール解決

```cpp
// src/frontend/resolver/stdlib_resolver.cpp

class StdlibResolver {
private:
    std::map<std::string, std::string> stdlib_paths;

public:
    StdlibResolver() {
        // 標準ライブラリモジュールのマッピング
        stdlib_paths["std::io"] = "std/io/mod.cm";
        stdlib_paths["std::fmt"] = "std/fmt/mod.cm";
        stdlib_paths["std::mem"] = "std/mem/mod.cm";
        stdlib_paths["std::string"] = "std/string/mod.cm";
        stdlib_paths["std::intrinsics"] = "std/intrinsics/mod.cm";
    }

    std::optional<std::string> resolve(const std::string& module_name) {
        auto it = stdlib_paths.find(module_name);
        if (it != stdlib_paths.end()) {
            return get_stdlib_path() + "/" + it->second;
        }
        return std::nullopt;
    }

private:
    std::string get_stdlib_path() {
        // 環境変数またはコンパイル時定数から取得
        if (auto env = std::getenv("CM_STDLIB_PATH")) {
            return env;
        }
        return CM_DEFAULT_STDLIB_PATH;
    }
};
```

## コンパイラの変更点

### 1. 組み込み関数の削除

```cpp
// src/mir/lowering/expr_call.cpp

// 変更前
if (is_builtin_function(name)) {
    if (name == "println") {
        // cm_println_* への変換
        return generate_cm_println_call(args);
    }
}

// 変更後
if (is_builtin_function(name)) {
    if (name == "println") {
        // std::io::println への参照解決
        auto module_ref = resolve_stdlib_module("std::io");
        return generate_module_call(module_ref, "println", args);
    }
}
```

### 2. 型推論の拡張

```cpp
// src/frontend/types/inference.cpp

// Displayトレイトの自動推論
if (requires_display_trait(context)) {
    // 基本型には自動的にDisplay実装があると仮定
    if (is_primitive_type(type)) {
        add_trait_impl(type, "std::fmt::Display");
    }
}
```

### 3. 最適化パスの追加

```cpp
// src/mir/passes/stdlib_optimization.cpp

class StdlibOptimizationPass : public MirPass {
    void run(MirFunction& func) override {
        // 標準ライブラリ呼び出しの最適化

        // 1. 小さな関数のインライン化
        inline_small_stdlib_functions(func);

        // 2. 文字列連結の最適化
        optimize_string_concatenation(func);

        // 3. フォーマット文字列のコンパイル時展開
        expand_format_strings(func);
    }
};
```

## テスト戦略

### 1. 標準ライブラリのユニットテスト

```cm
// tests/stdlib/test_string.cm
module test_string;

import std::string::String;
import std::testing::*;

#[test]
void test_string_creation() {
    String s = String("hello");
    assert_eq(s.length(), 5);
}

#[test]
void test_string_append() {
    String s1 = String("hello");
    String s2 = String(" world");
    s1.append(s2);
    assert_eq(s1.as_cstring(), "hello world");
}
```

### 2. 統合テスト

```bash
#!/bin/bash
# tests/integration/test_stdlib.sh

# 旧ランタイムとの比較テスト
for test in tests/integration/*.cm; do
    # 旧ランタイムでコンパイル・実行
    cm_old $test -o old.out
    ./old.out > old_result.txt

    # 新標準ライブラリでコンパイル・実行
    cm_new $test -o new.out
    ./new.out > new_result.txt

    # 結果を比較
    diff old_result.txt new_result.txt || exit 1
done
```

### 3. パフォーマンステスト

```cm
// benchmarks/stdlib/bench_io.cm
import std::io;
import std::time;

void bench_println() {
    auto start = time::now();

    for (int i = 0; i < 100000; i++) {
        io::println("Hello, World!");
    }

    auto elapsed = time::now() - start;
    println("Time: {elapsed}ms");
}
```

## 移行スケジュール

### Week 1-2: 基盤整備
- [ ] 最小Cランタイム実装
- [ ] intrinsicsモジュール実装
- [ ] ビルドシステム更新

### Week 3-4: コアモジュール
- [ ] メモリ管理モジュール
- [ ] 文字列モジュール
- [ ] 基本I/Oモジュール

### Week 5-6: 高レベル機能
- [ ] フォーマットモジュール
- [ ] コレクションモジュール
- [ ] エラーハンドリング

### Week 7-8: 統合とテスト
- [ ] コンパイラ統合
- [ ] 既存テストの移行
- [ ] パフォーマンス測定

### Week 9-10: 最適化と文書化
- [ ] 最適化パス実装
- [ ] APIドキュメント作成
- [ ] 移行ガイド作成

## まとめ

この実装計画により：

1. **cm_*関数の完全な排除**
2. **純粋なCmコードによる標準ライブラリ**
3. **効率的なリンク戦略**
4. **段階的で安全な移行**

が実現されます。

---

**作成者:** Claude Code
**ステータス:** 実装詳細
**次のステップ:** Week 1の基盤整備から開始