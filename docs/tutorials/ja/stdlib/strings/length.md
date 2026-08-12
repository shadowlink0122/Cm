---
title: 文字列の長さ（len / byte_len）
---

# 文字列の長さ - len() と byte_len()

Cmの文字列はUTF-8で表現されます。
`len()` は**コードポイント数**（文字数）、`byte_len()` は**UTF-8バイト数**を返します。

> **対応バックエンド:** JIT / Native / WASM / JS / TS（SVは文字列長が静的解決のため対象外）

---

## 基本

```cm
import std::io::println;

int main() {
    string ascii = "hello";
    println(ascii.len());       // 5
    println(ascii.byte_len());  // 5（ASCIIでは一致）

    string ja = "こんにちは";
    println(ja.len());          // 5（コードポイント数）
    println(ja.byte_len());     // 15（3バイト×5文字）

    string emoji = "😀🚀";
    println(emoji.len());       // 2
    println(emoji.byte_len());  // 8（4バイト×2文字）
    return 0;
}
```

## 注意事項

- v0.17.0で `len()` の意味がバイト数からコードポイント数へ変わりました。ASCIIのみの文字列は影響を受けません。バイト数が必要な場合は `byte_len()` を使ってください
- `substring()` / `slice()` の添字はコードポイント単位です（負添字は末尾からの位置）。`codepoint_at(i)` はコードポイント添字 `i` のUnicodeスカラ値を `uint` で返します（範囲外は0）
- `chars()` はコードポイント列を `uint[]` スライスで返し、`for (cp in s.chars())` で列挙できます
- `indexOf()` の戻り値はコードポイント添字です（`"あいうえお".indexOf("うえ")` は 2、未検出は -1）
- `charAt(i)` / `at(i)` は**コードポイント添字**の要素アクセスです（`len()` と同単位。v0.17.0でバイト単位から変更）。戻り型 `char` は1バイトのためASCIIのみ値を返し、非ASCIIコードポイントと範囲外は `'\0'` になります。非ASCIIの値が必要な場合は `codepoint_at(i)` を使ってください
- 生バイトへのアクセスは `byte_at(i)` を使います（`byte_len()` と対のバイト系API。バイト添字の生バイト値0..255を `int` で返し、範囲外は0。v0.17.0で追加）
- JSバックエンドではサロゲートペア（絵文字等）も1コードポイントとして数えます
- バイト列から文字列を構築するには `std::strings::from_bytes(utiny[])` を使います（埋め込みNUL(0x00)を含むバイト列も保持されます。v0.17.0）
