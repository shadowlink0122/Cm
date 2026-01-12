# JITコンパイラ詳細設計

## 概要

本ドキュメントでは、Cm言語のJITコンパイラ実装に必要な全情報を記載します。
このドキュメントに従えば、誰でも完璧にJITコンパイラを実装できます。

---

## 1. アーキテクチャ設計

### 1.1 ディレクトリ構造

```
src/codegen/jit/
├── CMakeLists.txt           # CMakeビルド設定
├── jit_engine.hpp           # JITエンジン ヘッダー
├── jit_engine.cpp           # JITエンジン 実装
├── jit_runtime.hpp          # ランタイムサポート ヘッダー
└── jit_runtime.cpp          # ランタイムサポート 実装
```

### 1.2 クラス図

```
┌─────────────────────────────────────────────────────────────────┐
│                       JITEngine                                 │
├─────────────────────────────────────────────────────────────────┤
│ - jit_: std::unique_ptr<llvm::orc::LLJIT>                      │
│ - context_: std::unique_ptr<llvm::LLVMContext>                 │
│ - converter_: std::unique_ptr<MIRToLLVM>                       │
│ - runtimeSymbols_: llvm::orc::SymbolMap                        │
├─────────────────────────────────────────────────────────────────┤
│ + JITEngine()                                                   │
│ + ~JITEngine()                                                  │
│ + execute(program: MirProgram, entry: string) → int            │
│ - initializeJIT() → Error                                       │
│ - registerRuntimeSymbols()                                      │
│ - compileModule(module: Module) → Error                         │
│ - lookupSymbol(name: string) → Expected<JITEvaluatedSymbol>    │
└─────────────────────────────────────────────────────────────────┘
          │
          │ 使用
          ▼
┌─────────────────────────────────────────────────────────────────┐
│                    MIRToLLVM (既存)                             │
├─────────────────────────────────────────────────────────────────┤
│ + convert(program: MirProgram)                                  │
│ + getModule() → llvm::Module*                                   │
└─────────────────────────────────────────────────────────────────┘
```

---

## 2. 実装詳細

### 2.1 jit_engine.hpp

```cpp
#pragma once

#include "../../mir/nodes.hpp"
#include "../llvm/core/context.hpp"
#include "../llvm/core/mir_to_llvm.hpp"

#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <memory>

namespace cm::codegen::jit {

/// JIT実行結果
struct JITResult {
    int exitCode;
    std::string errorMessage;
    bool success;
};

/// LLVM ORC JIT エンジン
class JITEngine {
public:
    JITEngine();
    ~JITEngine();

    // コピー禁止
    JITEngine(const JITEngine&) = delete;
    JITEngine& operator=(const JITEngine&) = delete;

    /// MIRプログラムをJITコンパイル＆実行
    /// @param program MIRプログラム
    /// @param entryPoint エントリポイント関数名（デフォルト: "main"）
    /// @return 実行結果
    JITResult execute(const mir::MirProgram& program,
                      const std::string& entryPoint = "main");

private:
    std::unique_ptr<llvm::orc::LLJIT> jit_;
    std::unique_ptr<llvm::orc::ThreadSafeContext> tsCtx_;

    /// JITエンジン初期化
    llvm::Error initializeJIT();

    /// ランタイムシンボル登録
    void registerRuntimeSymbols();

    /// ネイティブシンボル解決ジェネレータを作成
    llvm::orc::JITDylib::GeneratorFunction createHostProcessGenerator();
};

}  // namespace cm::codegen::jit
```

### 2.2 jit_engine.cpp

