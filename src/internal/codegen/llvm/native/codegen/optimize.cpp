// LLVMネイティブバックエンドの最適化処理
// 最適化パイプラインの実行（optimize）とサニタイザ計装・サニタイザリンク用ドライバ探索を担う
#include "internal/codegen/llvm/native/codegen.hpp"
#include "internal/codegen/llvm/native/pass_debugger.hpp"
#include "internal/codegen/llvm/optimizations/optimization_manager.hpp"
#include "internal/codegen/llvm/optimizations/pass_limiter.hpp"
#include "internal/codegen/llvm/optimizations/recursion_limiter.hpp"

#include <llvm/Transforms/Instrumentation/AddressSanitizer.h>
#include <llvm/Transforms/Instrumentation/BoundsChecking.h>
#include <llvm/Transforms/Instrumentation/MemorySanitizer.h>
#include <llvm/Transforms/Instrumentation/ThreadSanitizer.h>
#include <string>
#include <vector>

namespace cm::codegen::llvm_backend {

// 最適化
void LLVMCodeGen::optimize() {
    // Bug#13修正: UEFIターゲットでは全最適化をスキップ
    // O2のインライン展開+DCEがefi_mainの制御フローを破壊する
    if (context->getTargetConfig().target == BuildTarget::BaremetalUEFI) {
        return;
    }

    if (options.optimizationLevel == 0) {
        return;
    }

    // 再帰とループの事前検証
    RecursionLimiter::preprocessModule(context->getModule(), options.optimizationLevel);

    // パターンベースの最適化レベル調整
    int adjustedLevel = OptimizationPassLimiter::adjustOptimizationLevel(context->getModule(),
                                                                         options.optimizationLevel);
    if (adjustedLevel != options.optimizationLevel) {
        cm::debug::codegen::log(cm::debug::codegen::Id::LLVMOptimize,
                                "Optimization level adjusted from O" +
                                    std::to_string(options.optimizationLevel) + " to O" +
                                    std::to_string(adjustedLevel));
        options.optimizationLevel = adjustedLevel;

        if (adjustedLevel == 0) {
            cm::debug::codegen::log(cm::debug::codegen::Id::LLVMOptimize,
                                    "Skipping optimization due to complexity patterns");
            return;
        }
    }

    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMOptimize,
                            "Level " + std::to_string(options.optimizationLevel));

    // カスタム最適化を使用する場合
    if (options.useCustomOptimizations) {
        using namespace cm::codegen::llvm_backend::optimizations;

        OptimizationManager::OptLevel customLevel;
        switch (options.optimizationLevel) {
            case 1:
                customLevel = OptimizationManager::OptLevel::O1;
                break;
            case 2:
                customLevel = OptimizationManager::OptLevel::O2;
                break;
            case 3:
                customLevel = OptimizationManager::OptLevel::O3;
                break;
            case -1:
                customLevel = OptimizationManager::OptLevel::Oz;
                break;
            default:
                customLevel = OptimizationManager::OptLevel::O2;
        }

        auto config = createConfigFromLevel(customLevel);

        if (context->getTargetConfig().target == BuildTarget::Wasm) {
            config = createConfigForTarget("wasm32");
        } else if (context->getTargetConfig().target == BuildTarget::Baremetal) {
            config.level = OptimizationManager::OptLevel::Os;
            config.enableVectorization = false;
        }

        config.printStatistics = options.verbose;

        OptimizationManager optManager(config);
        optManager.optimizeModule(context->getModule());

        if (options.verbose) {
            llvm::errs() << "\n[Custom Optimizations Complete]\n";
        }
    }

    // PassBuilder設定。
    // O2以上ではMergeFunctions（同一コード折り畳み・ICF）を有効化する（M10）。
    // モノモーフ化はレイアウト同一の特殊化（pick__int/pick__uint等）を別関数として複製するため、証明可能に同一なIR本体をLLVMパスで正準実装へのサンクに折り畳み、生成物サイズと後段コンパイル時間の乗算的膨張を抑える。
    // 注: 設計文書の「int/uint特殊化のフロント側エイリアス化」案は、符号性が比較・除算・拡張の意味論を変えるため不採用（同一レイアウト≠同一意味論）。
    // MergeFunctionsは本体が構造的に完全同一の場合のみ折り畳むため安全
    llvm::TargetMachine* TM = targetManager ? targetManager->getTargetMachine() : nullptr;
    llvm::PipelineTuningOptions pipelineTuning;
    pipelineTuning.MergeFunctions = (options.optimizationLevel >= 2);
    llvm::PassBuilder passBuilder(TM, pipelineTuning);

    // 解析マネージャ
    llvm::LoopAnalysisManager LAM;
    llvm::FunctionAnalysisManager FAM;
    llvm::CGSCCAnalysisManager CGAM;
    llvm::ModuleAnalysisManager MAM;

    passBuilder.registerModuleAnalyses(MAM);
    passBuilder.registerCGSCCAnalyses(CGAM);
    passBuilder.registerFunctionAnalyses(FAM);
    passBuilder.registerLoopAnalyses(LAM);
    passBuilder.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    // 最適化レベル
    llvm::OptimizationLevel optLevel;
    switch (options.optimizationLevel) {
        case 1:
            optLevel = llvm::OptimizationLevel::O1;
            break;
        case 2:
            optLevel = llvm::OptimizationLevel::O2;
            break;
        case 3:
            optLevel = llvm::OptimizationLevel::O3;
            break;
        case -1:
            optLevel = llvm::OptimizationLevel::Oz;
            break;
        default:
            optLevel = llvm::OptimizationLevel::O2;
    }

