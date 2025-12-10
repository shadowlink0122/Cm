# LLVM バックエンド実装ガイド

## 概要
CmコンパイラにLLVMバックエンドを実装し、ネイティブコード生成を可能にしました。
これにより、インタプリタと同じ結果を生成しつつ、高速な実行が可能になります。

## 実装状況

### ✅ 実装済み機能

1. **基本構文**
   - 変数宣言（int, uint, tiny, utiny, short, ushort, float, double, bool, char, string）
   - 算術演算（+, -, *, /, %）
   - 比較演算（==, !=, <, <=, >, >=）
   - 論理演算（&&, ||, !）
   - 代入演算

2. **制御構造**
   - if/else文
   - for文
   - while文
   - switch文
   - return文

3. **関数**
   - 関数定義
   - 関数呼び出し
   - パラメータと戻り値
   - 再帰呼び出し

4. **出力**
   - println/print関数（文字列、整数、浮動小数点数、ブール値）
   - Rustスタイルのフォーマット文字列（`{}` プレースホルダー）
   - フォーマット指定子（`:x`, `:X`, `:b`, `:o`, `:.2`, `:e`, `:E`, `:<`, `:>`, `:^`, `:0>`）

5. **型システム**
   - 符号付き整数型（tiny, short, int）
   - 符号なし整数型（utiny, ushort, uint）
   - 浮動小数点型（float, double）
   - ブール型（bool）
   - 文字型（char）
   - 文字列型（string）
   - 適切な型変換（SExt/ZExt、FPTrunc/FPExt）

6. **最適化**
   - -O0: 最適化なし
   - -O1: 基本最適化
   - -O2: 標準最適化
   - -O3: アグレッシブ最適化

### ❌ 未実装機能

1. **型システム**
   - 構造体
   - 配列（完全サポート）
   - ポインタ操作

2. **高度な機能**
   - ジェネリクス
   - トレイト/インターフェース
   - モジュールシステム
   - 例外処理

## ビルド方法

```bash
# LLVMをインストール
brew install llvm@17  # macOS
apt install llvm-17   # Linux

# Cmコンパイラをビルド（LLVM有効）
mkdir build
cd build
cmake .. -DCM_USE_LLVM=ON
make

# または簡単に
./build_and_test_llvm.sh
```

## 使用方法

### 基本的な使い方

```bash
# ネイティブ実行ファイル生成
./cm compile program.cm --backend=llvm --target=native -o program
./program

# デバッグ情報付き
./cm compile program.cm --backend=llvm --target=native -o program --debug

# 最適化レベル指定
./cm compile program.cm --backend=llvm --target=native -o program -O3

# LLVM IR出力（デバッグ用）
./cm compile program.cm --backend=llvm --emit=llvm-ir -o program.ll
```

### テストの実行

```bash
# 単一ファイルのテスト
make test-llvm

# すべてのLLVMテスト
make test-llvm-all
```

## アーキテクチャ

```
Cm Source → Lexer → Parser → AST → HIR → MIR → LLVM IR → Native Code
                                                     ↑
                                               LLVMバックエンド
```

### ファイル構成

```
src/codegen/llvm/
├── context.hpp/cpp      # LLVMコンテキスト管理
├── mir_to_llvm.hpp/cpp  # MIR→LLVM IR変換
├── llvm_codegen.hpp/cpp # メインパイプライン
├── target.hpp/cpp       # ターゲット設定
├── intrinsics.hpp/cpp   # 組み込み関数
└── runtime.c            # Cランタイムライブラリ

tests/llvm/
├── test_basic.cm        # 基本テスト
├── basic/               # 基本構文テスト
├── baremetal/           # ベアメタルテスト
└── wasm/                # WebAssemblyテスト

tests/test_programs/
├── basic/               # 基本テスト
├── control_flow/        # 制御フローテスト
├── errors/              # エラーケーステスト
├── formatting/          # フォーマット文字列テスト
├── types/               # 型テスト
└── functions/           # 関数テスト
```

