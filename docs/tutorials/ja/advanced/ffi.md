---
title: FFI
parent: Advanced
---

[English](../../en/advanced/ffi.html)

# FFI（Foreign Function Interface）

**学習目標:** CライブラリやOS APIをCm言語から呼び出す方法を学びます。  
**所要時間:** 25分  
**難易度:** 🔴 上級

---

## 概要

FFI（Foreign Function Interface）を使うと、CライブラリやOS APIをCm言語から直接呼び出せます。

---

## use宣言

### libcの使用

```cm
use libc {
    void* malloc(int size);
    void free(void* ptr);
    void* memcpy(void* dst, void* src, int n);
    void* memset(void* ptr, int value, int n);
}

int main() {
    // malloc で 1024 バイト確保
    void* ptr = malloc(1024);

    if (ptr != null) {
        // メモリを 0 で初期化
        memset(ptr, 0, 1024);

        // 使い終わったら解放
        free(ptr);
    }

    return 0;
}
```

---

## std::memモジュール

安全なメモリ操作用のラッパー：

```cm
int main() {
    // アロケータ経由でメモリ確保
    int* arr = alloc(10);

    // 使用
    for (int i = 0; i < 10; i++) {
        arr[i] = i * i;
    }

    // 解放
    dealloc(arr);

    return 0;
}
```

---

## カスタム外部関数

### 宣言

```cm
// C関数の宣言
extern "C" {
    double sin(double x);
    double cos(double x);
}

int main() {
    double angle = 3.14159 / 4.0;
    println("sin(pi/4) = {sin(angle)}");
    return 0;
}
```

---

## ターゲット別対応

| 機能 | LLVM | WASM |
|------|------|------|
| libc | ✅ | ✅（WASI経由） |
| システムコール | ✅ | ❌ |
| ポインタ | ✅ | ✅ |

---

## 安全なFFI利用

### ラッパー作成

```cm
// 安全なラッパー
struct SafeBuffer {
    void* ptr;
    int size;
}

SafeBuffer create_buffer(int size) {
    void* p = malloc(size);
    return SafeBuffer { ptr: p, size: size };
}

void destroy_buffer(SafeBuffer* buf) {
    if (buf->ptr != null) {
        free(buf->ptr);
        buf->ptr = null;
    }
}
```

## 構造体の受け渡し

```cm
struct Point {
    int x;
    int y;
}

extern "C" {
    void process_point(Point p);
}
```

> **v0.11.0の重要アップデート**: C ABI互換性が修正されました。小さな構造体はポインタではなく、標準のC ABIに従って値として正しくレジスタまたはスタックで渡されるようになりました。これにより、既存のCライブラリとの連携がより安全かつ確実になります。

---

## 次のステップ

- [ポインタ](../basics/pointers.html) - ポインタ操作の基礎
- [WASMバックエンド](../compiler/wasm/index.html) - WASI FFI

---

**最終更新:** 2026-02-08

---

<!-- nav -->
← 前: [スライス型](slices.html) ｜ [目次](index.html) ｜ 次: [extern宣言 — 外部関数の呼び出し](extern.html) →
