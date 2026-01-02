#pragma once

#include "../buffered_codegen.hpp"

#include <chrono>
#include <deque>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace cm::codegen {

// バッファベースのブロック監視システム
class BufferedBlockMonitor : public BufferedCodeGenerator {
   public:
    // ブロック訪問情報
    struct BlockVisit {
        std::string block_id;
        size_t visit_count = 0;
        size_t instruction_count = 0;
        std::chrono::steady_clock::time_point last_visit;
    };

    // パターン検出結果
    enum class PatternType {
        NONE,          // パターンなし
        SIMPLE_LOOP,   // 単純ループ（A→A）
        OSCILLATION,   // 振動（A→B→A）
        COMPLEX_CYCLE  // 複雑な循環（A→B→C→A）
    };

   private:
    // 現在の関数とブロック
    std::string current_function;
    std::string current_block;

    // ブロック訪問記録
    std::unordered_map<std::string, BlockVisit> visits;

    // 最近の訪問履歴（パターン検出用）
    std::deque<std::string> recent_blocks;
    static constexpr size_t HISTORY_SIZE = 20;

    // 制限設定
    size_t max_visits_per_block = 100;
    size_t max_total_instructions = 1000000;
    size_t warning_threshold = 50;

    // 統計情報
    size_t total_blocks_visited = 0;
    size_t total_instructions_generated = 0;
    size_t cycle_warnings = 0;

    // パターン検出
    std::unordered_map<std::string, int> pattern_counts;

   public:
    BufferedBlockMonitor() {
        // より厳しい制限を設定
        limits.max_bytes = 50 * 1024 * 1024;                    // 50MB
        limits.max_lines = 500000;                              // 50万行
        limits.max_generation_time = std::chrono::seconds(10);  // 10秒
    }

    // ブロックへの進入
    bool enter_block(const std::string& func_name, const std::string& block_name) {
        current_function = func_name;
        current_block = block_name;

        std::string block_id = func_name + "::" + block_name;

        // 訪問記録を更新
        auto& visit = visits[block_id];
        visit.block_id = block_id;
        visit.visit_count++;
        visit.last_visit = std::chrono::steady_clock::now();

        // 訪問回数チェック
        if (visit.visit_count > max_visits_per_block) {
            set_error("ブロック '" + block_id + "' の訪問回数が上限(" +
                      std::to_string(max_visits_per_block) + ")を超過");
            return false;
        }

        // 警告閾値チェック
        if (visit.visit_count == warning_threshold) {
            std::cerr << "[MONITOR] 警告: ブロック '" << block_id << "' が " << warning_threshold
                      << "回訪問されています\n";
            cycle_warnings++;
        }

        // 履歴に追加
        recent_blocks.push_back(block_id);
        if (recent_blocks.size() > HISTORY_SIZE) {
            recent_blocks.pop_front();
        }

        // パターン検出
        auto pattern = detect_pattern();
        if (pattern != PatternType::NONE) {
            handle_pattern(pattern, block_id);
        }

        // バッファに記録
        append_line("// ENTER: " + block_id);

        total_blocks_visited++;
        return check_limits();
    }

    // ブロックから退出
    void exit_block() {
        if (!current_block.empty()) {
            append_line("// EXIT: " + current_function + "::" + current_block);
        }
        current_block.clear();
    }

    // 命令を追加
    bool add_instruction(const std::string& instruction) {
        if (current_block.empty()) {
            set_error("ブロック外で命令を生成しようとしました");
            return false;
        }

        std::string block_id = current_function + "::" + current_block;
        visits[block_id].instruction_count++;
        total_instructions_generated++;

        // 命令数制限チェック
        if (total_instructions_generated > max_total_instructions) {
            set_error("生成命令数が上限(" + std::to_string(max_total_instructions) + ")を超過");
            return false;
        }

        // バッファに追加
        return append_line("  " + instruction);
    }

