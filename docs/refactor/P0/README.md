# リファクタリング課題一覧

最終更新: 2026-04-29

---

## 概要

リポジトリ全体を調査し、改善点を優先度別に整理しました。

---

## 統計

| カテゴリ | 件数 |
|---------|------|
| 高優先度課題 | 4件 |
| 中優先度課題 | 5件 |
| 低優先度課題 | 3件 |
| TODO/FIXMEコメント | 30+ |
| コンパイラ警告 | 3 |
| 巨大ファイル (2000行超) | 7 |

---

## 高優先度

| ファイル | 内容 |
|---------|------|
| [compiler-warnings-bitfield.md](compiler-warnings-bitfield.md) | C++20拡張のコンパイラ警告 |
| [unused-variables.md](unused-variables.md) | 未使用変数の削除 |
| [goto-refactoring.md](goto-refactoring.md) | goto文のリファクタリング |
| [sv-error-codes.md](sv-error-codes.md) | SVエラーコードの統一 |

---

## 中優先度

| ファイル | 内容 |
|---------|------|
| [large-file-splitting.md](large-file-splitting.md) | 巨大ファイルの分割 |
| [todo-cleanup.md](todo-cleanup.md) | TODO/FIXMEの整理 |
| [debug-output-cleanup.md](debug-output-cleanup.md) | デバッグ出力の統一 |
| [sv-test-coverage.md](sv-test-coverage.md) | SVテストカバレッジの拡充 |
| [error-handling.md](error-handling.md) | 例外処理の統一 |

---

## 低優先度

| ファイル | 内容 |
|---------|------|
| [sv-initial-implementation.md](sv-initial-implementation.md) | SV initial構文の実装 |
| [macro-repetition.md](macro-repetition.md) | マクロ繰り返しの実装 |
| [bit-literal-info-dedup.md](bit-literal-info-dedup.md) | BitLiteralInfoの共通化 |

---

## 本セッションで対応済み

以下の問題は本セッションで修正しました:

1. ✅ `!x` → `~x` のドキュメント修正
2. ✅ `{}` 空ブロックのパースエラー修正
3. ✅ `__builtin_concat` の型推論改善
4. ✅ `{N{expr}}` の count パース改善
5. ✅ `assign` の wire 宣言修正
6. ✅ `bit<N>` → `bit[N]` のドキュメント統一
7. ✅ `initial` 未実装の明記
8. ✅ SV機能対応表の更新
9. ✅ SV固有トークン一覧の更新

---

## 推奨アクション

### 即時対応

1. `types.hpp` のビットフィールド初期化をコンストラクタに移動
2. 未使用変数の削除
3. SVエラーコードの統一

### 次期リリース

1. 5000行超の `mir_to_llvm.cpp` を分割
2. TODOをGitHub Issues化
3. デバッグ出力を `debug::log()` に統一

### 将来バージョン

1. `initial` 構文の実装
2. マクロ繰り返しの完全実装
3. Windowsサポート

