---
title: 設定ファイル
parent: Compiler
nav_order: 3
---

# 設定ファイル（.cmconfig.yml）

`.cmconfig.yml` はプロジェクト単位のコンパイラ設定ファイルです。カレントディレクトリから親ディレクトリへ向かって探索され、最初に見つかったものが使われます。

## 設定項目の全体像

```yaml
# .cmconfig.yml
language: en          # メッセージ言語（en | ja、省略時はen）

compile:
  optimization: 2     # -O未指定時の既定最適化レベル（0-3）
  target: native      # --target未指定時の既定ターゲット（native/sv/js等）
  sanitize: bounds    # --sanitize未指定時の既定サニタイザ（カンマ区切り。[bounds, undefined] のリスト表記も可）

lint:
  preset: recommended # ルールプリセット（minimal | recommended | strict）
  rules:
    W001: disabled    # ルール単位のレベル上書き（error | warning | hint | disabled）
  exclude:
    - tests/fixtures/ # cm check/lint/fmt -r のディレクトリ走査から除外するパス
```

## メッセージ言語（language）

コンパイラの全メッセージ（help・進捗・診断の枠組み文言）はデフォルトで英語です。日本語に切り替えるには次のいずれかを指定します（上にあるものが優先）:

1. CLIオプション: `cm check --lang=ja main.cm`
2. 環境変数: `CM_LANG=ja cm check main.cm`
3. `.cmconfig.yml`: `language: ja`

```console
$ cm check bad.cm
=== Check complete ===
files: 1/1
errors: 1, warnings: 0

$ cm check bad.cm --lang=ja
=== チェック完了 ===
ファイル数: 1/1
エラー: 1, 警告: 0
```

## コンパイル既定値（compile）

CLIオプションを毎回指定しなくても、プロジェクトの既定値をconfigで固定できます。CLIで明示した値（`-O<n>` / `--target=`）は常にconfigより優先されます。

```yaml
compile:
  optimization: 0   # デバッグ重視のプロジェクトでは既定を-O0に
  target: sv        # FPGAプロジェクトでは既定ターゲットをSVに
```

- `optimization` は0-3の整数のみ有効です。不正な値は無視され、組み込みデフォルト（-O3）が使われます
- `target` は `--target=` と同じ値（native/wasm/js/web/sv/uefi等）を指定します
- `sanitize` は `--sanitize=` と同じ値（address/thread/memory/bounds/undefined）をカンマ区切りで指定します。不正な値は警告して無視されます。詳細は[サニタイザ](sanitizer.html)を参照してください

## lintルール設定（lint）

- `preset` でルールセットの既定レベルをまとめて切り替えられます（`minimal`=全無効 / `recommended`=warning / `strict`=error）
- `rules` で個別ルールを上書きできます（`L001: disabled` 等）。行単位の抑止はソース内コメント `// @cm-disable-next-line L001` を使います
- `exclude` に列挙したパスは `cm check/lint/fmt -r` のディレクトリ走査から除外されます。明示的にファイルを指定した場合は除外の影響を受けません

除外設定の詳細は [Linter](linter.html) を参照してください。

---

<!-- nav -->
← 前: [Formatter (cm fmt)](formatter.html) ｜ [目次](../index.html) ｜ 次: [MIR最適化パス](optimization.html) →
