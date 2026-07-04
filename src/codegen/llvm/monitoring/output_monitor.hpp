#pragma once

#include <chrono>
#include <string>
#include <unordered_map>

namespace cm::codegen {

// 出力ファイル監視クラス
// 生成物の異常なサイズ増加（無限ループの兆候）を検出する
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
    void begin_file(const std::string& filename);

    // ファイル書き込みの終了
    void end_file() { current_file.clear(); }

    // データ書き込みの記録
    void write_data(size_t bytes);

    // 文字列データの書き込みを記録
    void write_string(const std::string& data) { write_data(data.size()); }

    // 実際のファイルサイズをチェック
    void check_actual_file_size(const std::string& filename);

    // 設定の更新
    void set_max_file_size(size_t max_size) { max_file_size = max_size; }

    void set_max_total_output(size_t max_total) { max_total_output = max_total; }

    // サイズのフォーマット（人間が読みやすい形式）
    static std::string format_size(size_t bytes);

    // 統計情報の取得
    std::string get_statistics() const;

    // リセット
    void reset();
};

}  // namespace cm::codegen
