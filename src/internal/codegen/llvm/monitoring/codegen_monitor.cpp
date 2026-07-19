// ============================================================
// CodeGenMonitor 実装
// ============================================================

#include "codegen_monitor.hpp"

#include <stdexcept>
#include <string>
#include <vector>

namespace cm::codegen {

void CodeGenMonitor::begin_function(const std::string& func_name, size_t code_hash) {
    generation_counts[func_name]++;

    // 生成回数チェック
    if (generation_counts[func_name] > max_generation_per_function) {
        throw std::runtime_error("無限ループ検出: 関数 '" + func_name + "' が" +
                                 std::to_string(max_generation_per_function) +
                                 "回以上生成されました");
    }

    // パターン履歴に追加
    auto& history = pattern_history[func_name];
    history.push_back(code_hash);

    // パターン検出（最後の10要素をチェック）
    if (history.size() >= 10) {
        detect_pattern(func_name, history);
    }

    // タイムスタンプ記録
    last_generation_time[func_name] = std::chrono::steady_clock::now();
}

void CodeGenMonitor::detect_pattern(const std::string& func_name,
                                    const std::vector<size_t>& history) {
    size_t size = history.size();

    // 周期2〜5のパターンを検出
    for (size_t period = 2; period <= 5 && period * 2 <= size; ++period) {
        bool is_repeating = true;

        // 最後のperiod*2個の要素をチェック
        for (size_t i = 0; i < period; ++i) {
            if (history[size - period - 1 - i] != history[size - 1 - i]) {
                is_repeating = false;
                break;
            }
        }

        if (is_repeating) {
            // 連続する繰り返し回数をカウント
            size_t repeat_count = 0;
            for (size_t i = size - period; i >= period; i -= period) {
                bool match = true;
                for (size_t j = 0; j < period; ++j) {
                    if (i - j - 1 >= history.size() ||
                        history[i - j - 1] != history[size - j - 1]) {
                        match = false;
                        break;
                    }
                }
                if (!match)
                    break;
                repeat_count++;
                if (repeat_count >= max_pattern_repeats) {
                    throw std::runtime_error(
                        "無限ループ検出: 関数 '" + func_name + "' で周期" + std::to_string(period) +
                        "のパターンが" + std::to_string(repeat_count + 1) + "回繰り返されました");
                }
            }
        }
    }
}

std::string CodeGenMonitor::get_statistics() const {
    std::string stats = "=== CodeGen Statistics ===\n";
    for (const auto& [func_name, count] : generation_counts) {
        stats += "  " + func_name + ": " + std::to_string(count) + " generations\n";
    }
    return stats;
}

void CodeGenMonitor::reset() {
    generation_counts.clear();
    pattern_history.clear();
    last_generation_time.clear();
}

}  // namespace cm::codegen
