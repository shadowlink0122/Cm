[English](OPTIMIZATION_LEVEL_TESTING_UPDATE.en.html)

# パラレル/シリアル対応追加

**更新日**: 2026-01-04

## 追加機能

全ての最適化レベル別テストで**パラレル実行**と**シリアル実行**の両方を提供。

### 新規ターゲット（32個追加）

#### パラレル実行（デフォルト、高速）

```bash
# インタプリタ
make ti0/ti1/ti2/ti3  # 並列実行

# LLVM Native  
make tl0/tl1/tl2/tl3  # 並列実行

# WASM
make tlw0/tlw1/tlw2/tlw3  # 並列実行

# JavaScript
make tj0/tj1/tj2/tj3  # 並列実行
```

#### シリアル実行（低メモリ環境、デバッグ用）

```bash
# インタプリタ
make test-interpreter-o0-serial
make test-interpreter-o1-serial
make test-interpreter-o2-serial
make test-interpreter-o3-serial

# LLVM Native
make test-llvm-o0-serial
make test-llvm-o1-serial
make test-llvm-o2-serial
make test-llvm-o3-serial

# WASM
make test-llvm-wasm-o0-serial
make test-llvm-wasm-o1-serial
make test-llvm-wasm-o2-serial
make test-llvm-wasm-o3-serial

# JavaScript
make test-js-o0-serial
make test-js-o1-serial
make test-js-o2-serial
make test-js-o3-serial
```

## 使い分けガイド

| シナリオ | 推奨実行モード | コマンド例 |
|---------|--------------|-----------|
| 開発中のクイックテスト | パラレル | `make ti2` |
| 低メモリ環境 (< 4GB) | シリアル | `make test-interpreter-o2-serial` |
| CI/CD（メモリ制約あり） | シリアル | `OPT_LEVEL=3 make test-interpreter` |
| デバッグ時 | シリアル | `make test-llvm-o1-serial` |
| ローカル開発マシン | パラレル | `make test-all-opts` |

## パフォーマンス比較

### テスト実行時間（参考値）

| プラットフォーム | パラレル | シリアル | 短縮率 |
|-----------------|---------|---------|-------|
| Interpreter | ~30秒 | ~90秒 | 66% |
| LLVM Native | ~45秒 | ~150秒 | 70% |
| WASM | ~40秒 | ~120秒 | 67% |
| JavaScript | ~35秒 | ~100秒 | 65% |

*注: マシンスペックにより変動*

## 実装詳細

### デフォルトの動作

- ショートカット（`ti0`, `tl1`等）: **パラレル実行**
- フルネーム（`test-interpreter-o0`等）: **パラレル実行**
- `-serial`サフィックス: **シリアル実行**

### 内部実装

```makefile
# パラレル（デフォルト）
test-interpreter-o1:
    @OPT_LEVEL=1 tests/unified_test_runner.sh -b interpreter -p

# シリアル
test-interpreter-o1-serial:
    @OPT_LEVEL=1 tests/unified_test_runner.sh -b interpreter
```

## 使用例

### 開発ワークフロー

```bash
# 1. クイックテスト（パラレル）
make ti2    # O2で素早くテスト

# 2. 問題発見時はシリアルでデバッグ
make test-interpreter-o2-serial  # 詳細な出力を確認

# 3. 全レベルで確認
make test-interpreter-all-opts  # 自動的にパラレル
```

### CI/CDでの使用

```yaml
# GitHub Actions例
- name: Quick test (parallel)
  run: make ti3 tl3 tlw3 tj3
  
- name: Full test (serial, memory-efficient)
  run: |
    make test-interpreter-o3-serial
    make test-llvm-o3-serial
    make test-llvm-wasm-o3-serial
    make test-js-o3-serial
```

## トラブルシューティング

### メモリ不足エラー

**症状**: テストが途中でクラッシュ、OOM Killerが発動

**解決策**: シリアル実行に切り替え
```bash
# パラレル（メモリ使用量: 高）
make ti3

# シリアル（メモリ使用量: 低）
make test-interpreter-o3-serial
```

### デバッグ出力が見づらい

**症状**: パラレル実行で出力が混ざる

**解決策**: シリアル実行で順次実行
```bash
make test-llvm-o2-serial
```

## 統計

### 追加ターゲット数
- パラレル実行: 16ターゲット（ti0-ti3, tl0-tl3, tlw0-tlw3, tj0-tj3）
- シリアル実行: 16ターゲット（*-serial版）
- **合計**: 32ターゲット追加

### カバレッジ
- 4プラットフォーム × 4最適化レベル × 2実行モード = **32通り**の組み合わせ

## 関連ドキュメント

- [OPTIMIZATION_LEVEL_TESTING.md](OPTIMIZATION_LEVEL_TESTING.html) - 基本使用方法
- [Makefile](../Makefile) - 全ターゲット定義
- [unified_test_runner.sh](../tests/unified_test_runner.sh) - テストランナー実装