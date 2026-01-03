#pragma once

#include "../../../mir/nodes.hpp"
#include <iostream>
#include <string>

namespace cm::codegen::llvm_backend {

// MIRレベルでパターンを検出し、最適化レベルを調整
class MIRPatternDetector {
   public:
    // MIRプログラムから問題のあるパターンを検出
    static int adjustOptimizationLevel(const mir::MirProgram& program, int requested_level) {
        if (requested_level == 0) {
            return 0;  // O0はそのまま
        }

        // クロージャとイテレータのカウント
        size_t closure_count = 0;
        size_t iterator_count = 0;
        size_t lambda_count = 0;
        size_t map_filter_count = 0;

        // 関数名からパターンを検出
        for (const auto& func : program.functions) {
            std::string name = func->name;

            if (name.find("closure") != std::string::npos ||
                name.find("$_") != std::string::npos) {
                closure_count++;
            }

            if (name.find("iter") != std::string::npos ||
                name.find("Iterator") != std::string::npos ||
                name.find("next") != std::string::npos) {
                iterator_count++;
            }

            if (name.find("lambda") != std::string::npos ||
                name.find("anon") != std::string::npos) {
                lambda_count++;
            }

            if (name.find("map") != std::string::npos ||
                name.find("filter") != std::string::npos ||
                name.find("fold") != std::string::npos ||
                name.find("reduce") != std::string::npos) {
                map_filter_count++;
            }
        }

        // iter_closureパターンの検出（厳密）
        bool has_iter_closure_pattern = false;

        // イテレータとクロージャが両方存在し、map/filter操作がある
        if (iterator_count > 0 && closure_count > 0 && map_filter_count > 0) {
            has_iter_closure_pattern = true;
            std::cerr << "[MIR_PATTERN] iter_closureパターンを検出:\n";
            std::cerr << "  - イテレータ関数: " << iterator_count << "\n";
            std::cerr << "  - クロージャ関数: " << closure_count << "\n";
            std::cerr << "  - map/filter操作: " << map_filter_count << "\n";
        }

        // 複雑度の高いクロージャパターン
        if (closure_count > 5 || lambda_count > 3) {
            std::cerr << "[MIR_PATTERN] 複雑なクロージャパターンを検出\n";
            if (requested_level >= 3) {
                std::cerr << "[MIR_PATTERN] O3からO1に最適化レベルを下げます\n";
                return 1;
            }
        }

        // iter_closureパターンが検出された場合
        if (has_iter_closure_pattern) {
            if (requested_level >= 2) {
                std::cerr << "[MIR_PATTERN] 警告: iter_closureパターンにより最適化問題が発生する可能性があります\n";
                std::cerr << "[MIR_PATTERN] 最適化レベルをO" << requested_level << "からO0に変更します\n";
                return 0;  // 最適化を完全に無効化
            }
        }

        // main関数の複雑度チェック
        for (const auto& func : program.functions) {
            if (func->name == "main" && func->basic_blocks.size() > 0) {
                auto& first_block = *func->basic_blocks[0];

                // main関数の最初のブロックが大きすぎる場合
                if (first_block.statements.size() > 100) {
                    std::cerr << "[MIR_PATTERN] main関数の最初のブロックが大きすぎます（"
                             << first_block.statements.size() << " statements）\n";
                    if (requested_level >= 3) {
                        std::cerr << "[MIR_PATTERN] O3からO2に最適化レベルを下げます\n";
                        return 2;
                    }
                }
            }
        }

        return requested_level;  // 変更なし
    }

    // デバッグ情報の出力
    static void printStatistics(const mir::MirProgram& program) {
        std::cerr << "[MIR_PATTERN] === MIRプログラム統計 ===\n";
        std::cerr << "  関数数: " << program.functions.size() << "\n";

        size_t total_blocks = 0;
        size_t total_statements = 0;

        for (const auto& func : program.functions) {
            total_blocks += func->basic_blocks.size();
            for (const auto& block : func->basic_blocks) {
                total_statements += block->statements.size();
            }
        }

        std::cerr << "  基本ブロック総数: " << total_blocks << "\n";
        std::cerr << "  ステートメント総数: " << total_statements << "\n";
        std::cerr << "================================\n";
    }
};

}  // namespace cm::codegen::llvm_backend