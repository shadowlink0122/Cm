[English](llvm_multiplatform.en.html)

# LLVM マルチプラットフォーム対応

## 概要
LLVMは中間表現（IR）を使用してマルチプラットフォーム対応を実現します。

## アーキテクチャ

```
┌─────────────┐
│  Cm Source  │
└──────┬──────┘
       ↓
┌─────────────┐
│  Cm → MIR   │
└──────┬──────┘
       ↓
┌─────────────────────────────────────┐
│         LLVM IR (*.ll/*.bc)          │
│  ・プラットフォーム非依存            │
│  ・SSA形式                          │
│  ・型付き中間表現                    │
└──────┬──────────────────────────────┘
       ↓
┌─────────────────────────────────────┐
│        LLVM Target Machine          │
├─────────────┬───────────┬───────────┤
│   x86_64    │   ARM64   │   WASM    │
│   Linux     │   macOS   │  Browser  │
│   Windows   │   iOS     │   Node.js │
└─────────────┴───────────┴───────────┘
```

## プラットフォーム対応の仕組み

### 1. LLVM IRレベル（プラットフォーム非依存）

```llvm
; すべてのプラットフォームで同じIR
define i32 @add(i32 %a, i32 %b) {
entry:
  %result = add i32 %a, %b
  ret i32 %result
}

; ポインタサイズも抽象化
define ptr @allocate(i64 %size) {
  %ptr = call ptr @malloc(i64 %size)
  ret ptr %ptr
}
```

### 2. Target Triple による指定

```cpp
// プラットフォーム指定の例
std::string getTargetTriple() {
    #ifdef __APPLE__
        #ifdef __aarch64__
            return "arm64-apple-macosx";  // Apple Silicon Mac
        #else
            return "x86_64-apple-macosx"; // Intel Mac
        #endif
    #elif __linux__
        #ifdef __aarch64__
            return "aarch64-linux-gnu";   // ARM Linux
        #else
            return "x86_64-linux-gnu";     // x86-64 Linux
        #endif
    #elif _WIN32
        return "x86_64-pc-windows-msvc";  // Windows
    #elif __EMSCRIPTEN__
        return "wasm32-unknown-emscripten"; // WebAssembly
    #endif
}
```

### 3. データレイアウトの自動調整

```cpp
// プラットフォーム固有のデータレイアウト
module->setDataLayout(targetMachine->createDataLayout());

// 例：x86_64-linux
// "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
//
// 説明：
// e     = リトルエンディアン
// m:e   = ELF mangling
// p270  = 32ビットアドレス空間270
// i64:64 = 64ビット整数は64ビットアライメント
```

## クロスコンパイル対応

### 1. 基本的なクロスコンパイル

```cpp
class CrossCompiler {
    llvm::Module* module;

    void compileForTarget(const std::string& targetTriple) {
        // ターゲット初期化
        llvm::InitializeAllTargetInfos();
        llvm::InitializeAllTargets();
        llvm::InitializeAllTargetMCs();
        llvm::InitializeAllAsmParsers();
        llvm::InitializeAllAsmPrinters();

        // ターゲット取得
        std::string error;
        auto target = llvm::TargetRegistry::lookupTarget(targetTriple, error);
        if (!target) {
            throw std::runtime_error("Target not found: " + error);
        }

        // ターゲットマシン作成
        llvm::TargetOptions options;
        auto targetMachine = target->createTargetMachine(
            targetTriple,
            "generic",  // CPU
            "",         // Features
            options,
            llvm::Reloc::PIC_,
            llvm::CodeModel::Small,
            llvm::CodeGenOpt::Level::Default
        );

        // コード生成
        generateCode(targetMachine);
    }

    void generateCode(llvm::TargetMachine* tm) {
        // オブジェクトファイル生成
        std::error_code ec;
        llvm::raw_fd_ostream dest("output.o", ec, llvm::sys::fs::OF_None);

        llvm::legacy::PassManager pass;
        tm->addPassesToEmitFile(pass, dest, nullptr,
                                llvm::CodeGenFileType::ObjectFile);
        pass.run(*module);
    }
};
```

### 2. プラットフォーム固有の処理

```cpp
// Cm言語でのプラットフォーム抽象化
class PlatformAbstraction {
    llvm::IRBuilder<> builder;

    llvm::Value* systemCall(const std::string& name,
                            llvm::ArrayRef<llvm::Value*> args) {
        // プラットフォーム固有の呼び出し規約
        auto triple = module->getTargetTriple();

        if (triple.find("windows") != std::string::npos) {
            // Windows: __stdcall
            return builder.CreateCall(
                getWindowsFunction(name), args
            );
        } else if (triple.find("linux") != std::string::npos) {
            // Linux: System V ABI
            return builder.CreateCall(
                getLinuxFunction(name), args
            );
        } else if (triple.find("apple") != std::string::npos) {
            // macOS: Darwin ABI
            return builder.CreateCall(
                getDarwinFunction(name), args
            );
        }
    }

    // メモリアライメント調整
    llvm::AllocaInst* allocateAligned(llvm::Type* type) {
        auto alloca = builder.CreateAlloca(type);

        // プラットフォーム固有のアライメント
        auto dataLayout = module->getDataLayout();
        auto alignment = dataLayout.getPrefTypeAlign(type);
        alloca->setAlignment(alignment);

        return alloca;
    }
};
```

