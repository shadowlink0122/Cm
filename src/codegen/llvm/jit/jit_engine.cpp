/// @file jit_engine.cpp
/// @brief LLVM ORC JIT コンパイラエンジン実装

#include "jit_engine.hpp"

#include "../core/context.hpp"
#include "../core/mir_to_llvm.hpp"

#include <llvm/Config/llvm-config.h>
#include <llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h>
#include <llvm/ExecutionEngine/Orc/ObjectLinkingLayer.h>
#include <llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h>
#include <llvm/ExecutionEngine/SectionMemoryManager.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Utils/Cloning.h>

// LLVM 14-16: レガシーPassManager (PassManagerBuilderが存在)
// LLVM 17+: 新PassManager のみ (PassManagerBuilder削除)
#if LLVM_VERSION_MAJOR < 17
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Transforms/IPO.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>
#include <llvm/Transforms/InstCombine/InstCombine.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Transforms/Scalar/GVN.h>
#endif

namespace cm::codegen::jit {

JITEngine::JITEngine() {
    // LLVMターゲット初期化（一度だけ）
    static bool initialized = []() {
        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmPrinter();
        llvm::InitializeNativeTargetAsmParser();
        return true;
    }();
    (void)initialized;

    // Thread-safe context作成
    tsContext_ =
        std::make_unique<llvm::orc::ThreadSafeContext>(std::make_unique<llvm::LLVMContext>());
}

JITEngine::~JITEngine() = default;

llvm::Error JITEngine::initializeJIT() {
    // LLJITビルダーでJIT作成
    auto jitBuilder = llvm::orc::LLJITBuilder();

    // JIT作成
    auto jitExpected = jitBuilder.create();
    if (!jitExpected) {
        return jitExpected.takeError();
    }
    jit_ = std::move(*jitExpected);

    // ランタイムシンボル登録
    registerRuntimeSymbols();

    return llvm::Error::success();
}

llvm::orc::SymbolStringPtr JITEngine::mangle(const std::string& name) {
    return jit_->mangleAndIntern(name);
}

void JITEngine::registerRuntimeSymbols() {
    auto& mainJD = jit_->getMainJITDylib();

    // ホストプロセスからシンボルを動的解決（libc, ランタイム等）
    // これにより printf, malloc, free 等が自動的に解決される
    auto generator = llvm::orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(
        jit_->getDataLayout().getGlobalPrefix());
    if (generator) {
        mainJD.addGenerator(std::move(*generator));
    }

    // 明示的なシンボル登録は不要
    // DynamicLibrarySearchGeneratorがホストプロセスの全シンボルを解決
    // ランタイム関数はLLVM IR内で宣言され、リンク時に解決される
}

/// LLVM最適化パスを適用
/// LLVM 14-16: レガシーPassManagerBuilder
/// LLVM 17+: 新PassBuilder
void JITEngine::optimizeModule(llvm::Module& module, int optLevel) {
    if (optLevel <= 0)
        return;

#if LLVM_VERSION_MAJOR < 17
    // LLVM 14-16: レガシーPassManager を使用
    llvm::legacy::PassManager pm;
    llvm::legacy::FunctionPassManager fpm(&module);

    // PassManagerBuilderで標準最適化パスを構成
    llvm::PassManagerBuilder builder;
    builder.OptLevel = static_cast<unsigned>(optLevel);
    builder.SizeLevel = 0;

    if (optLevel >= 2) {
        builder.Inliner = llvm::createFunctionInliningPass(optLevel, 0, false);
    }

    // 関数パス追加
    builder.populateFunctionPassManager(fpm);
    // モジュールパス追加
    builder.populateModulePassManager(pm);

    // 関数パス実行
    fpm.doInitialization();
    for (auto& func : module) {
        if (!func.isDeclaration()) {
            fpm.run(func);
        }
    }
    fpm.doFinalization();

    // モジュールパス実行
    pm.run(module);
#else
    // LLVM 17+: 新PassBuilder を使用
    llvm::LoopAnalysisManager LAM;
    llvm::FunctionAnalysisManager FAM;
    llvm::CGSCCAnalysisManager CGAM;
    llvm::ModuleAnalysisManager MAM;

    llvm::PassBuilder PB;

    // 分析マネージャーを登録
    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    // 最適化レベルに応じたパイプラインを構築
    llvm::OptimizationLevel level;
    switch (optLevel) {
        case 1:
            level = llvm::OptimizationLevel::O1;
            break;
        case 2:
            level = llvm::OptimizationLevel::O2;
            break;
        case 3:
        default:
            level = llvm::OptimizationLevel::O3;
            break;
    }

    llvm::ModulePassManager MPM = PB.buildPerModuleDefaultPipeline(level);
    MPM.run(module, MAM);
#endif
}

JITResult JITEngine::execute(const mir::MirProgram& program, const std::string& entryPoint,
                             int optLevel) {
    JITResult result;

    // JIT初期化
    if (auto err = initializeJIT()) {
        result.success = false;
        result.errorMessage = llvm::toString(std::move(err));
        return result;
    }

    // MIR → LLVM IR 変換
    // ネイティブ設定を使用
    auto targetConfig = llvm_backend::TargetConfig::getNative();
    llvm_backend::LLVMContext llvmCtx("jit_module", targetConfig);
    llvm_backend::MIRToLLVM converter(llvmCtx);
    converter.convert(program);

    // モジュール取得（所有権を移動するためにclone）
    auto& llvmModule = llvmCtx.getModule();

    // モジュール検証
    std::string verifyError;
    llvm::raw_string_ostream verifyStream(verifyError);
    if (llvm::verifyModule(llvmModule, &verifyStream)) {
        result.success = false;
        result.errorMessage = "LLVM module verification failed:\n" + verifyError;
        return result;
    }

    // モジュールをcloneしてThreadSafeModuleを作成
    auto clonedModule = llvm::CloneModule(llvmModule);

    // LLVM最適化パスを適用
    optimizeModule(*clonedModule, optLevel);

    auto tsm = llvm::orc::ThreadSafeModule(std::move(clonedModule), *tsContext_);
    if (auto err = jit_->addIRModule(std::move(tsm))) {
        result.success = false;
        result.errorMessage = "Failed to add module to JIT: " + llvm::toString(std::move(err));
        return result;
    }

    // エントリポイントをルックアップ
    auto mainSymbol = jit_->lookup(entryPoint);
    if (!mainSymbol) {
        result.success = false;
        result.errorMessage =
            "Entry point '" + entryPoint + "' not found: " + llvm::toString(mainSymbol.takeError());
        return result;
    }

    // 関数ポインタを取得して実行
    // LLVM 14: getAddress(), LLVM 15+: toPtr()
    using MainFnType = int (*)();
#if LLVM_VERSION_MAJOR >= 15
    auto mainFn = mainSymbol->toPtr<MainFnType>();
#else
    auto mainAddr = mainSymbol->getAddress();
    auto mainFn = reinterpret_cast<MainFnType>(static_cast<uintptr_t>(mainAddr));
#endif

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
