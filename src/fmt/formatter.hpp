#pragma once

// ============================================================
// Formatter - コスメティック整形
// ============================================================
// K&Rスタイル、インデント正規化、空白整理

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace cm {
namespace fmt {

/// フォーマット結果
struct FormatResult {
    std::string formatted_code;
    bool modified = false;
    size_t changes_applied = 0;
};

/// Formatter - コスメティック整形（K&Rスタイル）
class Formatter {
   public:
    Formatter() = default;

    /// インデント幅を設定
    void set_indent_width(int width) { indent_width_ = width; }

    /// コードをフォーマット
    FormatResult format(const std::string& original_code) {
        FormatResult result;
        std::string code = original_code;
        size_t changes = 0;

        // 1. 行末の空白を削除
        code = trim_trailing_whitespace(code, changes);

        // 2. タブをスペースに変換
        code = tabs_to_spaces(code, changes);

        // 3. 連続空行を1行に制限
        code = normalize_blank_lines(code, changes);

        // 4. K&Rスタイル: 開き波括弧を同一行に
        code = enforce_kr_braces(code, changes);

        // 5. セミコロン後の改行（括弧内除外）- インデント前に実行
        code = enforce_semicolon_newline(code, changes);

        // 6. インデント正規化（セミコロン改行後のコードも正規化）
        code = normalize_indentation(code, changes);

        // 7. 演算子周りの空白
        code = normalize_operator_spacing(code, changes);

        // 8. 行末コメントの位置揃え
        code = align_inline_comments(code, changes);

        // 9. ファイル末尾に1つの改行を保証
        code = ensure_trailing_newline(code, changes);

        result.formatted_code = code;
        result.modified = (code != original_code);
        result.changes_applied = changes;

        return result;
    }

    /// ファイルをフォーマットして上書き
    bool format_file(const std::string& filepath) {
        std::ifstream ifs(filepath);
        if (!ifs) {
            std::cerr << "エラー: ファイルを読み込めません: " << filepath << "\n";
            return false;
        }

        std::stringstream buffer;
        buffer << ifs.rdbuf();
        std::string original = buffer.str();
        ifs.close();

        auto result = format(original);

        if (result.modified) {
            std::ofstream ofs(filepath);
            if (!ofs) {
                std::cerr << "エラー: ファイルを書き込めません: " << filepath << "\n";
                return false;
            }
            ofs << result.formatted_code;
            return true;
        }

        return false;  // 変更なし
    }

    /// 修正サマリーを表示
    void print_summary(const FormatResult& result, std::ostream& out = std::cout) const {
        if (result.changes_applied > 0) {
            out << "✓ " << result.changes_applied << " 箇所のフォーマット修正\n";
        }
    }

   private:
    int indent_width_ = 4;

    /// 行末の空白を削除
    std::string trim_trailing_whitespace(const std::string& code, size_t& changes) {
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

    /// タブをスペースに変換
    std::string tabs_to_spaces(const std::string& code, size_t& changes) {
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

    /// 連続空行を1行に制限
    std::string normalize_blank_lines(const std::string& code, size_t& changes) {
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

    /// K&Rスタイル: 開き波括弧を同一行に
    std::string enforce_kr_braces(const std::string& code, size_t& changes) {
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

                // 次の行が "{" のみで、現在行が空でない場合
                if (next_trimmed == "{") {
                    // 現在行の末尾の空白を削除
                    size_t end = curr.find_last_not_of(" \t\r");
                    if (end != std::string::npos) {
                        curr = curr.substr(0, end + 1);
                    }

                    // 現在行に " {" を追加
                    result << curr << " {\n";
                    changes++;
                    i++;  // 次の行をスキップ
                    continue;
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

    /// インデント正規化（ブレース深さを追跡）
    std::string normalize_indentation(const std::string& code, size_t& changes) {
        std::istringstream stream(code);
        std::ostringstream result;
        std::string line;
        bool first = true;
        int brace_depth = 0;  // 現在のブレース深さ

        while (std::getline(stream, line)) {
            if (!first)
                result << '\n';
            first = false;

            // 空行はそのまま
            if (line.find_first_not_of(" \t") == std::string::npos) {
                result << "";
                continue;
            }

            // 行の内容を取得（先頭空白を除く）
            size_t content_start = line.find_first_not_of(" \t");
            std::string content =
                (content_start != std::string::npos) ? line.substr(content_start) : "";

            // 閉じブレースで始まる行は先にデクリメント
            bool starts_with_close = (!content.empty() && content[0] == '}');
            if (starts_with_close && brace_depth > 0) {
                brace_depth--;
            }

            // インデントを計算
            size_t indent = static_cast<size_t>(brace_depth) * indent_width_;

            // 正規化されたインデントで出力
            std::string new_line = std::string(indent, ' ') + content;
            if (new_line != line) {
                changes++;
            }
            result << new_line;

            // 行内のブレースを数える（文字列やコメント内は除外）
            bool in_string = false;
            bool in_char = false;
            bool in_comment = false;
            for (size_t i = 0; i < content.size(); ++i) {
                char c = content[i];
                char prev = (i > 0) ? content[i - 1] : 0;

                // コメント検出
                if (!in_string && !in_char && c == '/' && i + 1 < content.size() &&
                    content[i + 1] == '/') {
                    in_comment = true;
                    break;  // 行コメント以降は無視
                }

                // 文字列リテラル
                if (!in_char && !in_comment && c == '"' && prev != '\\') {
                    in_string = !in_string;
                }
                // 文字リテラル
                if (!in_string && !in_comment && c == '\'' && prev != '\\') {
                    in_char = !in_char;
                }

                // ブレースカウント
                if (!in_string && !in_char && !in_comment) {
                    if (c == '{') {
                        brace_depth++;
                    } else if (c == '}' && !starts_with_close) {
                        // 行頭の } は既にデクリメント済み
                        if (brace_depth > 0) {
                            brace_depth--;
                        }
                    }
                }
            }
        }

        return result.str();
    }

    /// 演算子周りの空白正規化
    std::string normalize_operator_spacing(const std::string& code, size_t& changes) {
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
                prev_char != '\\') {
                in_string = !in_string;
            }
            // 文字リテラルの検出
            if (!in_line_comment && !in_block_comment && !in_string && !in_backtick && c == '\'' &&
                prev_char != '\\') {
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
                if (next_char != ' ' && next_char != '\n' && next_char != '\r' &&
                    next_char != ')' && next_char != ']' && next_char != 0) {
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

    /// セミコロン後に改行を強制（括弧内・波括弧内は除外）
    std::string enforce_semicolon_newline(const std::string& code, size_t& changes) {
        std::string result;
        result.reserve(code.size() + 100);

        bool in_string = false;
        bool in_char = false;
        bool in_backtick = false;  // バッククォート（テンプレートリテラル）
        bool in_line_comment = false;
        int paren_depth = 0;  // ()の深さ
        int brace_depth = 0;  // {}の深さ（1行クロージャ対応）
        char prev_char = 0;

        for (size_t i = 0; i < code.size(); ++i) {
            char c = code[i];
            char next_char = (i + 1 < code.size()) ? code[i + 1] : 0;

            // バッククォートリテラルの検出
            if (!in_line_comment && !in_string && !in_char && c == '`') {
                in_backtick = !in_backtick;
            }

            // 文字列リテラルの検出（変数埋め込み{...}内も考慮）
            if (!in_line_comment && !in_char && !in_backtick && c == '"' && prev_char != '\\') {
                in_string = !in_string;
            }
            // 文字リテラルの検出
            if (!in_line_comment && !in_string && !in_backtick && c == '\'' && prev_char != '\\') {
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
                prev_char = c;
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
                prev_char = c;
                continue;
            }

            result += c;
            prev_char = c;
        }

        return result;
    }

    /// 現在行のインデントレベルを取得
    size_t get_current_indent(const std::string& code) const {
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

    /// ファイル末尾に1つの改行を保証
    std::string ensure_trailing_newline(const std::string& code, size_t& changes) {
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

    /// 行末コメントの位置揃え
    std::string align_inline_comments(const std::string& code, size_t& changes) {
        std::istringstream stream(code);
        std::vector<std::string> lines;
        std::string line;

        // 全行を読み込み
        while (std::getline(stream, line)) {
            lines.push_back(line);
        }

        // 行末コメントの情報を収集
        struct LineInfo {
            size_t code_end;       // コード部分の終わり（空白除く）
            size_t comment_start;  // コメントの開始位置
            bool has_comment;
        };

        std::vector<LineInfo> line_infos(lines.size());

        for (size_t i = 0; i < lines.size(); ++i) {
            const std::string& l = lines[i];
            line_infos[i].has_comment = false;

            // 文字列リテラル外での // を探す
            bool in_string = false;
            bool in_char = false;

            for (size_t j = 0; j < l.size(); ++j) {
                char c = l[j];
                char prev = (j > 0) ? l[j - 1] : 0;

                if (!in_char && c == '"' && prev != '\\') {
                    in_string = !in_string;
                }
                if (!in_string && c == '\'' && prev != '\\') {
                    in_char = !in_char;
                }

                if (!in_string && !in_char && j + 1 < l.size() && l[j] == '/' && l[j + 1] == '/') {
                    // コメント発見
                    line_infos[i].comment_start = j;

                    // コード部分の終わり（コメント前の空白を除く）
                    size_t code_end = j;
                    while (code_end > 0 && l[code_end - 1] == ' ') {
                        code_end--;
                    }
                    line_infos[i].code_end = code_end;

                    // インラインコメントのみ対象（行全体がコメントの場合は除外）
                    // code_end == 0 は行頭からコメントなので対象外
                    line_infos[i].has_comment = (code_end > 0);
                    break;
                }
            }
        }

        // 連続するコメント付き行をグループ化して揃える
        std::ostringstream result;
        size_t i = 0;

        while (i < lines.size()) {
            // コメントがない行はそのまま出力
            if (!line_infos[i].has_comment) {
                result << lines[i];
                if (i + 1 < lines.size())
                    result << '\n';
                i++;
                continue;
            }

            // コメント付き行のグループを見つける
            size_t group_start = i;
            size_t group_end = i;
            size_t max_code_end = line_infos[i].code_end;

            // 連続するコメント付き行を探す（空行や非コメント行で区切る）
            while (group_end + 1 < lines.size() && line_infos[group_end + 1].has_comment) {
                group_end++;
                max_code_end = std::max(max_code_end, line_infos[group_end].code_end);
            }

            // コメントを揃える位置（コード部分+2スペース）
            size_t align_col = max_code_end + 2;

            // グループ内の各行を出力
            for (size_t j = group_start; j <= group_end; ++j) {
                const std::string& l = lines[j];
                std::string code_part = l.substr(0, line_infos[j].code_end);
                std::string comment_part = l.substr(line_infos[j].comment_start);

                // コード部分を出力
                result << code_part;

                // パディングを追加
                size_t current_col = code_part.size();
                if (current_col < align_col) {
                    result << std::string(align_col - current_col, ' ');
                    if (line_infos[j].comment_start != line_infos[j].code_end + 2 ||
                        align_col != line_infos[j].code_end + 2) {
                        changes++;
                    }
                } else {
                    result << "  ";  // 最低2スペース
                }

                // コメント部分を出力
                result << comment_part;

                if (j + 1 < lines.size())
                    result << '\n';
            }

            i = group_end + 1;
        }

        return result.str();
    }
};

}  // namespace fmt
}  // namespace cm
