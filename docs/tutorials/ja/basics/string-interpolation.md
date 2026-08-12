---
title: 文字列補間
parent: Tutorials
---
{% raw %}

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

プレースホルダには任意の式を書けます（v0.17.0）。数値・配列リテラル・文字列リテラルで始まる式や三項演算子も、文中の式と同じ文法で評価されます。

```cm
int main() {
    bool ok = true;
    println("{2 + 3}");            // → 5（数値始まりの式）
    println("{[1, 2, 3].len()}");  // → 3（配列リテラル始まり）
    println("{ok ? 1 : 0}");       // → 1（三項演算子。: は書式指定子と誤認されない）
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

## 整形できる型（v0.17.0）

プレースホルダとprint/printlnの直接引数に書けるのは、単一の値として整形できる型（数値・bool・char・string・enum値・ユニオン）です。集約型（配列・スライス・構造体）を直接渡すとコンパイルエラーになります（従来はバックエンドごとに空・ゴミ・`[object Object]` 等へ分裂していました）:

```cm
#[derive(Debug)]
struct P { int x; int y; }

int main() {
    int[3] a = [1, 2, 3];
    // println("{a}");        // エラー: cannot format a value of type 'int[3]' ...
    println("{a[0]}, len={a.len()}");   // OK: 要素・スカラーは整形できる

    P p = P{x: 1, y: 2};
    // println(p);            // エラー
    println("{p.debug()}");   // OK: P { x: 1, y: 2 }（derive(Debug)の文字列化）
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

幅・整列・ゼロ埋め・科学記法にも対応しています（v0.17.0で全バックエンドの出力を統一。C/printf互換の仕様です）:

```cm
int main() {
    int n = 255;
    double pi = 3.14159265;
    println("[{n:6}]");      // → [   255]（幅6・数値は右詰めが既定）
    println("[{n:<6}]");     // → [255   ]（左詰め）
    println("[{n:^6}]");     // → [ 255  ]（中央）
    println("[{n:06}]");     // → [000255]（ゼロ埋め。負数は-00042のように符号を先頭に保つ）
    println("[{n:*>6}]");    // → [***255]（フィル文字指定）
    println("[{n:8x}]");     // → [      ff]（幅+基数の複合）
    println("{pi:.2e}");     // → 3.14e+00（科学記法・精度2桁・指数は2桁ゼロ埋め）
    println("{pi:e}");       // → 3.141593e+00（既定精度は6桁）
    return 0;
}
```

## 中括弧のエスケープと補間の併用

リテラルの `{` `}` を出力するには `{{` `}}` と書きます。
式として解釈できないプレースホルダ（`{x +}` 等のタイポ）はリテラル文字のまま出力され、コンパイル時に警告（`--strict`ではエラー）が出ます（v0.17.0。従来は無診断で未初期化値が出力されていました）。
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
リテラルの波括弧が必要な場合は文脈を問わず `{{` `}}` でエスケープしてください（`string braces = "{{x}}";` は文字列 `{x}` になります）。`\{` `\}` も同じ意味のエスケープです。
`${x}` 形式のプレースホルダをリテラルとして出力したい場合は `\$` で先頭の `$` をエスケープします（`"\${x}"` は文字列 `${x}` になります）。

**既知の制限**: 補間で挿入した値自体が `{` `}` を含む場合、同じ文字列内の後続プレースホルダが正しく処理されないことがあります（例: `{x}` という内容の変数を先に埋め込むと、それ以降のプレースホルダが誤認されます）。
波括弧を含む値を埋め込む場合は、`+` 連結で組み立てるのが安全です。

## raw文字列（バッククォート）と複数行

バッククォート`` ` ``で囲むraw文字列は、エスケープシーケンスを解釈せず（`\n`はバックスラッシュとnの2文字）、改行を含む複数行テキストをそのまま書けます。唯一の例外はデリミタ自体の埋め込み``\` ``です。波括弧`{}`は常にリテラルで、補間は`${式}`のみが有効です:

```cm
int twice(int x) { return x * 2; }

int main() {
    int v = 21;
    const string s = `result=${twice(v)}
        indented line
    literal braces: {v}  quote: "q"  backslash: \n`;
    println(s);
    return 0;
}
```

複数行のraw文字列は、**リテラル開始行のインデント幅が各継続行から剥がされます**（Pythonのdedentと同様に、コード位置の字下げが文字列内容へ漏れません）。開始行より深いインデントは相対的に保持されるため、全行へ一様に付けた意図的な先頭空白は残ります:

```cm
int main() {
    const string items = `items:
      - a
      - b`;
    // → "items:\n  - a\n  - b"（コード字下げ4は剥がれ、意図した2スペースは保持）
    return 0;
}
```

フォーマッタ（`cm fmt`）はバッククォート内の行を一切変更しません（行頭空白も文字列内容の一部です）。

## SVバックエンドでの補間

SystemVerilog ターゲットでは、文字列は packed ベクトル定数として扱われるため、`println` 系の補間はシミュレーション用 `initial` ブロック等の限定された文脈でのみ使用できます。詳細は [SVバックエンドの意味論保証](../compiler/sv/semantics.html) を参照してください。

---

<!-- nav -->
← 前: [モジュールシステム](modules.html) ｜ [目次](index.html) ｜ 次: [型システム編](../types/index.html) →
{% endraw %}
