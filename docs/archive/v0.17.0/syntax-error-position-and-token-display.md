---
title: 構文エラーの行番号がimport展開後の位置を指す（X5）
parent: v0.17.0 Design
---

# 構文エラーの行番号がimport展開後の位置を指す（X5）

## 概要

構文（パース）エラーの行番号が、importを含むファイルでユーザーソースの行ではなくimport展開後のマージ済みバッファの行を指す。
`import std::io::println;`と`import std::collections::vector;`の2行があるだけで、実際4行目のエラーが633行目と報告される（表示されるソース断片・カレット位置は正しいため、行番号だけが桁違いにずれる）。
型検査エラー（use-after-move等）は正しい行番号を報告しており、構文エラーの報告経路だけが展開後座標のままになっている。
併発問題として、予約語を識別子位置に使った場合のメッセージが`Expected identifier, got ''`とトークン字句が空表示になり、原因（例: `pub`が予約語であること）が読み取れない。

## 再現コード

```cm
import std::io::println;
import std::collections::vector;
int main() {
    int pub = 1;
    return 0;
}
```

```text
実際の出力:   min.cm:633:9: error: Expected identifier, got ''
期待:         min.cm:4:9: error: Expected identifier, got 'pub'（予約語）
```

importなしの同内容ファイルでは`2:9`と正しい行番号になるため、ずれ幅はimport展開で挿入された行数に等しい。

## 原因

プリプロセス/import解決がモジュールソースをユーザーソースへ前置インライン展開し、パーサのトークン位置が展開後バッファの絶対行のまま診断に渡っている。
型検査系診断は元ファイルへの位置写像（もしくは展開前パース）を持つため正しく、構文エラーだけ写像が未適用。
`got ''`は、キーワードトークンのkindが識別子系でないため字句取り出しが空文字になる表示ロジックの欠陥で、予約語の誤用時にユーザーへ手掛かりが渡らない。

## 修正方針

1. import展開時に行オフセット対応表（展開区間→元ファイル/元行）を保持し、構文エラー報告時に元ソース座標へ写像する（型検査診断と同じ座標系に統一）。
2. 診断表示でトークン字句が空の場合はトークンkindの表記（`got 'pub'`等）へフォールバックし、予約語の場合は「'pub'は予約語です」を付記する。
3. 展開起因のずれを検出する回帰として、import複数行+構文エラーのケースで報告行番号を厳密検証する。

## テスト計画

- unit（diagnostics）: import 0/1/2個のファイルでの構文エラー行番号一致、予約語誤用時のメッセージ内容。
- integration: errorsスイートに行番号を含む期待エラー出力のケースを追加する。

## 検出経緯

native/jit網羅検証第3ラウンドで検出。最小再現は `.tmp/nativejit-bughunt3/min_lineno_imports.cm` / `min_lineno_noimport.cm`。

## 解決記録（実装済み）

行番号: 型検査診断と同じsource_map写像（format_error_with_source_map）を構文エラー報告経路（build.cppとcheck.cpp）へ適用し、import展開後バッファでなく元ソースの座標で報告するようにした（633:9→4:9）。
トークン表示: expect_identの診断でトークン字句が空（キーワード等）の場合はtoken_kind_to_stringの表記へフォールバックし、予約語は「got reserved word 'pub'」と明記するようにした。
回帰テスト: errorsスイートの既存機構は行番号を照合しないため、修正の再現確認はimport 2本+構文エラーの手動検証で実施した（unitテスト化は診断基盤の将来課題）。