## ランタイムライブラリ

LLVMバックエンドはCランタイムライブラリ（`runtime.c`）を使用して文字列処理とフォーマット出力を実現します。

### 主要な関数

- **出力関数**: `cm_print_string`, `cm_println_string`, `cm_print_int`, `cm_println_int`, `cm_print_uint`, `cm_println_uint`, `cm_print_double`, `cm_println_double`, `cm_print_bool`
- **フォーマット関数**: `cm_format_int`, `cm_format_uint`, `cm_format_double`, `cm_format_bool`, `cm_format_char`
- **基数変換**: `cm_format_int_hex`, `cm_format_int_HEX`, `cm_format_int_binary`, `cm_format_int_octal`
- **フォーマット置換**: `cm_format_replace_int`, `cm_format_replace_uint`, `cm_format_replace_double`, `cm_format_replace_string`
- **文字列操作**: `cm_string_concat`, `cm_unescape_braces`

詳細は [LLVM ランタイムライブラリ](./LLVM_RUNTIME_LIBRARY.md) を参照してください。

## デバッグ

### デバッグ出力の有効化

```bash
# デバッグモードでコンパイル
./cm compile test.cm --backend=llvm --debug

# 詳細レベル指定
./cm compile test.cm --backend=llvm -d=trace
```

### デバッグメッセージ

```
[CODEGEN] LLVM生成を開始
[CODEGEN] LLVMを初期化: test
[CODEGEN] ターゲットを設定: x86_64-apple-darwin
[CODEGEN] MIRをLLVMに変換
[CODEGEN] 関数を処理: main
[CODEGEN] モジュールを検証
[CODEGEN] 検証合格
[CODEGEN] 最適化: Level 2
[CODEGEN] コードを出力: a.out
[CODEGEN] LLVM生成を完了
```

## トラブルシューティング

### LLVMが見つからない

```bash
# LLVMのパスを明示的に指定
cmake .. -DCM_USE_LLVM=ON -DLLVM_DIR=/usr/local/opt/llvm/lib/cmake/llvm
```

### リンクエラー

```bash
# clangが見つからない場合
export PATH="/usr/local/opt/llvm/bin:$PATH"
```

### 実行時エラー

```bash
# LLVM IRを確認
./cm compile test.cm --backend=llvm --emit=llvm-ir -o test.ll
cat test.ll

# clangで直接コンパイル
clang test.ll -o test
```

## 今後の改善点

1. **完全な型システムサポート**
   - 構造体、配列、ポインタの完全実装

2. **最適化の改善**
   - PassBuilderを使用した最新の最適化パイプライン
   - プロファイルガイド最適化（PGO）

3. **エラーハンドリング**
   - より詳細なエラーメッセージ
   - ソース位置情報の保持

4. **クロスコンパイル**
   - WebAssemblyターゲットの完全実装
   - ベアメタルターゲットの完全実装

5. **メモリ管理**
   - ガベージコレクションまたはアリーナアロケータの導入
   - フォーマット文字列で割り当てられたメモリの適切な解放

## 関連ドキュメント

- [LLVM ランタイムライブラリ](./LLVM_RUNTIME_LIBRARY.md)
- [LLVM 最適化パイプライン](./LLVM_OPTIMIZATION.md)
- [フォーマット文字列のLLVM実装](./STRING_INTERPOLATION_LLVM.md)
- [FFI設計](./design/ffi.md)

## まとめ

LLVMバックエンドの基本実装が完了し、Cmプログラムをネイティブコードにコンパイルできるようになりました。インタプリタと同じ結果を生成することを確認しており、基本的な機能は正しく動作しています。

全39のLLVMテストがパスしており、以下のカテゴリをカバーしています：
- basic（13テスト）
- control_flow（9テスト）
- errors（3テスト）
- formatting（3テスト）
- types（8テスト）
- functions（3テスト）

今後は型システムの完全実装と最適化の改善を進めることで、より実用的なコンパイラへと発展させることができます。