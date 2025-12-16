# LLVM移行計画：Cmの統一バックエンド戦略

## 現状分析

### 現在のアプローチ（MIR → C++/Rust/TS）の問題点

```
     MIR
      ↓
   ┌──┼──┐
   ↓  ↓  ↓
  C++ Rust TS
   ↓  ↓  ↓
 各種プラットフォーム
```

#### 問題点：
1. **3つのコードジェネレータのメンテナンス負担**
   - 各言語固有のバグ修正が必要
   - 機能追加時に3箇所の更新が必要
   - 言語間の微妙な意味論の違い

2. **最適化の限界**
   - 各言語コンパイラの最適化に依存
   - Cm固有の最適化が困難
   - クロスモジュール最適化が不可能

3. **プラットフォーム対応の複雑さ**
   - C++: ネイティブ（OS、組み込み）
   - Rust: システムプログラミング
   - TypeScript: Web/Node.js
   - 各言語でプラットフォーム対応が異なる

## LLVM統一アプローチの利点

### 新アーキテクチャ

```
     MIR
      ↓
   LLVM IR
      ↓
┌────┼────────┐
↓    ↓        ↓
Native  WASM  JIT
│       │     │
├─OS────┤     │
├─組込み─┤     │
├─デスクトップ┤     │
└─モバイル────┤     │
        └─Web─┘     │
              └─REPL┘
```

### 主要な利点

#### 1. 単一のコードジェネレータ
```cpp
// 現在：3つの実装
class CppCodeGen { /* C++固有の実装 */ };
class RustCodeGen { /* Rust固有の実装 */ };
class TsCodeGen { /* TypeScript固有の実装 */ };

// LLVM統一後：1つの実装
class LLVMCodeGen {
    llvm::Value* generateForAllPlatforms(const mir::Instruction& inst);
};
```

#### 2. 全プラットフォーム対応
```bash
# OS開発
cm compile kernel.cm --target=x86_64-none-elf --freestanding

# デスクトップアプリ
cm compile app.cm --target=native --gui

# Webフロントエンド
cm compile frontend.cm --target=wasm32 --web

# モバイルアプリ
cm compile mobile.cm --target=aarch64-ios

# 組み込みシステム
cm compile embedded.cm --target=arm-none-eabi

# すべて同じコンパイラ、同じ最適化パイプライン
```

#### 3. 高度な最適化
```cpp
// LLVMの最適化パスを活用
llvm::PassBuilder passBuilder;

// プロファイルガイド最適化（PGO）
if (hasProfile) {
    passBuilder.addPGOInstrumentationPasses();
}

// Link Time Optimization（LTO）
if (enableLTO) {
    passBuilder.addLTOPasses();
}

// Cm固有の最適化も追加可能
passBuilder.addPass(CmSpecificOptimizationPass());
```

## 実装計画

### Phase 1: LLVM基盤構築（1-2週間）

```cpp
// src/codegen/llvm_codegen.hpp
namespace cm::codegen {

class LLVMCodeGen {
private:
    std::unique_ptr<llvm::LLVMContext> context;
    std::unique_ptr<llvm::Module> module;
    std::unique_ptr<llvm::IRBuilder<>> builder;

public:
    void generate(const mir::Program& program) {
        // MIR → LLVM IR変換
        for (const auto& func : program.functions) {
            generateFunction(func);
        }
    }

    llvm::Function* generateFunction(const mir::Function& mirFunc) {
        // 関数シグネチャ作成
        auto llvmFunc = createFunctionSignature(mirFunc);

        // 基本ブロック生成
        for (const auto& block : mirFunc.blocks) {
            generateBasicBlock(block, llvmFunc);
        }

        return llvmFunc;
    }
};

}  // namespace cm::codegen
```

### Phase 2: 既存バックエンドとの共存（1週間）

```cmake
# CMakeLists.txt
option(CM_USE_LLVM "Use LLVM backend" ON)
option(CM_LEGACY_BACKENDS "Keep C++/Rust/TS backends" OFF)

if(CM_USE_LLVM)
    find_package(LLVM REQUIRED CONFIG)
    add_definitions(-DCM_LLVM_ENABLED)
endif()
```

### Phase 3: 段階的移行（2-3週間）

