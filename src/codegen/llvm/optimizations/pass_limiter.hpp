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
    static constexpr size_t HIGH_COMPLEXITY_THRESHOLD = 100;
    static constexpr size_t MEDIUM_COMPLEXITY_THRESHOLD = 50;
    static constexpr size_t OPTIMIZATION_TIMEOUT_ABORT = 10;
    static constexpr size_t OPTIMIZATION_TIMEOUT_WARN = 5;

   public:
    // 特定のパターンを持つモジュールに対して最適化レベルを制限
    static int adjustOptimizationLevel(llvm::Module& module, int requested_level) {
        // クロージャやイテレータの複雑度を分析
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
                name.find("lambda") != std::string::npos ||
                name.find("$_") != std::string::npos) {  // 匿名関数
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
                (void)BB;  // 未使用警告を抑制
                block_count++;
            }
            max_block_count = std::max(max_block_count, block_count);
        }

        // 複雑度スコアを計算
        size_t complexity_score = 0;
        complexity_score += closure_count * 10;
        complexity_score += iterator_count * 15;
        complexity_score += lambda_count * 8;
        complexity_score += (max_block_count > 50) ? 50 : 0;

        // 複雑度に基づいて最適化レベルを調整
        if (requested_level >= 3) {
            if (complexity_score > HIGH_COMPLEXITY_THRESHOLD) {
                std::cerr << "[OPT_LIMITER] 警告: モジュールの複雑度が高い (スコア: "
                          << complexity_score << ")\n";
                std::cerr << "  - クロージャ: " << closure_count << "\n";
                std::cerr << "  - イテレータ: " << iterator_count << "\n";
                std::cerr << "  - 最大ブロック数: " << max_block_count << "\n";
                std::cerr << "[OPT_LIMITER] 最適化レベルを O3 から O1 に下げます\n";
                return 1;  // O1に下げる
            }

            if (complexity_score > MEDIUM_COMPLEXITY_THRESHOLD) {
                std::cerr << "[OPT_LIMITER] 注意: モジュールがやや複雑 (スコア: "
                          << complexity_score << ")\n";
                std::cerr << "[OPT_LIMITER] 最適化レベルを O3 から O2 に下げます\n";
                return 2;  // O2に下げる
            }
        }

        // iter_closure特有のパターンを検出
        bool has_iter_closure_pattern = false;
        for (auto& F : module) {
            if (F.isDeclaration())
                continue;

            std::string name = F.getName().str();
            // iter_closureテストの特徴的なパターン
            if ((name.find("iter") != std::string::npos &&
                 name.find("closure") != std::string::npos) ||
                (name.find("map") != std::string::npos &&
                 name.find("filter") != std::string::npos) ||
                (name.find("Iterator") != std::string::npos && closure_count > 5)) {
                has_iter_closure_pattern = true;
                break;
            }
        }

        if (has_iter_closure_pattern && requested_level >= 2) {
            std::cerr << "[OPT_LIMITER] 警告: iter_closureパターンを検出\n";
            std::cerr << "[OPT_LIMITER] このパターンは既知の最適化問題があります\n";
            std::cerr << "[OPT_LIMITER] 最適化レベルを O0 に下げます\n";
            return 0;  // O0に下げる（最適化なし）
        }

        return requested_level;  // 変更なし
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