// ============================================================
// Formatter整形パス: 空白・空行・末尾改行の正規化
// ============================================================

#include "internal/fmt/formatter.hpp"

#include <sstream>
#include <string>

namespace cm {
namespace fmt {

std::string Formatter::trim_trailing_whitespace(const std::string& code, size_t& changes) {
    std::istringstream stream(code);
    std::ostringstream result;
    std::string line;
    bool first = true;

    while (std::getline(stream, line)) {
        if (!first)
            result << '\n';
        first = false;

        // 行末の空白を削除
        size_t end = line.find_last_not_of(" \t\r");
        if (end != std::string::npos) {
            std::string trimmed = line.substr(0, end + 1);
            if (trimmed != line) {
                changes++;
            }
            result << trimmed;
        } else if (!line.empty()) {
            // 空白のみの行
            changes++;
        }
    }

    return result.str();
}

std::string Formatter::tabs_to_spaces(const std::string& code, size_t& changes) {
    std::string result;
    result.reserve(code.size());

    for (char c : code) {
        if (c == '\t') {
            result += std::string(indent_width_, ' ');
            changes++;
        } else {
            result += c;
        }
    }

    return result;
}

std::string Formatter::normalize_blank_lines(const std::string& code, size_t& changes) {
    std::istringstream stream(code);
    std::ostringstream result;
    std::string line;
    int blank_count = 0;
    bool first = true;

    while (std::getline(stream, line)) {
        bool is_blank = line.find_first_not_of(" \t\r") == std::string::npos;

        if (is_blank) {
            blank_count++;
            if (blank_count <= 1) {
                if (!first)
                    result << '\n';
                first = false;
                result << "";
            } else {
                changes++;  // 余分な空行を削除
            }
        } else {
            blank_count = 0;
            if (!first)
                result << '\n';
            first = false;
            result << line;
        }
    }

    return result.str();
}

std::string Formatter::ensure_trailing_newline(const std::string& code, size_t& changes) {
    if (code.empty())
        return code;

    // 末尾の改行を削除
    std::string result = code;
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
        result.pop_back();
    }

    // 1つの改行を追加
    result += '\n';

    if (result != code) {
        changes++;
    }

    return result;
}

}  // namespace fmt
}  // namespace cm
