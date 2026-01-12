---
title: 所有権と借用
parent: Tutorials
---

[English](../../en/types/ownership.html)

# 型システム編 - 所有権と借用

**難易度:** 🔴 上級  
**所要時間:** 40分

## 📚 この章で学ぶこと

- 所有権システム (Ownership System)
- 移動セマンティクス (Move Semantics)
- 借用 (Borrowing)
- 参照 (References)

---

## 所有権とは

Cm言語（v0.11.0以降）では、所有権システムにより、ガベージコレクションなしでメモリ安全性を保証します。

**所有権の4つのルール:**
1. 各値は、ある時点で1つの変数（所有者）だけが所有する
2. 所有者がスコープを抜けると、値は破棄される
3. 所有権は `move` キーワードで明示的に移動できる
4. 移動後、元の変数は使用できなくなる

---

## 移動セマンティクス (v0.11.0以降)

`move`キーワードを使用して、値の所有権を明示的に移動します。

### moveキーワードの使用

```cm
int main() {
    // プリミティブ型も移動可能
    int x = 42;
    int y = move x;  // xの所有権がyへ移動

    println("{y}");     // OK: yが値を所有
    // println("{x}");  // エラー: xは移動済みで使用不可

    // 移動後の再代入も禁止
    // x = 50;  // エラー: 移動した変数への代入不可

    return 0;
}
```

### 構造体の移動

```cm
struct Box { int value; }

int main() {
    Box a = {value: 10};
    Box b = move a;  // 明示的な移動

    println("{}", b.value); // OK: bが所有者
    // println("{}", a.value); // エラー: aは移動済み

    return 0;
}
```

### コピー vs 移動

プリミティブ型は明示的に移動しない限り、デフォルトでコピーされます：

```cm
int main() {
    // デフォルトのコピー動作
    int x = 10;
    int y = x;        // コピー（xとy両方が有効）
    println("{x} {y}"); // OK: 両方アクセス可能

    // 明示的な移動
    int a = 20;
    int b = move a;   // 移動
    println("{b}");    // OK
    // println("{a}"); // エラー: aは移動済み

    return 0;
}
```

---

## 借用 (v0.11.0ではポインタベース)

**重要:** 現在のCmはポインタで借用を実現します。C++スタイルの参照（`&T`）は未実装です。

### 不変借用（constポインタ）

```cm
int main() {
    const int value = 42;
    const int* ref = &value;  // constポインタで借用

    println("値: {*ref}");  // デリファレンスしてアクセス

    // *ref = 50;  // エラー: constポインタで変更不可

    return 0;
}
```

### 可変借用（ポインタ）

```cm
int main() {
    int x = 10;
    int* px = &x;  // ポインタで可変借用

    *px = 20;  // ポインタ経由で変更
    println("x = {x}");  // 出力: x = 20

    return 0;
}
```

### 借用と移動の制限

```cm
int main() {
    int y = 100;
    const int* py = &y;  // yを借用中

    // int z = move y;  // エラー: 借用中は移動不可
    // y = 200;         // エラー: 借用中は変更不可

    println("*py = {*py}");

    return 0;
}
```

### 構造体の借用

```cm
struct Point { int x; int y; }

void print_point(const Point* p) {  // constポインタで借用
    println("Point({}, {})", p.x, p.y);  // 自動デリファレンス
}

int main() {
    Point pt = {x: 10, y: 20};
    print_point(&pt);  // アドレスを渡す

    // 借用後もptは有効
    println("x: {}", pt.x);

    return 0;
}
```

**注意:** Cmのポインタ構文は構造体フィールドアクセスを自動デリファレンスします（`(*p).x`ではなく`p.x`）。

---

## 借用規則

1.  任意のタイミングで、**一つの可変参照** か **複数の不変参照** のどちらかを持つことができる。
2.  参照は、参照先の値より長く生きてはならない。

---

## 次のステップ

✅ 所有権と移動を理解した  
✅ 借用（参照）の使い方がわかった  
⏭️ 次は [ライフタイム](lifetimes.html) を学びましょう

---

**前の章:** [Enum型](enums.html)  
**次の章:** [ライフタイム](lifetimes.html)
