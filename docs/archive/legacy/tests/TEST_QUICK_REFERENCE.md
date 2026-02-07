[English](TEST_QUICK_REFERENCE.en.html)

# Cm言語テスト クイックリファレンス

## 🚀 よく使うコマンド

### テスト実行（推奨：並列実行）

```bash
# 全テストを並列実行（最速）
make test-all-parallel -j8

# LLVM テストのみ並列実行
make test-llvm-parallel -j8

# インタプリタテストのみ
make test-interpreter

# WASM テストのみ
make test-llvm-wasm
```

### 個別テスト実行

```bash
# 特定のテストファイルを実行
./build/bin/cm run tests/test_programs/basic/hello.cm

# 特定のディレクトリのテストを実行
./tests/unified_test_runner.sh -b llvm tests/test_programs/generics/*.cm

# デバッグモードで実行
./build/bin/cm run --debug tests/test_programs/errors/type_error.cm
```

## 📝 テストファイル構造

```
test_name.cm          # テストプログラム（必須）
test_name.expect      # 期待される出力
test_name.error       # エラーを期待（.expectより優先）
.skip                 # カテゴリ全体をスキップ（ディレクトリ直下）
test_name.skip        # 個別テストをスキップ
test_name.expect.llvm # LLVM固有の期待値
```

## 🎯 新しいテストの作成

### 1. 基本テスト

```bash
# 新しいカテゴリを作成
mkdir tests/test_programs/my_feature

# テストを作成
cat > tests/test_programs/my_feature/test.cm << 'EOF'
int main() {
    println("Test output");
    return 0;
}
EOF

# 期待値を生成
./build/bin/cm run tests/test_programs/my_feature/test.cm > \
    tests/test_programs/my_feature/test.expect
```

### 2. エラーテスト

```bash
# エラーケースを作成
cat > tests/test_programs/errors/my_error.cm << 'EOF'
int main() {
    int x = "type error";  // 型エラー
    return 0;
}
EOF

# エラーファイルを作成（空ファイル）
touch tests/test_programs/errors/my_error.error
```

### 3. バックエンド固有のスキップ

```bash
# カテゴリ全体をスキップ（例：asmディレクトリ）
cat > tests/test_programs/asm/.skip << 'EOF'
interpreter
llvm-wasm
EOF

# 個別テストをスキップ
cat > tests/test_programs/modules/my_module.skip << 'EOF'
interpreter
EOF
```

## 🔍 デバッグ

```bash
# 詳細出力
./tests/unified_test_runner.sh -v -b llvm test.cm

# MIRダンプ
./build/bin/cm compile --emit-mir test.cm

# LLVMIRダンプ
./build/bin/cm compile --emit-llvm test.cm

# デバッグモード
CM_DEBUG=1 ./build/bin/cm run test.cm
```

## ⚡ パフォーマンステスト

```bash
# 実行時間測定
time ./build/bin/cm run test.cm

# コンパイル時間測定
time ./build/bin/cm compile test.cm -o test

# ベンチマーク比較（インタプリタ vs LLVM）
hyperfine './build/bin/cm run test.cm' './test'
```

## 🏃 並列実行のヒント

```bash
# CPUコア数を確認
nproc  # Linux
sysctl -n hw.ncpu  # macOS

# 最適な並列度（通常はCPUコア数）
make test-all-parallel -j$(nproc)

# メモリ制限がある場合は並列度を下げる
make test-all-parallel -j4
```

## 📊 テスト結果の確認

```bash
# 失敗したテストのみ表示
make test-llvm 2>&1 | grep FAIL

# テストカウントを確認
find tests/test_programs -name "*.cm" | wc -l

# カテゴリ別のテスト数
for dir in tests/test_programs/*/; do
    echo "$(basename $dir): $(find $dir -name "*.cm" | wc -l)"
done
```

## 🔧 Makefile ターゲット

| ターゲット | 説明 | 推奨並列度 |
|------------|------|------------|
| `test-interpreter` | インタプリタテスト | -j8 |
| `test-llvm` | LLVM ネイティブテスト | -j4 |
| `test-llvm-wasm` | WASM テスト | -j4 |
| `test-llvm-all` | LLVM + WASM | -j4 |
| `test-all-parallel` | 全テスト並列実行 | -j8 |

## ⚠️ 注意事項

### skipファイルのバックエンド名
- `interpreter` - MIR インタプリタ
- `llvm` - LLVM ネイティブ
- `llvm-wasm` - LLVM WASM ターゲット

### テスト実行順序
並列実行時は実行順序が不定なので、テスト間の依存関係を作らないこと。

### 一時ファイル
各テストは独自の一時ディレクトリを使用するため、並列実行でも衝突しない。

## 💡 Tips

1. **新機能のテスト追加時**
   ```bash
   # まずインタプリタで動作確認
   make test-interpreter
   # 次にLLVMで確認
   make test-llvm
   # 最後にWASM
   make test-llvm-wasm
   ```

2. **CI/CD 環境**
   ```bash
   # タイムアウト付き実行
   timeout 300 make test-all-parallel -j4
   ```

3. **ローカル開発**
   ```bash
   # 変更監視と自動テスト実行
   while true; do
     inotifywait -e modify tests/test_programs/**/*.cm
     make test-interpreter
   done
   ```