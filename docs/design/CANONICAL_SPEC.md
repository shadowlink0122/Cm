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

**正式構文：先頭に型パラメータ宣言**

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

## 2.1 インターフェース境界

**すべての制約はインターフェース境界として解釈される**

インターフェース境界は「型Tがインターフェースを実装している」ことを要求します。
具体的な型を直接指定することはできません。

```cm
// 単一インターフェース境界
// TはEqインターフェースを実装している型
<T: Eq> bool equals(T a, T b) {
    return a == b;
}

// AND境界（複合実装要求）
// TはEqとOrdの両方を実装している型
<T: Eq + Ord> T max_if_equal(T a, T b) {
    if (a == b) { return a; }
    return a > b ? a : b;
}

// OR境界（いずれかの実装要求）
// TはEqまたはHashのいずれかを実装している型
<T: Eq | Hash> void process(T value) {
    // EqかHashのどちらかが使える
}
```

### where句による境界指定

where句は、関数シグネチャの後でインターフェース境界を指定します。

```cm
// where句での境界指定
<T, U> T combine(T a, U b) where T: Eq, U: Clone {
    // TはEqを実装、UはCloneを実装
}

// 複合境界をwhere句で指定
<T> T process(T value) where T: Eq + Ord + Clone {
    // TはEq, Ord, Cloneのすべてを実装
}
```

### 境界の種類

| 構文 | 意味 |
|------|------|
| `T: I` | TはインターフェースIを実装している |
| `T: I + J` | TはIとJの両方を実装している（AND） |
| `T: I \| J` | TはIまたはJを実装している（OR） |

**注意**: 境界はインターフェースを指定します。具体的な型（int, stringなど）を直接境界として使用することはできません。

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
    void push(T item) { }         // publicメソッド（デフォルト）
    T pop() { }

    // privateメソッド（impl内からのみ呼び出し可能）
    private void grow() {
        // 内部ヘルパー関数
    }

    // メソッドのオーバーロード
    overload void push(T item, size_t count) { }
}
```

### 4.1 privateメソッド

`private`修飾子をメソッドの前に付けると、そのメソッドは同じimplブロック内からのみ呼び出し可能になります。

```cm
interface Calculator {
    int calculate(int x);
}

struct MyCalc {
    int base;
}

impl MyCalc for Calculator {
    // privateメソッド：外部から呼び出し不可
    private int helper(int n) {
        return n * 2;
    }

    // publicメソッド：外部から呼び出し可能
    int calculate(int x) {
        return self.helper(x) + self.base;  // impl内からはprivateを呼べる
    }
}

void main() {
    MyCalc c;
    c.base = 10;
    int result = c.calculate(5);  // OK: publicメソッド
    // c.helper(5);                // エラー: privateメソッドは外部から呼べない
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

### 6.1 メンバ修飾子

#### private修飾子
`private`修飾子を持つメンバは直接アクセス不可。コンストラクタ・デストラクタ内の`this`ポインタからのみアクセス可能。

```cm
struct Person {
    string name;        // 外部からアクセス可能
    private int age;    // コンストラクタ/デストラクタ内からのみアクセス可能
}

impl Person {
    self(string name, int age) {
        this.name = name;
        this.age = age;   // ✓ コンストラクタ内からアクセス可能
    }
}

// アクセサはフリー関数として定義
int getAge(const Person& p);  // 実装はfriend関数等で別途提供

void main() {
    Person p("Alice", 30);
    p.name = "Bob";     // ✓ OK
    // p.age = 30;      // ❌ エラー: privateメンバへの直接アクセス
}
```

#### default修飾子
`default`修飾子はデフォルトメンバを指定。構造体に1つだけ設定可能。メンバ名を省略した場合にこのメンバにアクセス。

```cm
struct Wrapper {
    default int value;  // デフォルトメンバ
    string tag;
}

void main() {
    Wrapper w;
    w.value = 10;  // 通常のアクセス
    w = 20;        // デフォルトメンバへのアクセス（w.value = 20と同等）
    int x = w;     // デフォルトメンバの取得（x = w.valueと同等）
}
```

**制約：**
- `default`修飾子は構造体に1つのメンバにのみ指定可能
- 複数のdefaultメンバはコンパイルエラー

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