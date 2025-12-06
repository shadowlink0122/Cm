# Cm言語 サンプルプログラムと実行方法

## 📁 サンプルプログラム一覧

| ファイル | 説明 | 主な機能 |
|---------|------|---------|
| `01_hello.cm` | Hello World | 基本的な関数とprint |
| `02_variables.cm` | 変数と型 | int, float, bool, char型の使用 |
| `03_control_flow.cm` | 制御フロー | if文、while文、for文 |
| `04_functions.cm` | 関数 | 関数定義、再帰、戻り値 |
| `05_compound_assignments.cm` | 複合代入 | +=、-=、*=、/=、ビット演算 |
| `06_optimization.cm` | 最適化例 | 定数畳み込み、デッドコード除去 |

## 🚀 実行方法

### 1. ビルド

```bash
# プロジェクトルートで
cmake -B build -G Ninja
cmake --build build
```

### 2. コンパイラの実行モード

#### a) AST表示モード
```bash
./build/bin/cm examples/01_hello.cm --ast
```

#### b) HIR表示モード（脱糖後）
```bash
./build/bin/cm examples/03_control_flow.cm --hir
# for文がwhile文に変換されているのを確認できます
```

#### c) MIR表示モード（最適化前）
```bash
./build/bin/cm examples/06_optimization.cm --mir
```

#### d) 最適化されたMIR表示モード
```bash
./build/bin/cm examples/06_optimization.cm --mir-opt
# 定数畳み込みやデッドコード除去が適用されます
```

#### e) インタプリタ実行（開発中）
```bash
./build/bin/cm examples/01_hello.cm --run
```

#### f) Rustコード生成（開発中）
```bash
./build/bin/cm examples/04_functions.cm --emit-rust > output.rs
```

### 3. デバッグモード

詳細なコンパイル過程を表示：

```bash
# デバッグ出力を有効化
./build/bin/cm examples/02_variables.cm --debug

# トレースレベル（最も詳細）
./build/bin/cm examples/03_control_flow.cm -d=trace
```

### 4. 最適化レベル

```bash
# -O0: 最適化なし
./build/bin/cm examples/06_optimization.cm -O0 --mir

# -O1: 基本的な最適化
./build/bin/cm examples/06_optimization.cm -O1 --mir

# -O2: より積極的な最適化
./build/bin/cm examples/06_optimization.cm -O2 --mir
```

## 📊 コンパイルパイプライン

```
Source (.cm)
    ↓
[Lexer] → Tokens
    ↓
[Parser] → AST
    ↓
[HIR Lowering] → HIR (脱糖)
    - for文 → while文
    - 複合代入 → 単純代入
    ↓
[Type Checker] → Typed HIR
    ↓
[MIR Lowering] → MIR (CFG)
    ↓
[Optimizations] → Optimized MIR
    - 定数畳み込み
    - デッドコード除去
    - コピー伝播
    ↓
[Backend]
    ├→ [Interpreter] → 直接実行
    ├→ [Rust Codegen] → .rs ファイル
    └→ [WASM/TS Codegen] → .wasm/.ts (将来)
```

## 🔍 サンプル実行例

### 例1: Hello World

```bash
$ ./build/bin/cm examples/01_hello.cm --run
Hello, Cm!
```

### 例2: 最適化の確認

```bash
# 最適化前のMIR
$ ./build/bin/cm examples/06_optimization.cm -O0 --mir
# 多くの中間変数と計算が残っている

# 最適化後のMIR
$ ./build/bin/cm examples/06_optimization.cm -O2 --mir
# 定数が事前計算され、デッドコードが削除されている
```

### 例3: for文の脱糖確認

```bash
$ ./build/bin/cm examples/03_control_flow.cm --hir
# for文がwhile文に変換されているのが確認できます
```

## 🧪 テスト実行

```bash
# すべてのサンプルをテスト
for file in examples/*.cm; do
    echo "Testing $file..."
    ./build/bin/cm "$file" --check
done
```

## 📝 注意事項

- 現在、インタプリタとコード生成機能は開発中です
- `--check`オプションで構文と型チェックのみ実行できます
- デバッグ出力は`CM_DEBUG`環境変数でも制御可能です

## 🔗 関連ドキュメント

- [言語仕様](../docs/spec/grammar.md)
- [HIR設計](../docs/design/hir.md)
- [MIR設計](../docs/design/mir.md)
- [最適化設計](../docs/design/optimization.md)