---
title: std::mem
---

# std::mem — メモリ管理

メモリの確保・解放・型情報取得を提供するモジュールです。

> **対応バックエンド:** Native (LLVM) のみ

**最終更新:** 2026-02-08

---

## メモリ確保 / 解放

```cm
import std::mem::{alloc, dealloc};

void* ptr = alloc(1024);   // 1024バイト確保
// ... use ptr ...
dealloc(ptr);               // 解放
```

---

## 型情報

```cm
import std::mem::{size_of, type_name, align_of};

uint s = size_of<int>();       // 4
string n = type_name<int>();   // "int"
uint a = align_of<double>();   // 8
```

| 関数 | 戻り値 | 説明 |
|------|--------|------|
| `size_of<T>()` | `uint` | 型Tのサイズ（バイト） |
| `type_name<T>()` | `string` | 型Tの名前 |
| `align_of<T>()` | `uint` | 型Tのアラインメント |

---

## Allocator インターフェース

カスタムアロケータの実装が可能です。

```cm
import std::mem::{Allocator, DefaultAllocator};

// デフォルトアロケータ（mallocベース）
DefaultAllocator da = DefaultAllocator{};
void* ptr = da.alloc(256);
da.dealloc(ptr);
```

| メソッド | 戻り値 | 説明 |
|---------|--------|------|
| `alloc(size)` | `void*` | メモリ確保 |
| `dealloc(ptr)` | `void` | メモリ解放 |
| `reallocate(ptr, new_size)` | `void*` | 再確保 |
| `alloc_zeroed(size)` | `void*` | ゼロ初期化確保 |

---

## libc FFI

内部的に使用可能なlibc関数:

```cm
use libc {
    void* malloc(int size);
    void* calloc(int nmemb, int size);
    void* realloc(void* ptr, int size);
    void free(void* ptr);
}
```

---

**関連:** [入出力](io.html) · [数学関数](math.html) · [コアユーティリティ](core-utils.html)
