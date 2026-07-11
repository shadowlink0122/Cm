---
title: 自動実装（with / #[derive]）
parent: Tutorials
---

[English](../../en/advanced/with-keyword.html)

# 自動実装（with / #[derive]）

構造体に対するインターフェースの自動実装（auto-derive）は、`with` キーワードまたは `#[derive(...)]` 属性で指定します。
両者は**完全に同一の機能**であり、違いは記法のみです（新規コードでは Rust と同形の `#[derive]` を推奨します）。

## 📋 目次

- [基本的な使い方](#基本的な使い方)
- [導出可能なインターフェース](#導出可能なインターフェース)
- [Eq - 等価比較](#eq---等価比較)
- [Ord - 順序比較](#ord---順序比較)
- [Clone - 複製](#clone---複製)
- [Hash - ハッシュ計算](#hash---ハッシュ計算)
- [Debug / Display - 文字列化](#debug--display---文字列化)
- [複数インターフェースの指定](#複数インターフェースの指定)
- [ジェネリック構造体](#ジェネリック構造体)
- [フィールド型の対応範囲](#フィールド型の対応範囲)
- [エラーになる指定](#エラーになる指定)
- [実装の仕組み](#実装の仕組み)

## 基本的な使い方

```cm
// 推奨: #[derive(...)] 属性
#[derive(Eq)]
struct Point {
    int x;
    int y;
}

// 従来のwith構文も引き続き有効（同じ意味）
struct Color with Eq {
    int r;
    int g;
    int b;
}

int main() {
    Point p1;
    p1.x = 10;
    p1.y = 20;
    Point p2;
    p2.x = 10;
    p2.y = 20;

    if (p1 == p2) {  // 自動生成された == 演算子
        println("Equal!");
    }
    return 0;
}
```

## 導出可能なインターフェース

導出できるのはコンパイラ組み込みの8種のみです（ユーザー定義インターフェースは `impl <型> for <interface>` で実装します）。

| インターフェース | 説明 | 生成されるメソッド/演算子 |
|---------|------|-------------------------|
| **Eq** | 等価比較 | `==`, `!=` |
| **Ord** | 順序比較 | `<`, `>`, `<=`, `>=` |
| **Copy** | ビット単位コピー | （マーカーのみ） |
| **Clone** | 深いコピー | `.clone()` |
| **Hash** | ハッシュ計算 | `.hash()` |
| **Debug** | デバッグ出力 | `.debug()` |
| **Display** | 文字列化 | `.toString()` |
| **Css** | CSS生成（js/web専用） | `.css()`, `.to_css()`, `.isCss()` |

## Eq - 等価比較

```cm
#[derive(Eq)]
struct Point {
    int x;
    int y;
}

int main() {
    Point a;
    a.x = 1;
    a.y = 2;
    Point b;
    b.x = 1;
    b.y = 2;

    bool same = (a == b);       // true（全フィールドの比較）
    bool different = (a != b);  // false（!= は == から自動導出）
    return 0;
}
```

ネストした構造体フィールドは再帰的に比較され、固定長1次元配列フィールドは要素ごとに比較されます。

## Ord - 順序比較

```cm
#[derive(Ord)]
struct Person {
    int age;
    string name;
}

// < が実装されると、他の演算子は自動導出される
// a > b   →  b < a
// a <= b  →  !(b < a)
// a >= b  →  !(a < b)
```

比較はフィールド宣言順の辞書式順序です。

## Clone - 複製

```cm
#[derive(Clone)]
struct Shape {
    int center_x;
    int center_y;
    int radius;
}

int main() {
    Shape s1;
    s1.radius = 10;
    Shape s2 = s1.clone();  // 深いコピー
    s2.radius = 30;         // s1は変更されない
    return 0;
}
```

## Hash - ハッシュ計算

```cm
#[derive(Hash)]
struct Point {
    int x;
    int y;
}

int main() {
    Point p;
    p.x = 10;
    p.y = 20;
    int hash_value = p.hash();  // フィールド値のFNV-1a混合
    return 0;
}
```

ネスト構造体フィールドは各構造体の `hash()` を再帰的に呼び出して混合されます。

## Debug / Display - 文字列化

```cm
#[derive(Debug, Display)]
struct User {
    int id;
    string name;
}

int main() {
    User u;
    u.id = 7;
    u.name = "cm";
    println(u.debug());     // User { id: 7, name: cm }
    println(u.toString());  // (7, cm)
    return 0;
}
```

## 複数インターフェースの指定

カンマ区切りで複数指定できます。
複数の `#[derive]` 属性はマージされ、`with` との併用も可能です（同一インターフェースの重複指定はエラー）。

```cm
// 1つのderiveにまとめる
#[derive(Eq, Ord, Clone)]
struct Point {
    int x;
    int y;
}

// 複数のderive属性はマージされる
#[derive(Eq)]
#[derive(Clone, Hash)]
struct Entry {
    int key;
    int value;
}

// withとの併用（リストはunionされる）
#[derive(Ord)]
struct Item with Eq {
    int priority;
}
```

## ジェネリック構造体

```cm
#[derive(Eq)]
struct Pair<T, U> {
    T first;
    U second;
}

int main() {
    Pair<int, string> p1;
    p1.first = 1;
    p1.second = "one";
    Pair<int, string> p2;
    p2.first = 1;
    p2.second = "one";

    if (p1 == p2) {
        println("Equal pairs!");
    }
    return 0;
}
```

ジェネリック構造体の自動実装はモノモーフィゼーション（単相化）後に型ごとに生成されます。

## フィールド型の対応範囲

| フィールド型 | Eq | Ord | Hash | Debug/Display | Clone/Copy |
|---|---|---|---|---|---|
| 整数・bool・char | ✅ | ✅ | ✅ | ✅ | ✅ |
| float/double | ✅ | ✅ | ❌ | ✅ | ✅ |
| string | ✅ | ✅ | ❌ | ✅ | ✅ |
| ネスト構造体 | ✅ | ✅ | ✅ | ✅ | ✅ |
| 固定長1次元配列 | ✅ | ❌ | ✅ 整数系要素のみ | ❌ | ✅ |
| 多次元配列・動的スライス・ユニオン型 | ❌ | ❌ | ❌ | ❌ | ✅ |

❌の組み合わせはコンパイルエラーになります（不正なコード生成は行いません）。

## エラーになる指定

```cm
#[derive(Foo)]        // エラー: 未知のインターフェース
#[derive(Greet)]      // エラー: 導出可能セット外（impl P for Greet を使う）
#[derive(Eq, Eq)]     // エラー: 重複指定
#[derive]             // エラー: インターフェース名が必要
struct P { int x; }

#[derive(Eq)]
enum Color { Red }    // エラー: enumへのderiveは未対応
```

## 実装の仕組み

`with` / `#[derive]` はパーサで同一の自動実装リストに合流し、MIRローワリング時に実装関数（`Point__op_eq` 等）が自動生成されます。
使用されない自動実装はデッドコード削除（DCE）で除去されるため、指定してもコストはかかりません。

## 関連ドキュメント

- [インターフェース](../types/interfaces.html) - interface/impl構文
- [演算子オーバーロード](../advanced/operators.html) - operator実装
- [正式言語仕様](../../../design/CANONICAL_SPEC.html) - 構文仕様
- [設計10: #[derive]属性](../../../design/v0.16.0/10_derive_attribute.html) - 設計文書

---

**最終更新:** 2026-07-11

---

<!-- nav -->
← 前: [高度な機能編 - match式](match.html) ｜ [目次](index.html) ｜ 次: [高度な機能編 - 演算子オーバーロード](operators.html) →
