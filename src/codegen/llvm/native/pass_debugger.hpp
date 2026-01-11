#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <llvm/IR/Module.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/IPO.h>
#include <llvm/Transforms/InstCombine/InstCombine.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Transforms/Scalar/GVN.h>
#include <llvm/Transforms/Scalar/Reassociate.h>
#include <llvm/Transforms/Scalar/SimplifyCFG.h>
#include <llvm/Transforms/Utils.h>
#include <llvm/Transforms/Vectorize.h>
#include <string>
#include <thread>
#include <vector>

namespace cm::codegen::llvm_backend {

// 個別パスの実行とタイムアウト検出
class PassDebugger {
   public:
    struct PassResult {
        std::string passName;
        bool success;
        bool timeout;
        double elapsedMs;
        std::string error;
    };

    static std::vector<PassResult> runPassesWithTimeout(llvm::Module& module,
                                                        llvm::PassBuilder& passBuilder,
                                                        llvm::OptimizationLevel optLevel,
                                                        int timeoutMs = 5000) {
        std::vector<PassResult> results;

        // O2/O3で一般的に使用される主要パス
        std::vector<std::pair<std::string, std::function<void(llvm::Module&)>>> passes;

        if (optLevel == llvm::OptimizationLevel::O2 || optLevel == llvm::OptimizationLevel::O3) {
            // 問題を起こしやすいパスを個別にテスト
            passes = {
                {"InstCombine",
                 [&](llvm::Module& m) {
                     llvm::FunctionPassManager FPM;
                     FPM.addPass(llvm::InstCombinePass());

                     llvm::ModuleAnalysisManager MAM;
                     llvm::FunctionAnalysisManager FAM;
                     llvm::CGSCCAnalysisManager CGAM;
                     llvm::LoopAnalysisManager LAM;

                     passBuilder.registerModuleAnalyses(MAM);
                     passBuilder.registerFunctionAnalyses(FAM);
                     passBuilder.registerCGSCCAnalyses(CGAM);
                     passBuilder.registerLoopAnalyses(LAM);
                     passBuilder.crossRegisterProxies(LAM, FAM, CGAM, MAM);

                     for (auto& F : m) {
                         if (!F.isDeclaration()) {
                             FPM.run(F, FAM);
                         }
                     }
                 }},

                {"SimplifyCFG",
                 [&](llvm::Module& m) {
                     llvm::FunctionPassManager FPM;
                     FPM.addPass(llvm::SimplifyCFGPass());

                     llvm::ModuleAnalysisManager MAM;
                     llvm::FunctionAnalysisManager FAM;
                     llvm::CGSCCAnalysisManager CGAM;
                     llvm::LoopAnalysisManager LAM;

                     passBuilder.registerModuleAnalyses(MAM);
                     passBuilder.registerFunctionAnalyses(FAM);
                     passBuilder.registerCGSCCAnalyses(CGAM);
                     passBuilder.registerLoopAnalyses(LAM);
                     passBuilder.crossRegisterProxies(LAM, FAM, CGAM, MAM);

                     for (auto& F : m) {
                         if (!F.isDeclaration()) {
                             FPM.run(F, FAM);
                         }
                     }
                 }},

                {"GVN",
                 [&](llvm::Module& m) {
                     llvm::FunctionPassManager FPM;
                     FPM.addPass(llvm::GVNPass());

                     llvm::ModuleAnalysisManager MAM;
                     llvm::FunctionAnalysisManager FAM;
                     llvm::CGSCCAnalysisManager CGAM;
                     llvm::LoopAnalysisManager LAM;

                     passBuilder.registerModuleAnalyses(MAM);
                     passBuilder.registerFunctionAnalyses(FAM);
                     passBuilder.registerCGSCCAnalyses(CGAM);
                     passBuilder.registerLoopAnalyses(LAM);
                     passBuilder.crossRegisterProxies(LAM, FAM, CGAM, MAM);

                     for (auto& F : m) {
                         if (!F.isDeclaration()) {
                             FPM.run(F, FAM);
                         }
                     }
                 }},

                // ベクトル化パスはLLVM17では直接利用できないため、コメントアウト
                // LoopVectorizeとSLPVectorizeは、PassBuilderを通じて間接的に追加される
                /*
                {"LoopVectorize", [&](llvm::Module& m) {
                    if (optLevel == llvm::OptimizationLevel::O3) {
                        // LoopVectorizePassは直接追加できない
                        // PassBuilderのパイプラインで自動的に含まれる
                    }
                }},

                {"SLPVectorize", [&](llvm::Module& m) {
                    if (optLevel == llvm::OptimizationLevel::O3) {
                        // SLPVectorizerPassは直接追加できない
                        // PassBuilderのパイプラインで自動的に含まれる
                    }
                }}
                */
            };
        }

        // 各パスを個別に実行
        for (const auto& [passName, runPass] : passes) {
            PassResult result;
            result.passName = passName;
            result.success = false;
            result.timeout = false;

            auto start = std::chrono::high_resolution_clock::now();

            try {
                // タイムアウト機構（簡易版）
                std::atomic<bool> completed{false};
                std::thread worker([&]() {
                    try {
                        runPass(module);
                        completed = true;
                    } catch (const std::exception& e) {
                        result.error = e.what();
                    }
                });

                // タイムアウト待機
                auto timeout = std::chrono::milliseconds(timeoutMs);
                auto endTime = start + timeout;

                while (!completed && std::chrono::high_resolution_clock::now() < endTime) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }

                if (!completed) {
                    result.timeout = true;
                    result.error = "Pass timeout after " + std::to_string(timeoutMs) + "ms";
                    // 注意: スレッドのデタッチは危険だが、デバッグ用として使用
                    worker.detach();
                } else {
                    worker.join();
                    result.success = true;
                }

            } catch (const std::exception& e) {
                result.error = e.what();
            }

            auto end = std::chrono::high_resolution_clock::now();
            result.elapsedMs = std::chrono::duration<double, std::milli>(end - start).count();

            results.push_back(result);

            // タイムアウトした場合は以降のパスをスキップ
            if (result.timeout) {
                llvm::errs() << "[PASS_DEBUG] Pass '" << passName
                             << "' timed out! Skipping remaining passes.\n";
                break;
            }
        }

        return results;
    }

    // デバッグ結果の出力
    static void printResults(const std::vector<PassResult>& results) {
        llvm::errs() << "\n[PASS_DEBUG] ===== Pass Execution Results =====\n";
        for (const auto& result : results) {
            llvm::errs() << "[PASS_DEBUG] " << result.passName << ": ";
            if (result.timeout) {
                llvm::errs() << "TIMEOUT (" << result.elapsedMs << "ms)\n";
                llvm::errs() << "  Error: " << result.error << "\n";
            } else if (result.success) {
                llvm::errs() << "SUCCESS (" << result.elapsedMs << "ms)\n";
            } else {
                llvm::errs() << "FAILED\n";
                if (!result.error.empty()) {
                    llvm::errs() << "  Error: " << result.error << "\n";
                }
            }
        }
        llvm::errs() << "[PASS_DEBUG] ==================================\n\n";
    }
};

}  // namespace cm::codegen::llvm_backend