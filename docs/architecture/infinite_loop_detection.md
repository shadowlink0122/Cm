[English](infinite_loop_detection.en.html)

# 無限ループ検出戦略

## エグゼクティブサマリー
コード生成と最適化における無限ループを防ぐため、バッファベース生成と多層防御を実装する。

## 問題領域の特定

### 1. コード生成での無限ループ
```cpp
// 危険な例
while (block->has_successors()) {
    generate_block(block);
    block = block->next();  // nextが循環している可能性
}
```

### 2. 最適化での無限ループ
```cpp
// 最適化パスが相互に変更を繰り返す
Pass A: x + 0 → x
Pass B: x → x + 0 (別の理由で)
// 永遠に繰り返す
```

### 3. LLVM最適化での無限ループ
```cpp
// カスタムLLVM最適化が終了しない
while (changed) {
    changed = optimize_instructions();  // 常にtrue
}
```

## 3層防御アーキテクチャ

### Layer 1: バッファベース生成（根本対策）
```cpp
class SafeCodeGenerator {
    BufferedCodeGenerator buffer;

    bool generate() {
        buffer.begin_generation();

        // サイズ/時間制限で自動停止
        while (has_work() && buffer.check_limits()) {
            if (!buffer.append(generate_next())) {
                return false;  // 制限到達
            }
        }

        return buffer.end_generation();
    }
};
```

**利点:**
- IO前にメモリで完結
- サイズ制限で物理的に停止
- 時間制限でハング防止

### Layer 2: パターン検出（早期発見）
```cpp
class PatternDetector {
    // 最近のN個の状態を記録
    CircularBuffer<State> recent_states;

    bool detect_cycle(const State& current) {
        // 同じ状態が繰り返されている？
        if (recent_states.contains(current)) {
            return true;  // 循環検出
        }

        // パターンマッチング
        if (is_oscillating_pattern()) {
            return true;  // A→B→A→B
        }

        return false;
    }
};
```

**検出パターン:**
- 完全一致（同じ状態）
- 振動（A↔B）
- 周期的（A→B→C→A）

### Layer 3: 統計的検出（補完的）
```cpp
class StatisticalMonitor {
    // 変更数の統計
    RunningStats change_stats;

    bool is_likely_infinite() {
        // 変更数が収束しない
        if (change_stats.variance() < EPSILON) {
            // 分散が小さい = 同じ変更の繰り返し
            return true;
        }

        // 異常な実行時間
        if (elapsed_time > expected_time * 10) {
            return true;
        }

        return false;
    }
};
```

## 実装箇所と優先順位

### Priority 1: MIR→LLVM変換（最も危険）
```cpp
// src/codegen/llvm/core/mir_to_llvm.cpp
class MirToLLVMConverter : public BufferedCodeGenerator {
    // バッファベースに変更
    bool convert_function(const MirFunction& func) {
        // 基本ブロックの訪問記録
        std::set<BlockId> visited;

        while (!worklist.empty()) {
            auto block = worklist.pop();

            // 循環検出
            if (visited.count(block.id) > MAX_VISITS) {
                return error("基本ブロックの循環");
            }

            // サイズチェック
            if (!check_limits()) {
                return error("コードサイズ超過");
            }

            generate_block(block);
            visited[block.id]++;
        }
    }
};
```

### Priority 2: 最適化パス（頻発）
```cpp
// src/mir/optimizations/optimization_pass.hpp
class OptimizationPassManager {
    SmartConvergenceManager convergence;

    void run_until_fixpoint(Program& prog) {
        for (int iter = 0; iter < SAFETY_LIMIT; ++iter) {
            int changes = run_all_passes(prog);

            // パターンベース収束判定
            auto state = convergence.add_iteration(changes);

            switch (state) {
                case CONVERGED:
                    return;  // 正常終了
                case CYCLE_DETECTED:
                    warn("最適化の循環");
                    return;  // 安全に停止
            }
        }
    }
};
```

