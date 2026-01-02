#pragma once

#include "../mir.hpp"

#include <deque>
#include <unordered_map>

namespace cm::mir::optimizations {

// スマート収束マネージャー：変更数パターンで収束を判定
class SmartConvergenceManager {
   public:
    struct ChangeInfo {
        int total_changes = 0;  // 総変更数
        int pattern_count = 0;  // 同じパターンの連続回数
    };

   private:
    // 最近の変更数履歴
    std::deque<int> recent_changes;
    static constexpr size_t HISTORY_SIZE = 5;

    // 変更数パターンの出現回数
    std::unordered_map<int, int> change_patterns;

    // 連続して同じ変更数が出た回数
    int consecutive_same_changes = 0;
    int last_change_count = -1;

    // 収束判定の閾値
    static constexpr int SAME_PATTERN_THRESHOLD = 2;  // 2回連続で同じなら疑う
    static constexpr int CONVERGENCE_THRESHOLD = 3;   // 3回連続で確定
    static constexpr int MINOR_CHANGE_THRESHOLD = 5;  // 5変更以下は軽微

   public:
    enum class State {
        CONTINUE,      // 最適化を継続
        LIKELY_CYCLE,  // 循環の可能性（警告）
        CONVERGED,     // 収束した
        NO_CHANGE      // 変更なし（完全収束）
    };

    // 変更数を追加して状態を判定
    State add_iteration(int change_count) {
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

            // 例: 10変更→10変更→10変更
            if (consecutive_same_changes >= CONVERGENCE_THRESHOLD) {
                return State::CONVERGED;  // 相互干渉で収束
            }
            if (consecutive_same_changes >= SAME_PATTERN_THRESHOLD) {
                return State::LIKELY_CYCLE;  // 循環の可能性
            }
        } else {
            consecutive_same_changes = 0;
        }

        last_change_count = change_count;

        // 変更が軽微で安定している
        if (change_count <= MINOR_CHANGE_THRESHOLD && recent_changes.size() >= 3) {
            // 最近3回の変更がすべて軽微
            bool all_minor = true;
            for (auto it = recent_changes.rbegin(); it != recent_changes.rbegin() + 3; ++it) {
                if (*it > MINOR_CHANGE_THRESHOLD) {
                    all_minor = false;
                    break;
                }
            }
            if (all_minor) {
                return State::CONVERGED;  // 実用的収束
            }
        }

        // パターンの繰り返しを検出
        // 例: 10→15→10→15（振動パターン）
        if (recent_changes.size() >= 4) {
            if (recent_changes[recent_changes.size() - 1] ==
                    recent_changes[recent_changes.size() - 3] &&
                recent_changes[recent_changes.size() - 2] ==
                    recent_changes[recent_changes.size() - 4]) {
                return State::LIKELY_CYCLE;  // 振動パターン
            }
        }

        return State::CONTINUE;
    }

    // 統計情報を取得
    std::string get_report() const {
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

        // 頻出パターン
        ss << "  変更パターン:\n";
        for (const auto& [changes, count] : change_patterns) {
            if (count >= 2) {
                ss << "    " << changes << "変更: " << count << "回\n";
            }
        }

        return ss.str();
    }

    void reset() {
        recent_changes.clear();
        change_patterns.clear();
        consecutive_same_changes = 0;
        last_change_count = -1;
    }
};