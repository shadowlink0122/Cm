[English](QUICKSTART.en.html)

# Cm言語コンパイラ - クイックスタート

## 🚀 実行方法

### 1. ビルド

```bash
# プロジェクトルートで
cd build
cmake --build . --target cm
```

### 2. 基本的な使い方

```bash
# 構文チェック
./bin/cm ../examples/00_simple.cm --check

# AST表示
./bin/cm ../examples/00_simple.cm --ast

# HIR表示（脱糖後）
./bin/cm ../examples/00_simple.cm --hir

# MIR表示（最適化前）
./bin/cm ../examples/00_simple.cm --mir

# 最適化されたMIR表示
./bin/cm ../examples/00_simple.cm -O2 --mir-opt
```

## 📝 サンプルプログラム

### 最も簡単な例（00_simple.cm）

```cm
int main() {
    int x = 10;
    int y = 20;
    int sum = x + y;
    return sum;
}
```

このプログラムは最適化により `return 30;` に変換されます。

## 🔍 最適化の動作確認

```bash
# 最適化前
./bin/cm ../examples/00_simple.cm --mir
# 多くの中間変数と計算が表示される

# 最適化後（-O2）
./bin/cm ../examples/00_simple.cm -O2 --mir-opt
# 定数畳み込みにより return 30; だけになる
```

## 📊 コンパイルパイプライン

```
1. Lexer: ソースコード → トークン列
2. Parser: トークン列 → AST
3. Type Checker: 型検査
4. HIR Lowering: AST → HIR（for文→while文、複合代入の脱糖）
5. MIR Lowering: HIR → MIR（CFG構築）
6. Optimization: 定数畳み込み、デッドコード除去、コピー伝播
```

## ✅ 実装済み機能

- **フロントエンド**: Lexer、Parser、Type Checker
- **HIR**: 脱糖処理（for文、複合代入）
- **MIR**: CFGベースの中間表現
- **最適化**:
  - 定数畳み込み（Constant Folding）
  - デッドコード除去（Dead Code Elimination）
  - コピー伝播（Copy Propagation）
- **デバッグ**: 多言語対応のデバッグシステム

## 🧪 テスト

```bash
# すべてのテストを実行
cd build
ctest

# 特定のテストのみ
./bin/lexer_test
./bin/hir_lowering_test
./bin/mir_lowering_test
./bin/mir_optimization_test
```

現在45個のテストがすべて成功しています。

## 🚧 開発中の機能

- インタプリタ（--run）
- Rustコード生成（--emit-rust）
- 組み込み関数（print等）

## 📖 詳細ドキュメント

- [サンプルプログラム集](examples/README.html)
- [アーキテクチャ設計](docs/design/architecture.html)
- [HIR設計](docs/design/hir.html)
- [MIR設計](docs/design/mir.html)