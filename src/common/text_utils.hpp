#pragma once

// ============================================================
// 文字列共通ユーティリティ
// ============================================================
// コンパイラ全体（フロントエンド・HIR/MIR・各バックエンド）で共有する
// 文字列処理。バックエンド固有の変換（SVエスケープ等）は各所に置き、
// ここには意味が文脈に依存しない純粋な操作のみを置く

#include <cctype>
#include <string>

namespace cm::text {

/// str中のfromをすべてtoに置換する
inline std::string replace_all(std::string str, const std::string& from, const std::string& to) {
    if (from.empty()) {
        return str;
    }
    size_t pos = 0;
    while ((pos = str.find(from, pos)) != std::string::npos) {
        str.replace(pos, from.length(), to);
        pos += to.length();
    }
    return str;
}

/// 名前空間修飾（a::b::c）を除いた末尾セグメント（c）を返す
inline std::string strip_namespace(const std::string& name) {
    auto pos = name.rfind("::");
    return (pos != std::string::npos) ? name.substr(pos + 2) : name;
}

/// 名前空間修飾の前置部（a::b::c → a::b）を返す。修飾が無ければ空文字
inline std::string namespace_prefix(const std::string& name) {
    auto pos = name.rfind("::");
    return (pos != std::string::npos) ? name.substr(0, pos) : std::string();
}

/// テキスト中に識別子nameが単語境界つきで出現するか
inline bool contains_identifier(const std::string& text, const std::string& name) {
    if (name.empty()) {
        return false;
    }
    size_t pos = 0;
    while ((pos = text.find(name, pos)) != std::string::npos) {
        bool at_start = (pos == 0 || (!std::isalnum(static_cast<unsigned char>(text[pos - 1])) &&
                                      text[pos - 1] != '_'));
        size_t after = pos + name.size();
        bool at_end =
            (after >= text.size() && true) ||
            (after < text.size() && !std::isalnum(static_cast<unsigned char>(text[after])) &&
             text[after] != '_');
        if (at_start && at_end) {
            return true;
        }
        pos += name.size();
    }
    return false;
}

/// 前後の空白（スペース・タブ・改行・CR）を除去する
inline std::string trim(const std::string& s) {
    const char* ws = " \t\r\n";
    auto b = s.find_first_not_of(ws);
    if (b == std::string::npos) {
        return "";
    }
    auto e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

}  // namespace cm::text
