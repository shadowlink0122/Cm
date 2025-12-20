#pragma once

#include "span.hpp"

#include <string>
#include <vector>

namespace cm {

/// ソースコードの位置情報を管理するクラス
class SourceLocationManager {
   public:
    explicit SourceLocationManager(const std::string& source_code, const std::string& filename = "")
        : source_(source_code), filename_(filename) {
        // 各行の開始位置を記録
        line_starts_.push_back(0);
        for (size_t i = 0; i < source_.size(); ++i) {
            if (source_[i] == '\n') {
                line_starts_.push_back(i + 1);
            }
        }
    }

    /// バイトオフセットから行・列情報を取得
    LineColumn get_line_column(uint32_t offset) const {
        // 二分探索で行を見つける
        size_t line = 0;
        for (size_t i = 1; i < line_starts_.size(); ++i) {
            if (line_starts_[i] > offset) {
                line = i - 1;
                break;
            }
        }
        if (line == 0 && !line_starts_.empty()) {
            line = line_starts_.size() - 1;
        }

        uint32_t column = offset - line_starts_[line] + 1;
        return LineColumn{static_cast<uint32_t>(line + 1), column};
    }

    /// 指定した行のテキストを取得
    std::string get_line(uint32_t line_num) const {
        if (line_num == 0 || line_num > line_starts_.size()) {
            return "";
        }

        size_t start = line_starts_[line_num - 1];
        size_t end = (line_num < line_starts_.size()) ? line_starts_[line_num] : source_.size();

        // 改行文字を除去
        if (end > start && source_[end - 1] == '\n') {
            --end;
        }
        if (end > start && source_[end - 1] == '\r') {
            --end;
        }

        return source_.substr(start, end - start);
    }

    /// エラー位置を示すキャレット文字列を生成
    std::string get_caret_line(uint32_t column) const {
        if (column == 0)
            return "";

        std::string result(column - 1, ' ');
        result += '^';
        return result;
    }

    /// エラー表示用のフォーマット済み文字列を生成
    std::string format_error_location(const Span& span, const std::string& message) const {
        auto loc = get_line_column(span.start);
        auto line_text = get_line(loc.line);
        auto caret = get_caret_line(loc.column);

        std::string result;
        if (!filename_.empty()) {
            result += filename_ + ":";
        }
        result += std::to_string(loc.line) + ":" + std::to_string(loc.column) + ": ";
        result += message + "\n";
        if (!line_text.empty()) {
            result += "  " + line_text + "\n";
            result += "  " + caret + "\n";
        }

        return result;
    }

   private:
    std::string source_;
    std::string filename_;
    std::vector<size_t> line_starts_;  // 各行の開始位置
};

}  // namespace cm