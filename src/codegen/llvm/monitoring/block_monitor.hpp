#pragma once

#include <functional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cm::codegen {

// ブロックレベル監視クラス
class BlockMonitor {
   private:
    // ブロック情報の構造体
    struct BlockInfo {
        size_t visit_count = 0;            // 訪問回数
        size_t instruction_count = 0;      // 生成された命令数
        size_t last_hash = 0;              // 最後に生成されたコードのハッシュ
        std::vector<size_t> hash_history;  // ハッシュ履歴
    };

    // 関数ごとのブロック情報
    std::unordered_map<std::string, std::unordered_map<std::string, BlockInfo>> function_blocks;

    // 現在処理中の関数とブロック
    std::string current_function;
    std::string current_block;

    // 設定値
    size_t max_block_visits = 10000;  // ブロックの最大訪問回数（スライスなど複雑な構造のため増加）
    size_t max_instructions_per_block =
        100000;  // ブロックごとの最大命令数（大規模なスライス操作のため増加）
    size_t max_duplicate_instructions = 1000;  // 同じ命令の最大連続生成数（配列初期化などで必要）

    // 連続する同一命令のカウント
    std::unordered_map<size_t, size_t> consecutive_instruction_count;

   public:
    // ブロック処理の開始
    void enter_block(const std::string& func_name, const std::string& block_name) {
        current_function = func_name;
        current_block = block_name;

        auto& block = function_blocks[func_name][block_name];
        block.visit_count++;

        // 訪問回数チェック
        if (block.visit_count > max_block_visits) {
            throw std::runtime_error("無限ループ検出: ブロック '" + block_name +
                                     "' (関数: " + func_name + ") が" +
                                     std::to_string(max_block_visits) + "回以上訪問されました");
        }
    }

    // ブロック処理の終了
    void exit_block() {
        current_function.clear();
        current_block.clear();
    }

    // 命令の追加を記録
    void add_instruction(const std::string& instruction_text) {
        if (current_function.empty() || current_block.empty()) {
            return;  // ブロック外の命令は無視
        }

        auto& block = function_blocks[current_function][current_block];
        block.instruction_count++;

        // 命令数チェック
        if (block.instruction_count > max_instructions_per_block) {
            throw std::runtime_error("無限ループ検出: ブロック '" + current_block +
                                     "' (関数: " + current_function + ") で" +
                                     std::to_string(max_instructions_per_block) +
                                     "個以上の命令が生成されました");
        }

        // 命令のハッシュを計算
        size_t instruction_hash = std::hash<std::string>{}(instruction_text);

        // 連続する同一命令をチェック
        if (block.last_hash == instruction_hash) {
            consecutive_instruction_count[instruction_hash]++;

            if (consecutive_instruction_count[instruction_hash] >= max_duplicate_instructions) {
                throw std::runtime_error(
                    "無限ループ検出: ブロック '" + current_block + "' で同じ命令が" +
                    std::to_string(consecutive_instruction_count[instruction_hash]) +
                    "回連続で生成されました");
            }
        } else {
            consecutive_instruction_count[instruction_hash] = 1;
            block.last_hash = instruction_hash;
        }

        // ハッシュ履歴に追加（最大100個保持）
        block.hash_history.push_back(instruction_hash);
        if (block.hash_history.size() > 100) {
            block.hash_history.erase(block.hash_history.begin());
        }

        // パターン検出
        detect_instruction_pattern(block);
    }

    // 命令パターンの検出
    void detect_instruction_pattern(const BlockInfo& block) {
        if (block.hash_history.size() < 20)
            return;

        // 周期的パターンを検出（周期2〜10）
        for (size_t period = 2; period <= 10 && period * 3 <= block.hash_history.size(); ++period) {
            if (is_periodic_pattern(block.hash_history, period)) {
                throw std::runtime_error("無限ループ検出: ブロック '" + current_block + "' で周期" +
                                         std::to_string(period) + "の命令パターンが検出されました");
            }
        }
    }

    // 周期的パターンの検出
    bool is_periodic_pattern(const std::vector<size_t>& history, size_t period) {
        size_t size = history.size();
        if (size < period * 3)
            return false;

        // 最後の3周期分をチェック
        size_t start = size - period * 3;
        for (size_t i = 0; i < period; ++i) {
            if (history[start + i] != history[start + i + period] ||
                history[start + i] != history[start + i + period * 2]) {
                return false;
            }
        }
        return true;
    }

    // 設定の更新
    void set_max_block_visits(size_t max_visits) { max_block_visits = max_visits; }

    void set_max_instructions(size_t max_inst) { max_instructions_per_block = max_inst; }

    // 統計情報の取得
    std::string get_statistics() const {
        std::string stats = "=== Block Statistics ===\n";
        for (const auto& [func_name, blocks] : function_blocks) {
            stats += "Function: " + func_name + "\n";
            for (const auto& [block_name, info] : blocks) {
                stats += "  Block " + block_name + ": " + std::to_string(info.visit_count) +
                         " visits, " + std::to_string(info.instruction_count) + " instructions\n";
            }
        }
        return stats;
    }

    // リセット
    void reset() {
        function_blocks.clear();
        current_function.clear();
        current_block.clear();
        consecutive_instruction_count.clear();
    }
};

}  // namespace cm::codegen