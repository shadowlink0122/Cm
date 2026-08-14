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

## グローバルアロケータの差し替え

`set_allocator_fns` にCm関数ポインタ3本（alloc / dealloc / realloc）を渡すと、以降の `std::mem` 経由の確保がそのアロケータを経由します。
`std::collections` のVector/HashMap/Queueの内部確保も `std::mem` 経由のため、登録したアロケータを通ります（v0.17.0で生malloc直呼びの素通しを解消）。
`reset_allocator()` で既定のアロケータへ戻ります。

> **対応バックエンド:** JIT / Native のみ（WASMは独自フリーリストアロケータ固定のためno-op、JS/TSはGC管理のため対象外）

```cm
import std::mem::{alloc, dealloc, set_allocator_fns, reset_allocator};

use libc {
    void* malloc(int size);
    void free(void* ptr);
    void* realloc(void* ptr, int size);
}

int alloc_count = 0;

// カウンタ付きアロケータ: 確保回数を記録してmallocへ委譲する
void* counting_alloc(long size) {
    alloc_count = alloc_count + 1;
    return malloc(size as int);
}

void counting_dealloc(void* ptr) {
    free(ptr);
}

void* counting_realloc(void* ptr, long new_size) {
    return realloc(ptr, new_size as int);
}

int main() {
    set_allocator_fns(counting_alloc as void*, counting_dealloc as void*, counting_realloc as void*);
    void* p = alloc(64);     // counting_alloc を経由（alloc_count == 1）
    dealloc(p);
    reset_allocator();       // 以降は既定のアロケータへ戻る
    return 0;
}
```

| 関数 | 説明 |
|------|------|
| `set_allocator_fns(alloc_fn, dealloc_fn, realloc_fn)` | グローバルアロケータをCm関数ポインタ3本で差し替え |
| `reset_allocator()` | 既定のアロケータへ戻す |

登録する関数のシグネチャは `void*(long)` / `void(void*)` / `void*(void*, long)` です。

---

## スマートポインタ（std::mem::smart・v0.17.2）

ヒープ確保した値の解放をRAII（スコープ末尾のデストラクタ）で自動化します。

```cm
import std::mem::smart::*;

int main() {
    // UniquePtr<T>: 単独所有。スコープを抜けると自動解放
    UniquePtr<int> u(42);
    println("{u.get()}");     // 42
    u.set(43);
    int* p = u.raw();         // 借用ビュー（所有権は移動しない）

    // 所有権の移動は必ず move で行う
    UniquePtr<int> v = move u;

    // SharedPtr<T>: 参照カウント共有。共有は必ず clone() で行う
    SharedPtr<int> a(100);
    SharedPtr<int> b = a.clone();   // rc=2
    println("rc={a.use_count()}");  // 2
    b.set(200);                     // 全共有者から見える
    return 0;
}

// 所有型を返す関数は必ず return move で書く
UniquePtr<int> make(int v) {
    UniquePtr<int> b(v);
    return move b;
}
```

**所有規律（重要）:**

- 暗黙コピー（`b = a;`）は浅いコピーで両方のデストラクタが走り**二重解放**になります。移動は `move`、SharedPtrの共有は `clone()` を必ず使ってください。
- 所有型を関数から返すときは `return move local;` と書きます（`return local;` はローカルのデストラクタが先に走りdanglingになります）。
- `raw()` の生ポインタはスマートポインタより長生きさせないでください。

| API | UniquePtr | SharedPtr |
|---|---|---|
| 構築 | `UniquePtr<T> u(value);` | `SharedPtr<T> a(value);` |
| 読み書き | `get()` / `set(v)` / `raw()` | `get()` / `set(v)` / `raw()` |
| 共有 | 不可（moveのみ） | `clone()`（参照カウント+1） |
| その他 | `release()` / `reset()` / `is_null()` | `use_count()` / `is_null()` |

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

---

<!-- nav -->
← 前: [std::io — 入出力](io.html) ｜ [目次](index.html) ｜ 次: [std::math — 数学関数](math.html) →