```cpp
// コマンドライン対応
class CmCompiler {
    void compile(const std::string& file, const Options& opts) {
        auto mir = generateMIR(file);

        if (opts.backend == "llvm") {
            // LLVM バックエンド
            LLVMCodeGen codegen;
            codegen.generate(mir);
            codegen.emit(opts.outputFormat);
        } else if (opts.backend == "legacy") {
            // 既存のバックエンド
            switch (opts.target) {
                case Target::Cpp: /* ... */ break;
                case Target::Rust: /* ... */ break;
                case Target::TypeScript: /* ... */ break;
            }
        }
    }
};
```

## 具体的な利用シナリオ

### 1. OS開発

```cm
// kernel.cm - Cmで書かれたOS カーネル
#[no_std]
#[no_main]

extern "C" void _start() {
    // ブートストラップ
    init_gdt();
    init_idt();
    init_paging();

    // カーネルメイン
    kernel_main();
}

void kernel_main() {
    terminal_write("Cm OS v1.0\n");

    // メモリ管理
    auto heap = HeapAllocator::init(HEAP_START, HEAP_SIZE);

    // プロセス管理
    auto scheduler = Scheduler::new();
    scheduler.run();
}
```

コンパイル：
```bash
cm compile kernel.cm \
    --target=x86_64-none-elf \
    --emit=obj \
    --freestanding \
    --no-stdlib \
    -O3

ld -T kernel.ld kernel.o -o kernel.elf
```

### 2. Webフロントエンド

```cm
// frontend.cm - Cmで書かれたWebアプリ
import { DOM, fetch } from "web";

class TodoApp {
    Vector<Todo> todos;

    void render() {
        DOM::getElementById("app").innerHTML =
            todos.map(|t| `<li>${t.text}</li>`).join("");
    }

    async void loadTodos() {
        auto response = await fetch("/api/todos");
        todos = await response.json<Vector<Todo>>();
        render();
    }
}

#[wasm_export]
void main() {
    auto app = TodoApp::new();
    app.loadTodos();
}
```

コンパイル：
```bash
cm compile frontend.cm \
    --target=wasm32-unknown \
    --emit=wasm \
    --web-bindings \
    -Os

# 生成: frontend.wasm + frontend.js (グルーコード)
```

### 3. ネイティブGUIアプリ

```cm
// desktop.cm - デスクトップアプリ
import { Window, Button, Layout } from "gui";

class MyApp : Application {
    void onCreate() {
        auto window = Window::new("My App", 800, 600);

        auto button = Button::new("Click me!");
        button.onClick([]{
            print("Clicked!");
        });

        window.setLayout(Layout::vertical({button}));
        window.show();
    }
}

int main() {
    MyApp app;
    return app.run();
}
```

コンパイル：
```bash
# macOS
cm compile desktop.cm --target=native --framework=cocoa

# Windows
cm compile desktop.cm --target=native --framework=win32

# Linux
cm compile desktop.cm --target=native --framework=gtk
```

## 移行のメリットまとめ

### 開発効率
- **Before**: 3つのコードジェネレータ × 各種プラットフォーム = 膨大な作業
- **After**: 1つのLLVMバックエンド = シンプルで保守しやすい

### パフォーマンス
- **Before**: 各言語コンパイラの最適化に依存
- **After**: LLVM の世界クラスの最適化 + Cm固有最適化

### プラットフォーム対応
- **Before**: 言語ごとに異なるプラットフォーム制限
- **After**: すべてのLLVMターゲット（100+）に対応

### エコシステム
- **Before**: 各言語のツールチェーンが必要
- **After**: LLVM ツールチェーン（デバッガ、プロファイラ等）を統一利用

## 実装優先順位

1. **必須機能**（Phase 1）
   - MIR → LLVM IR 基本変換
   - 基本型と演算
   - 関数呼び出し
   - 制御フロー

2. **コア機能**（Phase 2）
   - メモリ管理
   - 構造体/クラス
   - ジェネリクス
   - 例外処理

3. **高度な機能**（Phase 3）
   - SIMD最適化
   - 並列処理
   - プロファイルガイド最適化
   - Link Time Optimization

## 結論

LLVM への移行により、Cm は真の **"Write Once, Run Everywhere"** を実現できます：

- **OS カーネル**から**Webアプリ**まで同一言語
- **単一のコンパイラ**で全プラットフォーム対応
- **世界最高レベルの最適化**
- **開発・保守コストの大幅削減**

これこそが、Cm が目指すべき姿です。