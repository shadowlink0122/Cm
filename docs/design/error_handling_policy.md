# エラー処理方針（013 §4.3-6 の明文化）

作成日: 2026-07-05対象: Cmコンパイラ実装（C++）

013調査で指摘された「例外と `optional`/`Result` の混在」に対する方針を定める。

## 現状（2026-07-05 実測）

- `throw` 使用: **50箇所**（内訳: codegen/llvm 25、codegen/js 7、preprocessor 5、frontend/types 4、その他 9）
- `Result<T>` / `std::optional` 使用: 141箇所
- 統一エラー基盤は実装済み: `src/common/error.hpp`（`cm::Error`（種別/コード/メッセージ/Span）、`Result<T> = std::variant<T, Error>`、`ErrorCollector`。`error_test` でテスト済み）

## 方針

エラーは発生原因で3分類し、扱いを固定する。

### 1. ユーザ入力起因（構文・型・コード生成対象の制約違反）

**診断API（`ErrorCollector`）+ `Result<T>` を使用し、throwしない。**

- 複数エラーの収集・継続診断を可能にする（1個目で停止しない）
- エラーコード（`E001`/`SV005` 等）と `Span` を必ず付与する
- 例: 型チェッカの `error()`、svバックエンドの `SV005`（string幅超過）

### 2. 環境起因（ファイルI/O・外部ツール失敗）

**呼び出し境界で `Result<T>` または success/error_message 構造体で返す。**

- 例: `ImportPreprocessor::ProcessResult`、`read_file` の `ReadFileResult`
- 内部実装でthrowを使う場合は、公開APIの境界で必ず捕捉して`Result`/構造体へ変換する（例外を境界の外へ漏らさない）

### 3. 内部不変条件違反（コンパイラ自身のバグ）

**`throw std::runtime_error` を許容する（即座に停止すべき状況）。**

- `main()` のトップレベル try/catch が捕捉し、終了コード1で終了する
- 「起きないはず」の状態にのみ使用し、ユーザ入力で到達可能な経路には使わない
- メッセージには文脈（関数名・対象名）を含める

## 新規コードの規約

1. 新規の公開APIは `Result<T>` か success フィールド付き構造体を返す
2. ユーザに見せるエラーは必ず `cm::Error` を経由する（生の `std::cerr` 直書き禁止。ただし診断表示層そのものは除く）
3. `catch (...)` での握りつぶし禁止（変換して伝播するか、ログを残す）
4. デストラクタ・コード生成の出力パスでは throw しない

## 既存コードの段階的移行（優先度順）

| Phase | 対象 | 内容 |
|---|---|---|
| A（完了） | 方針 | 本文書の策定。新規コードは上記規約に従う |
| B | preprocessor（5箇所） | `process_imports` 内のthrowは `process()` 境界で捕捉済みであることを確認し、種別2として整理 |
| C | codegen/js（7箇所） | バリデーション系（ポインタ禁止等）は種別1へ移行（ErrorCollector） |
| D | codegen/llvm（25箇所） | 大半は種別3（内部不変条件）として妥当。ユーザ到達可能なもののみ種別1へ |

移行は機能変更と混ぜず、単独のリファクタリングコミットとして行う。
