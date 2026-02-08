---
title: extern宣言
parent: Advanced
---

# extern宣言 — 外部関数の呼び出し

**学習目標:** `extern "C"` を使ってC/C++/システムの関数をCm内から呼び出す方法を学びます。  
**所要時間:** 15分  
**難易度:** 🟡 中級〜上級

---

## 基本構文

```cm
extern "C" 戻り値型 関数名(引数...);
```

### 例: 数学関数

```cm
extern "C" double sin(double x);
extern "C" double cos(double x);
extern "C" double sqrt(double x);

int main() {
    double angle = 3.14159 / 4.0;
    double s = sin(angle);
    double c = cos(angle);
    println("sin={s}, cos={c}");
    return 0;
}
```

---

## 戻り値型のパターン

### スカラ型

```cm
extern "C" int    my_add(int a, int b);
extern "C" double my_sqrt(double x);
extern "C" long   my_hash(string s);
extern "C" bool   my_check(int x);
extern "C" void   my_init();
```

### 文字列

```cm
// Cmの string は内部的に char* として扱われる
extern "C" string my_greet(string name);
```

### ポインタ

```cm
extern "C" void* malloc(int size);
extern "C" void free(void* ptr);
extern "C" int* create_array(int n);
```

---

## ハンドルパターン

Cmの標準ライブラリで広く使われるパターンです。  
C++のオブジェクトを `long` ハンドルとして保持します。

### Cm側

```cm
// C++のリソースをlongハンドルで管理
extern "C" long resource_create(int size);
extern "C" int resource_read(long handle);
extern "C" void resource_destroy(long handle);

int main() {
    long r = resource_create(1024);
    int data = resource_read(r);
    resource_destroy(r);
    return 0;
}
```

### C++側

```cpp
#include <cstdlib>

struct Resource {
    int* data;
    int size;
};

extern "C" {

long resource_create(int size) {
    auto* r = new Resource();
    r->data = (int*)calloc(size, sizeof(int));
    r->size = size;
    return (long)r;
}

int resource_read(long handle) {
    auto* r = (Resource*)handle;
    return r->data[0];
}

void resource_destroy(long handle) {
    auto* r = (Resource*)handle;
    free(r->data);
    delete r;
}

}
```

---

## use libc との違い

| 方法 | 用途 | 宣言 |
|------|------|------|
| `extern "C"` | 自作C/C++関数、任意のライブラリ | 型シグネチャを明示 |
| `use libc { ... }` | 標準Cライブラリ (malloc, memcpy等) | 名前のみ |

```cm
// extern "C" — 型を明示
extern "C" void* malloc(int size);

// use libc — 名前のみ（型は暗黙）
use libc { malloc, free, memcpy }
```

---

## ビルド方法

自作のC/C++コードとリンクする場合:

```bash
# C++をコンパイル
g++ -c my_runtime.cpp -o my_runtime.o

# Cmからリンク
cm compile main.cm my_runtime.o -o main
```

Objective-C++ (macOS):

```bash
# .mmファイルをコンパイル
clang++ -ObjC++ -c gpu_runtime.mm -o gpu_runtime.o -framework Metal

# Cmからリンク
cm compile main.cm gpu_runtime.o -o main -framework Metal
```

---

## 注意事項

- `extern "C"` は**C ABI** でリンクされます（C++の名前マングリングは適用されません）
- C++側では必ず `extern "C" { }` ブロックで宣言してください
- 文字列引数は `char*` として渡されます
- メモリの所有権に注意: C++側でmallocした場合、Cm側でfreeするか、C++側のデストラクタで解放してください

---

## 次のステップ

- [インラインアセンブリ](inline-asm.html) — `__asm__` でCPU命令を直接実行
- [FFI](ffi.html) — use libc / 構造体の受け渡し
- [標準ライブラリの拡張](../stdlib/extending.html) — 独自モジュールの作成

---

**最終更新:** 2026-02-08