## 最適化レベル

### プラットフォーム別最適化

```cpp
llvm::PassBuilder passBuilder(targetMachine);

// モバイル向け（サイズ優先）
if (isEmbeddedTarget(triple)) {
    passBuilder.buildPerModuleDefaultPipeline(
        llvm::OptimizationLevel::Oz  // サイズ最小化
    );
}
// デスクトップ向け（速度優先）
else if (isDesktopTarget(triple)) {
    passBuilder.buildPerModuleDefaultPipeline(
        llvm::OptimizationLevel::O3  // 速度最大化
    );
}
// WebAssembly向け（バランス）
else if (isWasmTarget(triple)) {
    passBuilder.buildPerModuleDefaultPipeline(
        llvm::OptimizationLevel::O2  // バランス型
    );
}
```

## ビルド設定例

### CMakeLists.txt での設定

```cmake
# マルチプラットフォーム対応
option(CM_CROSS_COMPILE "Enable cross compilation" OFF)
set(CM_TARGET_TRIPLE "" CACHE STRING "Target triple for cross compilation")

if(CM_CROSS_COMPILE)
    # クロスコンパイル設定
    set(LLVM_TARGETS_TO_BUILD "X86;ARM;AArch64;WebAssembly")
else()
    # ホストプラットフォームのみ
    set(LLVM_TARGETS_TO_BUILD "host")
endif()

# LLVM設定
find_package(LLVM REQUIRED CONFIG
    COMPONENTS
        Core
        Support
        ${LLVM_TARGETS_TO_BUILD}
)
```

## 実装例：マルチターゲットコンパイラ

```cpp
class CmMultiTargetCompiler {
private:
    std::unique_ptr<llvm::Module> module;

public:
    struct CompileOptions {
        std::string targetTriple;
        llvm::CodeGenOpt::Level optLevel;
        bool generateDebugInfo;
        std::string outputFormat; // obj, asm, ll, bc
    };

    void compile(const mir::Program& mirProgram,
                const CompileOptions& options) {
        // 1. MIR → LLVM IR
        LLVMCodeGen codegen(options.targetTriple);
        module = codegen.generate(mirProgram);

        // 2. プラットフォーム固有の調整
        adjustForPlatform(options.targetTriple);

        // 3. 最適化
        optimize(options.optLevel);

        // 4. コード生成
        emit(options);
    }

private:
    void adjustForPlatform(const std::string& triple) {
        if (triple.find("wasm") != std::string::npos) {
            // WebAssembly固有の調整
            addWasmExports();
            lowerToWasmIntrinsics();
        } else if (triple.find("arm") != std::string::npos) {
            // ARM固有の調整
            addArmAttributes();
            useNeonIntrinsics();
        } else if (triple.find("x86_64") != std::string::npos) {
            // x86-64固有の調整
            addX86Attributes();
            useAVXIntrinsics();
        }
    }

    void emit(const CompileOptions& options) {
        if (options.outputFormat == "obj") {
            emitObjectFile(options.targetTriple);
        } else if (options.outputFormat == "asm") {
            emitAssembly(options.targetTriple);
        } else if (options.outputFormat == "ll") {
            emitLLVMIR();
        } else if (options.outputFormat == "bc") {
            emitBitcode();
        }
    }
};
```

## 使用例

```bash
# ホストプラットフォーム向けコンパイル
./cm compile program.cm -o program

# ARM64 Linux向けクロスコンパイル
./cm compile program.cm --target=aarch64-linux-gnu -o program.arm64

# WebAssembly向けコンパイル
./cm compile program.cm --target=wasm32-unknown-emscripten -o program.wasm

# Windows向けクロスコンパイル（Linux上で）
./cm compile program.cm --target=x86_64-pc-windows-msvc -o program.exe

# 複数ターゲット同時生成
./cm compile program.cm --targets=x86_64,arm64,wasm -o program
# 生成: program.x86_64, program.arm64, program.wasm
```

## まとめ

### LLVMの利点
1. **単一のIRから複数プラットフォーム対応**
   - 一度LLVM IRに変換すれば、任意のターゲットに変換可能

2. **プラットフォーム固有最適化**
   - 各アーキテクチャに特化した最適化パス

3. **ABI互換性**
   - 各プラットフォームのABIに準拠したコード生成

4. **クロスコンパイル**
   - ホストと異なるターゲット向けのコンパイルが容易

### Cm言語での活用
- MIR → LLVM IR の変換を一度実装すれば、全プラットフォーム対応
- プラットフォーム固有機能は条件コンパイルで対応
- LLVMの豊富な最適化パスを活用可能