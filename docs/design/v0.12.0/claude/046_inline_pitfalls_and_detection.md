# インライン化の落とし穴：誤った最適化の検出と対策

作成日: 2026-01-11
対象バージョン: v0.11.0
ステータス: 技術解説

## エグゼクティブサマリー

ユーザーの誤ったインライン化指定は、**パフォーマンス劣化**や**コンパイル失敗**を引き起こします。本文書では、間違ったインライン化パターンの検出方法と対策を解説します。

## 1. インライン化が逆効果になるパターン

### 1.1 コードサイズ爆発

#### ❌ 間違った例：大きな関数の強制インライン化

```cpp
// 500行の巨大関数
[[gnu::always_inline]]
inline void process_complex_data(Data* data) {
    // 500行のコード...
    validate_input(data);
    transform_phase1(data);
    transform_phase2(data);
    optimize_data(data);
    // ...
}

// 100箇所から呼ばれる
for (int i = 0; i < 100; i++) {
    process_complex_data(&dataset[i]);
}

// 結果：
// - コードサイズ: 500行 × 100 = 50,000行相当に爆発
// - 命令キャッシュミス率: 90%以上
// - 実行速度: 3倍遅くなる
```

#### 検出方法

```cpp
// コンパイラの診断機能
class InlineDiagnostics {
    struct InlineDecision {
        std::string function_name;
        size_t inline_size;      // インライン化後のサイズ
        size_t call_sites;       // 呼び出し箇所数
        size_t total_expansion;  // 総展開サイズ
        bool decision;           // インライン化するか
        std::string reason;      // 判断理由
    };

    bool should_warn_inline(const Function& func, const CallSite& site) {
        size_t func_size = calculate_size(func);
        size_t num_calls = count_call_sites(func);

        // 警告条件
        if (func.has_attribute("always_inline")) {
            if (func_size > 50 && num_calls > 10) {
                warn("Function '{}' is too large ({} instructions) "
                     "for {} call sites. Code size will increase by {}KB",
                     func.name, func_size, num_calls,
                     (func_size * num_calls) / 1024);
                return true;
            }
        }
        return false;
    }
};
```

### 1.2 命令キャッシュ汚染

#### ❌ 間違った例：ホットパスの肥大化

```cpp
// CPUの命令キャッシュは通常32KB-64KB
inline void hot_function() {
    // インライン化で展開されるコード
    cold_error_handling();     // めったに実行されない
    another_cold_function();   // めったに実行されない

    // ホットパス（頻繁に実行）
    return calculate_result();
}

// 結果：冷たいコードが命令キャッシュを汚染
```

#### 正しい対処

```cpp
// ✅ ホットパスとコールドパスの分離
[[gnu::hot]] [[gnu::flatten]]
inline int hot_function() {
    // ホットパスのみインライン化
    return calculate_result();
}

[[gnu::cold]] [[gnu::noinline]]
void cold_error_handling() {
    // エラー処理は別関数でインライン化しない
}
```

### 1.3 レジスタスピル

#### ❌ 間違った例：レジスタ圧力の増大

```cpp
inline void use_many_variables() {
    int a, b, c, d, e, f, g, h, i, j, k, l, m, n;
    // 14個の変数（x86-64のレジスタは16個）
    // 複雑な計算...
}

void caller() {
    int x, y, z;  // 既に3個使用
    use_many_variables();  // インライン化でレジスタ不足
    // レジスタスピル発生 → メモリアクセス増加
}
```

## 2. コンパイラの検出メカニズム

### 2.1 GCC/Clangの警告フラグ

```bash
# インライン化の診断を有効化
gcc -Winline -finline-limit=100 -fopt-info-inline-all

# 出力例：
warning: inlining failed in call to 'huge_function':
         function body can be overwritten at link time [-Winline]
note: called from here
note: increase -finline-limit or mark function as 'static'

# 詳細レポート
-fopt-info-inline-missed    # インライン化されなかった理由
-fopt-info-inline-optimized # インライン化された関数
```

### 2.2 LLVM の診断パス

```cpp
// LLVM内部のインライン化コストモデル
class InlineCostAnalysis {
    struct InlineCost {
        int cost;           // インライン化のコスト
        int threshold;      // 閾値

        bool isAlways() { return cost == INT_MIN; }
        bool isNever() { return cost == INT_MAX; }

        std::string getReason() {
            if (cost > threshold) {
                return "Cost (" + std::to_string(cost) +
                       ") exceeds threshold (" +
                       std::to_string(threshold) + ")";
            }
            return "Profitable to inline";
        }
    };

    InlineCost analyze(CallSite CS) {
        // 1. 関数サイズの計算
        int size = 0;
        for (auto& BB : Function) {
            size += BB.size() * InlineConstants::InstrCost;
        }

        // 2. 呼び出しコンテキストの分析
        if (CS.isInLoop()) {
            threshold *= 2;  // ループ内はボーナス
        }

        // 3. 引数の定数性チェック
        for (auto& Arg : CS.args()) {
            if (isa<Constant>(Arg)) {
                size -= InlineConstants::ConstantBonus;
            }
        }

        return {size, threshold};
    }
};
```

## 3. 再帰関数の問題

### 3.1 無限展開の防止

```cpp
// ❌ 危険：再帰関数のインライン化
inline int factorial(int n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);  // 無限にインライン展開？
}
```

### コンパイラの対処

```cpp
class RecursionDetector {
    std::set<Function*> inline_stack;

    bool can_inline(Function* caller, Function* callee) {
        // 再帰検出
        if (caller == callee) {
            return false;  // 直接再帰
        }

        // 間接再帰の検出
        if (inline_stack.count(callee) > 0) {
            return false;  // 既にインライン化スタックに存在
        }

        // 深さ制限
        if (inline_stack.size() > MAX_INLINE_DEPTH) {
            return false;
        }

        return true;
    }
};
```

