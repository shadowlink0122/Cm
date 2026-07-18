// ============================================================
// Formatter - コスメティック整形（実装）
// ============================================================

#include "formatter.hpp"

#include "../common/i18n.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>

namespace cm {
namespace fmt {

namespace {

// SV幅付きリテラル（8'd170, 4'b1010, 16'hFFFF）の ' かどうかを判定する。
// 文字リテラルの開始と誤認すると括弧カウントが狂うため、lexerの規則（数字 + ' + 基数文字 + 値）に合わせて除外する
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

// 1行に詰め込まれた文ブロック（{ 文; 文; }）の展開位置（wrap_long_linesの補助）
struct BlockExpansionStart {
    size_t pos;  // 新しい行の開始位置（行内絶対位置）
    int depth;   // その行の行内相対ブロック深さ（インデント段数）
};

// 展開位置を収集する。文レベル（丸括弧・角括弧の外）の波括弧内に ; がある場合のみ
// 非空を返す。分割は {の直後・;の直後・}の直前で行い、各行の相対深さも記録する（enforce_semicolon_newline / normalize_indentation の再実行と一致する固定点を作る）
std::vector<BlockExpansionStart> collect_block_expansion_starts(const std::string& line,
                                                                size_t content_start,
                                                                size_t code_end) {
    std::vector<BlockExpansionStart> starts;
    bool has_stmt_semicolon = false;
    bool in_string = false;
    bool in_char = false;
    int block_depth = 0;      // 行内で開いた文レベル波括弧の深さ
    std::vector<char> stack;  // 未閉の括弧（種類）
    // スタックに丸括弧・角括弧が無い＝文レベル（for(;;)やクロージャ引数内は対象外）
    auto brace_only = [&stack]() {
        for (char b : stack) {
            if (b == '(' || b == '[')
                return false;
        }
        return true;
    };
    for (size_t i = content_start; i < code_end; ++i) {
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
        } else if (c == '\'' && !is_sized_literal_quote(line, i)) {
            in_char = true;
        } else if (c == '(' || c == '[') {
            stack.push_back(c);
        } else if (c == ')' || c == ']') {
            if (!stack.empty())
                stack.pop_back();
        } else if (c == '{') {
            if (brace_only()) {
                block_depth++;
                starts.push_back({i + 1, block_depth});  // { の直後で改行
            }
            stack.push_back(c);
        } else if (c == '}') {
            if (!stack.empty())
                stack.pop_back();
            if (brace_only()) {
                if (block_depth > 0)
                    block_depth--;
                // } の直前で改行（"} else {" は同一行に残る）
                starts.push_back({i, block_depth});
            }
        } else if (c == ';') {
            if (brace_only()) {
                if (!stack.empty()) {
                    has_stmt_semicolon = true;
                }
                starts.push_back({i + 1, block_depth});  // ; の直後で改行
            }
        }
    }
    if (!has_stmt_semicolon) {
        return {};
    }
    std::sort(starts.begin(), starts.end(),
              [](const BlockExpansionStart& a, const BlockExpansionStart& b) {
                  return a.pos != b.pos ? a.pos < b.pos : a.depth < b.depth;
              });
    starts.erase(std::unique(starts.begin(), starts.end(),
                             [](const BlockExpansionStart& a, const BlockExpansionStart& b) {
                                 return a.pos == b.pos;
                             }),
                 starts.end());
    return starts;
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

    // 8. 最大行幅を超える宣言・式の折り返し（空白正規化で行長が確定した後に実行し、折り返した継続行のインデントは次のインデント正規化の再実行で整える）
    code = wrap_long_lines(code, changes);

    // 9. インデント正規化の再実行（折り返しで生じた継続行を整える）
    code = normalize_indentation(code, changes);

    // 10. 行末コメントの位置揃え
    code = align_inline_comments(code, changes);

    // 11. ファイル末尾に1つの改行を保証
    code = ensure_trailing_newline(code, changes);

    result.formatted_code = code;
    result.modified = (code != original_code);
    result.changes_applied = changes;

    return result;
}

bool Formatter::format_file(const std::string& filepath) {
    std::ifstream ifs(filepath);
    if (!ifs) {
        std::cerr << i18n::msgf(i18n::MsgId::FmtCannotReadFile, filepath);
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
            std::cerr << i18n::msgf(i18n::MsgId::FmtCannotWriteFile, filepath);
            return false;
        }
        ofs << result.formatted_code;
        return true;
    }