    // パターン検出
    PatternType detect_pattern() {
        if (recent_blocks.size() < 3) {
            return PatternType::NONE;
        }

        // 単純ループ検出（A→A）
        if (recent_blocks.back() == recent_blocks[recent_blocks.size() - 2]) {
            return PatternType::SIMPLE_LOOP;
        }

        // 振動検出（A→B→A）
        if (recent_blocks.size() >= 4) {
            if (recent_blocks[recent_blocks.size() - 1] ==
                    recent_blocks[recent_blocks.size() - 3] &&
                recent_blocks[recent_blocks.size() - 2] ==
                    recent_blocks[recent_blocks.size() - 4]) {
                return PatternType::OSCILLATION;
            }
        }

        // 複雑な循環検出
        if (recent_blocks.size() >= 6) {
            // 最後の3ブロックのパターンを探す
            std::string pattern;
            for (size_t i = recent_blocks.size() - 3; i < recent_blocks.size(); i++) {
                pattern += recent_blocks[i] + ";";
            }

            // このパターンが以前に出現したか
            if (++pattern_counts[pattern] >= 3) {
                return PatternType::COMPLEX_CYCLE;
            }
        }

        return PatternType::NONE;
    }

    // パターン処理
    void handle_pattern(PatternType type, const std::string& block_id) {
        switch (type) {
            case PatternType::SIMPLE_LOOP:
                std::cerr << "[MONITOR] 単純ループ検出: " << block_id << "\n";
                break;

            case PatternType::OSCILLATION:
                std::cerr << "[MONITOR] 振動パターン検出: ";
                for (size_t i = recent_blocks.size() - 4; i < recent_blocks.size(); i++) {
                    std::cerr << recent_blocks[i] << " → ";
                }
                std::cerr << "...\n";
                break;

            case PatternType::COMPLEX_CYCLE:
                std::cerr << "[MONITOR] 複雑な循環検出\n";
                // エラーとして扱う
                set_error("複雑な循環パターンが検出されました");
                break;

            default:
                break;
        }
    }

    // 統計情報を取得
    std::string get_monitor_stats() const {
        std::stringstream ss;
        ss << "=== ブロック監視統計 ===\n";
        ss << "総ブロック訪問数: " << total_blocks_visited << "\n";
        ss << "総命令生成数: " << total_instructions_generated << "\n";
        ss << "循環警告数: " << cycle_warnings << "\n";

        // 頻繁に訪問されたブロック
        ss << "\n頻繁に訪問されたブロック:\n";
        for (const auto& [block_id, visit] : visits) {
            if (visit.visit_count > 10) {
                ss << "  " << block_id << ": " << visit.visit_count << "回\n";
            }
        }

        // バッファ統計
        ss << "\nバッファ使用状況:\n";
        ss << "  使用サイズ: " << (stats.total_bytes / 1024) << " KB\n";
        ss << "  生成行数: " << stats.total_lines << "\n";

        return ss.str();
    }

    // 設定
    void configure(size_t max_visits = 100, size_t max_instructions = 1000000,
                   size_t warn_threshold = 50) {
        max_visits_per_block = max_visits;
        max_total_instructions = max_instructions;
        warning_threshold = warn_threshold;
    }

    // リセット
    void reset_monitor() {
        reset();  // BufferedCodeGeneratorのreset
        visits.clear();
        recent_blocks.clear();
        pattern_counts.clear();
        current_function.clear();
        current_block.clear();
        total_blocks_visited = 0;
        total_instructions_generated = 0;
        cycle_warnings = 0;
    }
};

// グローバルモニター（スレッドローカル）
inline thread_local std::unique_ptr<BufferedBlockMonitor> global_block_monitor;

inline BufferedBlockMonitor& get_block_monitor() {
    if (!global_block_monitor) {
        global_block_monitor = std::make_unique<BufferedBlockMonitor>();
    }
    return *global_block_monitor;
}

// RAIIスタイルのブロックガード
class BufferedBlockGuard {
   private:
    BufferedBlockMonitor* monitor;
    bool entered = false;

   public:
    BufferedBlockGuard(const std::string& func_name, const std::string& block_name)
        : monitor(&get_block_monitor()) {
        entered = monitor->enter_block(func_name, block_name);
        if (!entered) {
            throw std::runtime_error("ブロック進入に失敗: " + monitor->get_error_message());
        }
    }

    ~BufferedBlockGuard() {
        if (entered) {
            monitor->exit_block();
        }
    }

    // 命令を追加
    void add_instruction(const std::string& inst) {
        if (!monitor->add_instruction(inst)) {
            throw std::runtime_error("命令追加に失敗: " + monitor->get_error_message());
        }
    }

    // コピー/ムーブ禁止
    BufferedBlockGuard(const BufferedBlockGuard&) = delete;
    BufferedBlockGuard& operator=(const BufferedBlockGuard&) = delete;
    BufferedBlockGuard(BufferedBlockGuard&&) = delete;
    BufferedBlockGuard& operator=(BufferedBlockGuard&&) = delete;
};

}  // namespace cm::codegen