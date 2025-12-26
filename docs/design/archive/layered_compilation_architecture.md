# 階層化されたコンパイルアーキテクチャ

## 現状の問題
現在のパイプラインは各層の責任が不明確で、HIR→MIR変換時に既に基本ブロック分割が発生している。

## 提案する新アーキテクチャ

```
AST → HIR → MIR → Target-MIR → Target-Codegen
              ↓        ↓            ↓
           汎用的   ターゲット別  単純な変換
                     最適化
```

## 各層の責任と設計

### 1. HIR (High-level IR) - 汎用的な高レベル表現

**責任**: 言語の意味を保持した高レベル表現

```rust
// HIRの例（疑似コード）
Function main {
    Call(println, ["Hello, World!"])
    Return(0)
}
```

**特徴**:
- 基本ブロック分割なし
- 直接的な制御フロー表現
- ターゲット非依存

### 2. MIR (Mid-level IR) - 汎用的な中間表現

**責任**: 最適化可能な標準形式

```rust
// MIRの例（疑似コード）
Function main {
    %1 = const "Hello, World!"
    call println(%1)
    return 0
}
```

**特徴**:
- SSA形式（必要な場合のみ）
- 汎用的な最適化パス適用
- まだターゲット非依存

### 3. CPP-MIR - C++専用の最適化済みMIR

**責任**: C++ターゲット向けの最適化と準備

```cpp
// CPP-MIRの例（疑似コード）
Function main {
    // printlnをprintf呼び出しに変換済み
    printf_call("Hello, World!\n")
    return 0
}
```

**最適化内容**:
- println → printf 変換
- 文字列補間の前処理
- 不要な変数の除去
- 線形フローの検出

### 4. CPP-Codegen - シンプルな変換器

**責任**: CPP-MIRから直接C++コードへの1対1変換

```cpp
// 生成されるC++コード
#include <cstdio>

int main() {
    printf("Hello, World!\n");
    return 0;
}
```

**特徴**:
- 複雑なロジックなし
- 単純なテンプレート展開
- ステートマシン生成の回避

## 実装例

### CPP-MIR最適化パス

```cpp
class CppMirOptimizer {
    // 線形フロー検出
    bool isLinearFlow(const MirFunction& func) {
        for (const auto& bb : func.basic_blocks) {
            if (hasConditionalJump(bb) || hasLoop(bb)) {
                return false;
            }
        }
        return true;
    }

    // println最適化
    MirInstruction optimizePrintln(const MirCall& call) {
        if (call.name == "println") {
            // 文字列補間を解析
            auto format = analyzeFormat(call.args);
            return MirCall{
                .name = "printf",
                .args = buildPrintfArgs(format)
            };
        }
        return call;
    }

    // 変数インライン化
    void inlineVariables(MirFunction& func) {
        // 単一使用の変数を検出してインライン化
        for (auto& inst : func.instructions) {
            if (isSingleUse(inst.dest)) {
                inlineValue(inst);
            }
        }
    }
};
```

### CPP-Codegen実装

```cpp
class CppCodegen {
    void generate(const CppMir& mir) {
        // ヘッダー
        emit("#include <cstdio>");
        if (mir.usesString) {
            emit("#include <string>");
        }

        // 関数生成（単純な変換）
        for (const auto& func : mir.functions) {
            generateFunction(func);
        }
    }

    void generateFunction(const CppMirFunction& func) {
        // 線形フローは直接生成
        if (func.isLinear) {
            generateLinearFunction(func);
        } else {
            // 複雑な制御フローのみ特別処理
            generateComplexFunction(func);
        }
    }

    void generateLinearFunction(const CppMirFunction& func) {
        emit("int " + func.name + "() {");
        for (const auto& inst : func.instructions) {
            // 1対1の単純な変換
            switch (inst.type) {
                case Printf:
                    emit("printf(" + inst.args + ");");
                    break;
                case Return:
                    emit("return " + inst.value + ";");
                    break;
                // ...
            }
        }
        emit("}");
    }
};
```

## 利点

1. **明確な責任分離**
   - 各層が明確な役割を持つ
   - デバッグと保守が容易

2. **ターゲット別最適化**
   - C++専用の最適化をCPP-MIR層に集約
   - 他のターゲット（Rust、TS）も同様のアプローチ可能

3. **効率的なコード生成**
   - 最終的なCodegen層は単純
   - 不要な中間表現を回避

4. **拡張性**
   - 新しいターゲットの追加が容易
   - 最適化パスの追加が独立

## 実装ステップ

1. **フェーズ1**: HIRの簡潔化
   - 基本ブロック分割を延期
   - 直接的な表現を維持

2. **フェーズ2**: CPP-MIR層の追加
   - MIRからCPP-MIRへの変換パス
   - C++専用最適化の実装

3. **フェーズ3**: Codegenの簡潔化
   - ステートマシン生成の除去
   - テンプレートベースの単純な変換

## 期待される結果

- Hello World: 165行 → 10行以下
- コンパイル時間: 同等またはより高速
- メンテナンス性: 大幅に向上
- 拡張性: 新ターゲット追加が容易