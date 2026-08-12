// ============================================================
// Formatter整形パス: インデント正規化（括弧深さ・条件付きコンパイル・継続行の追跡）
// ============================================================

#include "internal/fmt/formatter.hpp"
#include "internal/fmt/formatter/scan.hpp"

#include <sstream>
#include <string>
#include <vector>

namespace cm {
namespace fmt {

std::string Formatter::normalize_indentation(const std::string& code, size_t& changes) {
    std::istringstream stream(code);
    std::ostringstream result;
    std::string line;
    bool first = true;
    int brace_depth = 0;       // 現在のブレース深さ
    int bracket_depth = 0;     // 現在のブラケット深さ（配列[]）
    int paren_depth = 0;       // 現在の丸括弧深さ（関数引数等）
    bool in_backtick = false;  // バッククォート文字列内かどうか

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

        // バッククォート文字列内の行は一切変更しない（行頭空白も文字列内容の一部。
        // 従来は内部行を絶対深さで再インデントしており、相対インデントが変わると
        // レキサの最小インデント正規化後の実行時文字列内容まで変わる実バグだった）
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
                // 閉じバッククォート行も行頭空白は文字列内容のため原文のまま出力する
                result << line;
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
                    if (!in_char && c == '"' && !is_escaped_char(content, i))
                        in_string = !in_string;
                    if (!in_string && c == '\'' && !is_escaped_char(content, i) &&
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
                // バッククォート内の通常行：原文のまま出力する
                result << line;
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
                    break;
                }
            }

            // 文字列リテラル
            if (!in_char && !in_comment && c == '"' && !is_escaped_char(content, i)) {
                in_string = !in_string;
            }
            // 文字リテラル（SV幅付きリテラルの ' は除外）
            if (!in_string && !in_comment && c == '\'' && !is_escaped_char(content, i) &&
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

}  // namespace fmt
}  // namespace cm
