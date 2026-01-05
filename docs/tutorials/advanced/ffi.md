---
layout: default
title: FFI
parent: Advanced
nav_order: 6
---

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
    malloc,
    free,
    memcpy,
    memset
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
import std::mem::*;

int main() {
    // アロケータ経由でメモリ確保
    int* arr = alloc<int>(10);
    
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
    int printf(string format, ...);
    double sin(double x);
    double cos(double x);
}

int main() {
    double angle = 3.14159 / 4.0;
    printf("sin(π/4) = %f\n", sin(angle));
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

---

## 次のステップ

- [ポインタ](../basics/pointers.md) - ポインタ操作の基礎
- [WASMバックエンド](../compiler/wasm.md) - WASI FFI
