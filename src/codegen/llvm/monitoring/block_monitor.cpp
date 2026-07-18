// ============================================================
// BlockMonitor 実装
// ============================================================

#include "block_monitor.hpp"

#include <functional>
#include <stdexcept>

namespace cm::codegen {

void BlockMonitor::enter_block(const std::string& func_name, const std::string& block_name) {
    current_function = func_name;
    current_block = block_name;

    auto& block = function_blocks[func_name][block_name];
    block.visit_count++;

    // 訪問回数チェック
    if (block.visit_count > max_block_visits) {
        throw std::runtime_error("無限ループ検出: ブロック '" + block_name +
                                 "' (関数: " + func_name + ") が" +
                                 std::to_string(max_block_visits) + "回以上訪問されました");
    }
}

void BlockMonitor::exit_block() {
    current_function.clear();
    current_block.clear();
}

void BlockMonitor::add_instruction(const std::string& instruction_text) {
    if (current_function.empty() || current_block.empty()) {
        return;  // ブロック外の命令は無視
    }

    auto& block = function_blocks[current_function][current_block];
    block.instruction_count++;

    // 命令数チェック
    if (block.instruction_count > max_instructions_per_block) {
        throw std::runtime_error(
            "無限ループ検出: ブロック '" + current_block + "' (関数: " + current_function + ") で" +
            std::to_string(max_instructions_per_block) + "個以上の命令が生成されました");
    }

    // 命令のハッシュを計算
    size_t instruction_hash = std::hash<std::string>{}(instruction_text);

    // 連続する同一命令をチェック
    if (block.last_hash == instruction_hash) {
        consecutive_instruction_count[instruction_hash]++;

        if (consecutive_instruction_count[instruction_hash] >= max_duplicate_instructions) {
            throw std::runtime_error(
                "無限ループ検出: ブロック '" + current_block + "' で同じ命令が" +
                std::to_string(consecutive_instruction_count[instruction_hash]) +
                "回連続で生成されました");
        }
    } else {
        consecutive_instruction_count[instruction_hash] = 1;
        block.last_hash = instruction_hash;
    }

    // ハッシュ履歴に追加（最大100個保持）
    block.hash_history.push_back(instruction_hash);
    if (block.hash_history.size() > 100) {
        block.hash_history.erase(block.hash_history.begin());
    }

    // 注意: かつてここで周期的な命令パターンを無限ループとして検出していたが、ループ展開（--funroll-loops）や手書きの反復コードなど正当な繰り返しを
    // 誤検出するため撤去した。暴走対策は命令数上限（max_instructions_per_block / max_duplicate_instructions）が担う
}

std::string BlockMonitor::get_statistics() const {
    std::string stats = "=== Block Statistics ===\n";
    for (const auto& [func_name, blocks] : function_blocks) {
        stats += "Function: " + func_name + "\n";
        for (const auto& [block_name, info] : blocks) {
            stats += "  Block " + block_name + ": " + std::to_string(info.visit_count) +
                     " visits, " + std::to_string(info.instruction_count) + " instructions\n";
        }
    }
    return stats;
}

void BlockMonitor::reset() {
    function_blocks.clear();
    current_function.clear();
    current_block.clear();
    consecutive_instruction_count.clear();
}

}  // namespace cm::codegen
