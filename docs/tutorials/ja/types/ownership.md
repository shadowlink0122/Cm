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

Cm言語（v0.11.0以降）では、Rustと同様の所有権システムにより、ガベージコレクションなしでメモリ安全性を保証します。

**所有権の3つのルール:**
1.  各値は、ある時点で1つの変数（所有者）だけが所有する。
2.  所有者がスコープを抜けると、値は破棄される。
3.  値の代入や引数渡しは、所有権の「移動（Move）」を意味する。

---

## 移動 (Move)

構造体などの非プリミティブ型は、代入によって所有権が移動します。

```cm
struct Box { int x; }

int main() {
    Box a = {x: 10};
    Box b = a;  // 所有権が a から b へ移動 (Move)
    
    // println("{}", a.x); // エラー: a は移動済みで無効
    println("{}", b.x); // OK: b が現在の所有者
    return 0;
}
```

### コピー (Copy)

`int` や `bool` などのプリミティブ型は `Copy` トレイトを実装しているため、移動ではなくコピーされます。

```cm
int main() {
    int x = 10;
    int y = x;  // 値がコピーされる
    
    println("x: {}, y: {}", x, y); // 両方使える
    return 0;
}
```

構造体でも `with Copy` を指定すればコピー可能になります（ただし、フィールドが全てCopy可能である必要があります）。

```cm
struct Point with Copy {
    int x;
    int y;
}
```

---

## 借用 (Borrowing)

所有権を渡さずに値を参照したい場合は、「借用」を使用します。

### 不変参照 (`&T`)

値を読み取るだけで、変更や所有権の移動はしません。

```cm
void print_box(const Box& b) { // 借用
    println("Box({})", b.x);
}

int main() {
    Box a = {x: 10};
    print_box(&a); // 参照を渡す
    println("{}", a.x); // 所有権は移動していないのでアクセス可能
    return 0;
}
```

### 可変参照 (`&mut T` / `T*`)

値を変更する必要がある場合は、可変参照（ポインタ）を使用します。

```cm
void increment(Box* b) {
    b->x++;
}

int main() {
    Box a = {x: 10};
    increment(&a);
    println("{}", a.x); // 11
    return 0;
}
```

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
