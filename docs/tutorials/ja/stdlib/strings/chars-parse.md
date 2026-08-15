---
title: 文字分類と数値解析
---

[English](../../../en/stdlib/strings/chars-parse.html)

# std::strings::chars / parse - 文字分類と数値解析

字句解析やテキスト処理の基礎部品として、文字1つの分類・変換（`chars`）と文字列から数値への解析（`parse`）を提供します（v0.17.2で追加。parse系はstd::ioから移設）。

> **対応バックエンド:** charsは全バックエンド（`digit_value`のみSV除く）、parseはSV以外（`Option`返しAPIのため）

---

## std::strings::chars - 文字分類・変換

```cm
import std::io::println;
import std::strings::chars::*;

int main() {
    println("{is_digit('7')}");        // true
    println("{is_alpha('a')}");        // true
    println("{is_alnum('_')}");        // false
    println("{is_space('\t')}");       // true
    println("{is_hex_digit('F')}");    // true

    // 識別子の字種判定（字句解析向け）
    println("{is_ident_start('_')}");     // true
    println("{is_ident_continue('9')}");  // true

    // 大文字・小文字変換（ASCII）
    println("{to_upper('a')}");        // A
    println("{to_lower('Z')}");        // z

    // 基数付きの数字値（失敗はNone）
    int v = digit_value('f', 16).unwrap_or(-1);
    println("{v}");                    // 15
    return 0;
}
```

| 関数 | 戻り値 | 説明 |
|------|--------|------|
| `is_digit(c)` / `is_alpha(c)` / `is_alnum(c)` | `bool` | 数字 / 英字 / 英数字か |
| `is_space(c)` | `bool` | 空白文字か（スペース・タブ・改行・CR） |
| `is_upper(c)` / `is_lower(c)` | `bool` | 大文字 / 小文字か |
| `is_hex_digit(c)` | `bool` | 16進数字か（0-9 / a-f / A-F） |
| `is_ident_start(c)` / `is_ident_continue(c)` | `bool` | 識別子の先頭 / 継続文字か（英字・`_` / 英数字・`_`） |
| `to_upper(c)` / `to_lower(c)` | `char` | ASCII大文字化 / 小文字化（対象外はそのまま） |
| `digit_value(c, base)` | `Option<int>` | 基数base（2〜36）での数字値（範囲外は`None`。SV除く） |

---

## std::strings::parse - 数値解析

失敗を `Option` で返す文字列→数値の解析関数です。
v0.17.2で `std::io` から移設されました（`import std::io::*;` 経由の従来コードは再エクスポートで引き続き動作します）。

```cm
import std::io::println;
import std::strings::parse::*;

int main() {
    int a = parse_int("-123").unwrap_or(0);          // -123
    long b = parse_long("9000000000").unwrap_or(0 as long);
    double c = parse_double("3.14").unwrap_or(0.0);
    bool d = parse_bool("yes").unwrap_or(false);     // true

    // 基数指定（2〜36。v0.17.2で追加）
    int hex = parse_int_radix("ff", 16).unwrap_or(-1);    // 255
    int bin = parse_int_radix("1010", 2).unwrap_or(-1);   // 10
    long big = parse_long_radix("ffffffff", 16).unwrap_or(-1 as long);

    // 失敗はNone
    bool bad = parse_int("abc").is_none();           // true
    println("{a} {hex} {bin} {bad}");
    return 0;
}
```

| 関数 | 戻り値 | 説明 |
|------|--------|------|
| `parse_int(s)` / `parse_long(s)` | `Option<int>` / `Option<long>` | 10進整数の解析（先頭の`-`対応・数字以外で停止） |
| `parse_int_radix(s, base)` / `parse_long_radix(s, base)` | `Option<int>` / `Option<long>` | 基数base（2〜36）での整数解析（v0.17.2） |
| `parse_double(s)` | `Option<double>` | 小数の解析 |
| `parse_bool(s)` | `Option<bool>` | `true/1/yes` → `Some(true)`、`false/0/no` → `Some(false)`、他は`None` |

**注意:** ライブラリ内部から `chars` / `parse` を利用する場合はワイルドカードimport（`import std::strings::parse::*;`）を使ってください（選択importは本体の即時型検査により、ユーザ定義`Option`を持つプログラムへ型衝突を波及させます）。

---

**関連:** [StringBuilder](builder.html) · [文字列の長さ](length.html)

---

[目次](../index.html)