### Priority 3: LLVM最適化（複雑）
```cpp
// src/codegen/llvm/optimizations/
class CustomLLVMOptimizer {
    // 命令の変更を追跡
    InstructionTracker tracker;

    bool optimize() {
        int rounds = 0;

        while (has_opportunities()) {
            if (++rounds > MAX_ROUNDS) {
                return false;  // 強制終了
            }

            // 同じ命令の繰り返し変更を検出
            if (tracker.is_ping_ponging()) {
                break;  // 振動検出
            }

            apply_optimizations();
        }
    }
};
```

## 実装チェックリスト

### Phase 1: 基盤整備（1-2日）
- [ ] BufferedCodeGenerator を core に導入
- [ ] 既存のストリーミング箇所を特定
- [ ] サイズ/時間制限の妥当な値を設定

### Phase 2: MIR→LLVM（2-3日）
- [ ] mir_to_llvm.cpp をバッファベースに変更
- [ ] BlockMonitor を PatternDetector に置換
- [ ] 基本ブロックの訪問回数制限を実装

### Phase 3: 最適化パス（1-2日）
- [ ] SmartConvergenceManager を統合
- [ ] 各最適化パスに変更カウント機能を追加
- [ ] 収束統計のロギング

### Phase 4: LLVM最適化（2-3日）
- [ ] カスタムLLVM最適化を一時無効化
- [ ] 命令追跡機構を実装
- [ ] 段階的に再有効化とテスト

## 設定値の指針

### コード生成
```cpp
// 小規模（<1000行）
max_bytes = 10 * MB;
max_lines = 100'000;
max_time = 5s;

// 中規模（<10000行）
max_bytes = 100 * MB;
max_lines = 1'000'000;
max_time = 30s;

// 大規模（>10000行）
max_bytes = 500 * MB;
max_lines = 10'000'000;
max_time = 300s;
```

### 最適化
```cpp
// 反復回数
max_iterations = 20;      // 通常
safety_limit = 100;       // 絶対上限

// 収束判定
same_change_threshold = 3;  // 3回同じ変更で収束
minor_change_threshold = 5; // 5変更以下は軽微
```

## テスト戦略

### 1. 単体テスト
```cpp
TEST(BufferedCodeGen, StopsAtSizeLimit) {
    BufferedCodeGenerator gen;
    gen.set_limits({.max_bytes = 100});

    // 無限ループを試みる
    while (true) {
        if (!gen.append("x")) break;
    }

    EXPECT_TRUE(gen.has_generation_error());
    EXPECT_LE(gen.current_buffer_size(), 100);
}
```

### 2. 統合テスト
```cpp
TEST(Optimization, DetectsCycle) {
    // 循環する最適化を意図的に作成
    PassManager mgr;
    mgr.add_pass(new FlipPass());  // x → !x
    mgr.add_pass(new FlipPass());  // !x → x

    auto result = mgr.run_with_timeout(program, 1s);
    EXPECT_EQ(result.reason, "cycle_detected");
}
```

### 3. ストレステスト
```bash
# 巨大ファイルでテスト
cm compile huge_file.cm --debug 2>&1 | grep "制限"

# 複雑な最適化でテスト
cm compile complex.cm --max-opt 2>&1 | grep "収束"
```

## モニタリング

### ログ出力
```
[CODEGEN] 生成開始: main.cm
[CODEGEN] ブロック 'BB0' 処理中 (1234 bytes)
[CODEGEN] 警告: サイズが 50MB を超過
[CODEGEN] エラー: 制限到達 (100MB/30s)
```

### 統計情報
```
=== コード生成統計 ===
総生成サイズ: 45.2 MB
総行数: 1,234,567
生成時間: 12.3 秒
中断理由: サイズ制限
```

## 成功指標

1. **無限ループゼロ** - 本番環境でのハング報告なし
2. **誤検出率 < 1%** - 正常なコードを誤って停止しない
3. **パフォーマンス影響 < 5%** - バッファリングのオーバーヘッド

## まとめ

無限ループ検出は「予防」「検出」「制限」の3段階で実現：

1. **予防**: バッファベース生成で根本解決
2. **検出**: パターン認識で早期発見
3. **制限**: 物理的制限で確実に停止

この多層防御により、コンパイラの堅牢性を大幅に向上させる。