#pragma once

#include "../../../mir/nodes.hpp"

#include <iostream>
#include <string>

namespace cm::codegen::llvm_backend {

// MIRレベルでパターンを検出し、最適化レベルを調整
class MIRPatternDetector {
   public:
    // MIRプログラムから問題のあるパターンを検出（情報提供のみ）
    // 注意: Cmポリシーに従い、暗黙的なダウングレードは行わない
    static int adjustOptimizationLevel(const mir::MirProgram& program, int requested_level) {
        if (requested_level == 0) {
            return 0;  // O0はそのまま
        }

        // クロージャとイテレータのカウント（情報収集のみ）
        size_t closure_count = 0;
        size_t iterator_count = 0;
        size_t lambda_count = 0;
        size_t map_filter_count = 0;

        // 関数名からパターンを検出
        for (const auto& func : program.functions) {
            std::string name = func->name;

            if (name.find("closure") != std::string::npos || name.find("$_") != std::string::npos) {
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

            if (name.find("map") != std::string::npos || name.find("filter") != std::string::npos ||
                name.find("fold") != std::string::npos ||
                name.find("reduce") != std::string::npos) {
                map_filter_count++;
            }
        }

        // 複雑なパターンがある場合は警告のみ（ダウングレードしない）
        if (closure_count > 10 || lambda_count > 6) {
            std::cerr << "[MIR_INFO] 複雑なクロージャパターンを検出\n";
            std::cerr << "  - クロージャ: " << closure_count << ", ラムダ: " << lambda_count
                      << "\n";
            // 注意: 暗黙的ダウングレードは廃止
        }

        // iter_closureパターンの検出（警告のみ）
        if (iterator_count > 0 && closure_count > 0 && map_filter_count > 0) {
            std::cerr << "[MIR_INFO] iter_closureパターンを検出:\n";
            std::cerr << "  - イテレータ関数: " << iterator_count << "\n";
            std::cerr << "  - クロージャ関数: " << closure_count << "\n";
            std::cerr << "  - map/filter操作: " << map_filter_count << "\n";
            // 注意: 暗黙的ダウングレードは廃止
        }

        // 常にユーザー指定の最適化レベルを維持
        return requested_level;
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