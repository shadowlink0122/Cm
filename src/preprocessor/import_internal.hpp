#pragma once

// ============================================================
// importプリプロセッサ内部ヘルパー（import / export_filter /
// module_resolve で共有する行解析ユーティリティ）
// ============================================================

#include <cstring>
#include <string>

namespace cm::preprocessor {

// 行頭の空白をスキップした位置を返す
inline size_t skip_ws(const std::string& s, size_t pos = 0) {
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t'))
        pos++;
    return pos;
}

// 指定位置からキーワードが始まるか（キーワード後に非英数字 or 行末）
inline bool starts_with_keyword(const std::string& s, size_t pos, const char* keyword) {
    size_t klen = std::strlen(keyword);
    if (pos + klen > s.size())
        return false;
    if (s.compare(pos, klen, keyword) != 0)
        return false;
    // キーワード後は非英数字 or 行末であること
    if (pos + klen < s.size()) {
        char next = s[pos + klen];
        if (std::isalnum(static_cast<unsigned char>(next)) || next == '_')
            return false;
    }
    return true;
}

// 行が空白の後に import or from で始まるかを高速チェック
inline bool is_import_line(const std::string& line) {
    size_t pos = skip_ws(line);
    return starts_with_keyword(line, pos, "import") || starts_with_keyword(line, pos, "from");
}

// 行が空白の後に指定キーワードで始まるかチェック
inline bool line_starts_with(const std::string& line, const char* keyword) {
    size_t pos = skip_ws(line);
    return starts_with_keyword(line, pos, keyword);
}

}  // namespace cm::preprocessor
