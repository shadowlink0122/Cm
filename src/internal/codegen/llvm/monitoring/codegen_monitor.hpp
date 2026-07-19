#pragma once

#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>

namespace cm::codegen {

// コード生成監視クラス
// 同一関数の過剰な再生成（無限ループ）を検出する
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

    // パターン検出
    void detect_pattern(const std::string& func_name, const std::vector<size_t>& history);

   public:
    // 関数の生成開始を記録
    void begin_function(const std::string& func_name, size_t code_hash);

    // 関数の生成終了を記録
    void end_function(const std::string& /* func_name */) {
        // 必要に応じて終了処理
    }

    // 設定の更新
    void set_max_generation(size_t max_gen) { max_generation_per_function = max_gen; }

    void set_max_pattern_repeats(size_t max_repeats) { max_pattern_repeats = max_repeats; }

    // 統計情報の取得
    std::string get_statistics() const;

    // リセット
    void reset();
};

}  // namespace cm::codegen
