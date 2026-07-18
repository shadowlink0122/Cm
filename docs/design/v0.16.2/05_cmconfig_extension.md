# 実装設計: .cmconfig.yml の言語設定・コンパイル設定拡張

## 背景・課題

`.cmconfig.yml` は現在lint設定（ルールレベル・除外パス）専用であり、言語やコンパイル既定値を設定できない。
メッセージi18n（設計04）の言語選択と、プロジェクト単位の最適化レベル等の既定値を設定ファイルで指定できるようにする。

## 設計

### 設定スキーマ

```yaml
# .cmconfig.yml
language: en          # メッセージ言語（en | ja、省略時はen）

compile:
  optimization: 2     # 既定の最適化レベル（0-3、省略時は既存デフォルト）
  target: native      # 既定のコンパイルターゲット（native/sv/js等、省略時はnative）

lint:
  exclude:
    - tests/linter/fixtures/
```

### 優先順位

- すべての項目で「CLIオプション > 環境変数（言語のみ） > .cmconfig.yml > 組み込みデフォルト」とする
- `-O<n>` / `--target=` が明示された場合はconfigの値を上書きする

### 実装方針

- 既存の `lint::ConfigLoader`（`src/lint/config.{hpp,cpp}`）を汎用の `cm::config` として拡張し、`language` / `compile.optimization` / `compile.target` を読み取る
- 探索は現状どおりカレントディレクトリから親方向へ `.cmconfig.yml` を探す
- 不正値（`optimization: 9` 等）は警告を出して無視し、デフォルトへフォールバックする
- CLI側（`src/cli/options.cpp`）は「明示指定されたか」を保持し、未指定時のみconfig値を適用する

## 段階分割

1. ConfigLoaderの汎用化（language / compile セクションのパース）
2. main.cpp / options.cpp への適用（優先順位の実装）
3. i18n言語決定（設計04）との接続

## テスト計画

- unit: configパーサの新項目（正常値・不正値・未設定）
- integration: `language: ja` 設定時のメッセージ言語切替、`compile.optimization` 設定時に `-O` 未指定コンパイルへ反映されること、CLI明示指定が優先されること

## 互換性

- 既存の `.cmconfig.yml`（lintのみ）はそのまま動作する（新項目は任意）
