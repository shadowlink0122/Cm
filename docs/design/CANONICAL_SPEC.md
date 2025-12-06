# Cm言語 正式仕様（矛盾解決版）

このドキュメントはCm言語の**唯一の正式仕様**です。
他のドキュメントと矛盾がある場合、この仕様が優先されます。

## 1. 関数定義構文

**正式構文：C++スタイル（fnキーワードなし）**

```cm
// 正しい
int add(int a, int b) {
    return a + b;
}

void print(string message) {
    println(message);
}

// 間違い（Rust風は使用しない）
fn add(a: int, b: int) -> int {  // ❌
    return a + b;
}
```

## 2. ジェネリック関数構文

**正式構文：先頭に型パラメータ宣言（オプション3）**

```cm
// 正しい
<T: Ord> T max(T a, T b) {
    return a > b ? a : b;
}

<T, U> U convert(T from, U default_val) {
    // 実装
}

// 間違い
fn max<T: Ord>(a: T, b: T) -> T { }  // ❌ Rust風
T max(T a, T b) { }                   // ❌ 暗黙的推論
```

## 3. オーバーロード修飾子

**正式規則：明示的な`overload`キーワード必須**

```cm
// 正しい：全ての宣言にoverloadが必要
overload int add(int a, int b) { return a + b; }
overload double add(double a, double b) { return a + b; }
overload string add(string a, string b) { return a + b; }

// 間違い：overloadなしで同名関数
int process(int x) { }
double process(double x) { }  // ❌ エラー
```

### 3.1 コンストラクタのオーバーロード

```cm
impl<T> Vec<T> {
    // デフォルトコンストラクタ（overload不要）
    self() {
        this.data = null;
        this.size = 0;
    }

    // 追加コンストラクタ（overload必須）
    overload self(size_t capacity) {
        this.data = new T[capacity];
        this.size = 0;
        this.capacity = capacity;
    }

    overload self(const Vec<T>& other) {
        // コピーコンストラクタ
    }

    // デストラクタ（オーバーロード不可）
    ~self() {
        delete[] this.data;
    }
}
```

### 3.2 演算子オーバーロード

**演算子は暗黙的にoverload可能（キーワード不要）**

```cm
// 正しい：演算子にoverloadは不要
Complex operator+(const Complex& a, const Complex& b) { }
Complex operator+(const Complex& a, double b) { }
Complex operator+(double a, const Complex& b) { }
```

## 4. impl ブロック構文

**2つの形式：**

```cm
// 形式1：コンストラクタ・デストラクタ専用
impl<T> Vec<T> {
    self() { }                    // コンストラクタ
    overload self(size_t) { }     // オーバーロード
    ~self() { }                   // デストラクタ
}

// 形式2：メソッド実装（for インターフェース）
impl<T> Vec<T> for Container<T> {
    void push(T item) { }         // メソッド
    T pop() { }

    // メソッドのオーバーロード
    overload void push(T item, size_t count) { }
}
```

## 5. 型エイリアス

**正式キーワード：`typedef`（C++互換）**

```cm
// 正しい
typedef Int32 = int;
typedef StringList = Vec<string>;
typedef Result<T> = Ok(T) | Err(string);

// リテラル型・ユニオン型
typedef HttpMethod = "GET" | "POST" | "PUT" | "DELETE";
typedef Primitive = int | double | string | bool;

// 間違い（typeは使用しない）
type Int32 = int;  // ❌
```

## 6. 構造体定義

```cm
// 構造体
struct Point {
    double x;
    double y;
}

// withキーワードで自動トレイト実装
struct Point3D with Debug, Clone {
    double x;
    double y;
    double z;
}

// 間違い
#[derive(Debug, Clone)]  // ❌ Rust風アトリビュート
struct Point3D { }
```

## 7. マクロ定義

**正式構文：`#macro`キーワード**

```cm
// 正しい
#macro void LOG(string msg) {
    println("[LOG] " + msg);
}

#macro <T> void SWAP(T& a, T& b) {
    T temp = a;
    a = b;
    b = temp;
}

// 間違い（バックスラッシュ継続は使わない）
#define LOG(msg) \     // ❌
    println(msg)
```

## 8. 関数ディレクティブ

```cm
#test
void test_addition() {
    assert(add(1, 2) == 3);
}

#bench
void bench_sort() {
    // ベンチマークコード
}

#inline
int square(int x) {
    return x * x;
}

#deprecated("Use new_function instead")
void old_function() { }
```

## 9. モジュールシステム

```cm
// インポート
import std::io;
import std::collections::{Vec, HashMap};
import math::*;

// エクスポート
export struct MyStruct { }
export void my_function() { }
export typedef MyType = int;
```

## 10. 型推論規則

**明示的宣言必須（暗黙的推論なし）**

```cm
// 正しい：型パラメータを明示的に宣言
<T> T identity(T value) {
    return value;
}

// 間違い：暗黙的な型推論
T identity(T value) {  // ❌ Tが未宣言
    return value;
}
```

### 例外：単一大文字の自動認識（検討中）

```cm
// 将来的に許可される可能性
T max(T a, T b) {  // Tは自動的にジェネリックと認識
    return a > b ? a : b;
}
```

## 優先順位

このドキュメントの仕様が最優先。
矛盾がある場合の優先順位：

1. **CANONICAL_SPEC.md**（このファイル）
2. overload_unified.md
3. impl_blocks.md
4. その他の設計ドキュメント

## 更新履歴

- 2024-12-XX: 初版作成（矛盾解決版）