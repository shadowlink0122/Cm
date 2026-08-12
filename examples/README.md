# Cm Examples

Cmの機能を示す公式サンプル集です。

## カテゴリ

### 01_basics - 基本機能
- `hello_world.cm` - 出力と文字列補間
- `variables.cm` - 変数、型、構造体
- `control_flow.cm` - if/for/while/switch/match

### 02_functions - 関数
- `callbacks.cm` - コールバック（関数ポインタ/interface/ラムダ）
- `generics.cm` - ジェネリクスと制約

### 03_ownership - 所有権
- `move_semantics.cm` - ムーブセマンティクス
- `borrowing.cm` - 借用と参照

### web-fullstack - フルスタックWeb開発（TypeScriptバックエンド）
- Cmだけで完結するタスク管理Webアプリ（`--target=ts`）。HTML/CSS/ルーティング/インメモリDBをすべてCmで書き、FFIはNode組み込み `http` のみ（vendor JSなし）
- `src/store.cm` - 可変長スライスによるインメモリストア、`src/style.cm` - `with Css`、`src/view.cm` - `web::html` ビルダー、`src/server.cm` - Node `http` サーバ

### 04_memory - メモリ管理
- `raii_pattern.cm` - RAIIパターン（デストラクタ）
- `heap_allocation.cm` - ヒープ確保（FFI + RAII）

### 05_data_structures - データ構造
- `linked_list.cm` - 単方向リンクリスト
- `priority_queue.cm` - 優先度付きキュー（最小ヒープ）

### 06_algorithms - アルゴリズム
- `memoization_dp.cm` - メモ化DP（フィボナッチ、最小コイン問題）
- `dijkstra.cm` - ダイクストラ法（最短経路）

### 07_selfhost_drill - セルフホスト素振り（OS連携CLI）
- `main.cm` - コンパイラの縮図となるCLIツール。`env::args()`で引数解析、`fs::read_bytes`で読み込み、`strings::lines/split`でトークン数を集計、`bytes::push_u32_le`+`fs::write_bytes`でバイナリ成果物を出力、`process::run`で外部コマンド呼び出し
- `sample_input.cm` - 入力サンプル。`cm run examples/07_selfhost_drill/main.cm -- examples/07_selfhost_drill/sample_input.cm -o out.bin` で実行

### uefi - UEFI アプリケーション
- `hello_world.cm` - UEFI Hello World（画面出力）
- `memory_test.cm` - メモリ管理テスト（AllocatePool/FreePool）
- `libs/` - UEFI低レベルライブラリ（SystemTable、テキスト出力）

## 実行方法

```bash
# インタプリタで実行
cm run examples/01_basics/hello_world.cm

# LLVMコンパイル & 実行
cm run -t llvm examples/06_algorithms/dijkstra.cm

# UEFI（要 lld-link, QEMU）
cd examples/uefi && make && make run

# フルスタックWebアプリ（要 Node.js。npm install不要）
cm compile --target=js examples/web-fullstack/src/server.cm -o server.js && node server.js
```

## 機能カバレッジ

| 機能 | サンプル |
|------|----------|
| 関数ポインタ | callbacks.cm |
| interface | callbacks.cm, generics.cm |
| ジェネリクス | generics.cm |
| move | move_semantics.cm |
| 借用 | borrowing.cm |
| RAII | raii_pattern.cm, heap_allocation.cm |
| FFI | heap_allocation.cm, linked_list.cm, priority_queue.cm |
| メモ化DP | memoization_dp.cm |
| グラフ探索 | dijkstra.cm |
| with Css | web-fullstack/src/style.cm |
| UEFIベアメタル | uefi/hello_world.cm |
| OS連携（args/fs/process） | 07_selfhost_drill/main.cm |
