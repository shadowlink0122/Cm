# MIRインタプリタ設計

## 目的
- Cmのインタプリタ（MIR実行系）の構造と責務を整理する。
- MIR最適化との境界を明確化する。

## 構成
- エントリ: `src/codegen/interpreter/interpreter.hpp`
- 値表現: `src/codegen/interpreter/types.hpp`
- 評価器: `src/codegen/interpreter/eval.hpp`
- 組み込み関数: `src/codegen/interpreter/builtins.hpp`

## 実行フロー
1) HIR → MIR 生成（共通）
2) MIR最適化（共通固定レベルを実行）
3) `Interpreter::execute()` が `main` などのエントリポイントを起動
4) `execute_function` → `execute_block` → `execute_statement/terminator` の順に実行

## 実行コンテキスト
`ExecutionContext` は関数単位のローカル状態を保持する。

- `function`: 対象関数（MIR）
- `locals`: `LocalId -> Value` マップ
- `builtins`: 組み込み関数レジストリ
- `skip_static_init`: static初期化スキップ対象

初期化時に、ローカル変数の型に応じてデフォルトの `Value` を生成する。
- Struct: `StructValue`
- Array: `ArrayValue`
- Slice: `SliceValue`（容量は初期値4）

実装: `src/codegen/interpreter/types.hpp`

## 値表現
`Value` は `std::any` で保持し、必要に応じて以下の構造体にキャストする。

- `StructValue`: 構造体名 + `field_id -> Value`
- `ArrayValue`: 固定配列の要素列 + 要素型
- `SliceValue`: 動的配列の要素列 + 容量
- `PointerValue`: ローカル参照/外部ポインタ（FFI）
- `ClosureValue`: ラムダ関数名 + キャプチャ済み値

実装: `src/codegen/interpreter/types.hpp`

## 式評価とメモリアクセス
`Evaluator` が `MirOperand` と `MirRvalue` を評価する。

- `load_from_place`: `MirPlace` と projection（Field/Index/Deref）を評価
- `store_to_place`: projection 付き代入を反映
- 定数変換: `MirConstant` → `Value`

実装: `src/codegen/interpreter/eval.hpp`

## 関数呼び出し
### 通常呼び出し
`execute_call` が `CallData` を解析し、引数評価 → 関数実行を行う。

- 直接呼び出し: `FunctionRef` / 定数関数名
- 関数ポインタ: `MirOperand::Copy/Move` から関数名取得

### クロージャ
- `LocalDecl::is_closure` と `captured_locals` から `ClosureValue` を作成。
- 呼び出し時はキャプチャ値を引数先頭に追加して実行。

### コンストラクタ
- `__ctor` 関数は特別扱い。
- `execute_constructor` で `self` を更新し、呼び出し元にコピーバックする。

### 動的ディスパッチ
- `Interface__method` 形式で呼び出された場合、
  `StructValue.type_name` を用いて `Type__method` を探索する。

実装: `src/codegen/interpreter/interpreter.hpp`

## 組み込み関数
`BuiltinManager` が `std::unordered_map<string, BuiltinFn>` を構築し、
I/O、文字列操作、slice/array操作、FFI呼び出しなどを提供する。

実装: `src/codegen/interpreter/builtins.hpp`

## static変数の扱い
- 関数名 + 変数名をキーに `static_variables_` に保存。
- 初回はデフォルト値で初期化、2回目以降は保存値を復元。

実装: `src/codegen/interpreter/interpreter.hpp`

## スライスと配列
`cm_slice_*` 系は特別処理で `SliceValue` を直接更新する。
- 構造体フィールドとしての slice も取り扱う。

実装: `src/codegen/interpreter/interpreter.hpp`

## 制限・注意点
- JITは未実装（MIRを逐次実行）。
- 高度な最適化は行わず、MIR最適化に依存する。
- 外部ポインタの読み書きは型情報に依存するため、型不一致は未定義動作になり得る。
