# Cm言語 テストドキュメント

このディレクトリには、Cm言語コンパイラのテストシステムに関するドキュメントが含まれています。

## 📚 ドキュメント一覧

### [TESTING_GUIDE.md](TESTING_GUIDE.md)
**包括的なテストガイド**
- テストディレクトリ構造の詳細
- テストファイルの種類と役割
- テストの作成方法（手順とベストプラクティス）
- バックエンド別の考慮事項
- トラブルシューティング

### [TEST_QUICK_REFERENCE.md](TEST_QUICK_REFERENCE.md)
**クイックリファレンス**
- よく使うコマンド一覧
- 並列実行の設定
- デバッグコマンド
- Tips & Tricks

## 🎯 クイックスタート

### 新しいテストを作成する

```bash
# 1. テストカテゴリを作成
mkdir tests/test_programs/my_feature

# 2. テストファイルを作成
cat > tests/test_programs/my_feature/test.cm << 'EOF'
int main() {
    println("Test output");
    return 0;
}
EOF

# 3. 期待値を生成
./build/bin/cm run tests/test_programs/my_feature/test.cm > \
    tests/test_programs/my_feature/test.expect
```

### すべてのテストを並列実行

```bash
# 推奨: 全テストを並列実行
make test-all-parallel -j8
```

### 特定のバックエンドでテスト

```bash
make test-interpreter-parallel  # インタプリタ
make test-llvm-parallel         # LLVM ネイティブ
make test-llvm-wasm-parallel    # WASM
```

## 📁 テストファイル構造

```
tests/test_programs/
├── category_name/           # カテゴリディレクトリ
│   ├── .skip                # カテゴリ全体のスキップ設定
│   ├── test1.cm             # テストソース
│   ├── test1.expect         # 期待される出力
│   ├── test2.cm
│   ├── test2.error          # エラーを期待
│   └── test3.skip           # 個別テストのスキップ
```

## 🔧 テストシステムの特徴

### 3つのバックエンド
- **インタプリタ**: MIRを直接実行（最速起動）
- **LLVM**: ネイティブコードにコンパイル（最高性能）
- **WASM**: WebAssemblyにコンパイル（ポータブル）

### スキップファイルの階層
- **`.skip`** - カテゴリ全体をスキップ（ディレクトリ直下）
- **`test_name.skip`** - 個別テストをスキップ

### 並列実行サポート
- CPUコア数に基づく自動並列化
- 一時ファイルの衝突回避
- 効率的なテスト実行

## 💡 ベストプラクティス

1. **カテゴリ単位でテストを整理**
   - 機能ごとにディレクトリを作成
   - 関連するテストをまとめる

2. **適切なスキップファイルの使用**
   - インラインアセンブリ → カテゴリ全体をスキップ
   - 特定の問題 → 個別テストをスキップ

3. **並列実行を活用**
   - 開発時は並列実行でテスト時間を短縮
   - CI/CDでも並列実行を有効化

4. **期待値ファイルの管理**
   - バックエンド固有の違いは `.expect.llvm` などで対応
   - エラーテストは `.error` ファイルを空にする

## 📊 現在のテスト状況

- **テストカテゴリ**: 20種類以上
- **総テスト数**: 130以上
- **対応バックエンド**: 3種類（インタプリタ、LLVM、WASM）
- **並列実行**: 全バックエンドでサポート

## 🔗 関連リンク

- [プロジェクトREADME](../../README.md)
- [設計ドキュメント](../design/README.md)
- [unified_test_runner.sh](../../tests/unified_test_runner.sh) - テストランナースクリプト