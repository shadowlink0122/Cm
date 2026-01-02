#pragma once

#include <chrono>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace cm::codegen {

// コード生成監視クラス
class CodeGenMonitor {
   private:
    // 関数ごとの生成回数
    std::unordered_map<std::string, size_t> generation_counts;

    // パターン検出用のハッシュ履歴
    std::unordered_map<std::string, std::vector<size_t>> pattern_history;

    // 設定値
    size_t max_generation_per_function = 100;  // 関数ごとの最大生成回数
    size_t max_pattern_repeats = 5;            // パターンの最大繰り返し回数

    // タイムスタンプ
    using TimePoint = std::chrono::steady_clock::time_point;
    std::unordered_map<std::string, TimePoint> last_generation_time;

   public:
    // 関数の生成開始を記録
    void begin_function(const std::string& func_name, size_t code_hash) {
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

    // 関数の生成終了を記録
    void end_function(const std::string& func_name) {
        // 必要に応じて終了処理
    }

    // パターン検出
    void detect_pattern(const std::string& func_name, const std::vector<size_t>& history) {
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
                        throw std::runtime_error("無限ループ検出: 関数 '" + func_name + "' で周期" +
                                                 std::to_string(period) + "のパターンが" +
                                                 std::to_string(repeat_count + 1) +
                                                 "回繰り返されました");
                    }
                }
            }
        }
    }

    // 設定の更新
    void set_max_generation(size_t max_gen) { max_generation_per_function = max_gen; }

    void set_max_pattern_repeats(size_t max_repeats) { max_pattern_repeats = max_repeats; }

    // 統計情報の取得
    std::string get_statistics() const {
        std::string stats = "=== CodeGen Statistics ===\n";
        for (const auto& [func_name, count] : generation_counts) {
            stats += "  " + func_name + ": " + std::to_string(count) + " generations\n";
        }
        return stats;
    }

    // リセット
    void reset() {
        generation_counts.clear();
        pattern_history.clear();
        last_generation_time.clear();
    }
};

}  // namespace cm::codegen