#pragma once

// ============================================================
// importプリプロセッサ内部ヘルパー（import / export_filter /
// module_resolve で共有する行解析ユーティリティ）
// ============================================================

#include <cctype>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace cm::preprocessor {

// 行頭の空白をスキップした位置を返す
inline size_t skip_ws(const std::string& s, size_t pos = 0) {
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t'))
        pos++;
    return pos;
}

// 行のコード部分（//コメントを除いた部分）を返す。
// 文字列/文字リテラル内の // は無視し、SV幅付きリテラル（16'd256等）の
// クォートは文字リテラル開始として扱わない
inline std::string code_portion(const std::string& line) {
    bool in_string = false;
    bool in_char = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (in_string) {
            if (c == '\\') {
                ++i;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }
        if (in_char) {
            if (c == '\\') {
                ++i;
            } else if (c == '\'') {
                in_char = false;
            }
            continue;
        }
        if (c == '"') {
            in_string = true;
        } else if (c == '\'') {
            bool sized_literal = (i > 0 && std::isalnum(static_cast<unsigned char>(line[i - 1])));
            if (!sized_literal) {
                in_char = true;
            }
        } else if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') {
            return line.substr(0, i);
        }
    }
    return line;
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

// 複数行にまたがる export { ... }; ブロックを1行へ正規化する。
// export処理（process_export_syntax / filter_exports / extract_reexports /
// extract_exported_blocks）はいずれも行単位の解析であり、複数行ブロックを
// そのまま渡すと未処理の生 export { がパーサへ届き構文エラーになる。
// ブロック内の行コメントは除去し、消費した行は行数維持のため空コメント行にする
inline std::string normalize_export_blocks(const std::string& source) {
    if (source.find("export") == std::string::npos) {
        return source;
    }
    std::vector<std::string> lines;
    {
        std::stringstream input(source);
        std::string line;
        while (std::getline(input, line)) {
            lines.push_back(line);
        }
    }
    auto strip_line_comment = [](const std::string& l) {
        size_t c = l.find("//");
        return c == std::string::npos ? l : l.substr(0, c);
    };
    std::stringstream result;
    for (size_t i = 0; i < lines.size(); ++i) {
        size_t pos = skip_ws(lines[i]);
        if (!starts_with_keyword(lines[i], pos, "export")) {
            result << lines[i] << "\n";
            continue;
        }
        size_t after_export = skip_ws(lines[i], pos + 6);
        if (after_export >= lines[i].size() || lines[i][after_export] != '{') {
            result << lines[i] << "\n";
            continue;
        }
        // 深さ追跡で閉じ '}' を探す（export { ns::{a,b} } の内側 '}' を許容）
        int depth = 0;
        bool closed_on_first = false;
        {
            std::string body = strip_line_comment(lines[i]);
            for (size_t k = after_export; k < body.size(); ++k) {
                if (body[k] == '{') {
                    depth++;
                } else if (body[k] == '}' && --depth == 0) {
                    closed_on_first = true;
                    break;
                }
            }
        }
        if (closed_on_first) {
            result << lines[i] << "\n";
            continue;
        }
        std::string merged = strip_line_comment(lines[i]);
        size_t end = i;
        bool closed = false;
        for (size_t j = i + 1; j < lines.size() && !closed; ++j) {
            std::string body = strip_line_comment(lines[j]);
            for (char c : body) {
                if (c == '{') {
                    depth++;
                } else if (c == '}' && --depth == 0) {
                    closed = true;
                    break;
                }
            }
            merged += " " + body;
            end = j;
        }
        if (!closed) {
            // 閉じ括弧のない不正ブロックはそのまま出力しパーサにエラーを報告させる
            result << lines[i] << "\n";
            continue;
        }
        result << merged << "\n";
        for (size_t j = i + 1; j <= end; ++j) {
            result << "//\n";
        }
        i = end;
    }
    return result.str();
}

}  // namespace cm::preprocessor
