# Cm言語 プロジェクト構造

## ディレクトリ構成

```
Cm/
├── src/                    # ソースコード
│   ├── main.cpp           # エントリポイント
│   ├── common/            # 共通ユーティリティ
│   │   ├── debug.hpp      # デバッグシステム
│   │   ├── span.hpp       # ソース位置情報
│   │   └── diagnostics.hpp # エラー報告
│   ├── frontend/          # フロントエンド
│   │   ├── lexer/         # 字句解析器
│   │   ├── parser/        # 構文解析器
│   │   └── ast/           # 抽象構文木
│   ├── hir/               # 高レベル中間表現
│   │   ├── hir_nodes.hpp
│   │   └── hir_lowering.hpp
│   ├── mir/               # 中レベル中間表現（SSA形式）
│   │   ├── mir_nodes.hpp
│   │   ├── mir_lowering.hpp
│   │   └── optimizations/ # 最適化パス
│   └── backends/          # バックエンド
│       ├── rust/          # Rustトランスパイラ
│       ├── typescript/    # TypeScriptトランスパイラ
│       └── wasm/          # WebAssembly生成
│
├── tests/                 # テストファイル
│   ├── unit/              # ユニットテスト（*.cpp）
│   ├── integration/       # 統合テスト（*.cpp）
│   └── regression/        # リグレッションテスト（*.cm）
│
├── docs/                  # ドキュメント
│   ├── design/            # 設計文書
│   │   ├── CANONICAL_SPEC.md  # 正式言語仕様
│   │   ├── architecture.md    # アーキテクチャ
│   │   └── ...
│   ├── spec/              # 言語仕様
│   │   └── grammar.md     # 文法定義
│   ├── progress/          # 進捗報告
│   └── PROJECT_STRUCTURE.md # このファイル
│
├── examples/              # サンプルコード
│   ├── basic/             # 基本的な例
│   ├── advanced/          # 高度な例
│   ├── overload/          # オーバーロード例
│   └── transpiler/        # トランスパイラ例
│
├── build/                 # ビルド出力（.gitignore）
│   ├── bin/               # 実行ファイル
│   └── test/              # テスト実行ファイル
│
├── .tmp/                  # 一時ファイル（.gitignore）
│
├── CMakeLists.txt         # ビルド設定
├── docker-compose.yml     # Docker設定
├── .gitignore            # Git除外設定
├── CLAUDE.md             # AI開発ガイド（100行以内）
├── same-claude.md        # 共通AI設定（100行以内）
├── README.md             # プロジェクト説明
└── LICENSE               # ライセンス
```

## ルールと規約

### プロジェクトルート
**禁止**：
- ✗ ソースファイル（*.cpp, *.hpp, *.cm）
- ✗ 実行ファイル
- ✗ テストファイル
- ✗ 個別のドキュメント

**許可**：
- ✓ 設定ファイル（CMakeLists.txt, docker-compose.yml）
- ✓ メタ文書（README.md, LICENSE, CLAUDE.md）
- ✓ Git関連（.gitignore）

### デバッグ出力形式

```
[STAGE] メッセージ
```

**正しい例**：
```
[LEXER] Starting tokenization
[PARSER] Parsing function: main
[MIR] Lowering HIR to MIR
```

**間違い**：
```
[LEXER] [DEBUG] Message  // ✗ 二重ブラケット
[[PARSER]] Message        // ✗ 不正な形式
```

### テスト配置

- `tests/unit/` - C++ユニットテスト（Google Test）
- `tests/integration/` - C++統合テスト
- `tests/regression/` - Cmファイルによるリグレッションテスト

### ドキュメント

重要度別：
1. **CANONICAL_SPEC.md** - 最優先の正式仕様
2. **architecture.md** - システム設計
3. その他の設計文書

## ビルドとテスト

```bash
# ビルド
cmake -B build -G Ninja
cmake --build build

# テスト実行
ctest --test-dir build
./build/bin/cm tests/regression/example.cm

# クリーンアップ
rm -rf build .tmp
```