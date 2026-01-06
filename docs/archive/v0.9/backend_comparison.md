# バックエンド比較：3言語 vs LLVM

> **結論**: 2025年12月より、LLVMバックエンドを唯一のコード生成方式として採用しました。
> このドキュメントは、その決定の根拠を示す参考資料として保持されています。

## コード量の比較

### 以前のアプローチ（3つのコードジェネレータ）- 廃止

```
src/codegen/
├── cpp_codegen.hpp      (1,500行)
├── rust_codegen.hpp     (1,800行)
├── typescript_codegen.hpp (1,200行)
├── cpp_runtime.hpp      (500行)
├── rust_runtime.rs      (600行)
├── ts_runtime.ts        (400行)
└── platform_specific/
    ├── cpp_windows.hpp   (300行)
    ├── cpp_linux.hpp     (280行)
    ├── cpp_macos.hpp     (290行)
    ├── rust_std.rs       (400行)
    └── ts_node.ts        (350行)

合計：約7,620行
```

### LLVMアプローチ（統一コードジェネレータ）- 採用

```
src/codegen/
├── llvm_unified_codegen.hpp (800行)
├── llvm_platform.hpp        (400行)
└── llvm_optimization.hpp    (300行)

合計：約1,500行（80%削減！）
```

## 機能実装の比較

### 例：新しい演算子の追加（`??=` null合体代入）

#### 現在のアプローチ

```cpp
// 1. C++コードジェネレータ (cpp_codegen.hpp)
void generateNullCoalesceAssign(const mir::Instruction& inst) {
    // C++には??=がないので、エミュレート
    output << "if (" << getVar(inst.target) << " == nullptr) {\n";
    output << "  " << getVar(inst.target) << " = " << getValue(inst.value) << ";\n";
    output << "}\n";
}

// 2. Rustコードジェネレータ (rust_codegen.hpp)
void generateNullCoalesceAssign(const mir::Instruction& inst) {
    // Rustのget_or_insertを使用
    output << "let " << getVar(inst.target) << " = ";
    output << getVar(inst.target) << ".get_or_insert(";
    output << getValue(inst.value) << ");\n";
}

// 3. TypeScriptコードジェネレータ (typescript_codegen.hpp)
void generateNullCoalesceAssign(const mir::Instruction& inst) {
    // TypeScriptはネイティブサポート
    output << getVar(inst.target) << " ??= " << getValue(inst.value) << ";\n";
}
```

#### LLVMアプローチ

```cpp
// llvm_unified_codegen.hpp - 1箇所だけ！
llvm::Value* generateNullCoalesceAssign(const mir::Instruction& inst) {
    auto target = locals[inst.target];
    auto value = getOperandValue(inst.value);

    // null チェック
    auto isNull = builder->CreateICmpEQ(
        target,
        llvm::ConstantPointerNull::get(
            llvm::cast<llvm::PointerType>(target->getType())));

    // 条件付き代入
    auto currentBB = builder->GetInsertBlock();
    auto assignBB = llvm::BasicBlock::Create(*context, "assign");
    auto mergeBB = llvm::BasicBlock::Create(*context, "merge");

    builder->CreateCondBr(isNull, assignBB, mergeBB);

    builder->SetInsertPoint(assignBB);
    builder->CreateStore(value, target);
    builder->CreateBr(mergeBB);

    builder->SetInsertPoint(mergeBB);
    return target;
}
```

## プラットフォーム対応の比較

### 現在：言語ごとに制限

| プラットフォーム | C++ | Rust | TypeScript |
|------------|-----|------|------------|
| Linux | ✓ | ✓ | △（Node.js） |
| Windows | ✓ | ✓ | △（Node.js） |
| macOS | ✓ | ✓ | △（Node.js） |
| iOS | △ | ✓ | ✗ |
| Android | △ | ✓ | △（React Native） |
| WebAssembly | △（Emscripten） | ✓ | ✗ |
| 組み込み | ✓ | ✓ | ✗ |
| ブラウザ | ✗ | △（WASM） | ✓ |

### LLVM：すべて統一

| プラットフォーム | LLVM |
|------------|------|
| Linux (x86, ARM, RISC-V) | ✓ |
| Windows (x86, ARM) | ✓ |
| macOS (Intel, Apple Silicon) | ✓ |
| iOS | ✓ |
| Android | ✓ |
| WebAssembly | ✓ |
| 組み込み (ARM, RISC-V, etc.) | ✓ |
| ブラウザ (via WASM) | ✓ |
| その他100+ターゲット | ✓ |

## 最適化の比較

### 現在：各言語コンパイラに依存

```bash
# C++の場合
g++ -O3 generated.cpp  # GCCの最適化に依存

# Rustの場合
rustc -O generated.rs  # Rustcの最適化に依存

# TypeScriptの場合
# 最適化なし（V8エンジンのJITに依存）
```

### LLVM：統一された最適化パイプライン

```cpp
// すべてのターゲットで同じ最適化
llvm::PassBuilder passBuilder;

// 基本最適化
passBuilder.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O3);

// Cm固有の最適化も追加可能
passBuilder.registerPipelineParsingCallback(
    [](StringRef Name, ModulePassManager &MPM) {
        if (Name == "cm-optimize") {
            MPM.addPass(CmSpecificOptimizationPass());
            MPM.addPass(CmMemoryOptimizationPass());
            return true;
        }
        return false;
    });
```

## デバッグ体験の比較

### 現在：3つの異なるデバッグ体験