```cpp
#include "jit_engine.hpp"

#include <llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h>
#include <llvm/ExecutionEngine/Orc/ObjectLinkingLayer.h>
#include <llvm/ExecutionEngine/SectionMemoryManager.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Transforms/InstCombine/InstCombine.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Transforms/Scalar/GVN.h>

// ランタイム関数のヘッダー
#include "../../../runtime/cm_runtime.h"

namespace cm::codegen::jit {

JITEngine::JITEngine() {
    // LLVMターゲット初期化（一度だけ実行される）
    static bool initialized = []() {
        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmPrinter();
        llvm::InitializeNativeTargetAsmParser();
        return true;
    }();
    (void)initialized;

    // ThreadSafeContext作成
    tsCtx_ = std::make_unique<llvm::orc::ThreadSafeContext>(
        std::make_unique<llvm::LLVMContext>());
}

JITEngine::~JITEngine() = default;

llvm::Error JITEngine::initializeJIT() {
    // LLJITビルダーでJIT作成
    auto jitBuilder = llvm::orc::LLJITBuilder();

    // ホストプロセスのシンボルを解決するジェネレータを追加
    jitBuilder.setObjectLinkingLayerCreator(
        [](llvm::orc::ExecutionSession& ES, const llvm::Triple& TT)
            -> llvm::Expected<std::unique_ptr<llvm::orc::ObjectLayer>> {
            auto GetMemMgr = []() {
                return std::make_unique<llvm::SectionMemoryManager>();
            };
            auto ObjLayer = std::make_unique<llvm::orc::RTDyldObjectLinkingLayer>(
                ES, std::move(GetMemMgr));
            return std::move(ObjLayer);
        });

    auto jitOrErr = jitBuilder.create();
    if (!jitOrErr) {
        return jitOrErr.takeError();
    }
    jit_ = std::move(*jitOrErr);

    // ホストプロセスシンボル登録
    registerRuntimeSymbols();

    return llvm::Error::success();
}

void JITEngine::registerRuntimeSymbols() {
    // メインのJITDylibを取得
    auto& mainJD = jit_->getMainJITDylib();

    // ホストプロセスからシンボルを動的に解決するジェネレータを追加
    auto generator = llvm::orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(
        jit_->getDataLayout().getGlobalPrefix());

    if (generator) {
        mainJD.addGenerator(std::move(*generator));
    }

    // Cmランタイム関数を明示的に登録
    llvm::orc::SymbolMap symbols;

    // I/O関数
    symbols[jit_->mangleAndIntern("cm_println_string")] =
        llvm::JITEvaluatedSymbol::fromPointer(&cm_println_string);
    symbols[jit_->mangleAndIntern("cm_print_string")] =
        llvm::JITEvaluatedSymbol::fromPointer(&cm_print_string);
    symbols[jit_->mangleAndIntern("cm_println_int")] =
        llvm::JITEvaluatedSymbol::fromPointer(&cm_println_int);
    symbols[jit_->mangleAndIntern("cm_print_int")] =
        llvm::JITEvaluatedSymbol::fromPointer(&cm_print_int);

    // 文字列関数
    symbols[jit_->mangleAndIntern("cm_string_concat")] =
        llvm::JITEvaluatedSymbol::fromPointer(&cm_string_concat);
    symbols[jit_->mangleAndIntern("cm_string_length")] =
        llvm::JITEvaluatedSymbol::fromPointer(&cm_string_length);
    symbols[jit_->mangleAndIntern("cm_format_unescape_braces")] =
        llvm::JITEvaluatedSymbol::fromPointer(&cm_format_unescape_braces);
    symbols[jit_->mangleAndIntern("cm_format_replace_int")] =
        llvm::JITEvaluatedSymbol::fromPointer(&cm_format_replace_int);
    symbols[jit_->mangleAndIntern("cm_format_replace_string")] =
        llvm::JITEvaluatedSymbol::fromPointer(&cm_format_replace_string);

    // スライス関数
    symbols[jit_->mangleAndIntern("cm_slice_new")] =
        llvm::JITEvaluatedSymbol::fromPointer(&cm_slice_new);
    symbols[jit_->mangleAndIntern("cm_slice_push")] =
        llvm::JITEvaluatedSymbol::fromPointer(&cm_slice_push);
    symbols[jit_->mangleAndIntern("cm_slice_len")] =
        llvm::JITEvaluatedSymbol::fromPointer(&cm_slice_len);

    // メモリ関数
    symbols[jit_->mangleAndIntern("cm_alloc")] =
        llvm::JITEvaluatedSymbol::fromPointer(&cm_alloc);
    symbols[jit_->mangleAndIntern("cm_free")] =
        llvm::JITEvaluatedSymbol::fromPointer(&cm_free);

    // libc関数
    symbols[jit_->mangleAndIntern("printf")] =
        llvm::JITEvaluatedSymbol::fromPointer(&printf);
    symbols[jit_->mangleAndIntern("puts")] =
        llvm::JITEvaluatedSymbol::fromPointer(&puts);
    symbols[jit_->mangleAndIntern("malloc")] =
        llvm::JITEvaluatedSymbol::fromPointer(&malloc);
    symbols[jit_->mangleAndIntern("free")] =
        llvm::JITEvaluatedSymbol::fromPointer(&free);
    symbols[jit_->mangleAndIntern("memcpy")] =
        llvm::JITEvaluatedSymbol::fromPointer(&memcpy);
    symbols[jit_->mangleAndIntern("memmove")] =
        llvm::JITEvaluatedSymbol::fromPointer(&memmove);
    symbols[jit_->mangleAndIntern("memset")] =
        llvm::JITEvaluatedSymbol::fromPointer(&memset);

    // シンボルをJITDylibに定義
    auto err = mainJD.define(llvm::orc::absoluteSymbols(std::move(symbols)));
    if (err) {
        llvm::errs() << "Failed to define runtime symbols: " << err << "\n";
    }
}

JITResult JITEngine::execute(const mir::MirProgram& program,
                             const std::string& entryPoint) {
    JITResult result{0, "", true};

    // JIT初期化
    if (auto err = initializeJIT()) {
        result.success = false;
        result.errorMessage = llvm::toString(std::move(err));
        return result;
    }

    // MIR → LLVM IR 変換
    auto ctx = tsCtx_->getContext();
    llvm_backend::LLVMContext llvmCtx(*ctx, "jit_module");
    llvm_backend::MIRToLLVM converter(llvmCtx);
    converter.convert(program);

    // モジュールを取得
    auto module = llvmCtx.takeModule();

    // モジュール検証
    std::string verifyError;
    llvm::raw_string_ostream verifyStream(verifyError);
    if (llvm::verifyModule(*module, &verifyStream)) {
        result.success = false;
        result.errorMessage = "LLVM module verification failed:\n" + verifyError;
        return result;
    }

    // ThreadSafeModuleを作成してJITに追加
    auto tsm = llvm::orc::ThreadSafeModule(std::move(module), *tsCtx_);
    if (auto err = jit_->addIRModule(std::move(tsm))) {
        result.success = false;
        result.errorMessage = llvm::toString(std::move(err));
        return result;
    }

    // エントリポイントをルックアップ
    auto mainSymbol = jit_->lookup(entryPoint);
    if (!mainSymbol) {
        result.success = false;
        result.errorMessage = "Entry point '" + entryPoint + "' not found: " +
                              llvm::toString(mainSymbol.takeError());
        return result;
    }

    // 関数ポインタを取得して実行
    using MainFnType = int (*)();
    auto mainFn = mainSymbol->toPtr<MainFnType>();

    // main()を実行
    try {
        result.exitCode = mainFn();
    } catch (const std::exception& e) {
        result.success = false;
        result.errorMessage = std::string("Runtime exception: ") + e.what();
    } catch (...) {
        result.success = false;
        result.errorMessage = "Unknown runtime exception";
    }

    return result;
}

}  // namespace cm::codegen::jit
```

