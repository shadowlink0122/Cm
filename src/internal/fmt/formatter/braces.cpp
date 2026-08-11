// ============================================================
// Formatter整形パス: K&R波括弧の結合とセミコロン後の改行強制
// ============================================================

#include "internal/fmt/formatter.hpp"
#include "internal/fmt/formatter/scan.hpp"

#include <cctype>
#include <sstream>
#include <string>
#include <vector>

namespace cm {
namespace fmt {

std::string Formatter::enforce_kr_braces(const std::string& code, size_t& changes) {
    std::istringstream stream(code);
    std::vector<std::string> lines;
    std::string line;

    // 全行を読み込み
    while (std::getline(stream, line)) {
        lines.push_back(line);
    }

    std::ostringstream result;

    for (size_t i = 0; i < lines.size(); ++i) {
        std::string& curr = lines[i];

        // 次の行が "{" のみかチェック
        if (i + 1 < lines.size()) {
            std::string next_trimmed = lines[i + 1];
            size_t start = next_trimmed.find_first_not_of(" \t");
            if (start != std::string::npos) {
                next_trimmed = next_trimmed.substr(start);
            }

            // 次の行が "{" のみで、現在行が宣言・制御ヘッダの場合のみ結合する。
            // 現在行が `)` または識別子で終わるとき（関数・if/for・struct名等）に限り、裸ブロックの `{`（前の行が `;`/`{`/`}` 等で終わる）や行末コメント付きの行への結合を防ぐ
            if (next_trimmed == "{") {
                size_t end = curr.find_last_not_of(" \t\r");
                bool has_comment = (curr.find("//") != std::string::npos);
                char last = (end != std::string::npos) ? curr[end] : 0;
                bool joinable = !has_comment && (last == ')' || last == '>' || last == '_' ||
                                                 std::isalnum(static_cast<unsigned char>(last)));
                if (joinable) {
                    // 現在行の末尾の空白を削除し " {" を追加
                    curr = curr.substr(0, end + 1);
                    result << curr << " {\n";
                    changes++;
                    i++;  // 次の行をスキップ
                    continue;
                }
            }
        }

        // 通常の行を出力
        result << curr;
        if (i + 1 < lines.size()) {
            result << '\n';
        }
    }

    return result.str();
}

std::string Formatter::enforce_semicolon_newline(const std::string& code, size_t& changes) {
    std::string result;
    result.reserve(code.size() + 100);

    bool in_string = false;
    bool in_char = false;
    bool in_backtick = false;  // バッククォート（テンプレートリテラル）
    bool in_line_comment = false;
    int paren_depth = 0;  // ()の深さ
    int brace_depth = 0;  // {}の深さ（1行クロージャ対応）

    for (size_t i = 0; i < code.size(); ++i) {
        char c = code[i];
        char next_char = (i + 1 < code.size()) ? code[i + 1] : 0;

        // バッククォートリテラルの検出
        if (!in_line_comment && !in_string && !in_char && c == '`') {
            in_backtick = !in_backtick;
        }

        // 文字列リテラルの検出（変数埋め込み{...}内も考慮）
        if (!in_line_comment && !in_char && !in_backtick && c == '"' && !is_escaped_char(code, i)) {
            in_string = !in_string;
        }
        // 文字リテラルの検出（SV幅付きリテラルの ' は除外）
        if (!in_line_comment && !in_string && !in_backtick && c == '\'' &&
            !is_escaped_char(code, i) && !is_sized_literal_quote(code, i)) {
            in_char = !in_char;
        }
        // 行コメントの検出
        if (!in_string && !in_char && !in_backtick && c == '/' && next_char == '/') {
            in_line_comment = true;
        }
        if (c == '\n') {
            in_line_comment = false;
            // 改行でbrace_depthをリセット（1行クロージャのみ対象）
            brace_depth = 0;
        }

        // リテラルやコメント内は変更しない
        if (in_string || in_char || in_backtick || in_line_comment) {
            result += c;
            continue;
        }

        // 括弧・波括弧の深さを追跡
        if (c == '(')
            paren_depth++;
        if (c == ')')
            paren_depth--;
        if (c == '{')
            brace_depth++;
        if (c == '}')
            brace_depth--;

        // セミコロンの後に改行を強制（括弧内・波括弧内は除外）
        // brace_depth > 0 は1行クロージャ内のセミコロン
        if (c == ';' && paren_depth == 0 && brace_depth <= 0) {
            result += c;
            // 次の文字が改行でなく、空白または文字の場合は改行を追加
            if (next_char != '\n' && next_char != '\r' && next_char != 0) {
                // 行末コメントがあるかチェック（// がある場合はスキップ）
                bool has_trailing_comment = false;
                for (size_t k = i + 1; k < code.size() && code[k] != '\n'; ++k) {
                    if (code[k] == '/' && k + 1 < code.size() && code[k + 1] == '/') {
                        has_trailing_comment = true;
                        break;
                    }
                }

                if (!has_trailing_comment) {
                    // 次の空白をスキップ
                    while (i + 1 < code.size() && code[i + 1] == ' ') {
                        i++;
                    }
                    next_char = (i + 1 < code.size()) ? code[i + 1] : 0;
                    if (next_char != '\n' && next_char != '\r' && next_char != 0) {
                        result += '\n';
                        // インデントを追加（現在のインデントレベルを維持）
                        size_t indent = get_current_indent(result);
                        result += std::string(indent, ' ');
                        changes++;
                    }
                }
            }
            continue;
        }

        result += c;
    }

    return result;
}

size_t Formatter::get_current_indent(const std::string& code) const {
    // 最後の改行位置を見つける
    size_t last_newline = code.rfind('\n');
    if (last_newline == std::string::npos) {
        last_newline = 0;  // 改行がなければ先頭から
    } else {
        last_newline++;  // 改行の次の位置から
    }

    // 行頭からの空白を数える
    size_t indent = 0;
    for (size_t i = last_newline; i < code.size(); ++i) {
        if (code[i] == ' ') {
            indent++;
        } else {
            break;
        }
    }
    return indent;
}

}  // namespace fmt
}  // namespace cm
