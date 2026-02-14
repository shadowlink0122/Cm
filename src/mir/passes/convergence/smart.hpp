#pragma once

#include <deque>
#include <sstream>
#include <string>
#include <unordered_map>

namespace cm::mir::optimizations {

// スマート収束マネージャー：変更数パターンで収束を判定
class SmartConvergenceManager {
   public:
    struct ChangeInfo {
        int total_changes = 0;
        int pattern_count = 0;
    };

    enum class State { CONTINUE, LIKELY_CYCLE, CONVERGED, NO_CHANGE };

    // 変更数を追加して状態を判定
    State add_iteration(int change_count);

    // 統計情報を取得
    std::string get_report() const;

    void reset();

   private:
    std::deque<int> recent_changes;
    static constexpr size_t HISTORY_SIZE = 5;

    std::unordered_map<int, int> change_patterns;

    int consecutive_same_changes = 0;
    int last_change_count = -1;

    static constexpr int SAME_PATTERN_THRESHOLD = 2;
    static constexpr int CONVERGENCE_THRESHOLD = 3;
    static constexpr int MINOR_CHANGE_THRESHOLD = 5;
};

}  // namespace cm::mir::optimizations