    return false;  // 変更なし
}

void Formatter::print_summary(const FormatResult& result, std::ostream& out) const {
    if (result.changes_applied > 0) {
        out << i18n::msgf(i18n::MsgId::FmtFormattingFixEs, result.changes_applied);
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
    // #else/#end を対応する #ifdef と同列に揃え、#else では分岐開始時点の括弧カウンタを復元する（分岐ごとに波括弧が不均衡でも崩れない）
    struct CondState {
        int brace;            // #ifdef 時点のブレース深さ
        int bracket;          // #ifdef 時点のブラケット深さ
        int paren;            // #ifdef 時点の丸括弧深さ
        int directive_depth;  // #ifdef 行を出力した深さ
    };
    std::vector<CondState> cond_stack;

    // 文の継続行（長い式の折り返し）のインデント状態。
    // 文の開始行と同じ括弧深さの継続行は1段深くする（1回目で+1し、2回目以降も同じ深さ）。開き括弧が未閉の継続行は括弧深さに従う
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

        // 条件付きコンパイルディレクティブの判定（ブロック内容を1段インデントする。#ifdef/#else/#end自体は外側の深さで出力）
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
                // 開き括弧で深くなっていない継続行のみ+1段（未閉括弧のある継続行は括弧深さのインデントに従う。
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
                // 次の分岐は #ifdef 直後と同じ状態から数え直す（前の分岐内で開いた波括弧等の影響を持ち越さない）
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
                    // 1行バッククォート: バッククォート内をスキップし、閉じバッククォート以降のブレースカウントを継続
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

std::string Formatter::wrap_long_lines(const std::string& code, size_t& changes) {
    // 最大行幅を超える行を、カンマ直後（優先）または二項演算子の直前で折り返す。
    // 折り返し位置はコード部（コメント・文字列リテラルの外）のみから選ぶ。
    // 継続行のインデントはこの後のインデント正規化の再実行で整うため、
    // ここでは継続1段（最浅括弧深さぶん）を仮置きして幅の判定に使う
    const size_t max_width = static_cast<size_t>(max_line_width_);

    // 折り返し候補となる二項演算子（長い順に照合する）
    static const char* const kWrapOps[] = {
        "<<", ">>", "&&", "||", "==", "!=", "<=", ">=", "+", "-", "*", "/", "%", "&", "|", "^"};

    std::istringstream stream(code);
    std::ostringstream result;
    std::string line;
    bool first = true;
    bool in_backtick = false;

    while (std::getline(stream, line)) {
        if (!first)
            result << '\n';
        first = false;

        // 複数行文字列（バッククォート）はそのまま通す
        bool line_has_backtick = line.find('`') != std::string::npos;
        if (in_backtick || line_has_backtick) {
            for (size_t i = 0; i < line.size(); ++i) {
                if (line[i] == '`')
                    in_backtick = !in_backtick;
            }
            result << line;
            continue;
        }

        if (line.size() <= max_width) {
            result << line;
            continue;
        }

        size_t content_start = line.find_first_not_of(' ');
        if (content_start == std::string::npos) {
            result << line;
            continue;
        }
        // コメント行・ディレクティブ/属性行は折り返さない
        if (line.compare(content_start, 2, "//") == 0 || line[content_start] == '#') {
            result << line;
            continue;
        }

        // コード部を走査して、コメント開始位置と折り返し候補を収集する
        struct Candidate {
            size_t pos;     // 次の行がこの位置から始まる
            int depth;      // 候補時点の括弧深さ
            bool is_comma;  // カンマ直後の候補か
            char group;     // カンマを直接囲む括弧の種類（トップレベルは0）
            size_t group_open;  // その括弧の開き位置（トップレベルはnpos）
        };
        std::vector<Candidate> cands;
        std::map<size_t, size_t> close_of;  // 開き括弧位置 → 対応する閉じ括弧位置
        size_t comment_start = std::string::npos;
        {
            bool in_string = false;
            bool in_char = false;
            int depth = 0;
            std::vector<std::pair<char, size_t>> bracket_stack;  // (種類, 開き位置)
            for (size_t i = content_start; i < line.size(); ++i) {
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
                } else if (c == '\'' && !is_sized_literal_quote(line, i)) {
                    in_char = true;
                } else if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') {
                    comment_start = i;
                    break;
                } else if (c == '(' || c == '[' || c == '{') {
                    depth++;
                    bracket_stack.push_back({c, i});
                } else if (c == ')' || c == ']' || c == '}') {
                    depth--;
                    if (!bracket_stack.empty()) {
                        close_of[bracket_stack.back().second] = i;
                        bracket_stack.pop_back();
                    }
                } else if (c == ',') {
                    char group = bracket_stack.empty() ? '\0' : bracket_stack.back().first;
                    size_t group_open =
                        bracket_stack.empty() ? std::string::npos : bracket_stack.back().second;
                    cands.push_back({i + 1, depth, true, group, group_open});
                } else if (c == ' ' && i + 1 < line.size()) {
                    // " op " の形の二項演算子: 演算子の直前で折り返す
                    for (const char* op : kWrapOps) {
                        size_t op_len = std::char_traits<char>::length(op);
                        if (line.compare(i + 1, op_len, op) == 0 && i + 1 + op_len < line.size() &&
                            line[i + 1 + op_len] == ' ') {
                            cands.push_back({i + 1, depth, false, '\0', std::string::npos});
                            break;
                        }
                    }
                }
            }
        }

        // コード部の終端（行末コメントは除外し、コメントだけで超過する行は対象外）
        size_t code_end = (comment_start != std::string::npos) ? comment_start : line.size();
        while (code_end > 0 && line[code_end - 1] == ' ')
            code_end--;
        if (code_end <= max_width) {
            result << line;
            continue;
        }

        // 1行に詰め込まれた文ブロック（{ 文; 文; }）を含む場合はブロック展開する。
        // 演算子折り返しでは if ヘッダの途中等で折れて読みにくく、さらに `; }` を含む
        // 継続行が次回実行の enforce_semicolon_newline で再分割されて冪等性が崩れるため
        {
            std::vector<BlockExpansionStart> starts =
                collect_block_expansion_starts(line, content_start, code_end);
            if (!starts.empty()) {
                std::vector<std::string> out_lines;
                size_t seg_start = content_start;
                // 各行のインデントは後段の normalize_indentation 再実行と一致するよう
                // 相対ブロック深さぶん深くする（再帰折り返しの幅判定を正確にするため）
                auto emit_segment = [&](size_t seg_end, int depth) {
                    std::string seg = line.substr(seg_start, seg_end - seg_start);
                    while (!seg.empty() && seg.front() == ' ')
                        seg.erase(seg.begin());
                    while (!seg.empty() && seg.back() == ' ')
                        seg.pop_back();
                    if (!seg.empty()) {
                        size_t indent = content_start + static_cast<size_t>(depth) * indent_width_;
                        out_lines.push_back(std::string(indent, ' ') + seg);
                    }
                };
                int seg_depth = 0;
                for (const auto& start : starts) {
                    if (start.pos >= code_end)
                        break;
                    emit_segment(start.pos, seg_depth);
                    seg_start = start.pos;
                    seg_depth = start.depth;
                }
                emit_segment(code_end, seg_depth);
                // 2行以上に展開できた場合のみ採用（1行のままなら通常の折り返しへ）。
                // 展開後もなお長い文は再帰適用で演算子折り返しに委ねる
                if (out_lines.size() > 1) {
                    if (comment_start != std::string::npos) {
                        out_lines.back() += "  " + line.substr(comment_start);
                    }
                    std::string joined;
                    for (size_t li = 0; li < out_lines.size(); ++li) {
                        if (li > 0)
                            joined += '\n';
                        joined += out_lines[li];
                    }
                    result << wrap_long_lines(joined, changes);
                    changes++;
                    continue;
                }
            }
        }

        // 折り返し位置の選択: より浅い括弧深さの候補を優先し、同深さならカンマを優先する（深い位置のカンマで呼び出しの途中を折るより、浅い演算子で折るほうが読みやすい）
        bool has_comma = false;
        bool has_op = false;
        int min_comma_depth = 0;
        int min_op_depth = 0;
        for (const auto& cand : cands) {
            if (cand.pos >= code_end)
                continue;
            if (cand.is_comma) {
                if (!has_comma || cand.depth < min_comma_depth)
                    min_comma_depth = cand.depth;
                has_comma = true;
            } else {
                if (!has_op || cand.depth < min_op_depth)
                    min_op_depth = cand.depth;
                has_op = true;
            }
        }
        bool use_comma = has_comma && (!has_op || min_comma_depth <= min_op_depth);
        int min_depth = use_comma ? min_comma_depth : min_op_depth;
        if (!has_comma && !has_op) {
            result << line;
            continue;
        }
        std::vector<size_t> breaks;
        for (const auto& cand : cands) {
            if (cand.pos < code_end && cand.is_comma == use_comma && cand.depth == min_depth) {
                breaks.push_back(cand.pos);
            }
        }
        if (breaks.empty()) {
            result << line;
            continue;
        }

        // 括弧グループ（{} / [] / 関数引数の () ）内の要素で折り返す場合は全要素を1行ずつに展開する（中途半端な貪欲詰めにせず、開き括弧で改行・1要素1行・閉じ括弧を独立行にする）
        if (use_comma) {
            bool explode_brace = true;
            size_t group_open = std::string::npos;
            for (const auto& cand : cands) {
                if (cand.pos < code_end && cand.is_comma && cand.depth == min_depth) {
                    if (cand.group == '\0' ||
                        (group_open != std::string::npos && cand.group_open != group_open)) {
                        explode_brace = false;
                        break;
                    }
                    group_open = cand.group_open;
                }
            }
            if (explode_brace && group_open != std::string::npos &&
                close_of.count(group_open) != 0 && close_of[group_open] < code_end) {
                size_t group_close = close_of[group_open];
                size_t cont_indent = content_start + static_cast<size_t>(indent_width_);
                std::vector<std::string> out_lines;
                out_lines.push_back(line.substr(0, group_open + 1));
                size_t elem_start = group_open + 1;
                for (size_t bp : breaks) {
                    std::string elem = line.substr(elem_start, bp - elem_start);
                    while (!elem.empty() && elem.front() == ' ')
                        elem.erase(elem.begin());
                    while (!elem.empty() && elem.back() == ' ')
                        elem.pop_back();
                    out_lines.push_back(std::string(cont_indent, ' ') + elem);
                    elem_start = bp;
                }
                std::string last_elem = line.substr(elem_start, group_close - elem_start);
                while (!last_elem.empty() && last_elem.front() == ' ')
                    last_elem.erase(last_elem.begin());
                while (!last_elem.empty() && last_elem.back() == ' ')
                    last_elem.pop_back();
                out_lines.push_back(std::string(cont_indent, ' ') + last_elem);
                std::string closing = line.substr(group_close, code_end - group_close);
                if (comment_start != std::string::npos) {
                    closing += "  " + line.substr(comment_start);
                }
                out_lines.push_back(std::string(content_start, ' ') + closing);
                for (size_t li = 0; li < out_lines.size(); ++li) {
                    if (li > 0)
                        result << '\n';
                    result << out_lines[li];
                }
                changes++;
                continue;
            }
        }

        // 貪欲パック: 各行が最大幅に収まる最遠の候補で折り返す
        size_t cont_indent =
            content_start + static_cast<size_t>(std::max(1, min_depth)) * indent_width_;
        std::vector<std::string> out_lines;
        size_t seg_start = 0;  // 現在のセグメント開始（行内絶対位置）
        size_t break_idx = 0;  // 次に検討する候補
        bool is_first_seg = true;
        while (break_idx < breaks.size()) {
            size_t seg_indent = is_first_seg ? 0 : cont_indent;
            size_t seg_budget = (max_width > seg_indent) ? max_width - seg_indent : 0;
            // このセグメントに収まる最遠の候補を選ぶ（1つも収まらなければ最初の候補で折る）
            size_t chosen = std::string::npos;
            for (size_t bi = break_idx; bi < breaks.size(); ++bi) {
                size_t len = breaks[bi] - seg_start;
                if (len <= seg_budget) {
                    chosen = bi;
                } else {
                    break;
                }
            }
            if (chosen == std::string::npos) {
                chosen = break_idx;
            }
            // 残り全体が収まるなら折り返し不要
            if (code_end - seg_start <= seg_budget) {
                break;
            }
            size_t bp = breaks[chosen];
            std::string seg = line.substr(seg_start, bp - seg_start);
            while (!seg.empty() && seg.back() == ' ')
                seg.pop_back();
            out_lines.push_back(std::string(seg_indent, ' ') + seg);
            // 次セグメントの開始（先頭の空白は除く）
            seg_start = bp;
            while (seg_start < code_end && line[seg_start] == ' ')
                seg_start++;
            break_idx = chosen + 1;
            is_first_seg = false;
        }
        if (out_lines.empty()) {
            result << line;
            continue;
        }
        // 最終セグメント（行末コメントがあれば同じ行に残す）
        std::string tail = line.substr(seg_start, code_end - seg_start);
        if (comment_start != std::string::npos) {
            tail += "  " + line.substr(comment_start);
        }
        out_lines.push_back(std::string(cont_indent, ' ') + tail);

        for (size_t li = 0; li < out_lines.size(); ++li) {
            if (li > 0)
                result << '\n';
            result << out_lines[li];
        }
        changes++;
    }

    return result.str();
}

}  // namespace fmt
}  // namespace cm
