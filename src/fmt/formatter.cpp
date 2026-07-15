// ============================================================
// Formatter - コスメティック整形（実装）
// ============================================================

#include "formatter.hpp"

#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>

namespace cm {
namespace fmt {

namespace {

// SV幅付きリテラル（8'd170, 4'b1010, 16'hFFFF）の ' かどうかを判定する。
// 文字リテラルの開始と誤認すると括弧カウントが狂うため、
// lexerの規則（数字 + ' + 基数文字 + 値）に合わせて除外する
bool is_sized_literal_quote(const std::string& s, size_t i) {
    if (i >= s.size() || s[i] != '\'')
        return false;
    if (i == 0 || !std::isdigit(static_cast<unsigned char>(s[i - 1])))
        return false;
    if (i + 2 >= s.size())
        return false;
    char base = s[i + 1];
    if (base != 'd' && base != 'D' && base != 'b' && base != 'B' && base != 'h' && base != 'H')
        return false;
    return std::isalnum(static_cast<unsigned char>(s[i + 2])) != 0;
}

}  // namespace

FormatResult Formatter::format(const std::string& original_code) {
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

bool Formatter::format_file(const std::string& filepath) {
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

void Formatter::print_summary(const FormatResult& result, std::ostream& out) const {
    if (result.changes_applied > 0) {
        out << "✓ " << result.changes_applied << " 箇所のフォーマット修正\n";
    }
}

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
            // 現在行が `)` または識別子で終わるとき（関数・if/for・struct名等）に限り、
            // 裸ブロックの `{`（前の行が `;`/`{`/`}` 等で終わる）や
            // 行末コメント付きの行への結合を防ぐ
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

std::string Formatter::normalize_indentation(const std::string& code, size_t& changes) {
    std::istringstream stream(code);
    std::ostringstream result;
    std::string line;
    bool first = true;
    int brace_depth = 0;          // 現在のブレース深さ
    int bracket_depth = 0;        // 現在のブラケット深さ（配列[]）
    int paren_depth = 0;          // 現在の丸括弧深さ（関数引数等）
    bool in_backtick = false;     // バッククォート文字列内かどうか
    int backtick_base_depth = 0;  // バッククォート開始時の深さ

    // 条件付きコンパイルブロック（#ifdef/#ifndef〜#end）をブロック扱いするためのスタック。
    // #else/#end を対応する #ifdef と同列に揃え、#else では分岐開始時点の
    // 括弧カウンタを復元する（分岐ごとに波括弧が不均衡でも崩れない）
    struct CondState {
        int brace;            // #ifdef 時点のブレース深さ
        int bracket;          // #ifdef 時点のブラケット深さ
        int paren;            // #ifdef 時点の丸括弧深さ
        int directive_depth;  // #ifdef 行を出力した深さ
    };
    std::vector<CondState> cond_stack;

    // 文の継続行（長い式の折り返し）のインデント状態。
    // 文の開始行と同じ括弧深さの継続行は1段深くする（1回目で+1し、
    // 2回目以降も同じ深さ）。開き括弧が未閉の継続行は括弧深さに従う
    bool stmt_open = false;    // 直前のコード行が文の途中で終わっているか
    int stmt_group_depth = 0;  // 文の開始行時点の bracket+paren 深さ

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

        // バッククォート文字列内の行はインデント変更しない（相対インデント保持）
        if (in_backtick) {
            // 閉じバッククォートを含む行かチェック
            bool has_close_backtick = false;
            for (size_t i = 0; i < content.size(); ++i) {
                if (content[i] == '`') {
                    has_close_backtick = true;
                    break;
                }
            }

            if (has_close_backtick) {
                // 閉じバッククォート行：開始深さでインデント
                size_t indent = static_cast<size_t>(backtick_base_depth) * indent_width_;
                std::string new_line = std::string(indent, ' ') + content;
                if (new_line != line) {
                    changes++;
                }
                result << new_line;
                in_backtick = false;

                // 閉じバッククォート以降の内容を解析（`);等）
                // ブレース・ブラケットカウントは通常通り
                bool in_string = false;
                bool in_char = false;
                bool past_backtick = false;
                for (size_t i = 0; i < content.size(); ++i) {
                    char c = content[i];
                    if (c == '`') {
                        past_backtick = true;
                        continue;
                    }
                    if (!past_backtick)
                        continue;

                    char prev = (i > 0) ? content[i - 1] : 0;
                    if (!in_char && c == '"' && prev != '\\')
                        in_string = !in_string;
                    if (!in_string && c == '\'' && prev != '\\' &&
                        !is_sized_literal_quote(content, i))
                        in_char = !in_char;
                    if (!in_string && !in_char) {
                        if (c == '{')
                            brace_depth++;
                        else if (c == '}' && brace_depth > 0)
                            brace_depth--;
                        else if (c == '[')
                            bracket_depth++;
                        else if (c == ']' && bracket_depth > 0)
                            bracket_depth--;
                        else if (c == '(')
                            paren_depth++;
                        else if (c == ')' && paren_depth > 0)
                            paren_depth--;
                    }
                }

                // 閉じ行（`); 等）でasm文は完結する
                stmt_open = false;
            } else {
                // バッククォート内の通常行：1段インデント追加
                size_t indent = static_cast<size_t>(backtick_base_depth + 1) * indent_width_;
                std::string new_line = std::string(indent, ' ') + content;
                if (new_line != line) {
                    changes++;
                }
                result << new_line;
            }
            continue;
        }

        // 閉じブレース/ブラケット/丸括弧で始まる行は先にデクリメント
        bool starts_with_close = (!content.empty() && content[0] == '}');
        bool starts_with_bracket_close = (!content.empty() && content[0] == ']');
        bool starts_with_paren_close = (!content.empty() && content[0] == ')');
        if (starts_with_close && brace_depth > 0) {
            brace_depth--;
        }
        if (starts_with_bracket_close && bracket_depth > 0) {
            bracket_depth--;
        }
        if (starts_with_paren_close && paren_depth > 0) {
            paren_depth--;
        }

        // 条件付きコンパイルディレクティブの判定
        // （ブロック内容を1段インデントする。#ifdef/#else/#end自体は外側の深さで出力）
        bool is_cond_open = (content.rfind("#ifdef", 0) == 0 || content.rfind("#ifndef", 0) == 0);
        bool is_cond_else = (content.rfind("#else", 0) == 0);
        bool is_cond_end = (content.rfind("#end", 0) == 0);

        // 文の継続行判定（コメント行・#ディレクティブ/属性行・閉じ括弧行は対象外）
        bool is_comment_line = (content.rfind("//", 0) == 0);
        bool is_directive_line = (!content.empty() && content[0] == '#');
        bool starts_with_any_close =
            starts_with_close || starts_with_bracket_close || starts_with_paren_close;
        int group_now = bracket_depth + paren_depth;
        bool is_continuation = false;
        if (!is_comment_line && !is_directive_line) {
            if (!stmt_open) {
                // 文の開始行: この時点の括弧深さを記録
                stmt_group_depth = group_now;
            } else if (group_now == stmt_group_depth && !starts_with_any_close) {
                // 開き括弧で深くなっていない継続行のみ+1段
                // （未閉括弧のある継続行は括弧深さのインデントに従う。
                // 末尾カンマなしの最終要素直後の閉じ括弧は継続行ではない）
                is_continuation = true;
            }
        }

        // インデントを計算（ブレース + ブラケット + 丸括弧 + 条件ブロックのネスト数）
        int total_depth;
        if ((is_cond_else || is_cond_end) && !cond_stack.empty()) {
            // #else/#end は対応する #ifdef と同じ深さに揃える
            const CondState& top = cond_stack.back();
            total_depth = top.directive_depth;
            if (is_cond_else) {
                // 次の分岐は #ifdef 直後と同じ状態から数え直す
                // （前の分岐内で開いた波括弧等の影響を持ち越さない）
                brace_depth = top.brace;
                bracket_depth = top.bracket;
                paren_depth = top.paren;
            } else {
                // #end: 最後の分岐の括弧状態を持ち越してブロックを閉じる
                cond_stack.pop_back();
            }
        } else {
            total_depth =
                brace_depth + bracket_depth + paren_depth + static_cast<int>(cond_stack.size());
            if (is_continuation) {
                total_depth++;  // 継続行は1段深く（2回目以降も同じ深さになる）
            }
        }
        size_t indent = static_cast<size_t>(total_depth) * indent_width_;

        // 正規化されたインデントで出力
        std::string new_line = std::string(indent, ' ') + content;
        if (new_line != line) {
            changes++;
        }
        result << new_line;

        // #ifdef/#ifndef の次行からブロック内容を1段深くする
        if (is_cond_open) {
            cond_stack.push_back({brace_depth, bracket_depth, paren_depth, total_depth});
        }

        // 行内のブレース・ブラケットを数える（文字列やコメント内は除外）
        bool in_string = false;
        bool in_char = false;
        bool in_comment = false;
        size_t comment_pos = std::string::npos;  // 行末コメントの開始位置
        for (size_t i = 0; i < content.size(); ++i) {
            char c = content[i];
            char prev = (i > 0) ? content[i - 1] : 0;

            // コメント検出
            if (!in_string && !in_char && c == '/' && i + 1 < content.size() &&
                content[i + 1] == '/') {
                in_comment = true;
                comment_pos = i;
                break;  // 行コメント以降は無視
            }

            // バッククォート検出（行内で開始）
            if (!in_string && !in_char && !in_comment && c == '`') {
                // 同一行内に閉じバッククォートがあるか先読み
                // 例: __asm__(`sti`) → 1行完結なのでin_backtickにしない
                bool found_close = false;
                size_t close_pos = 0;
                for (size_t j = i + 1; j < content.size(); ++j) {
                    if (content[j] == '`') {
                        found_close = true;
                        close_pos = j;
                        break;
                    }
                }

                if (found_close) {
                    // 1行バッククォート: バッククォート内をスキップし、
                    // 閉じバッククォート以降のブレースカウントを継続
                    i = close_pos;  // 閉じバッククォート位置までスキップ
                    // ループのi++で次の文字に進む
                } else {
                    // 複数行バッククォート: 次の行以降に閉じバッククォートがある
                    in_backtick = true;
                    // paren_depthはステートメントの一部（__asm__(` の `(`等）なので
                    // バッククォートのベース深さには含めない
                    backtick_base_depth = brace_depth + bracket_depth;
                    break;
                }
            }

            // 文字列リテラル
            if (!in_char && !in_comment && c == '"' && prev != '\\') {
                in_string = !in_string;
            }
            // 文字リテラル（SV幅付きリテラルの ' は除外）
            if (!in_string && !in_comment && c == '\'' && prev != '\\' &&
                !is_sized_literal_quote(content, i)) {
                in_char = !in_char;
            }

            // ブレース・ブラケット・丸括弧カウント
            if (!in_string && !in_char && !in_comment) {
                if (c == '{') {
                    brace_depth++;
                } else if (c == '}' && !starts_with_close) {
                    // 行頭の } は既にデクリメント済み
                    if (brace_depth > 0) {
                        brace_depth--;
                    }
                } else if (c == '[') {
                    bracket_depth++;
                } else if (c == ']' && !starts_with_bracket_close) {
                    // 行頭の ] は既にデクリメント済み
                    if (bracket_depth > 0) {
                        bracket_depth--;
                    }
                } else if (c == '(') {
                    paren_depth++;
                } else if (c == ')' && !starts_with_paren_close) {
                    // 行頭の ) は既にデクリメント済み
                    if (paren_depth > 0) {
                        paren_depth--;
                    }
                }
            }
        }

        // 文状態の更新: 行末コメントを除いたコード末尾で文の完結を判定する
        if (in_backtick) {
            // 複数行バッククォート中は閉じ行の処理で確定する
        } else if (is_directive_line) {
            stmt_open = false;
        } else if (!is_comment_line) {
            std::string code_part =
                (comment_pos != std::string::npos) ? content.substr(0, comment_pos) : content;
            size_t code_end = code_part.find_last_not_of(" \t\r");
            if (code_end != std::string::npos) {
                char last = code_part[code_end];
                stmt_open = (last != ';' && last != '{' && last != '}' && last != ',');
            }
        }
    }

    return result.str();
}

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
            prev_char != '\\') {
            in_string = !in_string;
        }
        // 文字リテラルの検出（SV幅付きリテラルの ' は除外）
        if (!in_line_comment && !in_block_comment && !in_string && !in_backtick && c == '\'' &&
            prev_char != '\\' && !is_sized_literal_quote(code, i)) {
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

std::string Formatter::enforce_semicolon_newline(const std::string& code, size_t& changes) {
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
        // 文字リテラルの検出（SV幅付きリテラルの ' は除外）
        if (!in_line_comment && !in_string && !in_backtick && c == '\'' && prev_char != '\\' &&
            !is_sized_literal_quote(code, i)) {
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
            char prev = (j > 0) ? line[j - 1] : 0;

            if (!in_char && c == '"' && prev != '\\') {
                in_string = !in_string;
            }
            if (!in_string && c == '\'' && prev != '\\' && !is_sized_literal_quote(line, j)) {
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
