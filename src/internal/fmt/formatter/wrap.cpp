// ============================================================
// Formatter整形パス: 最大行幅を超える宣言・式の折り返し
// ============================================================

#include "internal/fmt/formatter.hpp"
#include "internal/fmt/formatter/scan.hpp"

#include <algorithm>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace cm {
namespace fmt {

namespace {

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
        std::vector<size_t> chain_dots;  // 文レベルの .method( チェーン呼び出しの . 位置
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
                } else if (c == '.' && bracket_stack.empty()) {
                    // 文レベルの .method( をチェーン折り返し候補として収集する（直後が識別子＋開き括弧の場合のみ。フィールド参照や数値リテラルの小数点は除外）
                    size_t j = i + 1;
                    if (j < line.size() &&
                        (std::isalpha(static_cast<unsigned char>(line[j])) || line[j] == '_')) {
                        while (
                            j < line.size() &&
                            (std::isalnum(static_cast<unsigned char>(line[j])) || line[j] == '_')) {
                            ++j;
                        }
                        if (j < line.size() && line[j] == '(') {
                            chain_dots.push_back(i);
                        }
                    }
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

        // 文レベルのチェーン呼び出し（.method(...)）が2つ以上ある行は、カンマ・演算子折り返しより優先して各チェーンの . 直前で折り返す（TS風。最後の呼び出しの引数リスト途中で折れるのを防ぐ）
        {
            std::vector<size_t> dots;
            for (size_t d : chain_dots) {
                if (d < code_end) {
                    dots.push_back(d);
                }
            }
            if (dots.size() >= 2) {
                // 継続行は1段深く仮置きする（後段のインデント正規化の継続行+1段の規則と一致し冪等になる）
                size_t cont_indent = content_start + static_cast<size_t>(indent_width_);
                std::vector<std::string> out_lines;
                size_t seg_start = 0;
                for (size_t dot : dots) {
                    std::string seg = line.substr(seg_start, dot - seg_start);
                    while (!seg.empty() && seg.back() == ' ')
                        seg.pop_back();
                    out_lines.push_back(seg_start == 0 ? seg : std::string(cont_indent, ' ') + seg);
                    seg_start = dot;
                }
                std::string tail = line.substr(seg_start, code_end - seg_start);
                while (!tail.empty() && tail.back() == ' ')
                    tail.pop_back();
                if (comment_start != std::string::npos) {
                    tail += "  " + line.substr(comment_start);
                }
                out_lines.push_back(std::string(cont_indent, ' ') + tail);
                std::string joined;
                for (size_t li = 0; li < out_lines.size(); ++li) {
                    if (li > 0)
                        joined += '\n';
                    joined += out_lines[li];
                }
                // 折り返し後もなお長いセグメント（長い引数リスト等）はカンマ折り返しへ委ねる。
                // 各セグメントのチェーン . は高々1つなので再帰でチェーン折り返しは再発しない
                result << wrap_long_lines(joined, changes);
                changes++;
                continue;
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
