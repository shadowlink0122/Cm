#pragma once

#include "span.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace cm {

/// ソースファイルを管理するクラス
class Source {
   public:
    /// ソースコードから作成
    explicit Source(std::string content, std::string filename = "<input>")
        : content_(std::move(content)), filename_(std::move(filename)) {
        build_line_starts();
    }

    /// ソースコード全体を取得
    std::string_view content() const { return content_; }

    /// ファイル名を取得
    std::string_view filename() const { return filename_; }

    /// Spanから文字列を取得
    std::string_view get_text(Span span) const {
        return std::string_view(content_).substr(span.start, span.length());
    }

    /// オフセットから行・列を取得
    LineColumn get_line_column(uint32_t offset) const {
        // 二分探索で行を特定
        auto it = std::upper_bound(line_starts_.begin(), line_starts_.end(), offset);
        uint32_t line = static_cast<uint32_t>(it - line_starts_.begin());
        uint32_t line_start = (line > 0) ? line_starts_[line - 1] : 0;
        uint32_t column = offset - line_start + 1;
        return LineColumn{line, column};
    }

    /// 指定行の内容を取得
    std::string_view get_line(uint32_t line_number) const {
        if (line_number == 0 || line_number > line_starts_.size()) {
            return "";
        }
        uint32_t start = (line_number == 1) ? 0 : line_starts_[line_number - 2];
        uint32_t end = line_starts_[line_number - 1];
        // 改行文字を除去
        while (end > start && (content_[end - 1] == '\n' || content_[end - 1] == '\r')) {
            --end;
        }
        return std::string_view(content_).substr(start, end - start);
    }

   private:
    void build_line_starts() {
        line_starts_.clear();
        for (size_t i = 0; i < content_.size(); ++i) {
            if (content_[i] == '\n') {
                line_starts_.push_back(static_cast<uint32_t>(i + 1));
            }
        }
        // 最後の行（改行で終わらない場合）
        if (line_starts_.empty() || line_starts_.back() != content_.size()) {
            line_starts_.push_back(static_cast<uint32_t>(content_.size()));
        }
    }

    std::string content_;
    std::string filename_;
    std::vector<uint32_t> line_starts_;  // 各行の開始オフセット
};

}  // namespace cm
