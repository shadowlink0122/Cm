# Changelog

Cm言語コンパイラの変更履歴です。

## [Unreleased]

### 追加

#### LLVMバックエンド
- **フォーマット指定子のサポート**: Rustスタイルのフォーマット文字列で以下の指定子をサポート
  - 整数: `{:x}`, `{:X}`, `{:b}`, `{:o}` (16進/2進/8進)
  - 浮動小数点: `{:.N}`, `{:e}`, `{:E}` (精度指定/指数表記)
  - アライメント: `{:<N}`, `{:>N}`, `{:^N}` (左/右/中央揃え)
  - ゼロ埋め: `{:0>N}`
- **符号なし整数のサポート**: `utiny`, `ushort`, `uint`型の正しい出力
- **エスケープブレース**: `{{` → `{`, `}}` → `}` の変換
- **引数なしprintln**: `println()` で空行を出力
- **複数引数println**: `println(1, 2, 3)` で引数を連結して出力

#### ランタイムライブラリ
- `cm_print_uint`, `cm_println_uint`: 符号なし整数出力関数
- `cm_format_uint`: 符号なし整数を文字列に変換
- `cm_format_double_precision`: 指定精度で浮動小数点数を変換
- `cm_format_double_exp`, `cm_format_double_EXP`: 指数表記変換
- `cm_format_replace_int`: フォーマット指定子付き整数置換
- `cm_format_replace_uint`: フォーマット指定子付き符号なし整数置換
- `cm_format_replace_double`: フォーマット指定子付き浮動小数点数置換
- `cm_format_replace_string`: フォーマット指定子付き文字列置換
- `cm_unescape_braces`: エスケープブレース処理

#### 標準ライブラリ
- `std/io.cm`: FFI (Foreign Function Interface) を使用したCランタイム関数の直接呼び出し

### 修正

#### LLVMバックエンド
- **型変換の修正**: `store`操作での整数型間変換（i32→i8/i16）と浮動小数点型間変換（double→float）を修正
- **符号付き/符号なし整数の正しい処理**: HIR型情報を使用してSExt/ZExtを適切に使い分け
- **フォーマット文字列の処理改善**: 型に応じた適切なフォーマット関数を呼び出すように修正

#### 最適化
- **遅延宣言（オンデマンド宣言）の実装**: 未使用のintrinsic宣言を削除
  - 改善前: sqrt, sin, cos, tan, log, exp, pow, abs等30以上の宣言
  - 改善後: 実際に使用される関数のみ宣言
- **LLVMの最適化パス**: 定数畳み込み、デッドコード削除、tail call最適化が正しく動作

#### テストランナー
- LLVMバックエンドでコンパイルエラーの出力をキャプチャするように修正

### ドキュメント

- `docs/LLVM_RUNTIME_LIBRARY.md`: ランタイムライブラリの完全なドキュメントを追加
- `docs/LLVM_OPTIMIZATION.md`: 最適化パイプラインのドキュメントを追加
- `docs/llvm_backend_implementation.md`: 実装状況とランタイムライブラリの情報を更新
- `docs/STRING_INTERPOLATION_LLVM.md`: フォーマット指定子のサポート状況を更新

### 削除

- `src/codegen/llvm_unified_codegen.hpp`: 未使用のファイルを削除
- `src/codegen/llvm_platform.hpp`: 未使用のファイルを削除
- `std/io_ffi.cm`: `std/io.cm`に統合されたため削除
- ルートディレクトリのテスト実行ファイル: `build/bin/`に移動

### ビルドシステム

- テスト実行ファイルの出力先を`build/bin/`に変更
- `.gitignore`に`Testing/`ディレクトリを追加

### テスト

- 全39のLLVMテストがパス
  - basic: 13テスト
  - control_flow: 9テスト
  - errors: 3テスト
  - formatting: 3テスト
  - types: 8テスト
  - functions: 3テスト

---

## 形式

このChangelogは [Keep a Changelog](https://keepachangelog.com/ja/1.0.0/) に基づいています。
