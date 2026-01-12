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
    println("Hello, {}!", name);
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
    println("{}", x);  // 10（変更されない）
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
    println("{}", x);  // 11（変更される）
    return 0;
}
```

---

## 関数オーバーロード

同じ名前で、異なるパラメータ（型または数）を持つ関数を定義できます。
`overload` キーワードを付ける必要があります。

```cm
overload int max(const int a, const int b) {
    return a > b ? a : b;
}

overload double max(const double a, const double b) {
    return a > b ? a : b;
}

int main() {
    const int maxInt = max(10, 20);
    const double maxDouble = max(3.14, 2.71);
    println("max int: {}", maxInt);
    println("max double: {}", maxDouble);
    return 0;
}
```

---

## デフォルト引数

パラメータにデフォルト値を設定できます。呼び出し時に省略された場合、その値が使われます。

```cm
void log(string message, int level = 1) {
    println("[Level {}] {}", level, message);
}

    return 0;
}
```

---

## メソッド呼び出し

構造体に関連付けられた関数（メソッド）は、`.` 演算子で呼び出します。内部的には第1引数として `self` ポインタを受け取ります。

```cm
}

    void increment() {
    }
}

    Counter c;
    c.value = 0;
    c.increment();  // メソッド形式で呼び出し
    return 0;
}
```

詳細は [構造体](../types/structs.html) や [インターフェース](../types/interfaces.html) の章で詳しく解説します。

---

## よくある間違い

### ❌ overload キーワードの忘れ

```cm
// int foo(double x) { ... } // エラー: overloadキーワードが必要
overload int foo(double x) { ... } // OK
```

### ❌ 戻り値の型のみが異なるオーバーロード

オーバーロードはパラメータで区別する必要があります。

```cm
// overload double bar(int x) { ... } // エラー: パラメータが同じ
```

---

## 練習問題

### 問題1: 階乗関数
再帰を使って、与えられた整数の階乗を計算する `factorial(int n)` 関数を作成してください。

<details>
<summary>解答例</summary>

```cm
        return 1;
    }
    return n * factorial(n - 1);
}

    println("5! = {}", factorial(5));
    return 0;
}
```
</details>

---

## 次のステップ

✅ 関数の定義と呼び出しができる  
✅ オーバーロードの使い方がわかった  
✅ デフォルト引数が使える  
⏭️ 次は [配列](arrays.html) を学びましょう

## 関連リンク

- [構造体](../types/structs.html) - メソッドの定義
- [インターフェース](../types/interfaces.html) - ポリモーフィズム
- [関数ポインタ](../advanced/function-pointers.html) - 高階関数

---

**前の章:** [制御構文](control-flow.html)  
**次の章:** [配列](arrays.html)