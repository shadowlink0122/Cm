// ============================================================
// SourceLocationManager 実装
// ============================================================

#include "source_location.hpp"

#include <sstream>

namespace cm {

SourceLocationManager::SourceLocationManager(const std::string& source_code,
                                             const std::string& filename)
    : source_(source_code), filename_(filename) {
    // 各行の開始位置を記録
    line_starts_.push_back(0);
    for (size_t i = 0; i < source_.size(); ++i) {
        if (source_[i] == '\n') {
            line_starts_.push_back(i + 1);
        }
    }
}

LineColumn SourceLocationManager::get_line_column(uint32_t offset) const {
    if (line_starts_.empty()) {
        return LineColumn{1, 1};
    }

    // オフセットが無効な場合（Span{}で初期化された場合など）
    if (offset == 0) {
        return LineColumn{1, 1};
    }

    // ソースの範囲外の場合
    if (offset >= source_.size()) {
        uint32_t last_line = static_cast<uint32_t>(line_starts_.size());
        return LineColumn{last_line, 1};
    }

    // 二分探索で行を見つける
    size_t line = 0;
    for (size_t i = 1; i < line_starts_.size(); ++i) {
        if (line_starts_[i] > offset) {
            line = i - 1;
            break;
        }
        line = i;
    }

    uint32_t column = offset - line_starts_[line] + 1;
    return LineColumn{static_cast<uint32_t>(line + 1), column};
}

std::string SourceLocationManager::get_line(uint32_t line_num) const {
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

std::string SourceLocationManager::get_caret_line(uint32_t column) const {
    if (column == 0)
        return "";

    std::string result(column - 1, ' ');
    result += '^';
    return result;
}

std::string SourceLocationManager::format_error_location(const Span& span,
                                                         const std::string& message) const {
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

std::string SourceLocationManager::get_file_line(
    const std::string& filepath, const std::unordered_map<std::string, std::string>& file_contents,
    size_t line_num) {
    if (file_contents.count(filepath) == 0) {
        return "";
    }
    const std::string& content = file_contents.at(filepath);
    std::istringstream iss(content);
    std::string l;
    size_t ln = 0;
    while (std::getline(iss, l)) {
        ln++;
        if (ln == line_num) {
            return l;
        }
    }
    return "";
}

}  // namespace cm
