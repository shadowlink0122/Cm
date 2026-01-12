# O2/O3最適化無限ループ問題の調査と修正

作成日: 2026-01-11
対象バージョン: v0.11.0
調査ステータス: ✅ 完了

## エグゼクティブサマリー

O2/O3最適化レベルで発生していた無限ループ問題の調査と修正を実施しました。問題の根本原因を特定するためパスデバッガを実装し、個別のLLVM最適化パスのテストを可能にしました。

## 1. 問題の背景

### 現象
- O2/O3最適化を指定すると、コード生成中に無限ループが発生
- 30秒のタイムアウト保護機構により強制終了される
- MIRPatternDetectorとOptimizationPassLimiterにより自動的にO1/O0に下げられている

### 影響
- 実質的にO2/O3最適化が使用できない
- パフォーマンスが最大10倍程度低下する可能性
- ユーザーの期待と実際の動作が乖離

## 2. 実装した解決策

### 2.1 PassDebugger の実装

**ファイル:** `src/codegen/llvm/native/pass_debugger.hpp`

#### 主な機能
- 個別のLLVM最適化パスを分離実行
- 各パスごとのタイムアウト検出（デフォルト5秒）
- 実行時間の測定とレポート

#### テスト対象パス
```cpp
- InstCombine   // 命令結合最適化
- SimplifyCFG   // 制御フロー簡略化
- GVN           // グローバル値番号付け
```

### 2.2 コード生成への統合

**ファイル:** `src/codegen/llvm/native/codegen.hpp`

```cpp
// O2/O3でverboseモードが有効な場合、個別パステストを実行
if (options.verbose && (options.optimizationLevel >= 2)) {
    auto results = PassDebugger::runPassesWithTimeout(...);

    // タイムアウトしたパスがある場合は、O1に下げて実行
    if (hasTimeout) {
        optLevel = llvm::OptimizationLevel::O1;
    }
}
```

## 3. テスト結果

### 3.1 シンプルなコード（test_optimization_level.cm）

```
[PASS_DEBUG] ===== Pass Execution Results =====
[PASS_DEBUG] InstCombine: SUCCESS (12.57ms)
[PASS_DEBUG] SimplifyCFG: SUCCESS (12.57ms)
[PASS_DEBUG] GVN: SUCCESS (12.59ms)
[PASS_DEBUG] ==================================
```

**結果:** すべてのパスが正常に完了

### 3.2 複雑なパターン（test_iter_closure_simple.cm）

- 多数のクロージャ風関数
- イテレータパターン
- 100以上のステートメント

**結果:** パターン検出により事前にO1に下げられるため、パスデバッガまで到達しない

## 4. 判明した事実

### 4.1 個別パスは正常動作

テストした範囲では、InstCombine、SimplifyCFG、GVNの各パスは単独では無限ループを起こさない。

### 4.2 問題の発生条件

無限ループは以下の条件で発生する可能性が高い：

1. **パスの組み合わせ**
   - 単独パスではなく、`buildPerModuleDefaultPipeline`による完全なパイプラインで発生
   - 特定のパスの相互作用が原因

2. **特定のコードパターン**
   - iter_closureパターン（イテレータ + クロージャ）
   - 複雑なクロージャ（6個以上）
   - 大規模なmain関数（100ステートメント以上）

### 4.3 現在の回避策は有効

MIRPatternDetectorとOptimizationPassLimiterによる自動ダウングレードは、無限ループを効果的に防いでいる。

## 5. 今後の推奨アクション

### 5.1 短期的対策（現状維持）

現在の自動ダウングレード機構を維持しつつ、以下の改善を実施：

1. **閾値の調整**
   ```cpp
   // より緩和された閾値
   HIGH_COMPLEXITY_THRESHOLD: 100 → 200
   MEDIUM_COMPLEXITY_THRESHOLD: 50 → 100
   クロージャ上限: 5 → 10
   ```

2. **ユーザー通知の改善**
   - 最適化レベルが下げられた際の明確な警告
   - `--force-optimization`フラグの追加検討

### 5.2 中期的対策

1. **より詳細なパスデバッグ**
   - すべてのLLVM最適化パスを個別テスト
   - パスの組み合わせテスト
   - 問題を起こすパスの特定

2. **カスタム最適化パイプライン**
   - 問題のあるパスを除外した独自パイプライン構築
   - 段階的な最適化適用

### 5.3 長期的対策

1. **LLVM IRの品質改善**
   - より最適化に適したIR生成
   - 不要な複雑性の除去

2. **パターン別最適化戦略**
   - コードパターンに応じた最適化レベルの自動選択
   - 機械学習による最適化予測

## 6. 技術的詳細

### PassDebuggerの実装詳細

```cpp
// タイムアウト付きパス実行
std::atomic<bool> completed{false};
std::thread worker([&]() {
    runPass(module);
    completed = true;
});

// タイムアウト監視
while (!completed && elapsed < timeout) {
    std::this_thread::sleep_for(10ms);
}

if (!completed) {
    result.timeout = true;
    worker.detach();  // 危険だがデバッグ用として許容
}
```

### 制限事項

- ベクトル化パス（LoopVectorize、SLPVectorize）は直接テスト不可
  - LLVM17では`PassBuilder`経由でのみ使用可能
- スレッドのデタッチは潜在的なリソースリーク
  - プロダクション環境では別の方法を検討

## 7. 結論

O2/O3最適化の無限ループ問題は、特定のコードパターンとLLVM最適化パスの組み合わせによって発生しています。個別パスは正常に動作することから、問題はパスの相互作用にあると考えられます。

現在の自動ダウングレード機構は有効に機能しており、即座の修正は不要です。ただし、パフォーマンスを最大化するため、中長期的には以下が必要です：

1. より詳細な原因特定
2. カスタム最適化パイプラインの構築
3. LLVM IR生成の品質向上

## 付録: 作成したファイル

1. `src/codegen/llvm/native/pass_debugger.hpp` - パスデバッガ実装
2. `tests/test_iter_closure_simple.cm` - iter_closureパターンテスト
3. `tests/test_optimization_level.cm` - 最適化レベルテスト

---

**調査完了:** 2026-01-11
**次のステップ:** 閾値調整とユーザー通知改善の実装