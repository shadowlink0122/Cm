[English](implementation_details.en.html)

# Cm言語 最適化実装詳細

## 概要

このドキュメントは、Cm言語コンパイラにおける最適化パスの実装詳細を記載します。

## MIR最適化パス実装

### 基底クラス: OptimizationPass

すべてのMIR最適化パスは`OptimizationPass`基底クラスを継承します。

```cpp
class OptimizationPass {
public:
    virtual std::string name() const = 0;
    virtual bool run(MirFunction& func) = 0;
    virtual bool runOnProgram(MirProgram& program) {
        // デフォルト実装：各関数に対して実行
    }
};
```

### 定数畳み込み（Constant Folding）

**ファイル**: `src/mir/passes/folding.hpp`

```cpp
class ConstantFolding : public OptimizationPass {
    // 算術演算、比較演算、型変換の定数化
    // 複数回代入される変数の除外
};
```

**実装のポイント**:
- SSA形式を前提とした実装
- 型安全性の確保
- オーバーフロー検出

### デッドコード除去（Dead Code Elimination）

**ファイル**: `src/mir/passes/dce.hpp`

```cpp
class DeadCodeElimination : public OptimizationPass {
    // 使用されない変数の検出と除去
    // 副作用のない命令の削除
};
```

**実装のポイント**:
- Use-Defチェーンの構築
- 副作用（関数呼び出し、メモリ書き込み）の考慮
- ブロックレベルとプログラムレベルのDCE

### 共通部分式除去（Common Subexpression Elimination）

**ファイル**: `src/mir/passes/cse.hpp`

```cpp
class LocalGVN : public OptimizationPass {
    // ブロック内の同一計算の検出
    // Value Numberingによる同値性判定
};
```

**実装のポイント**:
- ハッシュベースのValue Numbering
- 可換演算の正規化
- メモリ操作の保守的な扱い

### ループ不変式移動（Loop Invariant Code Motion）

**ファイル**: `src/mir/passes/licm.hpp`

```cpp
class LoopInvariantCodeMotion : public OptimizationPass {
    // ループ不変式の検出
    // Pre-headerへの移動
};
```

**必要な解析**:
1. **Dominator Tree**: 支配関係の計算
2. **Loop Analysis**: ループ構造の検出
3. **Side Effect Analysis**: 副作用の有無判定

## 解析インフラストラクチャ

### データフロー解析

**ファイル**: `src/mir/analysis/dataflow.hpp`

```cpp
template<typename LatticeType>
class DataflowAnalysis {
    // Forward/Backward解析
    // Worklist algorithm
    // 収束判定
};
```

### 支配木（Dominator Tree）

**ファイル**: `src/mir/analysis/dominance.hpp`

```cpp
class DominatorTree {
    // Lengauer-Tarjanアルゴリズム
    // Post-dominator計算
    // Dominator frontier計算
};
```

### ループ解析

**ファイル**: `src/mir/analysis/loop_analysis.hpp`

```cpp
class LoopAnalysis {
    // Natural loop検出
    // ループネスト構造
    // Loop header/preheader/latch識別
};
```

## 最適化パイプライン

### パスマネージャ

**ファイル**: `src/mir/passes/manager.hpp`

```cpp
class PassManager {
    std::vector<std::unique_ptr<OptimizationPass>> passes;

    void addPass(std::unique_ptr<OptimizationPass> pass);
    bool run(MirProgram& program);

    // 収束管理
    int maxIterations = 10;
    bool converged();
};
```

### 最適化レベル別パス構成

```cpp
PassManager createO1Pipeline() {
    PassManager pm;
    pm.addPass(std::make_unique<SCCP>());           // 定数伝播
    pm.addPass(std::make_unique<SimplifyCFG>());    // CFG簡約化
    pm.addPass(std::make_unique<DCE>());            // デッドコード除去
    return pm;
}

PassManager createO2Pipeline() {
    PassManager pm = createO1Pipeline();
    pm.addPass(std::make_unique<LocalGVN>());       // CSE
    pm.addPass(std::make_unique<DSE>());            // デッドストア除去
    pm.addPass(std::make_unique<Inlining>());       // インライン化
    pm.addPass(std::make_unique<LICM>());           // ループ不変式移動
    return pm;
}

PassManager createO3Pipeline() {
    PassManager pm = createO2Pipeline();
    pm.addPass(std::make_unique<AggressiveInlining>());
    pm.addPass(std::make_unique<LoopUnrolling>());
    // LLVMバックエンドに追加最適化を委譲
    return pm;
}
```

## インポート問題の対処

### 問題の詳細

外部関数（インポート）を含むプログラムで最適化時に無限ループが発生。

### 一時的な対処

```cpp
// codegen.hpp
class LLVMCodeGen {
    bool hasImports = false;

    void compile(const MirProgram& program) {
        hasImports = !program.imports.empty();
    }

    void optimize() {
        if (hasImports && optLevel > 0) {
            // インポートがある場合は最適化をスキップ
            return;
        }
        // 通常の最適化処理
    }
};
```

### 根本的な解決策（実装予定）

1. **外部関数の適切なマーキング**
   ```cpp
   class MirFunction {
       bool isExternal = false;  // 外部関数フラグ
       bool isPure = false;      // 副作用なしフラグ
   };
   ```

2. **最適化パスでの外部関数考慮**
   ```cpp
   bool canOptimizeCall(const MirCall* call) {
       if (call->target->isExternal && !call->target->isPure) {
           return false;  // 外部関数は保守的に扱う
       }
       return true;
   }
   ```

3. **リンク時最適化（LTO）サポート**
   - 外部関数の定義が利用可能な場合のクロスモジュール最適化
   - LLVMのLTOインフラストラクチャの活用

## パフォーマンス測定

### ベンチマーク設定

```bash
# 最適化効果の測定
for level in 0 1 2 3; do
    time ./cm compile benchmark.cm -O$level -o bench_O$level
    time ./bench_O$level
done
```

### 統計情報収集

```cpp
class OptimizationStats {
    int instructionsEliminated = 0;
    int blocksRemoved = 0;
    int functionsInlined = 0;
    int loopInvariantsMoved = 0;

    void report();
};
```

## デバッグ支援

### 最適化前後のIR出力

```bash
./cm compile test.cm --emit-mir-before-opt
./cm compile test.cm --emit-mir-after-opt
./cm compile test.cm --emit-llvm
```

### パスごとの詳細ログ

```cpp
if (debug_mode) {
    std::cerr << "[" << pass->name() << "] Before:\n";
    dumpMIR(func);
    bool changed = pass->run(func);
    std::cerr << "[" << pass->name() << "] After (changed="
              << changed << "):\n";
    dumpMIR(func);
}
```

## 今後の実装予定

### 高度な最適化

1. **Alias Analysis**
   - ポインタエイリアス解析
   - Type-based alias analysis (TBAA)
   - Flow-sensitive analysis

2. **Vectorization**
   - Loop vectorization
   - SLP (Superword Level Parallelism)
   - Auto-vectorization hints

3. **Profile-Guided Optimization**
   - Instrumentation
   - Profile collection
   - Hot path optimization

### プラットフォーム固有最適化

1. **WASM最適化**
   - wasm-opt統合
   - バイナリサイズ削減
   - SIMD活用

2. **JavaScript最適化**
   - Minification
   - Tree shaking
   - Dead code elimination

## 関連ドキュメント

- [最適化ロードマップ](./mir_optimization_roadmap.html)
- [最適化パスAPI](./pass_api.html)
- [インポート無限ループ問題](../issues/optimization_import_hang.html)