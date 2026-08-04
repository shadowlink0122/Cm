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

**v0.16.0での拡張**: `{f(g(x))}` のようなネストした呼び出し、`{a + b}` や `{xs[1] * 10}` のような式、メソッド呼び出し（`{xs.some(fn)}` や `{o.unwrap_or(-1)}` 等のResult/Optionメソッドを含む）も埋め込めるようになりました。

```cm
int main() {
    Option<int> o = Option::Some(5);
    int a = 3;
    int b = 4;
    println("sum: {a + b}");             // → sum: 7
    println("value: {o.unwrap_or(-1)}"); // → value: 5
    return 0;
}
```

## スコープ検査（v0.16.0）

プレースホルダ内の変数参照は文中の式と同様にコンパイル時にスコープ検査されます。スコープ外・未定義の変数を参照するとコンパイルエラーになります（従来は検査されず、未定義値が出力されていました）。

```cm
int main() {
    {
        int b = 42;
        println("in: {b}");   // OK
    }
    println("out: {b}");      // エラー: Undefined variable 'b' in interpolation placeholder '{b}'
    return 0;
}
```

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

## 中括弧のエスケープと補間の併用

リテラルの `{` `}` を出力するには `{{` `}}` と書きます。
**v0.17.0でエスケープと補間の併用が全形態で動作するようになりました**。`{{ ... {変数} ... }}` のように、エスケープされた波括弧の内側にプレースホルダを埋め込めます。

```cm
import std::io::println;

int main() {
    int val = 42;
    string name = "cm";

    // エスケープの内側に補間を埋め込む
    println("{{text {val} ...}}");   // → {text 42 ...}
    println("{{{val}}}");            // → {42}（{{ + {val} + }}）

    // JSONテンプレート
    println("json: {{\"key\": {val}, \"name\": \"{name}\"}}");
    // → json: {"key": 42, "name": "cm"}

    // CSSテンプレート（通常の文字列リテラルでも補間・エスケープは同じ）
    string css = "css {{ width: {val}px; }}";
    println(css);                    // → css { width: 42px; }
    return 0;
}
```

**補間はすべての文字列リテラルで働きます（v0.17.0）**: `println` の引数に限らず、`string s = "sum: {x + 1}";` のような通常の文字列リテラルでもプレースホルダは評価されます。
リテラルの波括弧が必要な場合は文脈を問わず `{{` `}}` でエスケープしてください（`string braces = "{{x}}";` は文字列 `{x}` になります）。

**既知の制限**: 補間で挿入した値自体が `{` `}` を含む場合、同じ文字列内の後続プレースホルダが正しく処理されないことがあります（例: `{x}` という内容の変数を先に埋め込むと、それ以降のプレースホルダが誤認されます）。
波括弧を含む値を埋め込む場合は、`+` 連結で組み立てるのが安全です。

## SVバックエンドでの補間

SystemVerilog ターゲットでは、文字列は packed ベクトル定数として扱われるため、`println` 系の補間はシミュレーション用 `initial` ブロック等の限定された文脈でのみ使用できます。詳細は [SVバックエンドの意味論保証](../compiler/sv/semantics.html) を参照してください。

---

<!-- nav -->
← 前: [モジュールシステム](modules.html) ｜ [目次](index.html) ｜ 次: [型システム編](../types/index.html) →
