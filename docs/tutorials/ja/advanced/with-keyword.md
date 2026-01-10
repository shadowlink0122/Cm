---
title: with自動実装
parent: Tutorials
---

[English](../../en/advanced/with-keyword.html)

# withキーワード（自動トレイト実装）

`with`キーワードは、Rustの`#[derive(...)]`をC++風に再設計した機能で、インターフェースの自動実装を提供します。

## 📋 目次

- [基本的な使い方](#基本的な使い方)
- [サポートされるトレイト](#サポートされるトレイト)
- [Eq - 等価比較](#eq---等価比較)
- [Ord - 順序比較](#ord---順序比較)
- [Clone - 複製](#clone---複製)
- [Hash - ハッシュ計算](#hash---ハッシュ計算)
- [複数トレイトの指定](#複数トレイトの指定)
- [ジェネリック構造体](#ジェネリック構造体)
- [実装の仕組み](#実装の仕組み)

## 基本的な使い方

```cm
// Eq自動実装
struct Point with Eq {
    int x;
    int y;
}

int main() {
    Point p1 = Point(10, 20);
    Point p2 = Point(10, 20);

    if (p1 == p2) {  // 自動生成された == 演算子
        println("Equal!");
    }
    return 0;
}
```

## サポートされるトレイト

| トレイト | 説明 | 生成されるメソッド/演算子 | 状態 |
|---------|------|-------------------------|------|
| **Eq** | 等価比較 | `==`, `!=` | ✅ 完全実装 |
| **Ord** | 順序比較 | `<`, `>`, `<=`, `>=` | ✅ 完全実装 |
| **Clone** | 深いコピー | `.clone()` | ✅ 完全実装 |
| **Hash** | ハッシュ計算 | `.hash()` | ✅ 完全実装 |
| **Copy** | ビット単位コピー | （暗黙コピー） | ✅ マーカー |
| **Debug** | デバッグ出力 | `.debug()` | ⬜ v0.10.0予定 |
| **Display** | 文字列化 | `.toString()` | ⬜ v0.10.0予定 |

## Eq - 等価比較

### 基本的な使い方

```cm
struct Point with Eq {
    int x;
    int y;
}

int main() {
    Point a = Point(1, 2);
    Point b = Point(1, 2);
    Point c = Point(3, 4);

    bool same = (a == b);      // true
    bool different = (a != c);  // true
    return 0;
}
```

### 自動生成されるコード

```cm
// withなしで明示的に実装する場合と等価
impl Point for Eq {
    operator bool ==(Point other) {
        return self.x == other.x && self.y == other.y;
    }
}

// != は自動導出
// a != b  →  !(a == b)
```

### ネストした構造体

```cm
struct Point with Eq {
    int x;
    int y;
}

struct Line with Eq {
    Point start;
    Point end;
}

int main() {
    Line l1, l2;
    l1.start = Point(0, 0);
    l1.end = Point(10, 10);
    l2.start = Point(0, 0);
    l2.end = Point(10, 10);

    if (l1 == l2) {  // Pointの==も呼び出される
        println("Same line!");
    }
    return 0;
}
```

## Ord - 順序比較

### 基本的な使い方

```cm
struct Person with Ord {
    int age;
    string name;
}

int main() {
    Person p1 = Person(25, "Alice");
    Person p2 = Person(30, "Bob");

    if (p1 < p2) {  // age, nameの順に字句比較
        println("p1 is younger");
    }
    return 0;
}
```

### 派生演算子

```cm
// < が実装されると、他の演算子は自動導出される
// a > b   →  b < a
// a <= b  →  !(b < a)
// a >= b  →  !(a < b)

struct Number with Ord {
    int value;
}

int main() {
    Number a = Number(10);
    Number b = Number(20);

    bool lt = (a < b);   // true
    bool gt = (a > b);   // false
    bool le = (a <= b);  // true
    bool ge = (a >= b);  // false
    return 0;
}
```

## Clone - 複製

### 基本的な使い方

```cm
struct Point with Clone {
    int x;
    int y;
}

int main() {
    Point p1 = Point(10, 20);
    Point p2 = p1.clone();  // 深いコピー

    p2.x = 30;  // p1は変更されない
    return 0;
}
```

### ネストした構造体

```cm
struct Point with Clone {
    int x;
    int y;
}

struct Shape with Clone {
    Point center;
    int radius;
}

int main() {
    Shape s1 = Shape(Point(5, 5), 10);
    Shape s2 = s1.clone();  // Point::clone()も呼び出される
    return 0;
}
```

## Hash - ハッシュ計算

```cm
struct Point with Hash {
    int x;
    int y;
}

int main() {
    Point p = Point(10, 20);
    uint hash_value = p.hash();  // フィールドのハッシュを組み合わせる
    return 0;
}
```

## 複数トレイトの指定

### `+`で複数指定

```cm
struct Point with Eq + Ord + Clone {
    int x;
    int y;
}

// 以下が自動実装される：
// - operator ==
// - operator <
// - clone() メソッド
```

### 使用例

```cm
int main() {
    Point p1 = Point(10, 20);
    Point p2 = Point(10, 20);

    // Eq
    if (p1 == p2) { }

    // Ord
    if (p1 < p2) { }

    // Clone
    Point p3 = p1.clone();
    return 0;
}
```

## ジェネリック構造体

```cm
struct Pair<T> with Eq + Clone {
    T first;
    T second;
}

// 自動生成されるimpl:
// impl<T> Pair<T> for Eq {
//     operator bool ==(Pair<T> other) {
//         return self.first == other.first 
//             && self.second == other.second;
//     }
// }

int main() {
    Pair<int> p1 = Pair<int>(1, 2);
    Pair<int> p2 = Pair<int>(1, 2);

    if (p1 == p2) {
        println("Equal pairs!");
    }
    return 0;
}
```

## 実装の仕組み

### MIRレベルでの自動生成

`with`キーワードは、MIRローワリング時に実装関数を自動生成します。

```cm
struct Point with Eq {
    int x;
    int y;
}

// ↓ MIRで以下の関数が自動生成される

// Point__op_eq(Point& self, Point& other) -> bool
//   tmp0 = self.x == other.x
//   tmp1 = self.y == other.y
//   tmp2 = tmp0 && tmp1
//   return tmp2
```

### デッドコード削除

使用されない自動実装は、デッドコード削除で自動的に削除されます。

```cm
struct Point with Eq {
    int x;
    int y;
}

int main() {
    Point p = Point(1, 2);
    // == を使用していない
    return 0;
}

// Point__op_eq は生成されるが、DCEで削除される
```

## 明示的実装との比較

### with使用

```cm
struct Point with Eq {
    int x;
    int y;
}

// 全フィールドの比較が自動生成される
```

### 明示的実装

```cm
struct Point {
    int x;
    int y;
}

impl Point for Eq {
    operator bool ==(Point other) {
        // カスタムロジック
        return self.x == other.x && self.y == other.y;
    }
}
```

### カスタムロジックが必要な場合

```cm
struct Person {
    int id;
    string name;
}

// IDだけで比較したい場合は明示的実装
impl Person for Eq {
    operator bool ==(Person other) {
        return self.id == other.id;  // nameは無視
    }
}
```

## 実装状況

| トレイト | インタプリタ | LLVM | WASM |
|---------|------------|------|------|
| Eq | ✅ | ✅ | ✅ |
| Ord | ✅ | ✅ | ✅ |
| Clone | ✅ | ✅ | ✅ |
| Hash | ✅ | ✅ | ✅ |
| Debug | ⬜ v0.10.0 | ⬜ v0.10.0 | ⬜ v0.10.0 |
| Display | ⬜ v0.10.0 | ⬜ v0.10.0 | ⬜ v0.10.0 |

## サンプルコード

完全なサンプルは以下を参照してください：

- `examples/with/basic_eq.cm` - Eq基本例
- `examples/with/multiple_traits.cm` - 複数トレイト
- `tests/test_programs/interface/with_*.cm` - テストケース

## 関連ドキュメント

- [インターフェース](../types/interfaces.html) - interface/impl構文
- [演算子オーバーロード](../advanced/operators.html) - operator実装
- [正式言語仕様](../../../design/CANONICAL_SPEC.html) - with構文の仕様

---

**最終更新:** 2025-01-05
