---
title: 文字列補間
parent: Tutorials
---

[English](../../en/basics/string-interpolation.html)

# 基本編 - 文字列補間

Cm の文字列リテラルは `{}` による補間（interpolation）をサポートします。このページでは変数・式・関数呼び出しの埋め込みと、フォーマット指定子を解説します。

---

## 基本: 変数の埋め込み

```cm
import std::io::println;

int main() {
    string name = "Alice";
    int age = 25;
    println("Hello, {name}! You are {age} years old.");
    // → Hello, Alice! You are 25 years old.
    return 0;
}
```

## 式の埋め込み

メンバアクセスや配列要素も埋め込めます。

```cm
struct Point { int x; int y; }

int main() {
    Point p;
    p.x = 3;
    p.y = 4;
    int[3] arr = [10, 20, 30];
    println("p = ({p.x}, {p.y}), arr[1] = {arr[1]}");
    // → p = (3, 4), arr[1] = 20
    return 0;
}
```

## 関数呼び出しの埋め込み（v0.15.1で修正・拡張）

補間内で関数を呼び出せます。**変数引数・複数引数・負数リテラル**に対応しています。

```cm
int add3(int a, int b, int c) {
    return a + b + c;
}

int is_big(int status) {
    if (status >= 500) { return 1; }
    return 0;
}

int main() {
    int s = 503;
    int x = 10;
    println("check: {is_big(s)}");        // → check: 1（変数引数）
    println("sum: {add3(x, 20, -1)}");    // → sum: 29（複数引数・負数）
    return 0;
}
```

> **注意（既知の制限）**: `{f(g(x))}` のようなネストした呼び出しや、
> `{a + b}` のような二項演算式の埋め込みは未対応です。
> 一度ローカル変数に代入してから埋め込んでください。

## フォーマット指定子

`{変数:指定子}` の形式で基数などを指定できます。

```cm
int main() {
    int value = 255;
    println("hex: {value:x}");    // → hex: ff
    println("HEX: {value:X}");    // → HEX: FF
    println("bin: {value:b}");    // → bin: 11111111
    println("oct: {value:o}");    // → oct: 377
    double pi = 3.14159;
    println("pi: {pi:.2}");       // → pi: 3.14（小数点以下2桁）
    return 0;
}
```

## 中括弧のエスケープ

リテラルの `{` `}` を出力するには `{{` `}}` と書きます。

```cm
println("JSON: {{\"key\": {value}}}");
// value=42 のとき → JSON: {"key": 42}
```

## SVバックエンドでの補間

SystemVerilog ターゲットでは、文字列は packed ベクトル定数として扱われるため、`println` 系の補間はシミュレーション用 `initial` ブロック等の限定された文脈でのみ使用できます。詳細は [SVバックエンドの意味論保証](../compiler/sv/semantics.html) を参照してください。

---

<!-- nav -->
← 前: [モジュールシステム](modules.html) ｜ [目次](index.html) ｜ 次: [型システム編](../types/index.html) →
