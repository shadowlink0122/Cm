#include "manager.hpp"

#include <algorithm>

namespace cm::mir::optimizations {

size_t ConvergenceManager::compute_program_hash(const MirProgram& program) {
    std::hash<std::string> hasher;
    size_t hash = 0;

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

bool ConvergenceManager::detect_cycle(size_t current_hash) {
    return std::find(recent_state_hashes.begin(), recent_state_hashes.end(), current_hash) !=
           recent_state_hashes.end();
}

ConvergenceManager::ConvergenceState ConvergenceManager::update_and_check(
    const MirProgram& program, const ChangeMetrics& metrics) {
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

    // 変更量の振動をチェック
    if (metrics_history.size() >= 4) {
        int prev_change = metrics_history[metrics_history.size() - 4].total_changes();
        int curr_change = metrics_history[metrics_history.size() - 3].total_changes();

        if (metrics_history.size() >= 6) {
            int pattern1 = metrics_history[metrics_history.size() - 2].total_changes();
            int pattern2 = metrics_history[metrics_history.size() - 1].total_changes();
            if (prev_change == pattern1 && curr_change == pattern2) {
                return ConvergenceState::CYCLE_DETECTED;
            }
        }

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

std::string ConvergenceManager::get_statistics() const {
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

void ConvergenceManager::reset() {
    recent_state_hashes.clear();
    metrics_history.clear();
    consecutive_minor_changes = 0;
}

}  // namespace cm::mir::optimizations
