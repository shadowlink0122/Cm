# FFI (Foreign Function Interface) 設計

## 概要

CmからC/Rustライブラリを呼び出すためのFFI設計です。ネイティブターゲット専用。

## 基本構文

### C関数の宣言

```cpp
// Cm
extern "C" {
    int printf(const char* format, ...);
    void* malloc(size_t size);
    void free(void* ptr);
}

void main() {
    printf("Hello from Cm!\n");
}
```

### Rustライブラリの使用

```cpp
// Cm
extern "Rust" {
    // Crateから関数をインポート
    #[crate = "my_rust_lib"]
    fn rust_function(x: int) -> int;
}

void main() {
    int result = rust_function(42);
}
```

---

## 型マッピング (C ABI)

| Cm | C | サイズ |
|----|---|--------|
| `int8` | `int8_t` | 1 byte |
| `int16` | `int16_t` | 2 bytes |
| `int32` | `int32_t` | 4 bytes |
| `int64` / `int` | `int64_t` | 8 bytes |
| `float32` | `float` | 4 bytes |
| `float64` / `float` | `double` | 8 bytes |
| `bool` | `bool` / `_Bool` | 1 byte |
| `char` | `char` | 1 byte |
| `*T` | `T*` | pointer |
| `*const T` | `const T*` | pointer |

---

## ポインタと安全性

### unsafe ブロック

```cpp
// Cm
unsafe {
    int* ptr = malloc(sizeof(int) * 10) as *int;
    ptr[0] = 42;
    free(ptr as *void);
}
```

### 安全なラッパー

```cpp
// Cm
struct CString {
    *char data;
    
    static CString from(String s) {
        unsafe {
            *char ptr = malloc(s.length() + 1) as *char;
            memcpy(ptr, s.c_str(), s.length() + 1);
            return CString { data: ptr };
        }
    }
    
    void drop() {
        unsafe { free(this.data as *void); }
    }
}
```

---

## コールバック

### Cm関数をCに渡す

```cpp
// Cm
extern "C" {
    void qsort(
        void* base, 
        size_t nmemb, 
        size_t size, 
        fn(*void, *void) -> int compare
    );
}

int compare_int(*void a, *void b) {
    int* ia = a as *int;
    int* ib = b as *int;
    return *ia - *ib;
}

void main() {
    int[] arr = [3, 1, 4, 1, 5];
    unsafe {
        qsort(arr.data(), arr.len(), sizeof(int), compare_int);
    }
}
```

---

## 構造体レイアウト

### C互換レイアウト

```cpp
// Cm
#[repr(C)]
struct Point {
    int32 x;
    int32 y;
};

extern "C" {
    void draw_point(Point p);
}
```

### パッキング

```cpp
// Cm
#[repr(C, packed)]
struct PackedData {
    int8 a;
    int32 b;  // パディングなし
};
```

---

## プラットフォーム固有

### 条件付きコンパイル

```cpp
// Cm
#[cfg(os = "linux")]
extern "C" {
    int epoll_create(int size);
}

#[cfg(os = "macos")]
extern "C" {
    int kqueue();
}

#[cfg(os = "windows")]
extern "C" {
    void* CreateIoCompletionPort(...);
}
```

### リンク指定

```toml
# Cm.toml
[target.native.link]
libraries = ["ssl", "crypto"]
search_paths = ["/usr/local/lib"]

[target.native.link.linux]
libraries = ["pthread"]

[target.native.link.windows]
libraries = ["ws2_32"]
```

---

## 生成コード例

```cpp
// Cm
extern "C" {
    int add(int a, int b);
}

void main() {
    int result = add(1, 2);
}
```

```rust
// 生成Rust
extern "C" {
    fn add(a: i64, b: i64) -> i64;
}

fn main() {
    let result = unsafe { add(1, 2) };
}
```

---

## 制限事項

> [!WARNING]
> FFIは本質的にunsafeです

| 項目 | 状態 |
|------|------|
| C ABI | ✅ 基本対応 |
| Rust ABI | ⚠️ 限定的 |
| C++ ABI | ❌ 非対応（複雑すぎる） |
| varargs | ⚠️ printfのみ |
| SIMD | 📋 将来検討 |
