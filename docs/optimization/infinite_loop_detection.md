# MIR最適化無限ループ検出と対策

## 問題の症状
- LLVMコード生成時に無限ループ
- プロセスが終了不可能
- メモリ/ディスクへの継続的な書き込み

## 考えられる原因

### 1. 最適化パスの循環依存
```
ConstantFolding → CopyPropagation → GVN → ConstantFolding（ループ）
```

### 2. SimplifyControlFlowの無限変換
- ブロックマージ → 新たな最適化機会 → ブロック分割 → マージ...

### 3. FunctionInliningの再帰インライン化
- 相互再帰関数の無限展開
- インライン閾値の不適切な設定

## 対策実装案

### 1. 反復回数制限の実装

```cpp
// src/mir/optimizations/optimization_pipeline.hpp
class OptimizationPipeline {
    static constexpr size_t MAX_ITERATIONS = 10;  // 最大反復回数
    static constexpr size_t MAX_PASSES_PER_ITERATION = 100;  // パスあたり最大実行回数

public:
    bool run_until_fixpoint(MirProgram& program) {
        size_t iteration = 0;
        bool changed = true;

        while (changed && iteration < MAX_ITERATIONS) {
            changed = false;
            size_t pass_count = 0;

            for (auto& pass : passes) {
                if (pass_count++ > MAX_PASSES_PER_ITERATION) {
                    std::cerr << "警告: 最適化パスの実行回数が上限に達しました\n";
                    break;
                }

                bool local_changed = pass->run(program);
                changed |= local_changed;

                // 変更がなければ早期終了
                if (!local_changed && early_exit) {
                    break;
                }
            }

            iteration++;
        }

        if (iteration >= MAX_ITERATIONS) {
            std::cerr << "警告: 最適化の反復回数が上限に達しました\n";
        }

        return iteration > 1;  // 変更があったか
    }
};
```

### 2. 変更検出の改善

```cpp
// src/mir/optimizations/optimization_pass.hpp
class OptimizationPass {
protected:
    // MIRのハッシュ値で変更を検出
    size_t compute_mir_hash(const MirFunction& func) {
        size_t hash = 0;
        for (const auto& block : func.basic_blocks) {
            if (!block) continue;
            hash ^= std::hash<size_t>{}(block->statements.size());
            hash ^= std::hash<BlockId>{}(block->id);
            // 各ステートメントのハッシュも計算
        }
        return hash;
    }

public:
    bool run(MirFunction& func) {
        size_t before_hash = compute_mir_hash(func);

        // 最適化実行
        bool changed = run_impl(func);

        // ハッシュ値で実際の変更を確認
        size_t after_hash = compute_mir_hash(func);
        if (before_hash == after_hash && changed) {
            // 変更フラグが立っているがハッシュ値が同じ
            // → 無駄な変更の可能性
            debug_msg("最適化パス {} が変更を報告しましたが、実際には変更されていません", name());
            return false;
        }

        return changed;
    }

    virtual bool run_impl(MirFunction& func) = 0;
};
```

### 3. タイムアウト機構

```cpp
// src/mir/optimizations/timeout_guard.hpp
#include <chrono>
#include <thread>
#include <atomic>

class TimeoutGuard {
    std::atomic<bool> timeout_flag{false};
    std::thread timeout_thread;

public:
    TimeoutGuard(std::chrono::seconds timeout_duration) {
        timeout_thread = std::thread([this, timeout_duration]() {
            std::this_thread::sleep_for(timeout_duration);
            timeout_flag.store(true);
        });
    }

    ~TimeoutGuard() {
        if (timeout_thread.joinable()) {
            timeout_thread.join();
        }
    }

    bool is_timeout() const {
        return timeout_flag.load();
    }
};

// 使用例
bool run_optimization_with_timeout(MirProgram& program) {
    TimeoutGuard guard(std::chrono::seconds(30));  // 30秒タイムアウト

    while (!guard.is_timeout()) {
        bool changed = run_optimization_pass(program);
        if (!changed) break;
    }

    if (guard.is_timeout()) {
        std::cerr << "エラー: 最適化がタイムアウトしました\n";
        return false;
    }

    return true;
}
```

### 4. 最適化パスの相互作用管理

```cpp
// src/mir/optimizations/pass_dependency.hpp
class PassDependencyManager {
    struct PassInfo {
        std::string name;
        std::set<std::string> invalidates;  // このパスが無効化する他のパス
        std::set<std::string> depends_on;   // 依存するパス
        size_t run_count = 0;

        bool can_run_after(const std::string& prev_pass) {
            // 前のパスが自分を無効化していないか確認
            return true;  // 簡略化
        }
    };

    std::map<std::string, PassInfo> pass_infos;

public:
    bool should_run_pass(const std::string& pass_name,
                         const std::string& prev_pass) {
        auto& info = pass_infos[pass_name];

        // 実行回数制限
        if (info.run_count > 5) {
            return false;  // 同じパスを5回以上実行しない
        }

        // 依存関係チェック
        if (!info.can_run_after(prev_pass)) {
            return false;
        }

        info.run_count++;
        return true;
    }
};
```

### 5. デバッグ・診断機能

```cpp
// src/mir/optimizations/diagnostic.hpp
class OptimizationDiagnostic {
    struct PassStatistics {
        std::string name;
        size_t run_count = 0;
        std::chrono::milliseconds total_time{0};
        size_t changes_made = 0;
    };

    std::vector<PassStatistics> stats;

public:
    void record_pass_run(const std::string& name,
                        std::chrono::milliseconds duration,
                        bool changed) {
        // 統計を記録
    }

    void print_report() {
        std::cout << "\n=== 最適化統計 ===\n";
        for (const auto& stat : stats) {
            std::cout << stat.name << ":\n";
            std::cout << "  実行回数: " << stat.run_count << "\n";
            std::cout << "  実行時間: " << stat.total_time.count() << "ms\n";
            std::cout << "  変更回数: " << stat.changes_made << "\n";
        }

        // 異常検出
        for (const auto& stat : stats) {
            if (stat.run_count > 10) {
                std::cerr << "警告: " << stat.name
                         << " が異常に多く実行されています\n";
            }
            if (stat.total_time > std::chrono::seconds(5)) {
                std::cerr << "警告: " << stat.name
                         << " の実行時間が長すぎます\n";
            }
        }
    }
};
```

## 実装優先順位

### 即座に実装すべき（1日）
1. **反復回数制限** - 最も簡単で効果的
2. **タイムアウト機構** - 安全弁として必須

### 短期的に実装（1週間）
3. **変更検出の改善** - 無駄な反復を防ぐ
4. **デバッグ診断** - 問題の特定に必要

### 中期的に実装（2週間）
5. **パス依存関係管理** - より洗練された制御

## テスト戦略

```cm
// tests/optimization/infinite_loop_test.cm

// 相互再帰（インライン化の無限ループテスト）
int foo(int n) {
    if (n == 0) return 1;
    return bar(n - 1);
}

int bar(int n) {
    if (n == 0) return 2;
    return foo(n - 1);
}

// 複雑な制御フロー（SimplifyCFGのテスト）
int complex_cfg(int x) {
    while (true) {
        if (x > 10) {
            if (x > 20) {
                break;
            }
            x = x - 1;
        } else {
            x = x + 1;
        }
    }
    return x;
}
```

## まとめ

無限ループ問題への対策：
1. **即座の対策**: 反復回数制限とタイムアウト
2. **根本的対策**: 変更検出の改善とパス管理
3. **診断機能**: 問題の早期発見と分析

これらにより、最適化の暴走を防ぎ、安定したコンパイルを実現できます。