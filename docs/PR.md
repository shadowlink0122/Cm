[English](PR.en.html)

# v0.10.0 Release - Cm言語コンパイラ

## 概要

v0.10.0からの主要な変更点をまとめました。このリリースでは**モジュールシステム**、**FFI**、**ラムダ式**、**動的スライス**、**no_std対応**、**MIR最適化の安定化**が完成しました。

## 主要な新機能

### モジュールシステム（完全実装）
- `import std::io::println;` - 階層的インポート
- `import ./path/*;` - ワイルドカードインポート
- `export { M };` - 再エクスポート
- `import foo as F;` - エイリアスインポート
- 選択的インポート `::{items}` 構文
- 循環依存検出

### FFI (Foreign Function Interface)
- `use libc { malloc, free, ... }` 構文
- `std::mem` モジュールによるメモリ管理
- LLVM/WASM両対応

### ラムダ式
- `(params) => { body }` 構文
- 型推論対応
- 関数ポインタへの代入可能

### 動的スライス型
- `T[]` スライス型
- `T[][]` 多次元スライス
- 高階関数（map/filter/some/every/find/reduce）

### no_std対応
- カスタムアロケータ抽象化
- 自前文字列/メモリ操作関数

## MIR最適化の安定化

### 実装済みパス
- 定数畳み込み (Constant Folding)
- デッドコード除去 (DCE)
- 定数伝播 (SCCP)
- コピー伝播
- 共通部分式除去 (CSE/GVN)
- デッドストア除去 (DSE)
- 制御フロー簡約化
- インライン化（回数制限付き）
- ループ不変式移動 (LICM)
- プログラムレベルDCE

### 無限ループ防止機構
- 反復回数制限（最大10回）
- 循環検出（ハッシュベース、履歴8個）
- タイムアウトガード（30秒/パス5秒）
- 振動検出

## バグ修正

### Ubuntu CI 対応
- **std::io モジュールパス解決**: 実行ファイル相対パスで標準ライブラリを検索
- **再帰的ワイルドカードインポート**: `std::filesystem::canonical()` でパス正規化
- **LLVM 18 文字列補間**: 配列型 `[n x i8]` に対する `add` 命令のエラー修正

### MIR/Codegen
- ジェネリックimplメソッドのself引数参照化
- `-O0` 時の構造体変更が反映されない問題を修正
- `fix_struct_method_self_args` パス追加

## テスト結果

| 環境 | 結果 |
|------|------|
| Mac (LLVM 14) | 250/262 passed, 0 failed |
| Ubuntu Docker (LLVM 18) | 250/262 passed, 0 failed |

## 既知の問題（スキップ中）

`advanced_modules/` - 高度なモジュール構文（10テスト）
- 詳細: `docs/issues/unimplemented_features.md`

## 破壊的変更

なし

## アップグレード手順

既存のCmコードは変更なしで動作します。

## 今後の予定

### v0.11.0 - 大規模リリース
- **イテレータ拡張**: `for ((index, item) in arr)`, 範囲イテレータ
- **JS改善**: ブラウザAPI統合、Debug/Display対応
- **所有権・借用システム**: `&` vs `&mut`、二重借用検出
- **File I/O**: ファイル読み書き、パス操作
- **std::io拡張**: stdin/stdout/stderr完全サポート
- **リファクタリング**: 型システム遅延評価、エラー報告統一

### v0.12.0
- Lint/Formatter (`cm lint`, `cm fmt`)
- constexpr（コンパイル時計算）
- クロージャ改善

### v0.13.0
- パッケージマネージャ (`cm pkg`)
- 依存関係管理

### v0.14.0+
- インラインアセンブリ
- ベアメタル拡張

