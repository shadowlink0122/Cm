# v0.16.0 実装設計 7: ツーリングUX — #構文ハイライトと条件ブロックのインデント

優先度: 追加（ユーザー要望 2026-07-10）
目的: `#` で始まる構文（ディレクティブ・属性）の視認性と、
条件付きコンパイルブロックの構造の読みやすさを改善する。

## 背景（指摘4点）

1. VSCode拡張で `#end` がハイライトされない（`#endif` は定義済みだがCmは `#end`）
2. `#` で始まる構文全般にハイライトが必要
3. `#ifdef` は良いが `#[input]` 等の属性にもハイライトが必要
4. `#ifdef` があるとインデントが分かりづらい。モダンな言語として
   ブロック内容にインデントを追加してはどうか

## 設計判断

### インデント（指摘4）: 採用

Cmの `#ifdef` はC系の行指向ディレクティブと異なり、`#end` で明示的に閉じる
**ブロック構文**である。C系の「ディレクティブはカラム0」という慣習に
従う理由はなく、ブロック内容を1段インデントする方が構造が読める。

規則: **ディレクティブ（`#ifdef`/`#ifndef`/`#else`/`#end`）は外側の
インデントレベル、ブロック内容は+1段**。ブレース・ブラケット・丸括弧の
深さと合算し、ネストにも対応する。`cm fmt` が機械的に正規化し、冪等。

```cm
void f(posedge clk) {
    #ifdef TEST
        int x = 1;
    #end
}
```

プリプロセッサ・パーサは行頭空白に依存しないため意味論への影響はない。

## 実装

- `fmt/formatter.cpp` `normalize_indentation`: `ifdef_depth` を追加。
  `#ifdef`/`#ifndef` の次行から+1、`#end` で-1、`#else` は
  出力時-1/次行+1（`} else {` と同じ扱い）
- VSCode `cm.tmLanguage.json`（`#` 構文の統一配色）:
  - 配色規則: **`#` = 白（デフォルト前景）、属性の `[` `]` = 黄、
    中身・ディレクティブ名 = ピンク**
    - ディレクティブ `#ifdef` 等: `#` → `punctuation.definition.directive`、
      名前 → `keyword.control.directive`（ピンク）。`end` を追加（指摘1）
    - 属性 `#[...]`: `#` → `punctuation.definition.attribute`、
      `[` `]` → `entity.name.function.attribute.bracket`（黄）、
      中身は一律 `keyword.control.attribute`（ピンク）（指摘3）
  - `#define`/`#undef`/`#error`/`#warning`/`#include` も同じ配色規則
  - 未知の `#ディレクティブ` のキャッチオール規則を追加（将来の構文）（指摘2）
- VSCode `language-configuration.json`:
  - indentationRules: `#ifdef`/`#ifndef`/`#else` で次行インデント増、
    `#end`/`#else` の行自体をデデント（エディタの自動インデント）
  - folding: `#ifdef`/`#ifndef` 〜 `#end` を折りたたみ可能に

## テスト計画

- `make format` で既存 .cm（tests/common/preprocessor）が新規則へ収束し、
  再実行で変更ゼロ（冪等）
- 既存スイート（JIT/LLVM/SV）が整形後も全PASS
- tmLanguage / language-configuration のJSON妥当性検証
