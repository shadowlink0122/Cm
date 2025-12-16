# デバッグシステム改善ドキュメント

## 概要
Cm言語コンパイラのデバッグシステムを大幅に改善し、各コンパイルステージで詳細なデバッグ情報を出力できるようにしました。

## 改善内容

### 1. 新規デバッグヘッダーファイルの追加

#### MIRステージ (`src/common/debug/mir.hpp`)
- **基本フロー**: 変換開始/終了、最適化開始/終了
- **関数処理**: 関数変換、基本ブロック作成、PHIノード挿入
- **命令生成**: alloca, load, store, branch, jumpなど全MIR命令
- **SSA形式**: SSA構築、支配木計算、支配境界計算
- **最適化パス**: デッドコード除去、定数伝播、インライン化など
- **制御フロー**: CFG構築、簡略化、ループ検出

#### CodeGenステージ (`src/common/debug/codegen.hpp`)
- **Rust生成**: 関数、構造体、列挙型、式、文の生成
- **TypeScript生成**: クラス、インターフェース、モジュールの生成
- **C++生成**: テンプレート、ヘッダー、ソースファイルの生成
- **共通処理**: 型マッピング、名前マングル、シンボル解決
- **MIR変換**: ステートマシン生成、PHIノード除去

### 2. 既存デバッグヘッダーの拡充

#### Lexerステージ (`src/common/debug/lex.hpp`)
- トークンタイプの細分化（キーワード、識別子、リテラル、演算子など）
- リテラル処理の詳細化（整数、浮動小数点、文字列エスケープなど）
- 位置情報の追跡
- ヘルパー関数追加: `dump_token()`, `dump_position()`

#### Parserステージ (`src/common/debug/par.hpp`)
- 定義の詳細化（関数、構造体、列挙型、インターフェース、マクロなど）
- 式と文の細分化
- 制御構造の追跡（if, while, for, match）
- ヘルパー関数追加: `dump_node()`, `dump_expectation()`, `dump_scope()`

#### HIRステージ (`src/common/debug/hir.hpp`)
- ノード作成の詳細化
- 式と文の変換追跡
- 型処理（解決、推論、チェック）
- シンボル解決とスコープ管理
- ヘルパー関数追加: `dump_node()`, `dump_type()`, `dump_symbol()`

#### インタプリタステージ (`src/common/debug/interp.hpp`)
- MIR命令の実行追跡
- スタック操作の可視化
- メモリ操作の追跡
- エラー処理の詳細化
- ヘルパー関数追加: `dump_value()`, `dump_frame()`, `dump_instruction()`, `dump_memory()`

## 使用方法

### 基本的な使用

```cpp
#include "common/debug_messages.hpp"

// デバッグモードを有効化
cm::debug::set_debug_mode(true);
cm::debug::set_level(cm::debug::Level::Debug);

// 言語設定（0=英語, 1=日本語）
cm::debug::set_lang(1);

// メッセージ出力
cm::debug::lex::log(cm::debug::lex::Id::Start);
cm::debug::par::log(cm::debug::par::Id::FuncDef, "main");
cm::debug::hir::log(cm::debug::hir::Id::TypeResolve, "int");
cm::debug::mir::log(cm::debug::mir::Id::InstAlloc, "%0 = alloca i32");
```

### デバッグレベル

- `Level::Error`: エラーのみ
- `Level::Warn`: 警告とエラー
- `Level::Info`: 情報メッセージを含む
- `Level::Debug`: デバッグメッセージを含む（デフォルト）
- `Level::Trace`: 全ての詳細情報を含む

### Traceレベルでの詳細情報

```cpp
// Traceレベルでのみ出力される詳細情報
cm::debug::set_level(cm::debug::Level::Trace);

// 各ステージの詳細ダンプ関数
cm::debug::lex::dump_token("KEYWORD", "int", 1, 1);
cm::debug::par::dump_node("Function", "main");
cm::debug::hir::dump_type("x", "int");
cm::debug::mir::dump_value("%0", "42");
cm::debug::codegen::dump_code("function", "int main() { return 42; }");
cm::debug::interp::dump_value("x", "42", "int");
```

## コマンドラインオプション

```bash
# デバッグモード有効化
./cm --debug file.cm
./cm -d file.cm

# デバッグレベル指定
./cm -d=trace file.cm
./cm -d=debug file.cm
./cm -d=info file.cm

# 日本語メッセージ
./cm --lang=ja -d file.cm
```

## 出力例

```
[LEXER] Starting lexical analysis
[LEXER] Keyword detected: int
[LEXER] Identifier detected: main
[PARSER] Starting parsing
[PARSER] Parsing function definition: main
[HIR] Starting HIR lowering
[HIR] Creating function node: main
[MIR] Starting MIR lowering
[MIR] Lowering function: main
[MIR] Creating basic block: entry
[MIR] Generating alloca instruction: %0 = alloca i32
[CODEGEN_CPP] Starting C++ generation
[CODEGEN_CPP] Generating C++ function: main
[INTERP] Starting interpreter
[INTERP] Calling function: main
```

## テストファイル

- `/tests/debug/test_debug_messages.cpp`: 全デバッグメッセージのテスト
- `/examples/debug/debug_integration_example.cpp`: 実装への統合例
- `/.tmp/test_debug.sh`: テスト実行スクリプト

## 今後の拡張案

1. **フィルタリング機能**: 特定のステージやメッセージIDのみを出力
2. **ログファイル出力**: デバッグ情報をファイルに保存
3. **カラー出力**: ターミナルでの色分け表示
4. **パフォーマンス測定**: 各ステージの実行時間計測
5. **インタラクティブデバッガ**: ブレークポイント設定と変数検査

## メリット

- **問題の特定が容易**: 各ステージの処理内容が明確に分かる
- **多言語対応**: 英語と日本語のメッセージを切り替え可能
- **詳細レベル制御**: 必要な情報量を調整可能
- **統一的なインターフェース**: 全ステージで一貫した使用方法
- **拡張性**: 新しいメッセージIDやステージを簡単に追加可能