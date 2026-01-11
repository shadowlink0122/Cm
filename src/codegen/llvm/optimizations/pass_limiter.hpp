#pragma once

#include <chrono>
#include <iostream>
#include <llvm/Config/llvm-config.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Passes/PassBuilder.h>
#include <map>
#include <string>

namespace cm::codegen::llvm_backend {

// 最適化パスの実行を制限するクラス
class OptimizationPassLimiter {
   private:
    static constexpr size_t MAX_ITERATIONS_PER_PASS = 100;
    static constexpr size_t MAX_TOTAL_ITERATIONS = 1000;
    static constexpr size_t HIGH_COMPLEXITY_THRESHOLD = 200;    // 100→200に緩和
    static constexpr size_t MEDIUM_COMPLEXITY_THRESHOLD = 100;  // 50→100に緩和
    static constexpr size_t OPTIMIZATION_TIMEOUT_ABORT = 10;
    static constexpr size_t OPTIMIZATION_TIMEOUT_WARN = 5;

   public:
    // 特定のパターンを持つモジュールに対して最適化レベルを検証（情報提供のみ）
    // 注意: Cmポリシーに従い、暗黙的なダウングレードは行わない
    static int adjustOptimizationLevel(llvm::Module& module, int requested_level) {
        // クロージャやイテレータの複雑度を分析（情報収集のみ）
        size_t closure_count = 0;
        size_t iterator_count = 0;
        size_t lambda_count = 0;
        size_t max_block_count = 0;

        for (auto& F : module) {
            if (F.isDeclaration())
                continue;

            std::string name = F.getName().str();

            // クロージャパターンの検出
            if (name.find("closure") != std::string::npos ||
                name.find("lambda") != std::string::npos || name.find("$_") != std::string::npos) {
                closure_count++;
            }

            // イテレータパターンの検出
            if (name.find("iter") != std::string::npos || name.find("next") != std::string::npos ||
                name.find("Iterator") != std::string::npos) {
                iterator_count++;
            }

            // ラムダパターンの検出
            if (name.find("lambda") != std::string::npos ||
                name.find("anon") != std::string::npos) {
                lambda_count++;
            }

            // 基本ブロック数を確認
            size_t block_count = 0;
            for (auto& BB : F) {
                (void)BB;
                block_count++;
            }
            max_block_count = std::max(max_block_count, block_count);
        }

        // 複雑度スコアを計算（情報提供のみ）
        size_t complexity_score = 0;
        complexity_score += closure_count * 10;
        complexity_score += iterator_count * 15;
        complexity_score += lambda_count * 8;
        complexity_score += (max_block_count > 50) ? 50 : 0;

        // 複雑度が高い場合は警告のみ（ダウングレードしない）
        if (complexity_score > HIGH_COMPLEXITY_THRESHOLD) {
            std::cerr << "[OPT_INFO] モジュールの複雑度が高い (スコア: " << complexity_score
                      << ")\n";
            std::cerr << "  - クロージャ: " << closure_count << "\n";
            std::cerr << "  - イテレータ: " << iterator_count << "\n";
            std::cerr << "  - 最大ブロック数: " << max_block_count << "\n";
            // 注意: 暗黙的ダウングレードは廃止 - ユーザー指定レベルを維持
        }

        // 常にユーザー指定の最適化レベルを維持
        return requested_level;
    }

    // 問題のある最適化パスを無効化
    static void disableProblematicPasses(llvm::PassBuilder& /* PB */, int /* opt_level */) {
        // 将来的に必要に応じて実装
    }

    // 最適化の実行時間を監視
    static bool shouldAbortOptimization(const std::chrono::steady_clock::time_point& start_time,
                                        const std::string& phase_name) {
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();

        if (seconds > OPTIMIZATION_TIMEOUT_ABORT) {
            std::cerr << "[OPT_LIMITER] エラー: " << phase_name << " が " << seconds
                      << " 秒を超えています\n";
            return true;
        }

        if (seconds > OPTIMIZATION_TIMEOUT_WARN) {
            std::cerr << "[OPT_LIMITER] 警告: " << phase_name << " が " << seconds
                      << " 秒かかっています\n";
        }

        return false;
    }
};

}  // namespace cm::codegen::llvm_backend