### 2.3 jit_runtime.hpp（オプション）

```cpp
#pragma once

// JIT実行時に必要な追加ランタイム機能があればここに追加
// 基本的には既存の runtime/cm_runtime.h を使用

namespace cm::codegen::jit {

// JIT専用のランタイム初期化（必要に応じて）
void initializeRuntime();

// JIT専用のランタイムクリーンアップ（必要に応じて）
void cleanupRuntime();

}  // namespace cm::codegen::jit
```

---

## 3. CLIインテグレーション

### 3.1 main.cpp への追加

`src/main.cpp`に以下を追加：

```cpp
// ヘッダー追加
#include "codegen/jit/jit_engine.hpp"

// runコマンドでJITモードフラグ追加
bool useJIT = false;  // --jitフラグ

// runコマンド処理部分
if (command == "run") {
    // パース → HIR → MIR の既存処理
    // ...

    if (useJIT) {
        // JIT実行
        codegen::jit::JITEngine engine;
        auto result = engine.execute(mirProgram);
        if (!result.success) {
            std::cerr << result.errorMessage << std::endl;
            return 1;
        }
        return result.exitCode;
    } else {
        // 既存インタープリター実行
        mir::interp::Interpreter interp;
        return interp.execute(mirProgram);
    }
}
```

### 3.2 コマンドラインオプション

