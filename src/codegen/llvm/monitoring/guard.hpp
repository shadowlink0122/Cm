#pragma once

#include <deque>
#include <fstream>
#include <functional>
#include <string>
#include <unordered_map>

namespace cm::codegen {

// コード生成の無限ループを検出するガード
class CodeGenGuard {
   private:
    // 最近生成されたコードのハッシュ値を記録
    std::deque<size_t> recent_hashes;
    static constexpr size_t HISTORY_SIZE = 100;
    static constexpr size_t DUPLICATE_THRESHOLD = 10;  // 同じコードが10回連続したらエラー

    // 同じハッシュの連続回数
    std::unordered_map<size_t, size_t> consecutive_counts;

    // 出力ファイルサイズの監視
    size_t total_bytes_written = 0;
    static constexpr size_t MAX_OUTPUT_SIZE = 1024 * 1024 * 100;  // 100MB制限

    // コード生成回数
    size_t generation_count = 0;
    static constexpr size_t MAX_GENERATIONS = 10000;  // 最大生成回数

   public:
    // コード生成前にチェック
    bool check_before_generate(const std::string& code_snippet) {
        generation_count++;

        // 生成回数チェック
        if (generation_count > MAX_GENERATIONS) {
            throw std::runtime_error("コード生成エラー: 生成回数が上限(" +
                                     std::to_string(MAX_GENERATIONS) + ")を超えました");
        }

        // コードのハッシュを計算
        size_t hash = std::hash<std::string>{}(code_snippet);

        // 連続する同一コードをチェック
        if (!recent_hashes.empty() && recent_hashes.back() == hash) {
            consecutive_counts[hash]++;

            if (consecutive_counts[hash] >= DUPLICATE_THRESHOLD) {
                throw std::runtime_error("コード生成エラー: 同じコードが" +
                                         std::to_string(consecutive_counts[hash]) +
                                         "回連続で生成されました（無限ループの可能性）");
            }
        } else {
            consecutive_counts[hash] = 1;
        }

        // 履歴に追加
        recent_hashes.push_back(hash);
        if (recent_hashes.size() > HISTORY_SIZE) {
            recent_hashes.pop_front();
        }

        return true;
    }

    // ファイル書き込み前にチェック
    void check_write_size(size_t bytes_to_write) {
        total_bytes_written += bytes_to_write;

        if (total_bytes_written > MAX_OUTPUT_SIZE) {
            throw std::runtime_error("コード生成エラー: 出力サイズが上限(" +
                                     std::to_string(MAX_OUTPUT_SIZE / 1024 / 1024) +
                                     "MB)を超えました");
        }
    }

    // パターン検出（より高度な検出）
    bool detect_pattern() {
        if (recent_hashes.size() < 20)
            return false;

        // 周期的パターンを検出（例: A->B->C->A->B->C...）
        for (size_t period = 2; period <= 10; ++period) {
            if (is_periodic(period)) {
                throw std::runtime_error("コード生成エラー: 周期" + std::to_string(period) +
                                         "のパターンが検出されました（無限ループの可能性）");
            }
        }

        return false;
    }

   private:
    bool is_periodic(size_t period) {
        if (recent_hashes.size() < period * 3)
            return false;

        // 最後の3周期分をチェック
        size_t start = recent_hashes.size() - period * 3;

        for (size_t i = 0; i < period; ++i) {
            if (recent_hashes[start + i] != recent_hashes[start + i + period] ||
                recent_hashes[start + i] != recent_hashes[start + i + period * 2]) {
                return false;
            }
        }

        return true;
    }

   public:
    // 統計情報を取得
    std::string get_statistics() const {
        return "生成回数: " + std::to_string(generation_count) +
               ", 出力サイズ: " + std::to_string(total_bytes_written / 1024) + "KB";
    }

    // リセット（新しいファイルのコンパイル時）
    void reset() {
        recent_hashes.clear();
        consecutive_counts.clear();
        total_bytes_written = 0;
        generation_count = 0;
    }
};

// グローバルインスタンス（またはCodeGenクラスのメンバーとして）
inline thread_local CodeGenGuard codegen_guard;

}  // namespace cm::codegen