[English](simplified_architecture.en.html)

# シンプル化されたコンパイルアーキテクチャ

## 洞察：汎用MIRは不要

ご指摘の通り、汎用MIRとHIRの差が小さく、各ターゲット用のMIRが必要なら、汎用MIRは冗長です。

## 最適化されたパイプライン

```
        AST
         ↓
        HIR (汎用・高レベル)
       ／｜＼
      /  |  \
    CPP  Rust TS
    MIR  MIR  MIR  (ターゲット最適化)
     ↓    ↓    ↓
    CPP  Rust TS  (単純な変換)
```

## 各層の明確な責任

### 1. HIR - 唯一の汎用表現
```rust
// HIRが汎用MIRの役割も兼ねる
struct HirFunction {
    name: String,
    params: Vec<Param>,
    body: Vec<Statement>,
    // 型情報、最適化ヒントなど
}

enum Statement {
    Let(name, expr),
    Assign(target, expr),
    Call(func, args),
    If(cond, then_block, else_block),
    While(cond, body),
    For(init, cond, update, body),
    Return(expr),
}
```

### 2. Target-MIR - ターゲット特化の最適化

#### CPP-MIR
```cpp
// C++に最適化された表現
struct CppMir {
    // println → printf 変換済み
    // 文字列補間 → フォーマット文字列
    // 不要な変数削除済み
}
```

#### Rust-MIR
```rust
// Rustに最適化された表現
struct RustMir {
    // ownership/borrowing 追加
    // match式への変換
    // Result/Option handling
}
```

#### TS-MIR
```typescript
// TypeScriptに最適化された表現
interface TsMir {
    // async/await 変換
    // オプショナルチェイニング
    // 型アノテーション追加
}
```

## 実装例：Hello World

### 入力（Cm）
```cm
import std::io::println;
int main() {
    println("Hello, World!");
    return 0;
}
```

### HIR（汎用表現）
```
Function main() -> int {
    Call(println, ["Hello, World!"])
    Return(0)
}
```

### 各ターゲットMIRへの直接変換

#### → CPP-MIR
```cpp
Function main() -> int {
    printf("Hello, World!\n")  // printlnをprintfに最適化
    return 0
}
```

#### → Rust-MIR
```rust
fn main() -> i32 {
    println!("Hello, World!")  // マクロに変換
    0  // 暗黙のreturn
}
```

#### → TS-MIR
```typescript
function main(): number {
    console.log("Hello, World!")  // console.logに変換
    return 0
}
```

## 実装アーキテクチャ

```cpp
// 基底クラス：Target-MIR変換器
class TargetMirConverter {
public:
    virtual void convert(const Hir& hir) = 0;
};

// C++用変換器
class CppMirConverter : public TargetMirConverter {
    CppMir convert(const Hir& hir) override {
        CppMir mir;

        // C++特有の最適化
        optimizePrintCalls(hir);     // println → printf
        processStringInterpolation(); // 文字列補間の処理
        eliminateDeadVariables();     // 不要変数削除
        detectLinearFlow();           // 線形フロー検出

        return mir;
    }
};

// Rust用変換器
class RustMirConverter : public TargetMirConverter {
    RustMir convert(const Hir& hir) override {
        RustMir mir;

        // Rust特有の最適化
        inferLifetimes(hir);         // ライフタイム推論
        addOwnership();               // 所有権情報追加
        convertToMacros();            // println! マクロ化
        optimizePatternMatch();       // match最適化

        return mir;
    }
};

// TypeScript用変換器
class TsMirConverter : public TargetMirConverter {
    TsMir convert(const Hir& hir) override {
        TsMir mir;

        // TypeScript特有の最適化
        inferTypes(hir);              // 型推論
        addAsyncAwait();              // 非同期処理
        convertToConsoleLog();        // console.log変換
        addOptionalChaining();        // ?.演算子

        return mir;
    }
};
```

## 文字列補間の例

### 入力（Cm）
```cm
let name = "Alice";
println("Hello, {name}!");
```

### HIR（共通）
```
Let(name, "Alice")
Call(println, ["Hello, {name}!"])
```

### ターゲット別最適化

#### CPP-MIR
```cpp
const char* name = "Alice";
printf("Hello, %s!\n", name);  // フォーマット文字列に変換
```

#### Rust-MIR
```rust
let name = "Alice";
println!("Hello, {}!", name);  // Rustのフォーマットマクロ
```

#### TS-MIR
```typescript
const name = "Alice";
console.log(`Hello, ${name}!`);  // テンプレートリテラル
```

## メリット

### 1. シンプリティ
- 層が1つ減って理解しやすい
- HIRが汎用表現として十分機能

### 2. 効率性
- 変換ステップが減少
- 各ターゲットに直接最適化

### 3. 保守性
- 各ターゲットの最適化が独立
- 新ターゲット追加が簡単

### 4. パフォーマンス
- 余計な中間変換なし
- コンパイル時間短縮

## 実装優先順位

1. **HIRの強化**
   - 必要な情報を全て保持
   - 型情報、メタデータ追加

2. **CPP-MIR実装**
   - printf最適化
   - 線形フロー検出
   - 変数削減

3. **Rust-MIR実装**
   - 所有権システム
   - マクロ変換

4. **TS-MIR実装**
   - 型推論
   - 非同期処理

## 結論

汎用MIRを削除することで：
- **アーキテクチャがシンプル**に
- **各ターゲットの最適化が明確**に
- **開発・保守が容易**に

これが最も実用的なアプローチです。