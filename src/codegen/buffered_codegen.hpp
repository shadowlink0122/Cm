#pragma once

#include <chrono>
#include <memory>
#include <sstream>
#include <vector>

namespace cm::codegen {

// バッファベースのコード生成インターフェース
class BufferedCodeGenerator {
   public:
    // コード生成の統計情報
    struct GenerationStats {
        size_t total_lines = 0;
        size_t total_bytes = 0;
        size_t max_buffer_size = 0;
        std::chrono::milliseconds generation_time;
        bool exceeded_limit = false;
    };

    // コード生成の制限設定
    struct Limits {
        size_t max_lines = 1000000;                         // 最大100万行
        size_t max_bytes = 100 * 1024 * 1024;               // 最大100MB
        size_t warning_threshold_bytes = 50 * 1024 * 1024;  // 50MBで警告
        std::chrono::seconds max_generation_time{30};       // 最大30秒
    };

   protected:
    // 内部バッファ
    std::stringstream buffer;
    std::vector<std::string> lines;
    GenerationStats stats;
    Limits limits;

    // 生成開始時刻
    std::chrono::time_point<std::chrono::high_resolution_clock> start_time;

    // エラーフラグ
    bool has_error = false;
    std::string error_message;

   public:
    BufferedCodeGenerator() = default;
    virtual ~BufferedCodeGenerator() = default;

    // コード生成開始
    void begin_generation() {
        buffer.str("");
        buffer.clear();
        lines.clear();
        stats = GenerationStats{};
        has_error = false;
        error_message.clear();
        start_time = std::chrono::high_resolution_clock::now();
    }

    // バッファに行を追加
    bool append_line(const std::string& line) {
        // サイズチェック
        if (!check_limits()) {
            return false;
        }

        lines.push_back(line);
        buffer << line << "\n";

        stats.total_lines++;
        stats.total_bytes += line.size() + 1;  // +1 for newline

        return true;
    }

    // バッファに直接書き込み
    bool append(const std::string& content) {
        // サイズチェック
        if (!check_limits()) {
            return false;
        }

        buffer << content;
        stats.total_bytes += content.size();

        // 行数をカウント
        size_t newlines = std::count(content.begin(), content.end(), '\n');
        stats.total_lines += newlines;

        return true;
    }

    // フォーマット付き追加
    template <typename... Args>
    bool append_formatted(const char* format, Args... args) {
        char buffer_local[4096];
        int len = snprintf(buffer_local, sizeof(buffer_local), format, args...);

        if (len > 0 && len < sizeof(buffer_local)) {
            return append(std::string(buffer_local, len));
        }

        // バッファが足りない場合
        std::vector<char> larger_buffer(len + 1);
        snprintf(larger_buffer.data(), larger_buffer.size(), format, args...);
        return append(std::string(larger_buffer.data(), len));
    }

    // 制限チェック
    bool check_limits() {
        // 時間制限チェック
        auto elapsed = std::chrono::high_resolution_clock::now() - start_time;
        if (elapsed > limits.max_generation_time) {
            set_error("コード生成時間が制限を超過しました");
            return false;
        }

        // サイズ制限チェック
        if (stats.total_bytes > limits.max_bytes) {
            set_error("生成コードサイズが制限を超過しました");
            stats.exceeded_limit = true;
            return false;
        }

        if (stats.total_lines > limits.max_lines) {
            set_error("生成コード行数が制限を超過しました");
            stats.exceeded_limit = true;
            return false;
        }

        // 警告閾値チェック
        if (stats.total_bytes > limits.warning_threshold_bytes) {
            if (!stats.exceeded_limit) {  // 一度だけ警告
                std::cerr << "[CODEGEN] 警告: 生成コードが" << (stats.total_bytes / (1024 * 1024))
                          << "MBに達しています\n";
            }
        }

        return true;
    }

    // コード生成終了
    std::string end_generation() {
        auto end_time = std::chrono::high_resolution_clock::now();
        stats.generation_time =
            std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        stats.max_buffer_size = buffer.str().size();

        if (has_error) {
            return "";  // エラー時は空文字列
        }

        return buffer.str();
    }

    // 生成されたコードを取得（検証付き）
    std::string get_generated_code() {
        if (has_error) {
            return "";
        }
        return buffer.str();
    }

    // 行単位で取得
    const std::vector<std::string>& get_lines() const { return lines; }

    // 統計情報を取得
    const GenerationStats& get_stats() const { return stats; }

    // エラー情報
    bool has_generation_error() const { return has_error; }

    const std::string& get_error_message() const { return error_message; }

    // 制限設定を変更
    void set_limits(const Limits& new_limits) { limits = new_limits; }

    // デバッグ用：現在のバッファサイズ
    size_t current_buffer_size() const { return stats.total_bytes; }

   protected:
    void set_error(const std::string& msg) {
        has_error = true;
        error_message = msg;
        std::cerr << "[CODEGEN] エラー: " << msg << "\n";
    }
};

// 二段階バッファリング（さらに安全）
class TwoPhaseCodeGenerator : public BufferedCodeGenerator {
   private:
    // フェーズ1: 構造を構築
    struct CodeBlock {
        std::string name;
        std::string content;
        size_t estimated_size;
        bool is_critical;  // 必須ブロック
    };

    std::vector<CodeBlock> blocks;
    size_t total_estimated_size = 0;

   public:
    // ブロック単位で追加
    bool add_block(const std::string& name, const std::string& content, bool is_critical = false) {
        size_t size = content.size();

        // 推定サイズチェック
        if (total_estimated_size + size > limits.max_bytes) {
            if (is_critical) {
                set_error("必須ブロック '" + name + "' を追加できません（サイズ超過）");
                return false;
            }
            // 非必須ブロックはスキップ
            return true;
        }

        blocks.push_back({name, content, size, is_critical});
        total_estimated_size += size;
        return true;
    }

    // フェーズ2: 実際に生成
    std::string generate() {
        begin_generation();

        // ブロックを順番に追加
        for (const auto& block : blocks) {
            if (!append("// === " + block.name + " ===\n")) {
                break;
            }
            if (!append(block.content)) {
                if (block.is_critical) {
                    set_error("必須ブロック '" + block.name + "' の生成に失敗");
                    return "";
                }
                // 非必須ブロックはスキップして続行
                continue;
            }
            if (!append("\n")) {
                break;
            }
        }

        return end_generation();
    }

    // 推定サイズを事前チェック
    bool validate_size() const { return total_estimated_size <= limits.max_bytes; }

    // ブロック数を取得
    size_t block_count() const { return blocks.size(); }

    void clear_blocks() {
        blocks.clear();
        total_estimated_size = 0;
    }
};

// スコープ付きコード生成（RAII）
class ScopedCodeSection {
   private:
    BufferedCodeGenerator& gen;
    std::string section_name;
    size_t start_size;
    bool committed = false;

   public:
    ScopedCodeSection(BufferedCodeGenerator& g, const std::string& name)
        : gen(g), section_name(name) {
        start_size = gen.current_buffer_size();
        gen.append_line("// BEGIN: " + section_name);
    }

    ~ScopedCodeSection() {
        if (!committed) {
            // エラー時はロールバック（概念的に）
            std::cerr << "[CODEGEN] セクション '" << section_name
                      << "' はコミットされませんでした\n";
        }
    }

    void commit() {
        gen.append_line("// END: " + section_name);
        committed = true;
    }

    size_t section_size() const { return gen.current_buffer_size() - start_size; }
};

}  // namespace cm::codegen