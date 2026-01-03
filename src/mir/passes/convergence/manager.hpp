#pragma once

#include "../../nodes.hpp"

#include <deque>
#include <functional>
#include <sstream>
#include <unordered_set>

namespace cm::mir::optimizations {

// 収束判定を管理するクラス
class ConvergenceManager {
   public:
    // 変更の影響度メトリクス
    struct ChangeMetrics {
        int instructions_changed = 0;
        int blocks_changed = 0;
        int functions_changed = 0;
        bool cfg_changed = false;  // 制御フローグラフの変更

        int total_changes() const {
            return instructions_changed + blocks_changed * 10 + functions_changed * 100 +
                   (cfg_changed ? 1000 : 0);
        }

        bool is_minor() const { return total_changes() < 10 && !cfg_changed; }
    };

    // 収束状態
    enum class ConvergenceState {
        NOT_CONVERGED,          // まだ収束していない
        CONVERGED,              // 完全に収束
        PRACTICALLY_CONVERGED,  // 実用的に収束（軽微な変更のみ）
        CYCLE_DETECTED          // 循環を検出
    };

   private:
    // 最近の状態のハッシュ（循環検出用）
    std::deque<size_t> recent_state_hashes;
    static constexpr size_t MAX_HISTORY = 8;  // より長い循環も検出可能に

    // 変更メトリクスの履歴
    std::vector<ChangeMetrics> metrics_history;

    // 軽微な変更の連続回数
    int consecutive_minor_changes = 0;
    static constexpr int MINOR_CHANGE_THRESHOLD = 2;

    // プログラムの簡易ハッシュを計算
    size_t compute_program_hash(const MirProgram& program) {
        std::hash<std::string> hasher;
        size_t hash = 0;

        // 各関数の基本ブロック数とインストラクション数でハッシュ
        // （完全なコードのハッシュより軽量）
        for (const auto& func : program.functions) {
            if (!func)
                continue;
            hash ^= hasher(func->name) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            hash ^= func->basic_blocks.size() + 0x9e3779b9 + (hash << 6) + (hash >> 2);

            for (const auto& block : func->basic_blocks) {
                if (!block)
                    continue;
                hash ^= block->statements.size() + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            }
        }
        return hash;
    }

    // 循環を検出
    bool detect_cycle(size_t current_hash) {
        return std::find(recent_state_hashes.begin(), recent_state_hashes.end(), current_hash) !=
               recent_state_hashes.end();
    }

   public:
    // 反復後の状態を更新して収束状態を判定
    ConvergenceState update_and_check(const MirProgram& program, const ChangeMetrics& metrics) {
        // メトリクスを記録
        metrics_history.push_back(metrics);

        // 変更なし → 完全収束
        if (metrics.total_changes() == 0) {
            return ConvergenceState::CONVERGED;
        }

        // プログラムのハッシュを計算
        size_t current_hash = compute_program_hash(program);

        // 循環検出
        if (detect_cycle(current_hash)) {
            return ConvergenceState::CYCLE_DETECTED;
        }

        // ハッシュ履歴を更新
        recent_state_hashes.push_back(current_hash);
        if (recent_state_hashes.size() > MAX_HISTORY) {
            recent_state_hashes.pop_front();
        }

        // 軽微な変更の判定
        if (metrics.is_minor()) {
            consecutive_minor_changes++;
            if (consecutive_minor_changes >= MINOR_CHANGE_THRESHOLD) {
                return ConvergenceState::PRACTICALLY_CONVERGED;
            }
        } else {
            consecutive_minor_changes = 0;
        }

        // 変更量の振動をチェック（最適化が互いに打ち消しあっている）
        if (metrics_history.size() >= 4) {
            // 最近4回の変更量を確認
            bool oscillating = true;
            int prev_change = metrics_history[metrics_history.size() - 4].total_changes();
            int curr_change = metrics_history[metrics_history.size() - 3].total_changes();

            // 変更量が交互に同じパターンを繰り返している場合
            if (metrics_history.size() >= 6) {
                int pattern1 = metrics_history[metrics_history.size() - 2].total_changes();
                int pattern2 = metrics_history[metrics_history.size() - 1].total_changes();
                if (prev_change == pattern1 && curr_change == pattern2) {
                    // 振動を検出
                    return ConvergenceState::CYCLE_DETECTED;
                }
            }

            // 変更量が単調減少している場合
            int recent_total = 0;
            for (size_t i = metrics_history.size() - 3; i < metrics_history.size(); ++i) {
                recent_total += metrics_history[i].total_changes();
            }
            if (recent_total < 20) {
                return ConvergenceState::PRACTICALLY_CONVERGED;
            }
        }

        return ConvergenceState::NOT_CONVERGED;
    }

    // 統計情報を取得
    std::string get_statistics() const {
        std::stringstream ss;
        ss << "収束統計:\n";
        ss << "  反復回数: " << metrics_history.size() << "\n";
        if (!metrics_history.empty()) {
            int total_changes = 0;
            for (const auto& m : metrics_history) {
                total_changes += m.total_changes();
            }
            ss << "  総変更数: " << total_changes << "\n";
            ss << "  最終変更数: " << metrics_history.back().total_changes() << "\n";
        }
        return ss.str();
    }

    void reset() {
        recent_state_hashes.clear();
        metrics_history.clear();
        consecutive_minor_changes = 0;
    }
};

}  // namespace cm::mir::optimizations