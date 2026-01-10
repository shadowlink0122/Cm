# O2/O3最適化レベル無効化の調査報告

作成日: 2026-01-11
対象バージョン: v0.11.0
調査ステータス: ✅ 完了

## 重要な発見: O2/O3が自動的に無効化されている

### 🔴 問題: 最適化レベルが自動的に下げられる

現在のCm言語コンパイラでは、**コードの複雑度やパターンに応じて、O2/O3最適化が自動的にO1またはO0に下げられています**。

## 無効化メカニズム

### 1. MIRPatternDetector（MIRレベル検出）

**ファイル:** `src/codegen/llvm/optimizations/mir_pattern_detector.hpp`

#### 検出パターンと対応

| パターン | 条件 | アクション |
|---------|------|-----------|
| **複雑なクロージャ** | closure_count > 5 または lambda_count > 3 | O3 → O1 |
| **iter_closureパターン** | イテレータ + クロージャ + map/filter | O2/O3 → O0 |
| **main関数の複雑度** | statements > 100 | O3 → O2 |

```cpp
// 複雑なクロージャパターン検出
if (closure_count > 5 || lambda_count > 3) {
    std::cerr << "[MIR_PATTERN] 複雑なクロージャパターンを検出\n";
    if (requested_level >= 3) {
        std::cerr << "[MIR_PATTERN] O3からO1に最適化レベルを下げます\n";
        return 1;  // O1に強制変更
    }
}

// iter_closureパターン検出
if (has_iter_closure_pattern) {
    if (requested_level >= 2) {
        std::cerr << "[MIR_PATTERN] 最適化レベルをO0に変更します\n";
        return 0;  // 最適化を完全に無効化
    }
}
```

### 2. OptimizationPassLimiter（LLVMレベル検出）

**ファイル:** `src/codegen/llvm/optimizations/pass_limiter.hpp`

#### 複雑度スコア計算

```cpp
// 複雑度スコア = クロージャ×10 + イテレータ×15 + ラムダ×8 + ブロック数ペナルティ
complexity_score += closure_count * 10;
complexity_score += iterator_count * 15;
complexity_score += lambda_count * 8;
complexity_score += (max_block_count > 50) ? 50 : 0;
```

#### 自動調整ルール

| 複雑度スコア | 条件 | アクション |
|-------------|------|-----------|
| **高複雑度** | score > 100 | O3 → O1 |
| **中複雑度** | score > 50 | O3 → O2 |
| **iter_closure検出** | 特定パターン | O2/O3 → O0 |

```cpp
if (complexity_score > HIGH_COMPLEXITY_THRESHOLD) {  // 100
    std::cerr << "[OPT_LIMITER] 最適化レベルを O3 から O1 に下げます\n";
    return 1;  // O1に下げる
}

if (complexity_score > MEDIUM_COMPLEXITY_THRESHOLD) {  // 50
    std::cerr << "[OPT_LIMITER] 最適化レベルを O3 から O2 に下げます\n";
    return 2;  // O2に下げる
}
```

### 3. 適用順序

**ファイル:** `src/codegen/llvm/native/codegen.hpp`

```cpp
// 1. MIRレベルで検出
int adjusted_level =
    MIRPatternDetector::adjustOptimizationLevel(program, options.optimizationLevel);

// 2. LLVMレベルで再検出
if (options.optimizationLevel > 0) {
    adjusted_level = OptimizationPassLimiter::adjustOptimizationLevel(
        context->getModule(), options.optimizationLevel);
}
```

## 影響を受けるコードパターン

### 1. 明示的に無効化されるケース

```cm
// 例1: 多数のクロージャ（6個以上）
auto f1 = () => { ... };
auto f2 = () => { ... };
auto f3 = () => { ... };
auto f4 = () => { ... };
auto f5 = () => { ... };
auto f6 = () => { ... };  // ← ここでO3→O1

// 例2: イテレータとクロージャの組み合わせ
list.iter()
    .map(x => x * 2)      // ← iter_closureパターン
    .filter(x => x > 10)  // ← O2/O3→O0
    .collect();

// 例3: 複雑なmain関数
int main() {
    // 100個以上のステートメント → O3→O2
}
```

### 2. 関数名による検出

以下の文字列を含む関数名は自動的に検出対象:

- **クロージャ系**: `closure`, `lambda`, `anon`, `$_`
- **イテレータ系**: `iter`, `Iterator`, `next`
- **高階関数系**: `map`, `filter`, `fold`, `reduce`

## 実際の影響

### テスト結果（tlp）

```bash
make tlp0  # O0: 296/296 成功
make tlp1  # O1: 266/296 成功
make tlp2  # O2: 266/296 成功（実際は多くがO1/O0で実行）
make tlp3  # O3: 266/296 成功（実際は多くがO1/O0で実行）
```

**重要:** O2/O3を指定しても、多くのケースで自動的にO1またはO0に下げられている。

## 問題点と影響

### 1. パフォーマンスへの影響

- **期待される最適化が適用されない**
  - インライン化
  - ベクトル化
  - ループ展開
  - 定数伝播

### 2. ユーザーの期待との乖離

- `-O3`を指定してもO1やO0で実行される
- デバッグメッセージなしには気づかない

### 3. 過度に保守的な判定

- 通常のコードでも「複雑」と判定される
- ジェネリクスを使うと自動的に複雑度が上がる

## 推奨される対処法

### 1. 即座の対処（ユーザー向け）

```bash
# パターン検出を確認
./cm compile --debug -O3 your_file.cm 2>&1 | grep -E "MIR_PATTERN|OPT_LIMITER"

# 強制的にO3を適用したい場合（現在は方法なし）
# → コード側で回避が必要
```

### 2. コード側の回避策

```cm
// クロージャを減らす
// 関数名に特定の文字列を含めない
// main関数を分割する
```

### 3. 根本的な解決案

1. **閾値の調整**
   - HIGH_COMPLEXITY_THRESHOLD: 100 → 200
   - MEDIUM_COMPLEXITY_THRESHOLD: 50 → 100
   - クロージャ上限: 5 → 10

2. **オプトアウト機能の追加**
   ```cpp
   // --force-optimization フラグの追加
   if (!options.force_optimization) {
       // パターン検出を実行
   }
   ```

3. **警告レベルの導入**
   - 自動無効化ではなく警告のみ
   - ユーザーが選択可能に

## まとめ

現在のCm言語コンパイラでは、**O2/O3最適化は事実上無効化されています**。これは無限ループ対策として実装されたものですが、過度に保守的であり、通常のコードでも最適化が適用されない状況です。

### 影響度

- **パフォーマンス**: 🔴 重大（最大10倍の性能低下の可能性）
- **互換性**: 🟡 中程度（動作は正しいが遅い）
- **ユーザビリティ**: 🔴 重大（期待と異なる動作）

### 推奨アクション

1. **短期**: 閾値の緩和（100→200、50→100）
2. **中期**: オプトアウト機能の実装
3. **長期**: より洗練された複雑度判定アルゴリズム

---

**調査完了:** 2026-01-11
**結論:** O2/O3は自動的に無効化されており、実質的にO1/O0で動作している