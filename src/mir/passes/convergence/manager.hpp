#pragma once

#include "../../nodes.hpp"

#include <deque>
#include <functional>
#include <sstream>
#include <unordered_set>
#include <vector>

namespace cm::mir::optimizations {

// 収束判定を管理するクラス
class ConvergenceManager {
   public:
    // 変更の影響度メトリクス
    struct ChangeMetrics {
        int instructions_changed = 0;
        int blocks_changed = 0;
        int functions_changed = 0;
        bool cfg_changed = false;

        int total_changes() const {
            return instructions_changed + blocks_changed * 10 + functions_changed * 100 +
                   (cfg_changed ? 1000 : 0);
        }

        bool is_minor() const { return total_changes() < 10 && !cfg_changed; }
    };

    // 収束状態
    enum class ConvergenceState { NOT_CONVERGED, CONVERGED, PRACTICALLY_CONVERGED, CYCLE_DETECTED };

    // 反復後の状態を更新して収束状態を判定
    ConvergenceState update_and_check(const MirProgram& program, const ChangeMetrics& metrics);

    // 統計情報を取得
    std::string get_statistics() const;

    void reset();

   private:
    std::deque<size_t> recent_state_hashes;
    static constexpr size_t MAX_HISTORY = 8;

    std::vector<ChangeMetrics> metrics_history;

    int consecutive_minor_changes = 0;
    static constexpr int MINOR_CHANGE_THRESHOLD = 2;

    size_t compute_program_hash(const MirProgram& program);
    bool detect_cycle(size_t current_hash);
};

}  // namespace cm::mir::optimizations