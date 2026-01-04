#pragma once

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace cm::codegen {

// 出力ファイル監視クラス
class OutputMonitor {
   private:
    // ファイルごとのサイズ情報
    struct FileInfo {
        size_t total_bytes_written = 0;
        size_t line_count = 0;
        std::chrono::steady_clock::time_point start_time;
        std::chrono::steady_clock::time_point last_write_time;
    };

    // ファイル情報のマップ
    std::unordered_map<std::string, FileInfo> file_info;

    // 現在書き込み中のファイル
    std::string current_file;

    // 設定値（デフォルト16GB）
    size_t max_file_size = 16ULL * 1024 * 1024 * 1024;     // 16GB
    size_t max_total_output = 32ULL * 1024 * 1024 * 1024;  // 32GB（全体）
    size_t warning_threshold = 1ULL * 1024 * 1024 * 1024;  // 1GBで警告

    // 全体の出力サイズ
    size_t total_output_size = 0;

    // 警告フラグ
    bool warning_issued = false;

   public:
    // ファイル書き込みの開始
    void begin_file(const std::string& filename) {
        current_file = filename;

        if (file_info.find(filename) == file_info.end()) {
            FileInfo& info = file_info[filename];
            info.start_time = std::chrono::steady_clock::now();
        }

        file_info[filename].last_write_time = std::chrono::steady_clock::now();
    }

    // ファイル書き込みの終了
    void end_file() { current_file.clear(); }

    // データ書き込みの記録
    void write_data(size_t bytes) {
        if (current_file.empty()) {
            return;  // ファイル外の書き込みは無視
        }

        FileInfo& info = file_info[current_file];
        info.total_bytes_written += bytes;
        info.line_count++;
        total_output_size += bytes;

        // 警告チェック（1GBを超えた場合）
        if (!warning_issued && info.total_bytes_written > warning_threshold) {
            std::cerr << "[警告] ファイル '" << current_file << "' のサイズが"
                      << format_size(info.total_bytes_written) << "を超えました\n";
            warning_issued = true;
        }

        // ファイルサイズチェック
        if (info.total_bytes_written > max_file_size) {
            throw std::runtime_error("出力サイズ超過: ファイル '" + current_file + "' が上限(" +
                                     format_size(max_file_size) + ")を超えました。現在のサイズ: " +
                                     format_size(info.total_bytes_written));
        }

        // 全体サイズチェック
        if (total_output_size > max_total_output) {
            throw std::runtime_error("出力サイズ超過: 全体の出力が上限(" +
                                     format_size(max_total_output) + ")を超えました");
        }

        // 書き込み速度の異常チェック（1秒で100MB以上）
        auto now = std::chrono::steady_clock::now();
        auto duration =
            std::chrono::duration_cast<std::chrono::seconds>(now - info.start_time).count();

        if (duration > 0) {
            size_t bytes_per_second = info.total_bytes_written / duration;
            if (bytes_per_second > 100 * 1024 * 1024) {  // 100MB/s
                // 高速書き込みを検出（無限ループの可能性）
                if (info.line_count > 10000) {
                    std::cerr << "[警告] 高速書き込みを検出: " << format_size(bytes_per_second)
                              << "/秒\n";
                }
            }
        }
    }

    // 文字列データの書き込みを記録
    void write_string(const std::string& data) { write_data(data.size()); }

    // 実際のファイルサイズをチェック
    void check_actual_file_size(const std::string& filename) {
        if (std::filesystem::exists(filename)) {
            size_t actual_size = std::filesystem::file_size(filename);

            // 記録されたサイズと実際のサイズを比較
            if (file_info.find(filename) != file_info.end()) {
                FileInfo& info = file_info[filename];
                if (actual_size > info.total_bytes_written * 1.1) {
                    // 10%以上の誤差がある場合は警告
                    std::cerr << "[警告] ファイルサイズの不一致: " << filename
                              << " (記録: " << format_size(info.total_bytes_written)
                              << ", 実際: " << format_size(actual_size) << ")\n";
                }
            }

            // 実際のサイズが制限を超えているかチェック
            if (actual_size > max_file_size) {
                throw std::runtime_error("出力サイズ超過: ファイル '" + filename +
                                         "' の実際のサイズ(" + format_size(actual_size) +
                                         ")が上限を超えています");
            }
        }
    }

    // 設定の更新
    void set_max_file_size(size_t max_size) { max_file_size = max_size; }

    void set_max_total_output(size_t max_total) { max_total_output = max_total; }

    // サイズのフォーマット（人間が読みやすい形式）
    static std::string format_size(size_t bytes) {
        const char* units[] = {"B", "KB", "MB", "GB", "TB"};
        int unit_index = 0;
        double size = static_cast<double>(bytes);

        while (size >= 1024.0 && unit_index < 4) {
            size /= 1024.0;
            unit_index++;
        }

        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%.2f%s", size, units[unit_index]);
        return std::string(buffer);
    }

    // 統計情報の取得
    std::string get_statistics() const {
        std::string stats = "=== Output Statistics ===\n";
        stats += "Total output size: " + format_size(total_output_size) + "\n";
        stats += "Files written:\n";

        for (const auto& [filename, info] : file_info) {
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(info.last_write_time -
                                                                             info.start_time)
                                .count();

            stats += "  " + filename + ": " + format_size(info.total_bytes_written) + " (" +
                     std::to_string(info.line_count) + " lines, " + std::to_string(duration) +
                     "s)\n";
        }

        return stats;
    }

    // リセット
    void reset() {
        file_info.clear();
        current_file.clear();
        total_output_size = 0;
        warning_issued = false;
    }
};

}  // namespace cm::codegen