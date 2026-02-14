#include "smart.hpp"

namespace cm::mir::optimizations {

SmartConvergenceManager::State SmartConvergenceManager::add_iteration(int change_count) {
    // 変更なし → 完全収束
    if (change_count == 0) {
        return State::NO_CHANGE;
    }

    // 履歴に追加
    recent_changes.push_back(change_count);
    if (recent_changes.size() > HISTORY_SIZE) {
        recent_changes.pop_front();
    }

    // パターンを記録
    change_patterns[change_count]++;

    // 連続同一変更数をチェック
    if (change_count == last_change_count) {
        consecutive_same_changes++;

        if (consecutive_same_changes >= CONVERGENCE_THRESHOLD) {
            return State::CONVERGED;
        }
        if (consecutive_same_changes >= SAME_PATTERN_THRESHOLD) {
            return State::LIKELY_CYCLE;
        }
    } else {
        consecutive_same_changes = 0;
    }

    last_change_count = change_count;

    // 変更が軽微で安定している
    if (change_count <= MINOR_CHANGE_THRESHOLD && recent_changes.size() >= 3) {
        bool all_minor = true;
        for (auto it = recent_changes.rbegin(); it != recent_changes.rbegin() + 3; ++it) {
            if (*it > MINOR_CHANGE_THRESHOLD) {
                all_minor = false;
                break;
            }
        }
        if (all_minor) {
            return State::CONVERGED;
        }
    }

    // パターンの繰り返しを検出
    if (recent_changes.size() >= 4) {
        if (recent_changes[recent_changes.size() - 1] ==
                recent_changes[recent_changes.size() - 3] &&
            recent_changes[recent_changes.size() - 2] ==
                recent_changes[recent_changes.size() - 4]) {
            return State::LIKELY_CYCLE;
        }
    }

    return State::CONTINUE;
}

std::string SmartConvergenceManager::get_report() const {
    std::stringstream ss;
    ss << "収束分析:\n";

    if (recent_changes.empty()) {
        ss << "  データなし\n";
        return ss.str();
    }

    ss << "  最近の変更数: ";
    for (int c : recent_changes) {
        ss << c << " ";
    }
    ss << "\n";

    if (consecutive_same_changes > 0) {
        ss << "  連続同一変更: " << last_change_count << " × " << (consecutive_same_changes + 1)
           << "回\n";
    }

    ss << "  変更パターン:\n";
    for (const auto& [changes, count] : change_patterns) {
        if (count >= 2) {
            ss << "    " << changes << "変更: " << count << "回\n";
        }
    }

    return ss.str();
}

void SmartConvergenceManager::reset() {
    recent_changes.clear();
    change_patterns.clear();
    consecutive_same_changes = 0;
    last_change_count = -1;
}

}  // namespace cm::mir::optimizations
