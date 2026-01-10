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
    return a + b;
}

void greet(string name) {
    println("Hello, {}!", name);
}

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
}

    println("{}", x);  // 10（変更されない）
    return 0;
}
```

### ポインタ渡し

元の値を変更するにはポインタを使用します。

```cm
void increment(int* n) {
}

    println("{}", x);  // 11（変更される）
    return 0;
}
```

---

## 関数オーバーロード

同じ名前で、異なるパラメータ（型または数）を持つ関数を定義できます。
`overload` キーワードを付ける必要があります。

```cm
overload int max(int a, int b) {
    return a > b ? a : b;
}

overload double max(double a, double b) {
    return a > b ? a : b;
}

    println("max int: {}", max(10, 20));
    println("max double: {}", max(3.14, 2.71));
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