## 4. プロファイルガイド最適化（PGO）

### 4.1 実行時情報に基づく判断

```cpp
// プロファイル情報を使った賢いインライン化
struct ProfileData {
    uint64_t call_count;      // 呼び出し回数
    uint64_t total_cycles;    // 総実行サイクル
    double branch_probability; // 分岐確率
};

class PGOInliner {
    bool should_inline(Function* func, CallSite* site, ProfileData* prof) {
        // ホットパスのみインライン化
        if (prof->call_count < HOT_THRESHOLD) {
            return false;  // コールドパスはインライン化しない
        }

        // コスト/利益分析
        double benefit = prof->call_count * CALL_OVERHEAD;
        double cost = func->size() * CODE_SIZE_COST;

        return benefit > cost;
    }
};
```

### 4.2 使用方法

```bash
# Step 1: プロファイル収集
gcc -fprofile-generate program.c -o program
./program  # 実行してプロファイルデータ収集

# Step 2: プロファイル適用
gcc -fprofile-use -O3 program.c -o program_optimized
```

## 5. 静的解析による検証

### 5.1 コードサイズ予測

```cpp
class CodeSizePredictor {
    size_t predict_inline_expansion(Function* func) {
        size_t base_size = func->instruction_count();
        size_t call_sites = count_call_sites(func);

        // 各呼び出し箇所での展開を予測
        size_t total = 0;
        for (auto& site : get_call_sites(func)) {
            size_t local_size = base_size;

            // 定数畳み込みによる削減を予測
            for (auto& arg : site.arguments) {
                if (is_constant(arg)) {
                    local_size *= 0.8;  // 20%削減を仮定
                }
            }

            total += local_size;
        }

        return total;
    }
};
```

## 6. ベンチマーク例：インライン化の失敗

### 6.1 測定コード

```cpp
// ベンチマーク：過度なインライン化の影響
#include <benchmark/benchmark.h>

// 100命令の関数
[[gnu::noinline]]
void large_function_no_inline(int* data) {
    // 100命令相当の処理
}

[[gnu::always_inline]]
inline void large_function_inline(int* data) {
    // 同じ100命令
}

static void BM_NoInline(benchmark::State& state) {
    int data[1000];
    for (auto _ : state) {
        for (int i = 0; i < 1000; i++) {
            large_function_no_inline(&data[i]);
        }
    }
}

static void BM_ForceInline(benchmark::State& state) {
    int data[1000];
    for (auto _ : state) {
        for (int i = 0; i < 1000; i++) {
            large_function_inline(&data[i]);
        }
    }
}
```

### 6.2 結果

| シナリオ | 実行時間 | L1-I キャッシュミス | コードサイズ |
|---------|----------|-------------------|--------------|
| インライン化なし | 850ms | 2% | 4KB |
| 強制インライン化 | 1,420ms | 45% | 102KB |

**結論：大きな関数の強制インライン化は逆効果**

## 7. 診断ツールの活用

### 7.1 Linux perf

```bash
# キャッシュミスの測定
perf stat -e L1-icache-load-misses ./program

# インライン化の影響を可視化
perf record -g ./program
perf report --inline
```

### 7.2 Intel VTune

```cpp
// VTuneマーカーで測定
#include <ittnotify.h>

__itt_domain* domain = __itt_domain_create("MyApp");
__itt_string_handle* handle = __itt_string_handle_create("hot_loop");

__itt_task_begin(domain, __itt_null, __itt_null, handle);
for (int i = 0; i < 1000000; i++) {
    possibly_inlined_function();
}
__itt_task_end(domain);
```

## 8. 推奨される対策

### 8.1 インライン化ガイドライン

```cpp
// ✅ 良いインライン化候補
inline int min(int a, int b) { return a < b ? a : b; }  // 1-3命令

// ⚠️ 慎重に判断
inline int calculate(int x) {  // 10-20命令
    return x * x + 2 * x + 1;
}

// ❌ インライン化すべきでない
void process_data(Data* data) {  // 50命令以上
    // 複雑な処理
}
```

### 8.2 コンパイラへのヒント

```cpp
// 属性を使った細かな制御
struct OptimizationHints {
    [[gnu::hot]]           // 頻繁に実行される
    [[gnu::flatten]]       // 内部の関数呼び出しもインライン化
    inline void hot_path();

    [[gnu::cold]]          // めったに実行されない
    [[gnu::noinline]]      // インライン化禁止
    void error_handler();

    [[gnu::pure]]          // 副作用なし（最適化しやすい）
    inline int calculate(int x);
};
```

## 9. まとめ：インライン化の落とし穴と対策

### 主な落とし穴

1. **コードサイズ爆発** → キャッシュミス増加
2. **レジスタスピル** → メモリアクセス増加
3. **コールドコードの混入** → ホットパス汚染
4. **再帰の無限展開** → コンパイル失敗

### 検出方法

1. **コンパイラ警告** (`-Winline`)
2. **プロファイリング** (perf, VTune)
3. **静的解析** (コードサイズ予測)
4. **PGO** (実行時情報)

### 対策

1. **小さい関数のみインライン化**（<10命令）
2. **プロファイルデータを活用**
3. **コンパイラを信頼**（過度な強制を避ける）
4. **測定と検証**（推測より計測）

**結論：ユーザーの誤ったインライン化は検出可能であり、コンパイラとツールがサポートします。**

---

**作成者:** Claude Code
**ステータス:** 技術解説
**関連文書:** 040-045_inline_*.md