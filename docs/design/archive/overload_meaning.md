# overload 修飾子の意味と使い方

## overload ≠ override

**重要**: Cm言語の `overload` は「上書き（override）」ではありません。

### 他言語との比較

| 言語 | キーワード | 意味 |
|------|------------|------|
| C++ | (なし) | 同名関数の暗黙的許可 |
| Java | `@Override` | 親クラスのメソッドを上書き |
| C# | `override` | 仮想メソッドの上書き |
| **Cm** | `overload` | **同名・異シグネチャの関数を許可** |

## overload の正確な意味

```cm
// overload = 「同じ名前で異なるパラメータの関数を許可する」

overload int add(int a, int b);      // int版
overload double add(double a, double b);  // double版
overload string add(string a, string b);  // string版

// これらは共存する（上書きではない）
int x = add(1, 2);           // int版が呼ばれる
double y = add(1.0, 2.0);    // double版が呼ばれる
string z = add("a", "b");    // string版が呼ばれる
```

## なぜ明示的な overload が必要か？

### 1. 意図の明確化

```cm
// 意図しない名前衝突を防ぐ
void process(int x) { }

// 後から追加（うっかり同じ名前を使用）
void process(double x) { }  // エラー！overloadが必要

// 意図的なオーバーロードは明示
overload void process(int x) { }
overload void process(double x) { }  // OK
```

### 2. トランスパイラ対応

Rustは関数オーバーロードをサポートしないため、
`overload` 修飾子があることで、コンパイラは適切な変換戦略を選択できます。

```cm
// Cm（明示的なoverload）
overload void print(int x);
overload void print(string s);
```

```rust
// Rust変換（名前マングリング）
fn print_i32(x: i32) { }
fn print_String(s: String) { }
```

### 3. コード可読性

```cm
// overloadキーワードで、複数の実装があることが一目瞭然
overload Complex operator+(Complex a, Complex b);
overload Complex operator+(Complex a, double b);
overload Complex operator+(double a, Complex b);
```

## override との違い（将来の拡張）

将来的に継承をサポートする場合、`override` は別の意味で使用される可能性があります。

```cm
// 将来の継承構文（仮）
class Animal {
    virtual void speak() { println("..."); }
}

class Dog : Animal {
    override void speak() { println("Woof!"); }  // 上書き
}

// overloadとは全く異なる
class Calculator {
    overload int add(int a, int b);      // オーバーロード
    overload double add(double a, double b);  // オーバーロード
}
```

## まとめ

- **`overload`** = 同名・異シグネチャの関数を許可（C++のオーバーロード）
- **`override`** = （将来）親クラスのメソッドを上書き（Java/C#のオーバーライド）
- デフォルトでは同名関数はエラー（安全性重視）
- `overload` で明示的に許可（意図の明確化）