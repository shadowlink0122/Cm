// ============================================================
// Formatter整形パス: 演算子周りの空白正規化と行末コメントの最小間隔
// ============================================================

#include "internal/fmt/formatter.hpp"
#include "internal/fmt/formatter/scan.hpp"

#include <cstring>
#include <sstream>
#include <string>

namespace cm {
namespace fmt {

std::string Formatter::normalize_operator_spacing(const std::string& code, size_t& changes) {
    std::string result;
    result.reserve(code.size() + 100);

    bool in_string = false;
    bool in_char = false;
    bool in_backtick = false;  // バッククォート（テンプレートリテラル）
    bool in_line_comment = false;
    bool in_block_comment = false;
    char prev_char = 0;

    for (size_t i = 0; i < code.size(); ++i) {
        char c = code[i];
        char next_char = (i + 1 < code.size()) ? code[i + 1] : 0;

        // バッククォートリテラルの検出
        if (!in_line_comment && !in_block_comment && !in_string && !in_char && c == '`') {
            in_backtick = !in_backtick;
        }

        // 文字列リテラルの検出
        if (!in_line_comment && !in_block_comment && !in_char && !in_backtick && c == '"' &&
            !is_escaped_char(code, i)) {
            in_string = !in_string;
        }
        // 文字リテラルの検出（SV幅付きリテラルの ' は除外）
        if (!in_line_comment && !in_block_comment && !in_string && !in_backtick && c == '\'' &&
            !is_escaped_char(code, i) && !is_sized_literal_quote(code, i)) {
            in_char = !in_char;
        }
        // 行コメントの検出
        if (!in_string && !in_char && !in_block_comment && c == '/' && next_char == '/') {
            in_line_comment = true;
        }
        // ブロックコメントの検出
        if (!in_string && !in_char && !in_line_comment && c == '/' && next_char == '*') {
            in_block_comment = true;
        }
        if (in_block_comment && c == '*' && next_char == '/') {
            result += c;
            prev_char = c;
            continue;  // '*' を追加して '/' は次のループで追加
        }
        if (in_block_comment && prev_char == '*' && c == '/') {
            in_block_comment = false;
        }
        // 改行でコメント終了
        if (c == '\n') {
            in_line_comment = false;
        }

        // リテラルやコメント内は変更しない
        if (in_string || in_char || in_backtick || in_line_comment || in_block_comment) {
            result += c;
            prev_char = c;
            continue;
        }

        // カンマの後に空白を追加 (,X → , X)
        if (c == ',' && next_char != ' ' && next_char != '\n' && next_char != '\r') {
            result += c;
            result += ' ';
            changes++;
            prev_char = c;
            continue;
        }

        // 曖昧性のない二項演算子の前後に不足している空白を追加する（L5）。
        // 既存の空白は削らない（意図的な整列を保持し、追加のみなので冪等）。
        // 単独の + - * / % < > & ^ は単項演算子・ジェネリクス（<T>）・ポインタ・指数表記（1e+5）と曖昧なため対象外
        {
            static const char* kBinaryOps[] = {
                // 長いものから順に照合する（"==" より先に "=" に一致させない）
                "<<=", ">>=", "==", "!=", "<=", ">=", "&&", "||", "+=",
                "-=",  "*=",  "/=", "%=", "&=", "|=", "^=", "=>", "=",
            };
            const char* matched = nullptr;
            for (const char* op : kBinaryOps) {
                size_t len = std::strlen(op);
                if (code.compare(i, len, op) == 0) {
                    matched = op;
                    break;
                }
            }
            // operator宣言行（operator bool ==(T other) 等）は宣言スタイル（==( の密着）を保持するため対象外
            if (matched) {
                size_t line_start = result.rfind('\n');
                line_start = (line_start == std::string::npos) ? 0 : line_start + 1;
                if (result.find("operator ", line_start) != std::string::npos) {
                    matched = nullptr;
                }
            }
            // "=" 単独一致は、直前が他演算子の一部（! < > 等）でないことを確認する
            // （"!=" 等は長い候補で先に一致するが、直前文字経由の合成を防ぐ）
            if (matched && matched[0] == '=' && matched[1] == '\0') {
                if (prev_char == '!' || prev_char == '<' || prev_char == '>' || prev_char == '=' ||
                    prev_char == '+' || prev_char == '-' || prev_char == '*' || prev_char == '/' ||
                    prev_char == '%' || prev_char == '&' || prev_char == '|' || prev_char == '^') {
                    matched = nullptr;
                }
            }
            // "<=" ">=" は直後にさらに '=' が続く場合（"<==" は無いが安全側）や、
            // SVの "<=" 代入もCm構文上は比較と同じ整形で問題ない
            if (matched) {
                size_t len = std::strlen(matched);
                char after = (i + len < code.size()) ? code[i + len] : 0;
                // 演算子の後ろがさらに '=' なら複合の一部（例: "==" の後の '='）なので見送る
                if (after == '=') {
                    matched = nullptr;
                }
                if (matched) {
                    // 行頭（継続行の演算子先頭）は行構造を保つため前空白を追加しない
                    bool at_line_start = result.empty() || result.back() == '\n';
                    if (!at_line_start && result.back() != ' ' && result.back() != '(' &&
                        result.back() != '[') {
                        result += ' ';
                        changes++;
                    }
                    result += matched;
                    if (after != ' ' && after != '\n' && after != '\r' && after != 0) {
                        result += ' ';
                        changes++;
                    }
                    i += len - 1;
                    prev_char = matched[len - 1];
                    continue;
                }
            }
        }

        // パイプの前後に空白を追加 (ただし ||, |= は除外)
        if (c == '|' && prev_char != '|' && next_char != '|' && next_char != '=') {
            // 前に空白がない場合
            if (!result.empty() && result.back() != ' ' && result.back() != '\n' &&
                result.back() != '(' && result.back() != '[') {
                result += ' ';
                changes++;
            }
            result += c;
            // 後に空白がない場合
            if (next_char != ' ' && next_char != '\n' && next_char != '\r' && next_char != ')' &&
                next_char != ']' && next_char != 0) {
                result += ' ';
                changes++;
            }
            prev_char = c;
            continue;
        }

        result += c;
        prev_char = c;
    }

    return result;
}

std::string Formatter::align_inline_comments(const std::string& code, size_t& changes) {
    // 行末コメントの位置は手動調整を尊重する。
    // コードとコメントの間隔が2スペース未満の場合のみ2スペースへ広げる
    std::istringstream stream(code);
    std::ostringstream result;
    std::string line;
    bool first = true;

    while (std::getline(stream, line)) {
        if (!first)
            result << '\n';
        first = false;

        // 文字列リテラル外での // を探す
        size_t comment_start = std::string::npos;
        bool in_string = false;
        bool in_char = false;

        for (size_t j = 0; j < line.size(); ++j) {
            char c = line[j];

            if (!in_char && c == '"' && !is_escaped_char(line, j)) {
                in_string = !in_string;
            }
            if (!in_string && c == '\'' && !is_escaped_char(line, j) &&
                !is_sized_literal_quote(line, j)) {
                in_char = !in_char;
            }

            if (!in_string && !in_char && j + 1 < line.size() && line[j] == '/' &&
                line[j + 1] == '/') {
                comment_start = j;
                break;
            }
        }

        if (comment_start == std::string::npos) {
            result << line;
            continue;
        }

        // コード部分の終わり（コメント前の空白を除く）
        size_t code_end = comment_start;
        while (code_end > 0 && line[code_end - 1] == ' ') {
            code_end--;
        }

        // 行全体がコメント（code_end == 0）は対象外
        if (code_end > 0 && comment_start - code_end < 2) {
            result << line.substr(0, code_end) << "  " << line.substr(comment_start);
            changes++;
        } else {
            result << line;
        }
    }

    return result.str();
}

}  // namespace fmt
}  // namespace cm
