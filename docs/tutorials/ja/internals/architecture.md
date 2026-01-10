---
title: コンパイラアーキテクチャ
parent: コンパイラ内部
---

[English](../../en/internals/architecture.html)

# コンパイラアーキテクチャ

**学習目標:** Cmコンパイラのパイプラインと各フェーズを理解します。  
**所要時間:** 25分  
**難易度:** 🔴 上級

---

## パイプライン概要

```
ソースコード (.cm)
       ↓
┌──────────────────┐
│   Lexer（字句解析）   │  トークン列生成
└──────────────────┘
       ↓
┌──────────────────┐
│  Parser（構文解析）   │  AST生成
└──────────────────┘
       ↓
┌──────────────────┐
│   HIR Lowering     │  高レベルIR生成
└──────────────────┘
       ↓
┌──────────────────┐
│   Type Checker     │  型検査・推論
└──────────────────┘
       ↓
┌──────────────────┐
│   MIR Lowering     │  中間表現生成
└──────────────────┘
       ↓
┌──────────────────┐
│   MIR Optimization │  最適化パス群
└──────────────────┘
       ↓
┌──────────────────────────────────────┐
│     Code Generation                   │
├──────────┬───────────┬───────────────┤
│  LLVM    │  WASM     │  Interpreter  │
│  Native  │  WebAssembly │  直接実行    │
└──────────┴───────────┴───────────────┘
```

---

## 中間表現（IR）

### AST（Abstract Syntax Tree）

パーサーが生成する構文木。ソースコードの構造を直接表現。

```
FunctionDecl
├── name: "add"
├── params: [("a", int), ("b", int)]
└── body:
    └── ReturnStmt
        └── BinaryOp(+)
            ├── Ident("a")
            └── Ident("b")
```

### HIR（High-level IR）

型情報を付加し、糖衣構文を展開した表現。

**特徴:**
- 型アノテーション付き
- for-in → while へのデシュガリング
- 複合代入の展開

### MIR（Mid-level IR）

SSA形式の低レベル表現。最適化の対象。

```
bb0:
    _1 = const 10        // a
    _2 = const 20        // b
    _3 = Add(_1, _2)     // a + b
    Return(_3)
```

**特徴:**
- SSA形式（Static Single Assignment）
- 基本ブロックとターミネータ
- 明示的な制御フロー

---

## 主要コンポーネント

### 1. フロントエンド

| モジュール | 役割 | ファイル |
|-----------|------|---------|
| Lexer | 字句解析 | `src/frontend/lexer/` |
| Parser | 構文解析 | `src/frontend/parser/` |
| TypeChecker | 型検査 | `src/frontend/types/` |

### 2. ミドルエンド

| モジュール | 役割 | ファイル |
|-----------|------|---------|
| HIR | 高レベルIR | `src/hir/` |
| MIR | 中レベルIR | `src/mir/` |
| Optimization | 最適化 | `src/mir/optimization/` |

### 3. バックエンド

| モジュール | ターゲット | ファイル |
|-----------|-----------|---------|
| LLVM Native | x86_64, arm64 | `src/codegen/llvm/native/` |
| LLVM WASM | WebAssembly | `src/codegen/llvm/wasm/` |
| Interpreter | 直接実行 | `src/interpreter/` |
| JS Transpiler | JavaScript | `src/codegen/js/` |

---

## 型システム

### 型の種類

```cpp
enum class TypeKind {
    Void, Bool, Char,
    Tiny, Short, Int, Long,        // 符号付き整数
    UTiny, UShort, UInt, ULong,    // 符号なし整数
    Float, Double,                 // 浮動小数点
    String, Array, Slice,          // コレクション
    Struct, Enum,                  // ユーザー定義
    Pointer, Function,             // 複合型
    Generic, Interface             // ジェネリック
};
```

### モノモーフィゼーション

ジェネリック関数を具象型ごとにインスタンス化：

```cm
// 定義
func max<T>(T a, T b) -> T { ... }

// 使用
max(10, 20);      // max__int が生成
max(1.5, 2.5);    // max__double が生成
```

---

## メモリモデル

### スタック割り当て

- ローカル変数
- 関数パラメータ
- 一時値

### ヒープ割り当て

- 動的スライス（`[]T`）
- 明示的なメモリ確保（FFI経由）

### 文字列

- 内部的にはC文字列（null終端）
- 連結時は新しいメモリを確保

---

## ランタイム

### ネイティブランタイム

```c
// runtime.c
void cm_print_string(const char* s);
void cm_print_int(int i);
char* cm_file_read_all(const char* path);
// ...
```

### WASMランタイム

```c
// runtime_wasm.c
// WASI準拠のI/O関数
```

---

## ビルドシステム

```bash
# ビルド
cd Cm && mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make

# テスト
ctest --output-on-failure
make test-interpreter
make test-compile-llvm
```

---

## 参考リンク

- [最適化パス](optimization.html) - 各最適化の詳細
- [内部アルゴリズム](algorithms.html) - 使用アルゴリズム
- [PROJECT_STRUCTURE.md](/PROJECT_STRUCTURE.html) - ファイル構成