| オプション | 短縮形 | 説明 |
|-----------|--------|------|
| `--jit` | `-j` | JITコンパイラで実行 |
| `--interpreter` | `-i` | インタープリターで実行（デフォルト） |

**使用例：**
```bash
# JIT実行
./cm run --jit program.cm

# インタープリター実行（デフォルト）
./cm run program.cm
./cm run --interpreter program.cm
```

---

## 4. テストインフラ変更

### 4.1 unified_test_runner.sh への追加

`tests/unified_test_runner.sh` の133行目付近を修正：

```bash
# バックエンド検証（jitを追加）
if [[ ! "$BACKEND" =~ ^(interpreter|typescript|rust|cpp|llvm|llvm-wasm|js|jit)$ ]]; then
    echo "Error: Invalid backend '$BACKEND'"
    echo "Valid backends: interpreter, typescript, rust, cpp, llvm, llvm-wasm, js, jit"
    exit 1
fi
```

case文に追加（530行目付近）：

```bash
        jit)
            # JITバックエンドで実行
            local test_dir="$(dirname "$test_file")"
            local test_basename="$(basename "$test_file")"

            # JITで実行
            (cd "$test_dir" && run_with_timeout "$CM_EXECUTABLE" run --jit -O$OPT_LEVEL "$test_basename" > "$output_file" 2>&1) || exit_code=$?
            ;;
```

### 4.2 Makefileへの追加

```makefile
# ========================================
# JIT Backend Test Commands
# ========================================

# JIT テスト（デフォルトはO3）
.PHONY: test-jit
test-jit:
	@echo "Running JIT tests (O3)..."
	@chmod +x tests/unified_test_runner.sh
	@OPT_LEVEL=3 tests/unified_test_runner.sh -b jit

# JIT テスト（並列）
.PHONY: test-jit-parallel
test-jit-parallel:
	@echo "Running JIT tests (parallel, O3)..."
	@chmod +x tests/unified_test_runner.sh
	@OPT_LEVEL=3 tests/unified_test_runner.sh -b jit -p

# JIT最適化レベル別テスト（パラレル）
.PHONY: test-jit-o0-parallel
test-jit-o0-parallel:
	@echo "Running JIT tests (O0, parallel)..."
	@chmod +x tests/unified_test_runner.sh
	@OPT_LEVEL=0 tests/unified_test_runner.sh -b jit -p

.PHONY: test-jit-o1-parallel
test-jit-o1-parallel:
	@echo "Running JIT tests (O1, parallel)..."
	@chmod +x tests/unified_test_runner.sh
	@OPT_LEVEL=1 tests/unified_test_runner.sh -b jit -p

.PHONY: test-jit-o2-parallel
test-jit-o2-parallel:
	@echo "Running JIT tests (O2, parallel)..."
	@chmod +x tests/unified_test_runner.sh
	@OPT_LEVEL=2 tests/unified_test_runner.sh -b jit -p

.PHONY: test-jit-o3-parallel
test-jit-o3-parallel:
	@echo "Running JIT tests (O3, parallel)..."
	@chmod +x tests/unified_test_runner.sh
	@OPT_LEVEL=3 tests/unified_test_runner.sh -b jit -p

# ========================================
# JIT Shortcuts
# ========================================

.PHONY: tji
tji: test-jit

.PHONY: tjip
tjip: test-jit-parallel

.PHONY: tjip0
tjip0: test-jit-o0-parallel

.PHONY: tjip1
tjip1: test-jit-o1-parallel

.PHONY: tjip2
tjip2: test-jit-o2-parallel

.PHONY: tjip3
tjip3: test-jit-o3-parallel
```

---

