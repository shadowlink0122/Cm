# Changelog

Cm言語コンパイラの変更履歴です。

## [0.9.0] - 2024-12-14 (進行中)

### 追加

#### 配列サポート
- **C++スタイル配列宣言**: `int[5] arr;` 形式の配列型宣言
- **配列インデックスアクセス**: `arr[i]` での読み書き
- **構造体配列**: `Point[3] points;` のような構造体配列
- **インタプリタ配列**: MIRインタプリタでの完全な配列サポート
- **LLVMネイティブ配列**: プリミティブ型・構造体配列のLLVMコンパイル
- **WASM配列**: WebAssemblyでの配列サポート

#### ポインタサポート（完全実装）
- **C++スタイルポインタ宣言**: `int* p;` 形式のポインタ型
- **アドレス演算子**: `&x` で変数のアドレスを取得
- **デリファレンス**: `*p` でポインタの参照先にアクセス
- **ポインタ代入**: `*p = value;` でポインタ経由の書き込み
- **構造体ポインタ**: `Point* ptr = &p;` で構造体へのポインタ
- **LLVMネイティブ**: ポインタのネイティブコンパイル完全対応
- **WASMポインタ**: WebAssemblyでのポインタ完全対応

#### 配列→ポインタ暗黙変換（Array Decay）
- **暗黙変換**: `int* p = arr;` で配列から先頭要素ポインタへ変換
- **C/C++互換性**: 配列を関数にポインタとして渡せる

### 既知の問題
- typedef型ポインタのLLVM/WASMコンパイルは未完了（インタプリタでは動作）

### テスト追加
- `tests/test_programs/array/array_basic.cm`
- `tests/test_programs/array/array_struct.cm`
- `tests/test_programs/pointer/pointer_basic.cm`
- `tests/test_programs/pointer/pointer_struct.cm`
- `tests/test_programs/pointer/pointer_typedef.cm`
- `tests/test_programs/pointer/pointer_array_decay.cm`

## [0.8.0] - 2024-12-14

### 追加

#### match式
- **match式**: `match (expr) { pattern => body, ... }` 形式のパターンマッチング
- **リテラルパターン**: 数値、真偽値のマッチング
- **enum値パターン**: `Status::Ok => ...` 形式のenum値マッチング
- **ワイルドカードパターン**: `_ => default` 形式のデフォルトマッチ
- **パターンガード**: `pattern if condition => body` 形式の条件付きマッチ
- **変数束縛パターンガード**: `n if n > 0 => ...` 形式の変数束縛付きガード
- **網羅性チェック**: 整数型/bool型/enum型のmatch網羅性検証

#### 型制約チェック
- **型制約検証**: `<T: Ord>`などの型制約をコンパイル時にチェック
- **組み込みインターフェース**: `Ord`, `Eq`, `Clone`のプリミティブ型への自動実装
- **制約違反エラー**: 型が制約を満たさない場合に明確なエラーメッセージ

### 修正

#### v0.7.0の「既知の問題」
以下の問題は実際には解決済みでした：
- **impl<T>のLLVM関数署名**: 正常に動作確認
- **複数型パラメータとジェネリック関数**: `make_pair<T,U>`パターンが動作
- **Option<T>/Result<T,E>型**: ジェネリック構造体として実装可能

### 修正（追加）

#### 三項演算子の型処理
- **文字列型match式のLLVMコンパイル**: 三項演算子の結果変数が正しい型を使用するように修正
- これにより文字列を返すmatch式がLLVM/WASMでも正常に動作

### テスト追加
- `tests/test_programs/match/match_basic.cm`
- `tests/test_programs/match/match_enum.cm`
- `tests/test_programs/match/match_guard.cm`
- `tests/test_programs/match/match_enum_exhaustive.cm`
- `tests/test_programs/match/match_option.cm`
- `tests/test_programs/match/match_result.cm`
- `tests/test_programs/generics/impl_generics.cm`
- `tests/test_programs/generics/multiple_type_params.cm`
- `tests/test_programs/generics/option_type.cm`
- `tests/test_programs/generics/result_type.cm`
- `tests/test_programs/generics/generic_with_struct.cm`
- `tests/test_programs/generics/simple_pair.cm`
- `tests/test_programs/errors/constraint_error.cm`
- `tests/test_programs/errors/match_non_exhaustive.cm`
- `tests/test_programs/errors/match_enum_non_exhaustive.cm`

## [0.7.0] - 2024-12-14

### 追加

#### 高度なジェネリクス
- **impl<T>構文**: `impl<T> Container<T> for Interface` 形式のジェネリックimpl
- **インターフェース型引数**: `impl<T> Type<T> for Interface<T>` 形式のジェネリックインターフェース実装
- **ジェネリック構造体メソッドの解決**: `Container<int>`から`Container<T>`のメソッドを自動検索
- **ジェネリック関数戻り値型の置換**: `Option<T>` → `Option<int>` の自動変換
- **複数型パラメータ**: `struct Pair<T, U>`構文のサポート（構造体）
- **型マングリング**: `Container<int>__print` → `Container__int__print`の正規化

#### WASMバックエンド
- **WASMコンパイル**: LLVMを経由したWebAssemblyへのコンパイル
- **WASMランタイム**: println, 文字列フォーマットなどの基本機能
- **wasm-ldリンク**: WASIサポートによる実行可能なWASMバイナリ生成
- **ジェネリクス対応**: WASMでもジェネリクスが動作

### 修正

#### ジェネリクス
- **モノモーフィゼーション**: `Container<T>__method` → `Container__int__method` の正しい特殊化
- **関数名マングリング**: 一貫性のある関数名生成
- **呼び出し箇所書き換え**: MIR内の関数呼び出しを自動的に特殊化関数に変更
- **型推論**: ジェネリック構造体を返す関数の戻り値型を正しく推論
- **メソッド解決**: ジェネリック型のメソッド呼び出しを型パラメータで正しく解決
- **複数型パラメータ置換**: `Pair__T__U` → `Pair__int__string` の正しい変換

### 既知の問題

#### v0.8.0で修正予定
- impl<T>メソッドのLLVMコンパイル時に関数パラメータ型の置換が不完全
- 複数型パラメータとジェネリック関数の組み合わせでGEPエラー
- 型制約チェック（`<T: Ord>`）の実行時検証が未実装

### 備考
- 単一型パラメータのジェネリクスは完全動作
- WASMバックエンドで基本的なプログラムとジェネリクスが動作
- インタプリタでは全てのジェネリクス機能が動作

## [0.6.0] - 2024-12-14

### 追加

#### ジェネリクス基本機能
- **ジェネリック関数**: `<T> T identity(T value)` 形式のジェネリック関数定義
- **ジェネリック構造体**: `struct Box<T> { T value; }` 形式のジェネリック構造体
- **型推論**: 呼び出し時に引数から型パラメータを推論
- **モノモーフィゼーション**: ジェネリック関数/構造体の具体的な型への特殊化

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
