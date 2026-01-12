# JITコンパイラ最適化手法

## 概要
Cm言語のインタープリタは現在Python比で100-150倍遅い状況です。JITコンパイラの導入により、この性能差を10-20倍程度まで改善できる可能性があります。本文書では、JITコンパイラで使用される主要な最適化手法をまとめます。

## 1. プロファイリングベース最適化

### ホットスポット検出
実行頻度の高い関数やループを検出し、優先的にJITコンパイルします。

```cpp
class HotspotDetector {
    struct FunctionProfile {
        uint64_t call_count = 0;
        uint64_t total_cycles = 0;
        bool is_hot = false;
    };

    // 実行回数が閾値を超えたらJITコンパイル
    void recordCall(FunctionId id) {
        if (++profiles[id].call_count > JIT_THRESHOLD) {
            scheduleJITCompilation(id);
        }
    }
};
```

**メリット:**
- コンパイルオーバーヘッドを最小化
- 重要なコードに最適化リソースを集中

## 2. 階層型コンパイル（Tiered Compilation）

段階的に最適化レベルを上げていく手法です。

```cpp
enum CompilationTier {
    TIER_0_INTERPRETER,      // 初期: インタープリタ実行
    TIER_1_SIMPLE_JIT,       // 簡易JIT（最適化なし）
    TIER_2_OPTIMIZED_JIT,    // 最適化JIT
    TIER_3_AGGRESSIVE_JIT    // 積極的最適化JIT
};

// 段階的に最適化レベルを上げる
void promoteFunction(FunctionId id) {
    auto& tier = function_tiers[id];
    if (shouldPromote(id)) {
        tier = static_cast<CompilationTier>(tier + 1);
        recompileAtTier(id, tier);
    }
}
```

**メリット:**
- 起動時間とピーク性能のバランス
- 実行パターンに応じた最適化

## 3. 型特殊化（Type Specialization）

実行時の型情報を使って、型固有の高速なコードを生成します。

```cpp
class TypeSpecializer {
    // 汎用版（型チェックあり）
    int add_generic(Value a, Value b) {
        if (a.isInt() && b.isInt()) {
            return a.asInt() + b.asInt();
        }
        // ... 他の型処理
    }

    // int特殊化版（実行時に生成、型チェック不要）
    int add_int_int(int a, int b) {
        return a + b;
    }
};
```

**メリット:**
- 型チェックのオーバーヘッド削減
- CPU分岐予測の改善

## 4. インライン展開（Inlining）

小さい関数を呼び出し元に展開し、関数呼び出しのオーバーヘッドを削減します。

```cpp
class InlineOptimizer {
    bool shouldInline(Function* func) {
        return func->instruction_count < INLINE_THRESHOLD
            && func->call_count > HOT_THRESHOLD;
    }

    void inlineFunction(CallSite* site, Function* callee) {
        // 関数呼び出しを実際のコードで置換
        replaceCallWithBody(site, callee->body);
    }
};
```

**メリット:**
- 関数呼び出しオーバーヘッドの削減
- さらなる最適化機会の創出

## 5. On-Stack Replacement (OSR)

実行中のループを最適化版に動的に置き換える技術です。

```cpp
class OSRManager {
    void performOSR(LoopContext* loop) {
        // 1. 現在のスタックフレーム状態を保存
        auto state = captureFrameState();

        // 2. 最適化されたコードを生成
        auto optimized = compileOptimized(loop);

        // 3. スタックを新しいコードに切り替え
        transferToOptimized(state, optimized);
    }
};
```

**メリット:**
- 長時間実行ループの即座の最適化
- プログラムの停止不要

## 6. トレーシングJIT（Tracing JIT）

実際の実行パスを記録し、そのパスに特化した最適化を行います。

```cpp
class TracingJIT {
    struct Trace {
        std::vector<Instruction> recorded_path;
        std::map<Value, Type> type_guards;
    };

    void recordTrace(ExecutionPath path) {
        if (isHotPath(path)) {
            Trace trace;
            for (auto& inst : path) {
                trace.recorded_path.push_back(inst);
                trace.type_guards[inst.value] = inst.runtime_type;
            }
            compileTrace(trace);
        }
    }
};
```

**メリット:**
- 実際の実行パターンに基づく最適化
- ループの境界を越えた最適化

## 7. 脱最適化（Deoptimization）

最適化の前提が崩れた場合に、安全に元の状態に戻す機構です。

```cpp
class DeoptimizationManager {
    void deoptimize(Function* func, DeoptReason reason) {
        if (reason == TYPE_ASSUMPTION_FAILED) {
            // 型推論が間違っていた場合
            invalidateCompiledCode(func);
            fallbackToInterpreter(func);

            // プロファイル情報を更新
            updateProfile(func, reason);
        }
    }
};
```

**メリット:**
- 積極的な投機的最適化が可能
- 正確性の保証

## 8. 投機的最適化（Speculative Optimization）

高確率のケースに対して最適化し、低確率ケースはフォールバックします。

```cpp
class SpeculativeOptimizer {
    void generateSpeculativeCode(Function* func) {
        // よくあるケースに最適化
        if (likely_int_operation) {
            // ガード付き高速パス
            emit("if (isInt(a) && isInt(b)) {");
            emit("  return fastIntAdd(a, b);");     // 高速版
            emit("} else {");
            emit("  return slowGenericAdd(a, b);");  // フォールバック
            emit("}");
        }
    }
};
```

**メリット:**
- 一般的なケースの高速化
- 柔軟性の維持

## 9. ループ最適化