    // verboseモードでのパステスト
    if (options.verbose && (options.optimizationLevel >= 2)) {
        llvm::errs() << "[PASS_DEBUG] Running individual pass debugging for O"
                     << options.optimizationLevel << "\n";
        auto results =
            PassDebugger::runPassesWithTimeout(context->getModule(), passBuilder, optLevel, 5000);
        cm::codegen::llvm_backend::PassDebugger::printResults(results);

        bool hasTimeout = false;
        for (const auto& result : results) {
            if (result.timeout) {
                hasTimeout = true;
                llvm::errs() << "[PASS_DEBUG] Detected timeout in pass: " << result.passName
                             << "\n";
                llvm::errs() << "[PASS_DEBUG] Falling back to O1 optimization\n";
                break;
            }
        }

        if (hasTimeout) {
            optLevel = llvm::OptimizationLevel::O1;
        }
    }

    // モジュールパスマネージャ
    llvm::ModulePassManager MPM;

    // ターゲット別の最適化
    if (context->getTargetConfig().target == BuildTarget::Wasm) {
        MPM = passBuilder.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::Oz);
    } else if (context->getTargetConfig().target == BuildTarget::Baremetal) {
        MPM = passBuilder.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::Os);
    } else if (context->getTargetConfig().target == BuildTarget::BaremetalUEFI) {
        MPM = passBuilder.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2);
    } else {
        MPM = passBuilder.buildPerModuleDefaultPipeline(optLevel);
    }

    MPM.run(context->getModule(), MAM);

    if (options.verbose) {
        llvm::errs() << "=== Optimized LLVM IR ===\n";
        context->getModule().print(llvm::errs(), nullptr);
        llvm::errs() << "========================\n";
    }

    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMOptimizeEnd);
}

// サニタイザリンク用のclang++ドライバ探索
// LLVM計装が参照するランタイム記号（__asan_version_mismatch_check_v8等）を持つHomebrew LLVMのclang++を新しい順に探索する。
// 古いcompiler-rt（例: LLVM 17）は新しいmacOS（26.x）で初期化に失敗するため、バージョン非固定のllvm（最新）を最優先する
std::string LLVMCodeGen::findSanitizerLinkDriver() {
    std::vector<std::string> candidates = {
        "/opt/homebrew/opt/llvm/bin/clang++",
        "/usr/local/opt/llvm/bin/clang++",
        "/opt/homebrew/opt/llvm@17/bin/clang++",
        "/usr/local/opt/llvm@17/bin/clang++",
    };
    for (const auto& path : candidates) {
        if (std::filesystem::exists(path)) {
            return path;
        }
    }
    return "/usr/bin/clang++";
}

// サニタイザ計装
// address/thread/memory: 本体を持つ関数へ対応する sanitize_* 属性を付与してから各LLVM計装パスを実行する（ランタイムはリンク時に -fsanitize= で解決）
// bounds: BoundsCheckingPass（clang -fsanitize=local-bounds 相当）で静的にサイズが分かるメモリアクセスへ境界チェックを挿入し、違反時は llvm.trap で即停止する（ランタイム不要のためnative/wasm両対応）
void LLVMCodeGen::instrumentSanitizers() {
    const bool needsRuntimeSanitizer =
        options.sanitizeAddress || options.sanitizeThread || options.sanitizeMemory;
    if (!needsRuntimeSanitizer && !options.sanitizeBounds) {
        return;
    }

    cm::debug::codegen::log(
        cm::debug::codegen::Id::LLVMOptimize,
        std::string("Sanitizer instrumentation:") + (options.sanitizeAddress ? " address" : "") +
            (options.sanitizeThread ? " thread" : "") + (options.sanitizeMemory ? " memory" : "") +
            (options.sanitizeBounds ? " bounds" : ""));

    if (needsRuntimeSanitizer) {
        for (auto& func : context->getModule()) {
            // 宣言のみの外部関数と純ASMのNaked関数（prologue/epilogueが無くredzone操作が壊れる）は計装しない
            if (func.isDeclaration() || func.hasFnAttribute(llvm::Attribute::Naked)) {
                continue;
            }
            if (options.sanitizeAddress) {
                func.addFnAttr(llvm::Attribute::SanitizeAddress);
            }
            if (options.sanitizeThread) {
                func.addFnAttr(llvm::Attribute::SanitizeThread);
            }
            if (options.sanitizeMemory) {
                func.addFnAttr(llvm::Attribute::SanitizeMemory);
            }
        }
    }

    llvm::PassBuilder passBuilder(targetManager ? targetManager->getTargetMachine() : nullptr);
    llvm::LoopAnalysisManager LAM;
    llvm::FunctionAnalysisManager FAM;
    llvm::CGSCCAnalysisManager CGAM;
    llvm::ModuleAnalysisManager MAM;
    passBuilder.registerModuleAnalyses(MAM);
    passBuilder.registerCGSCCAnalyses(CGAM);
    passBuilder.registerFunctionAnalyses(FAM);
    passBuilder.registerLoopAnalyses(LAM);
    passBuilder.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    llvm::ModulePassManager MPM;
    if (options.sanitizeBounds) {
        MPM.addPass(llvm::createModuleToFunctionPassAdaptor(llvm::BoundsCheckingPass()));
    }
    if (options.sanitizeAddress) {
        MPM.addPass(llvm::AddressSanitizerPass(llvm::AddressSanitizerOptions()));
    }
    if (options.sanitizeThread) {
        MPM.addPass(llvm::ModuleThreadSanitizerPass());
        MPM.addPass(llvm::createModuleToFunctionPassAdaptor(llvm::ThreadSanitizerPass()));
    }
    if (options.sanitizeMemory) {
        MPM.addPass(llvm::MemorySanitizerPass(llvm::MemorySanitizerOptions()));
    }
    MPM.run(context->getModule(), MAM);
}

}  // namespace cm::codegen::llvm_backend
