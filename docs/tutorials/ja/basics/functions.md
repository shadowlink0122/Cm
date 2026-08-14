---
title: 関数
parent: Tutorials
---

[English](../../en/basics/functions.html)

# 基本編 - 関数

**難易度:** 🟢 初級  
**所要時間:** 20分

## 📚 この章で学ぶこと

- 関数の定義と呼び出し
- 戻り値とパラメータ
- 関数オーバーロード
- デフォルト引数
- メソッド呼び出しの基礎

---

## 基本的な関数定義

関数は `戻り値の型 関数名(パラメータ) { ... }` の形式で定義します。

```cm
int add(const int a, const int b) {  // パラメータは変更しないのでconst
    return a + b;
}

void greet(const string name) {  // nameも変更しないのでconst
    println("Hello, {name}!");
}

int main() {
    const int result = add(3, 5);  // 結果も変更しない
    greet("Alice");
    return 0;
}
```

### 戻り値のない関数

戻り値がない場合は `void` 型を指定します。

```cm
void print_hello() {
    println("Hello");
}
```

---

## パラメータ（引数）

### 値渡し

Cmではデフォルトで値渡しが行われます。

```cm
void increment(int n) {
    n++;  // コピーを変更（元の値には影響なし）
}

int main() {
    int x = 10;  // 可変変数（変更予定がないならconstにすべき）
    increment(x);
    println("{x}");  // 10（変更されない）
    return 0;
}
```

### ポインタ渡し

元の値を変更するにはポインタを使用します。

```cm
void increment(int* n) {
    (*n)++;  // ポインタの参照先を変更
}

int main() {
    int x = 10;  // 可変変数（変更するのでconstは使えない）
    increment(&x);
    println("{x}");  // 11（変更される）
    return 0;
}
```

---

## 関数オーバーロード（未対応）

**自由関数のオーバーロードは現在未対応です。** 同じ名前で異なるシグネチャの関数を定義するとコンパイルエラーになります:

```cm
int process(int x) { return x; }
double process(double x) { return x; }
// エラー: 関数 'process' は既に異なるシグネチャで定義されています
```

型ごとに別名を使用してください（例: `max_int` / `max_double`）。

なお、**コンストラクタのオーバーロード**は `overload self(...)` 構文で対応しています（[構造体](../types/structs.html)参照）。自由関数のオーバーロードは将来のバージョンで検討されます。

---

## デフォルト引数

パラメータにデフォルト値を設定できます。呼び出し時に省略された場合、その値が使われます。
デフォルト値の式にはリテラル・関数呼び出し・グローバル変数を書けますが、同じ関数の他のパラメータは参照できません（デフォルト引数はパラメータ束縛前に呼び出し側で評価されるため。`int f(int a, int b = a)` はコンパイルエラーになります。v0.17.0）。

```cm
void log(string message, int level = 1) {
    println("[Level {level}] {message}");
}

int main() {
    log("System started");   // level は既定値の 1
    log("Debug info", 3);    // level を明示指定
    return 0;
}
```

---

## メソッド呼び出し

構造体に関連付けられた関数（メソッド）は `impl` ブロックで定義し、`.` 演算子で呼び出します。内部的には第1引数として `self` ポインタを受け取ります。

```cm
struct Counter {
    int value;
}

impl Counter {
    void increment() {
        self.value++;  // self.value は (*self).value と同等（自動デリファレンス）
    }
}

int main() {
    Counter c;
    c.value = 0;
    c.increment();  // メソッド形式で呼び出し
    println("{c.value}");  // 1
    return 0;
}
```

詳細は [構造体](../types/structs.html) や [インターフェース](../types/interfaces.html) の章で詳しく解説します。

---

## よくある間違い

### ❌ 自由関数を同名で多重定義する

自由関数のオーバーロードは未対応のため、同じ名前の関数を複数定義するとコンパイルエラーになります。

```cm
int foo(int x) { return x; }
// double foo(double x) { return x; }
// エラー: function 'foo' is already defined with a different signature
//        (free-function overloading is not supported; use a different name)
```

型ごとに別名を付けてください（例: `foo_int` / `foo_double`）。コンストラクタは `overload self(...)` で多重定義できます（[構造体](../types/structs.html) 参照）。

---

## 練習問題

### 問題1: 階乗関数
再帰を使って、与えられた整数の階乗を計算する `factorial(int n)` 関数を作成してください。

<details>
<summary>解答例</summary>

```cm
int factorial(int n) {
    if (n <= 1) {
        return 1;
    }
    return n * factorial(n - 1);
}

int main() {
    println("5! = {factorial(5)}");
    return 0;
}
```
</details>

---

## 次のステップ

✅ 関数の定義と呼び出しができる  
✅ 自由関数のオーバーロードが未対応であることを理解した  
✅ デフォルト引数が使える  
⏭️ 次は [配列](arrays.html) を学びましょう

## 関連リンク

- [構造体](../types/structs.html) - メソッドの定義
- [インターフェース](../types/interfaces.html) - ポリモーフィズム
- [関数ポインタ](../advanced/function-pointers.html) - 高階関数

---

**前の章:** [制御構文](control-flow.html)  
**次の章:** [配列](arrays.html)

---

**最終更新:** 2026-02-08

---

<!-- nav -->
← 前: [基本編 - 制御構文](control-flow.html) ｜ [目次](index.html) ｜ 次: [配列（Arrays）](arrays.html) →