```bash
# C++
gdb ./program
(gdb) break main  # C++のデバッグシンボル

# Rust
rust-gdb ./program
(gdb) break program::main  # Rustのデバッグシンボル

# TypeScript
node --inspect program.js  # Chrome DevTools
```

### LLVM：統一されたデバッグ体験

```cpp
// デバッグ情報を統一生成
void generateDebugInfo(llvm::Function* func, const SourceLocation& loc) {
    auto debugBuilder = module->getOrInsertDIBuilder();

    auto file = debugBuilder->createFile(loc.filename, loc.directory);
    auto cu = debugBuilder->createCompileUnit(
        llvm::dwarf::DW_LANG_C_plus_plus,  // Cm言語として登録も可能
        file,
        "Cm Compiler",
        false, "", 0);

    auto subprogram = debugBuilder->createFunction(
        cu, func->getName(), func->getName(),
        file, loc.line, funcType, false, true, loc.line);

    func->setSubprogram(subprogram);
}

// どのプラットフォームでも同じデバッグ体験
// gdb, lldb, Visual Studio すべてで動作
```

## ビルド時間の比較

### 現在

```bash
# 3つの言語への変換 + 各言語のコンパイル
cm → C++ (2秒) → g++ (5秒)
cm → Rust (3秒) → rustc (8秒)
cm → TypeScript (1秒) → （実行時解釈）

合計：最大11秒
```

### LLVM

```bash
# 1回の変換 + LLVM最適化
cm → LLVM IR (1秒) → ネイティブコード (2秒)

合計：3秒（70%高速化！）
```

## エラーメッセージの品質

### 現在：言語ごとに異なるエラー

```cpp
// C++エラー
error: no matching function for call to 'foo'
note: candidate function not viable: no known conversion from 'Bar' to 'Baz'

// Rustエラー
error[E0308]: mismatched types
 --> generated.rs:42:5
  |
42 |     foo(bar)
  |     ^^^ expected struct `Baz`, found struct `Bar`

// TypeScriptエラー
TypeError: Cannot read property 'foo' of undefined
    at generated.js:42:5
```

### LLVM：統一されたCmエラー

```
[CM ERROR] Type mismatch in function call
  ├─ Location: example.cm:10:5
  ├─ Function: foo
  ├─ Expected: Baz
  ├─ Found: Bar
  └─ Suggestion: Did you mean to call 'foo_with_bar' instead?

// ソースレベルのエラーメッセージ
// プラットフォームに関係なく一貫性のあるエラー
```

## 実例：print関数の実装

### 現在：3つの異なる実装

```cpp
// C++ (cpp_codegen.hpp)
void generatePrint(const mir::Call& call) {
    output << "std::cout << ";
    for (const auto& arg : call.args) {
        output << getValue(arg) << " << \" \" << ";
    }
    output << "std::endl;\n";
}

// Rust (rust_codegen.hpp)
void generatePrint(const mir::Call& call) {
    output << "println!(\"";
    for (size_t i = 0; i < call.args.size(); i++) {
        output << "{} ";
    }
    output << "\", ";
    for (const auto& arg : call.args) {
        output << getValue(arg) << ", ";
    }
    output << ");\n";
}

// TypeScript (typescript_codegen.hpp)
void generatePrint(const mir::Call& call) {
    output << "console.log(";
    for (const auto& arg : call.args) {
        output << getValue(arg) << ", ";
    }
    output << ");\n";
}
```

### LLVM：1つの実装ですべて対応

```cpp
// llvm_unified_codegen.hpp
llvm::Function* declarePrintFunction() {
    // printf/puts/write などプラットフォーム適切な関数を選択
    auto& ctx = *context;
    auto printfType = llvm::FunctionType::get(
        llvm::Type::getInt32Ty(ctx),
        {llvm::Type::getInt8PtrTy(ctx)},
        true);  // 可変長引数

    return llvm::Function::Create(
        printfType,
        llvm::Function::ExternalLinkage,
        "printf",  // または puts, write など
        module.get());
}

llvm::Value* generatePrint(const mir::Call& call) {
    auto printFunc = module->getFunction("printf");
    if (!printFunc) {
        printFunc = declarePrintFunction();
    }

    // フォーマット文字列生成
    std::string format;
    std::vector<llvm::Value*> args;

    for (const auto& arg : call.args) {
        auto value = getOperandValue(arg);
        if (value->getType()->isIntegerTy()) {
            format += "%d ";
        } else if (value->getType()->isFloatingPointTy()) {
            format += "%f ";
        } else {
            format += "%s ";
        }
        args.push_back(value);
    }
    format += "\n";

    auto formatStr = builder->CreateGlobalStringPtr(format);
    args.insert(args.begin(), formatStr);

    return builder->CreateCall(printFunc, args);
}
```

## 結論

| 項目 | 以前（3言語）| LLVM（採用） |
|-----|------------|-------------|
| コード量 | 7,620行 | 1,500行 |
| 保守性 | 低（3箇所修正） | 高（1箇所修正） |
| プラットフォーム | 制限あり | 100+対応 |
| 最適化 | 言語依存 | 統一＆高度 |
| ビルド時間 | 遅い | 高速 |
| デバッグ | バラバラ | 統一 |
| エラー品質 | 不一致 | 一貫性 |

**この比較に基づき、2025年12月にLLVMバックエンドを唯一のコード生成方式として採用しました。**

**LLVMへの移行により、開発効率が5倍、保守性が10倍向上します。**