ループ特有の最適化を適用します。

```cpp
class LoopOptimizer {
    // ループ不変式の移動
    void hoistInvariantCode(Loop* loop) {
        for (auto& inst : loop->body) {
            if (isInvariant(inst, loop)) {
                moveBeforeLoop(inst, loop);
            }
        }
    }

    // ループ展開
    void unrollLoop(Loop* loop) {
        if (loop->trip_count <= UNROLL_THRESHOLD) {
            replicateBody(loop, loop->trip_count);
        }
    }
};
```

**メリット:**
- ループオーバーヘッドの削減
- ベクトル化の機会創出

## 10. エスケープ解析（Escape Analysis）

オブジェクトのスコープを解析し、スタック割り当てを可能にします。

```cpp
class EscapeAnalysis {
    bool canAllocateOnStack(AllocationSite* site) {
        return !escapesFunction(site)
            && !storedToHeap(site);
    }

    void optimizeAllocation(AllocationSite* site) {
        if (canAllocateOnStack(site)) {
            // ヒープ割り当てをスタック割り当てに変換
            convertToStackAllocation(site);
        }
    }
};
```

**メリット:**
- GC圧力の削減
- メモリアクセスの高速化

## 11. キャッシュ戦略

コンパイル済みコードの再利用を最適化します。

```cpp
class CodeCache {
    struct CacheEntry {
        MIRFunction* source;
        void* compiled_code;
        CompilationTier tier;
        uint64_t hit_count;
    };

    void* lookupOrCompile(MIRFunction* func) {
        if (auto* entry = cache.find(func)) {
            entry->hit_count++;
            return entry->compiled_code;
        }
        return compileAndCache(func);
    }
};
```

**メリット:**
- 再コンパイルの回避
- メモリ効率の改善

## Cm言語への適用戦略

### 実装フェーズ案

#### Phase 1: 基礎インフラ（3ヶ月）
- ホットスポット検出
- 基本的なJITコンパイル機能
- コードキャッシュ

#### Phase 2: 基本最適化（3ヶ月）
- 型特殊化
- インライン展開
- 簡単なループ最適化

#### Phase 3: 高度な最適化（6ヶ月）
- 階層型コンパイル
- OSR
- トレーシングJIT

#### Phase 4: 投機的最適化（3ヶ月）
- 投機的最適化
- 脱最適化
- エスケープ解析

### 期待される性能改善

現状のベンチマーク結果から、以下の改善が期待できます：

| ベンチマーク | 現状（vs Python） | JIT導入後（予測） |
|------------|-----------------|-----------------|
| 素数判定 | 100-150x遅い | 10-20x遅い |
| フィボナッチ再帰 | 100-150x遅い | 5-10x遅い |
| 配列操作 | 50-80x遅い | 5-10x遅い |

### 実装例

```cpp
// Cm言語用の段階的JITコンパイラ
class CmJITCompiler {
    // 実行統計
    struct ExecutionStats {
        uint64_t execution_count = 0;
        std::vector<TypeInfo> observed_types;
        bool has_stable_types = false;
    };

    std::unordered_map<MIRFunction*, ExecutionStats> function_stats;
    std::unordered_map<MIRFunction*, void*> compiled_code;

    void execute(MIRFunction* func, ExecutionContext& ctx) {
        auto& stats = function_stats[func];
        stats.execution_count++;

        // 実行回数に応じて段階的に最適化
        if (stats.execution_count == TIER1_THRESHOLD) {
            // 簡易JITコンパイル（最適化なし）
            compileBasicJIT(func);

        } else if (stats.execution_count == TIER2_THRESHOLD) {
            // 型情報を収集
            collectTypeInfo(func, stats);

            // 型特殊化付きJIT
            if (stats.has_stable_types) {
                compileSpecializedJIT(func, stats.observed_types);
            }

        } else if (stats.execution_count == TIER3_THRESHOLD) {
            // 積極的最適化（インライン展開、ループ最適化）
            compileAggressiveJIT(func);
        }

        // コンパイル済みコードがあれば実行
        if (auto* code = compiled_code[func]) {
            executeNative(code, ctx);
        } else {
            // インタープリタで実行
            interpretMIR(func, ctx);
        }
    }

private:
    void compileBasicJIT(MIRFunction* func) {
        LLVMJITCompiler compiler;
        compiler.setOptLevel(0);  // 最適化なし
        auto* code = compiler.compile(func);
        compiled_code[func] = code;
    }

    void compileSpecializedJIT(MIRFunction* func,
                               const std::vector<TypeInfo>& types) {
        LLVMJITCompiler compiler;
        compiler.setOptLevel(2);
        compiler.setTypeHints(types);  // 型情報を提供
        auto* code = compiler.compile(func);
        compiled_code[func] = code;
    }

    void compileAggressiveJIT(MIRFunction* func) {
        LLVMJITCompiler compiler;
        compiler.setOptLevel(3);
        compiler.enableInlining(true);
        compiler.enableLoopOptimizations(true);
        auto* code = compiler.compile(func);
        compiled_code[func] = code;
    }
};
```

## まとめ

JITコンパイラの導入により、Cm言語のインタープリタ性能を大幅に改善できる可能性があります。特に：

1. **ホットスポットの高速化**: 頻繁に実行されるコードに最適化を集中
2. **実行時情報の活用**: 型情報やパスプロファイルを使った最適化
3. **段階的な最適化**: 起動時間とピーク性能のバランス

これらの技術を組み合わせることで、開発の柔軟性を保ちながら、ネイティブコードに近い性能を実現できます。