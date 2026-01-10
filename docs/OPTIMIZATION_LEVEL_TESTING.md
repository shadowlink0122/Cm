[English](OPTIMIZATION_LEVEL_TESTING.en.html)

# 最適化レベル別テスト実装完了

**実装日**: 2026-01-04  
**関連Issue**: 全プラットフォームでO0-O3最適化レベルテストのサポート

## 実装内容

### 1. Makefile更新

全プラットフォームでO0-O3の最適化レベル別テストをサポート。

#### 新規追加ターゲット

**インタプリタ**:
```bash
make ti0  # O0 (最適化なし)
make ti1  # O1 (基本最適化)
make ti2  # O2 (標準最適化)
make ti3  # O3 (最大最適化)
make test-interpreter-all-opts  # O0-O3全テスト
```

**LLVM Native**:
```bash
make tl0/tl1/tl2/tl3
make test-llvm-all-opts
```

**LLVM WASM**:
```bash
make tlw0/tlw1/tlw2/tlw3
make test-llvm-wasm-all-opts
```

**JavaScript**:
```bash
make tj0/tj1/tj2/tj3
make test-js-all-opts
```

**全プラットフォーム**:
```bash
make test-all-opts  # 全プラットフォーム・全最適化レベル
make tao            # ショートカット
```

#### デフォルトの動作

従来のコマンドは**O3（最大最適化）**で実行：
```bash
make tip   # O3で実行
make tlp   # O3で実行
make tlwp  # O3で実行
make tjp   # O3で実行
```

### 2. unified_test_runner.sh更新

#### OPT_LEVEL環境変数のサポート

```bash
# 環境変数で指定
OPT_LEVEL=0 tests/unified_test_runner.sh -b interpreter

# Makefile経由（推奨）
make ti1  # OPT_LEVEL=1で実行
```

#### 実装詳細

- デフォルト値: `OPT_LEVEL=3`（O3）
- 全コンパイル・実行コマンドに`-O$OPT_LEVEL`オプションを追加
- 対象コマンド:
  - `cm run -O$OPT_LEVEL`
  - `cm compile --emit-llvm -O$OPT_LEVEL`
  - `cm compile --target=wasm -O$OPT_LEVEL`
  - `cm compile --target=js -O$OPT_LEVEL`

### 3. CI設定更新 (.github/workflows/ci.yml)

#### デフォルトテスト（全Push/PR）

O3でのみテスト実行（高速）：
- Unit Tests
- Interpreter Tests (O3)
- LLVM Native Tests (O3)
- LLVM WASM Tests (O3)

#### 最適化レベルマトリックステスト（mainブランチのみ）

4プラットフォーム × 4最適化レベル = **16通りの組み合わせ**で動作保証：

| プラットフォーム | O0 | O1 | O2 | O3 |
|-----------------|----|----|----|----|
| Interpreter     | ✅ | ✅ | ✅ | ✅ |
| LLVM Native     | ✅ | ✅ | ✅ | ✅ |
| LLVM WASM       | ✅ | ✅ | ✅ | ✅ |
| JavaScript      | ✅ | ✅ | ✅ | ✅ |

**実行条件**: 
- `github.ref == 'refs/heads/main'`
- PRではスキップ（時間短縮）
- mainへのマージ後に完全テスト

### 4. 動作確認

```bash
# 単一最適化レベルでテスト
$ OPT_LEVEL=1 make tip
Running interpreter tests (O1)...
[PASS] control_flow/test_for_loop
...
Status: SUCCESS

# 全最適化レベルでテスト
$ make test-interpreter-all-opts
Running interpreter tests (O0)...
Running interpreter tests (O1)...
Running interpreter tests (O2)...
Running interpreter tests (O3)...
✅ All interpreter optimization level tests completed!
```

## 使用例

### 開発中のテスト

```bash
# 特定の最適化レベルでクイックテスト
make ti1              # インタプリタ O1
make tl2              # LLVM Native O2
make tlw3             # WASM O3

# カテゴリ指定
OPT_LEVEL=0 tests/unified_test_runner.sh -b llvm -c control_flow
```

### リリース前の完全テスト

```bash
# 全プラットフォーム・全最適化レベル
make test-all-opts

# または個別に
make test-interpreter-all-opts
make test-llvm-all-opts
make test-llvm-wasm-all-opts
make test-js-all-opts
```

### CI/CDでの使用

```yaml
# GitHub Actions例
- name: Test all optimization levels
  run: make test-all-opts
  
# または特定レベルのみ
- name: Test O2
  run: |
    OPT_LEVEL=2 make tip
    OPT_LEVEL=2 make tlp
    OPT_LEVEL=2 make tlwp
```

## メリット

### 1. バグの早期発見
- 各最適化レベルで異なる問題が発見できる
- O0: 基本的なバグ
- O1-O2: 最適化の副作用
- O3: 過剰最適化による問題

### 2. パフォーマンス検証
- 最適化レベルごとの効果を確認
- デグレードの検出

### 3. CI/CDでの品質保証
- mainブランチで全組み合わせを自動テスト
- リリース品質を保証

## 今後の拡張

### WASM専用最適化（計画中）

現在はLLVMのOz（サイズ優先）を使用。今後追加予定：
- 文字列リテラルの定数化
- 配列境界チェック最適化
- SIMD最適化

Version 1.2以降で段階的に実装。

## 関連ドキュメント

- [ROADMAP.md](../ROADMAP.html) - Version 1.1.0セクション
- [docs/optimization/OPTIMIZATION_STATUS.md](../docs/optimization/OPTIMIZATION_STATUS.html)
- [Makefile](../Makefile) - 全ターゲット定義
- [.github/workflows/ci.yml](../.github/workflows/ci.yml) - CI設定