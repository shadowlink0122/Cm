---
title: 標準ライブラリの拡張
---

# 標準ライブラリの拡張方法

CmのNative標準ライブラリは C/C++/Objective-C++ のバッキングコードで実装されています。同じ仕組みを使って独自のモジュールを作成できます。

**最終更新:** 2026-02-08

---

## アーキテクチャ

```
┌──────────────┐     extern "C"     ┌──────────────────┐
│  Cm モジュール  │ ───────────────→ │  C/C++/ObjC++    │
│  (.cm)       │                    │  ランタイム (.cpp) │
└──────────────┘                    └──────────────────┘
```

**Cmモジュール** は `extern "C"` で C 関数を宣言し、**C++バッキングコード** が実装を提供します。LLVM がリンク時に両者を結合します。

---

## 方法1: extern "C" — C/C++関数の呼び出し

最も基本的な方法です。

### Cm側 (my_module.cm)

```cm
module my_module;

// C関数を宣言
extern "C" int my_add(int a, int b);
extern "C" string my_greet(string name);

// Cmラッパー（export）
export int add(int a, int b) {
    return my_add(a, b);
}

export string greet(string name) {
    return my_greet(name);
}
```

### C++側 (my_runtime.cpp)

```cpp
#include <cstring>
#include <cstdlib>

extern "C" {

int my_add(int a, int b) {
    return a + b;
}

// Cmのstringはchar*として受け渡し
char* my_greet(const char* name) {
    char* result = (char*)malloc(256);
    snprintf(result, 256, "Hello, %s!", name);
    return result;
}

}
```

### ビルド

```bash
# C++をオブジェクトファイルにコンパイル
g++ -c my_runtime.cpp -o my_runtime.o

# Cmからリンク
cm compile main.cm my_runtime.o -o main
```

---

## 方法2: use libc — システムコールの直接利用

`use libc` ブロックで libc 関数を直接呼び出せます。

```cm
module my_posix;

use libc {
    int pthread_mutex_init(void* mutex, void* attr);
    int pthread_mutex_lock(void* mutex);
    int pthread_mutex_unlock(void* mutex);
    int open(string path, int flags);
    int read(int fd, void* buf, int count);
    int close(int fd);
}

export int mutex_init(void* mutex) {
    void* null_ptr = 0 as void*;
    return pthread_mutex_init(mutex, null_ptr);
}
```

---

## 方法3: Objective-C++ — Apple API の利用

macOS/iOS のフレームワークにアクセスできます。GPU (Metal) モジュールはこの方法で実装されています。

### Objective-C++側 (gpu_runtime.mm)

```objc
#import <Metal/Metal.h>
#import <Foundation/Foundation.h>

extern "C" {

long gpu_device_create() {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    return (long)(__bridge_retained void*)device;
}

const char* gpu_device_name(long device) {
    id<MTLDevice> dev = (__bridge id<MTLDevice>)(void*)device;
    return [[dev name] UTF8String];
}

}
```

### Cm側 (std/gpu/mod.cm)

```cm
module std.gpu;

extern "C" long gpu_device_create();
extern "C" string gpu_device_name(long device);

export long device_create() {
    return gpu_device_create();
}

export string device_name(long device) {
    return gpu_device_name(device);
}
```

---

## 方法4: インラインアセンブリ

ハードウェアアクセスやパフォーマンスクリティカルな処理に使用します。

```cm
// CPUサイクルカウンタ取得 (x86_64)
long rdtsc() {
    long lo = 0;
    long hi = 0;
    __asm__("rdtsc" : "=a"(lo), "=d"(hi));
    return (hi << 32) | lo;
}

// メモリバリア
void memory_barrier() {
    __asm__("mfence" ::: "memory");
}
```

> **注意:** インラインアセンブリはアーキテクチャ依存です。`#ifdef` で分岐してください。

```cm
#ifdef __ARM64__
    __asm__("dmb sy" ::: "memory");
#endif
#ifdef __X86_64__
    __asm__("mfence" ::: "memory");
#endif
```

---

## 実装パターン: ハンドルベースAPI

Cmのstdモジュールではリソースを `long` ハンドルで管理します。

```
Cm:   long handle = create_resource();
C++:  void* ptr = new Resource(); return (long)ptr;

Cm:   use_resource(handle);
C++:  Resource* r = (Resource*)handle; r->do_something();

Cm:   destroy_resource(handle);
C++:  delete (Resource*)handle;
```

**理由:** Cmの型システムではC++のクラスを直接表現できないため、不透明ハンドルとして扱います。

---

## 既存のstdバッキングファイル

| ファイル | 言語 | 提供機能 |
|---------|------|---------|
| `std/http/http_runtime.cpp` | C++ | HTTP/HTTPS通信 |
| `std/net/net_runtime.cpp` | C++ | TCP/UDP/DNS/poll |
| `std/thread/thread_runtime.cpp` | C++ | スレッド管理 |
| `std/sync/sync_runtime.cpp` | C++ | Mutex/RwLock/Atomic |
| `std/sync/channel_runtime.cpp` | C++ | Channel |

---

**関連:** [FFI](../../advanced/ffi.html) · [LLVMバックエンド](../../compiler/llvm.html)
