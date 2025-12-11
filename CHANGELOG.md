# Changelog

Cm言語コンパイラの変更履歴です。

## [0.5.0] - 2025-12-11

### 追加

#### Enum型
- **enum定義**: `enum NAME { MEMBER = value, ... }` 形式のenum定義
- **enum値アクセス**: `NAME::MEMBER` 形式でのenum値アクセス
- **負の値サポート**: `Error = -1` のような負の値をサポート
- **オートインクリメント**: 値を指定しない場合、0から開始または直前値+1
- **値重複チェック**: 同じ値を持つメンバがある場合コンパイルエラー
- **フィールド型としての使用**: 構造体フィールドにenum型を使用可能

#### Typedef型エイリアス
- **型エイリアス**: `typedef Integer = int;` 形式の型エイリアス
- **関数シグネチャ**: typedef型を関数の引数・戻り値として使用可能
- **リテラル型定義**: `typedef T = "a" | "b";` 形式（定義のみ、型チェック未実装）
- **ユニオン型定義**: `typedef T = T1 | T2;` 形式（定義のみ、型チェック未実装）

### 修正

#### LLVMバックエンド
- **構造体戻り値バグ修正**: 関数から構造体を返す際のポインタ型不一致を修正
- **switch文内リテラル修正**: switch文内の文字列リテラル型推論を修正
- **enum型フィールド修正**: 構造体フィールドのenum型をint型として正しく解決

#### MIR Lowering
- **リテラル型推論**: 型がnull/errorの場合、リテラル値から型を自動推論

## [0.4.0] - 2025-12-11

### 追加

#### privateメソッド
- **impl内でのprivate修飾子サポート**: メソッドに`private`修飾子を付けることで、そのメソッドを同じimplブロック内からのみ呼び出し可能に制限
- **アクセス制御の強制**: privateメソッドを外部から呼び出そうとすると型エラーを報告

#### defaultメンバの暗黙的アクセス
- **暗黙的な代入**: `w = 100`で構造体のdefaultメンバ（`w.value`）に代入
- **暗黙的な取得**: `int x = w`で構造体のdefaultメンバから値を取得
- **型互換性の拡張**: 構造体とそのdefaultメンバ型との互換性チェックを追加

#### コンストラクタ/デストラクタ
- **impl Type構文**: `impl Type { self() { ... } }`形式のコンストラクタ専用implブロック
- **self()コンストラクタ**: デフォルトコンストラクタの定義
- **overload self(...)**: 引数付きコンストラクタのオーバーロード
- **Type name(args)構文**: コンストラクタ呼び出し構文（`Point p(10, 20)`）
- **selfへの参照渡し**: コンストラクタ内での`self.x = a`が呼び出し元に反映
- **~self()デストラクタ**: デストラクタの定義（構文解析のみ）
- **selfキーワード**: コンストラクタ/デストラクタ/メソッド内でのインスタンス参照（ポインタ）
- **overloadキーワード**: 関数オーバーロード修飾子の追加

#### ドキュメント
- CANONICAL_SPEC.mdにprivateメソッドの仕様を追加

### 修正

#### パーサー
- `impl Type for Interface`構文の解析順序を修正（CANONICAL_SPECに準拠）

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