## 5. CMakeビルド設定

### 5.1 src/codegen/jit/CMakeLists.txt

```cmake
# JIT Compiler source files
set(JIT_SOURCES
    jit_engine.cpp
    jit_runtime.cpp
)

# JIT用のヘッダー
set(JIT_HEADERS
    jit_engine.hpp
    jit_runtime.hpp
)

# ソースをメインターゲットに追加
# （親CMakeLists.txtでincludeされる）
target_sources(cm PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/jit_engine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/jit_runtime.cpp
)

# ORC JIT用のコンポーネントが必要
# 親CMakeLists.txtでllvm_map_componentsにOrcJITを追加すること
```

### 5.2 CMakeLists.txt（メイン）への変更

```cmake
# LLVM必要コンポーネントにOrcJITを追加
llvm_map_components_to_libnames(LLVM_LIBS
    core
    support
    irreader
    executionengine
    orcjit              # ← 追加
    native
    passes
    codegen
    # ... 既存のコンポーネント
)
```

---

## 6. 実装フェーズ

### Phase 1: 基本JITエンジン（1日）

1. `src/codegen/jit/` ディレクトリ作成
2. `jit_engine.hpp/cpp` 基本実装
3. CMake設定追加
4. 単純な`hello.cm`でのテスト

**検証コマンド：**
```bash
make build
./cm run --jit tests/test_programs/basic/hello.cm
```

### Phase 2: ランタイムシンボル登録（1日）

1. 全ランタイム関数の明示的登録
2. libc関数の登録
3. 動的シンボル解決の設定

**検証コマンド：**
```bash
./cm run --jit tests/test_programs/io/println.cm
./cm run --jit tests/test_programs/string/string_concat.cm
```

### Phase 3: テストインフラ統合（0.5日）

1. `unified_test_runner.sh`にjitバックエンド追加
2. Makefileにtest-jit*コマンド追加
3. 基本テストでの検証

**検証コマンド：**
```bash
make tjip  # test-jit-parallel
```

### Phase 4: 全テストパス達成（1-2日）

1. 失敗テストの修正
2. 不足しているランタイム関数の追加
3. 最終検証

**成功条件：**
```bash
make tjip
# → tip/tlpと同等のパス数達成
```

---

## 7. 成功基準

| 項目 | 基準 |
|------|------|
| `make tjip` | tip/tlpと同じパス数 |
| パフォーマンス | インタープリター比10倍以上高速 |
| コンパイル時間 | 1秒未満（小規模プログラム） |
| メモリ使用 | LLVM AOTと同等以下 |

---

## 8. リスクと対策

| リスク | 対策 |
|--------|------|
| シンボル解決失敗 | ログ出力を追加し、不足シンボルを特定 |
| JIT初期化遅延 | 遅延初期化パターンを使用 |
| プラットフォーム依存 | macOS/Linuxで動作確認、Windowsは後回し |
| LLVM API変更 | LLVM 15-18での互換性確認 |

---

## 9. 参考実装

既存の`MIRToLLVM`クラスはそのまま使用します。JITでは以下が異なります：

| 項目 | AOT (llvm) | JIT |
|------|-----------|-----|
| 出力 | 実行ファイル | メモリ上実行 |
| リンク | clang/ld | ORC JITLink |
| シンボル解決 | リンク時 | 実行時 |
| 最適化 | llc/opt | JIT内蔵 |

---

## 10. ファイル変更一覧

| ファイル | 変更内容 |
|----------|---------|
| `src/codegen/jit/jit_engine.hpp` | 新規作成 |
| `src/codegen/jit/jit_engine.cpp` | 新規作成 |
| `src/codegen/jit/jit_runtime.hpp` | 新規作成 |
| `src/codegen/jit/jit_runtime.cpp` | 新規作成 |
| `src/codegen/jit/CMakeLists.txt` | 新規作成 |
| `src/main.cpp` | `--jit`オプション追加 |
| `CMakeLists.txt` | OrcJITコンポーネント追加、jitディレクトリ追加 |
| `tests/unified_test_runner.sh` | jitバックエンド追加 |
| `Makefile` | test-jit*コマンド追加 |
