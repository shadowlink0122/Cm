[English](MAKEFILE_GUIDE.en.html)

# Makefile使用ガイド

## 概要

Cmプロジェクトの開発を効率化するためのMakefileを用意しました。CMakeコマンドを覚える必要がなく、簡単なmakeコマンドで開発タスクを実行できます。

## 基本的な使い方

### ヘルプ表示
```bash
make help        # 利用可能なコマンド一覧を表示
```

## 主要コマンド

### 🔨 ビルド関連

```bash
make build       # デバッグビルド（通常の開発用）
make release     # リリースビルド（最適化あり）
make clean       # ビルドディレクトリを削除
make rebuild     # クリーン後に再ビルド
```

### 🧪 テスト実行

```bash
make test        # すべてのテストを実行

# 個別テスト
make test-unit   # ユニットテストのみ
make test-lexer  # 字句解析テスト
make test-hir    # HIR変換テスト
make test-mir    # MIR変換テスト
make test-opt    # 最適化テスト
```

### 🚀 プログラム実行

```bash
# ファイル指定して実行
make run FILE=examples/basics/01_simple.cm

# デバッグモード
make run-debug FILE=examples/basics/02_hello.cm

# 詳細表示モード
make run-verbose FILE=examples/basics/03_variables.cm
```

### 📝 サンプル実行（ショートカット）

```bash
make run-hello       # Hello Worldを実行
make run-variables   # 変数サンプルを実行
make run-format      # フォーマット文字列サンプルを実行
make run-all-examples # すべてのサンプルを順番に実行
```

### 🔬 コード生成テスト

```bash
make test-compile-rust  # Rust変換機能をテスト
make test-compile-ts    # TypeScript変換機能をテスト
make test-compile-wasm  # WASM生成機能をテスト
```

### 📊 解析・デバッグ

```bash
# ファイルの解析結果を表示
make check FILE=test.cm     # 型チェックのみ
make ast FILE=test.cm       # AST（抽象構文木）表示
make hir FILE=test.cm       # HIR（高レベル中間表現）表示
make mir FILE=test.cm       # MIR（中レベル中間表現）表示
make mir-opt FILE=test.cm   # 最適化後のMIR表示
```

### 🎨 開発ツール

```bash
make format      # C++コードを自動フォーマット
make lint        # 静的解析（clang-tidy）
make docs        # ドキュメント生成
make serve-docs  # ドキュメントをローカルで表示
```

### 🐳 Docker環境

```bash
make docker-build  # Docker環境でビルド
make docker-test   # Docker環境でテスト
```

## 実用例

### 新機能を開発する場合

```bash
# 1. コードを編集後、ビルド
make build

# 2. サンプルファイルで動作確認
make run FILE=my_test.cm

# 3. デバッグが必要な場合
make run-debug FILE=my_test.cm

# 4. AST/HIR/MIRを確認
make ast FILE=my_test.cm
make hir FILE=my_test.cm

# 5. テストを実行
make test
```

### コンパイラの動作を確認する場合

```bash
# 最適化の効果を確認
make mir FILE=test.cm        # 最適化前
make mir-opt FILE=test.cm    # 最適化後

# パフォーマンス比較
make bench                    # ベンチマーク実行
```

### トランスパイラ機能を試す場合

```bash
# Rustコード生成をテスト
make test-compile-rust

# TypeScriptコード生成をテスト
make test-compile-ts
```

## ショートカット

開発効率を上げるため、短縮コマンドも用意：

```bash
make b   # make build と同じ
make t   # make test と同じ
make r   # make run と同じ（デフォルトファイル使用）
make c   # make clean と同じ
```

## Tips

### デフォルトファイル

`FILE` パラメータを省略すると、デフォルトで `examples/basics/01_simple.cm` が使用されます：

```bash
make r   # examples/basics/01_simple.cm を実行
```

### 並列ビルド

Ninjaを使用しているため、自動的に並列ビルドされます。

### エラー時の詳細表示

テストでエラーが発生した場合、詳細が自動的に表示されます（`--output-on-failure` オプション）。

## トラブルシューティング

### "make: command not found" エラー

```bash
# macOS
brew install make

# Ubuntu/Debian
sudo apt-get install build-essential

# Fedora/RHEL
sudo dnf install make
```

### ビルドエラーが発生する場合

```bash
# クリーンビルドを試す
make rebuild

# Dockerを使う（環境差分を排除）
make docker-build
```

## まとめ

このMakefileにより、Cmコンパイラの開発・テスト・デバッグが簡単になります。`make help` でいつでもコマンド一覧を確認できます。