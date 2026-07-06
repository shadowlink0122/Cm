---
title: ポインタ
parent: Tutorials
---

[English](../../en/basics/pointers.html)

# ポインタ（Pointers）

Cm言語のポインタは、C/C++互換のメモリアドレス操作を提供します。

## 📋 目次

- [基本的な使い方](#基本的な使い方)
- [ポインタ宣言](#ポインタ宣言)
- [アドレス演算子](#アドレス演算子)
- [デリファレンス](#デリファレンス)
- [ポインタ演算](#ポインタ演算)
- [構造体ポインタ](#構造体ポインタ)
- [配列とポインタ](#配列とポインタ)
- [関数ポインタ](#関数ポインタ)

## 基本的な使い方

### ポインタ宣言

```cm
// 基本的なポインタ宣言
int* p;

// 複数のポインタ
int* a, *b, *c;

// ポインタのポインタ
int** pp;

struct Point { int x; int y; }

// 構造体ポインタ
Point* ptr;
```

## アドレス演算子

`&`演算子で変数のアドレスを取得します。

```cm
struct Point { int x; int y; }

int main() {
    int x = 42;
    int* p = &x;  // xのアドレス

    Point pt = Point(10, 20);
    Point* ptr = &pt;  // ptのアドレス
    return 0;
}
```

## デリファレンス

`*`演算子でポインタの参照先にアクセスします。

```cm
int main() {
    int x = 10;
    int* p = &x;

    // 読み取り
    int value = *p;  // 10

    // 書き込み
    *p = 20;
    // xは20になる
    return 0;
}
```

## ポインタと借用（v0.11.0以降）

Cm v0.11.0では、ポインタが借用システムの基盤となっています：

```cm
int main() {
    // constポインタによる不変借用
    const int value = 42;
    const int* immutable_ref = &value;
    println("借用した値: {*immutable_ref}");
    // *immutable_ref = 50;  // エラー: 変更不可

    // ポインタによる可変借用
    int mut_value = 100;
    int* mutable_ref = &mut_value;
    *mutable_ref = 200;  // OK: 変更可能
    println("変更後: {mut_value}");  // 200

    // 借用中は移動不可
    int data = 10;
    int* borrowed = &data;
    // int moved = move data;  // エラー: 借用中の値は移動できない

    return 0;
}
```

**重要なポイント:**
- `const T*` で不変借用（読み取り専用）
- `T*` で可変借用（読み書き可能）
- 借用された値は移動できない
- ポインタが所有権ルールでメモリ安全性を保証

## ポインタ演算

```cm
int main() {
    int[5] arr = [1, 2, 3, 4, 5];
    int* p = arr;  // 配列の先頭アドレス

    // ポインタの加算
    int first = *p;      // 1
    int second = *(p + 1);  // 2
    int third = *(p + 2);   // 3

    // ポインタのインクリメント
    p++;
    int value = *p;  // 2

    // ポインタループ
    int* end = arr + 5;
    for (int* iter = arr; iter != end; iter++) {
        println("{}", *iter);
    }
    return 0;
}
```

## 構造体ポインタ

### 基本的なアクセス

```cm
struct Point {
    int x;
    int y;
}

int main() {
    Point pt = Point(10, 20);
    Point* ptr = &pt;

    // Cmでは . 演算子で構造体ポインタのフィールドにアクセス
    // （-> 演算子ではなく . 演算子を使用）
    (*ptr).x = 30;
    (*ptr).y = 40;
    return 0;
}
```

### ポインタ経由の関数呼び出し

```cm
interface Printable {
    void print();
}

struct Point { int x; int y; }

impl Point for Printable {
    void print() {
        println("({}, {})", self.x, self.y);
    }
}

int main() {
    Point pt = Point(5, 10);
    Point* ptr = &pt;
    
    (*ptr).print();  // "(5, 10)"
    return 0;
}
```

## 配列とポインタ

### Array Decay

配列は自動的にポインタに変換されます。

```cm
int main() {
    int[5] arr = [1, 2, 3, 4, 5];

    // 暗黙的変換
    int* p = arr;  // arr[0]のアドレス
    return 0;
}

// 関数に渡す
void process(int* data, int size) {
    for (int i = 0; i < size; i++) {
        data[i] *= 2;
    }
}

int main() {
    int[5] arr = [1, 2, 3, 4, 5];
    process(arr, 5);  // 配列→ポインタ変換
    return 0;
}
```

### ポインタと配列の等価性

```cm
int main() {
    int[5] arr = {10, 20, 30, 40, 50};
    int* p = arr;

    // 以下は全て同じ
    int v1 = arr[2];   // 30
    int v2 = p[2];     // 30
    int v3 = *(p + 2); // 30
    int v4 = *(arr + 2);  // 30
    return 0;
}
```

## 関数ポインタ

### 関数ポインタ宣言

```cm
// 関数ポインタ型: 戻り値型*(引数型...)
int*(int, int) op;

int add(int a, int b) {
    return a + b;
}

int multiply(int a, int b) {
    return a * b;
}

int main() {
    // 関数ポインタに代入
    op = add;
    int result1 = op(10, 20);  // 30
    
    op = multiply;
    int result2 = op(10, 20);  // 200
    
    return 0;
}
```

### 高階関数

```cm
// 関数ポインタを引数に取る関数
int apply(int*(int, int) fn, int x, int y) {
    return fn(x, y);
}

int max(int a, int b) {
    return a > b ? a : b;
}

int min(int a, int b) {
    return a < b ? a : b;
}

int main() {
    int result1 = apply(max, 10, 5);  // 10
    int result2 = apply(min, 10, 5);  // 5
    return 0;
}
```

## Null ポインタ

```cm
int main() {
    // Nullポインタ
    int* p = null;

    // Nullチェック
    if (p == null) {
        println("Pointer is null");
    }

    // Nullポインタのデリファレンスは未定義動作
    // *p = 10;  // 危険！
    return 0;
}
```

## typedef型ポインタ

```cm
typedef MyInt = int;
typedef MyFloat = float;

// typedef型のポインタ
MyInt* p1;
MyFloat* p2;

int main() {
    MyInt x = 42;
    p1 = &x;
    return 0;
}

// インタプリタおよびLLVMで動作
```

## 実装状況

| 機能 | JIT | LLVM | WASM | JS |
|------|-----|------|------|----|
| 基本的なポインタ | ✅ | ✅ | ✅ | ✅ |
| デリファレンス | ✅ | ✅ | ✅ | ✅ |
| ポインタ演算 | ✅ | ✅ | ✅ | ❌ |
| 構造体ポインタ | ✅ | ✅ | ✅ | ✅ |
| 配列→ポインタ | ✅ | ✅ | ✅ | ❌ |
| 関数ポインタ | ✅ | ✅ | ✅ | ❌ |
| typedef型ポインタ | ✅ | ✅ | ⚠️ | ❌ |

## サンプルコード

完全なサンプルは以下を参照してください：

- `examples/pointers/basic_pointer.cm` - 基本的なポインタ操作
- `examples/pointers/struct_pointer.cm` - 構造体ポインタ
- `examples/pointers/function_pointer.cm` - 関数ポインタ
- `tests/test_programs/pointer/` - テストケース

## 関連ドキュメント

- [配列](arrays.html) - 配列操作
- メモリ管理 - メモリ安全性
- 型システム - 型の詳細

---

**更新日:** 2026-02-10

---

<!-- nav -->
← 前: [配列（Arrays）](arrays.html) ｜ [目次](index.html) ｜ 次: [モジュールシステム](modules.html) →
