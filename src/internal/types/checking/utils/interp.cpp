// ============================================================
// TypeChecker 実装 - 文字列補間プレースホルダの解析・スコープ検査・使用マーク
// ============================================================

#include "internal/base/i18n.hpp"
#include "internal/syntax/lexer/lexer.hpp"
#include "internal/types/type_checker.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace cm {

std::vector<std::string> TypeChecker::extract_format_variables(const std::string& format_str) {
    std::vector<std::string> variables;
    std::regex placeholder_regex(R"(\{([a-zA-Z_][a-zA-Z0-9_]*)\})");
    std::smatch match;
    std::string::const_iterator search_start(format_str.cbegin());

    while (std::regex_search(search_start, format_str.cend(), match, placeholder_regex)) {
        std::string var_name = match[1];
        if (std::find(variables.begin(), variables.end(), var_name) == variables.end()) {
            variables.push_back(var_name);
        }
        search_start = match.suffix().first;
    }

    return variables;
}

namespace {

// 補間プレースホルダの式文字列を抽出する（MIRのexpr_interpと同じ規則:
// {{/}}エスケープ、::はパス区切り、[]/()外の単独コロンはフォーマット指定子の開始）
std::vector<std::string> extract_placeholder_exprs(const std::string& format_str) {
    std::vector<std::string> contents;
    size_t pos = 0;
    while (pos < format_str.length()) {
        if (pos + 1 < format_str.length() && format_str[pos] == '{' && format_str[pos + 1] == '{') {
            pos += 2;
            continue;
        }
        if (pos + 1 < format_str.length() && format_str[pos] == '}' && format_str[pos + 1] == '}') {
            pos += 2;
            continue;
        }
        if (format_str[pos] != '{') {
            pos++;
            continue;
        }
        // 対応する } を探しつつ、フォーマット指定子のコロン位置を判定する。
        // L2: ネストした波括弧（構造体リテラル）と文字列リテラル内の } { : は式の一部として
        // 扱い、深度0・引用符外の } のみを終端とする（MIRのexpr_interpと同一規則）
        size_t end = pos + 1;
        size_t colon_pos = std::string::npos;
        int depth = 0;
        int brace_depth = 0;
        bool in_quotes = false;
        while (end < format_str.length()) {
            char c = format_str[end];
            if (in_quotes) {
                if (c == '\\' && end + 1 < format_str.length()) {
                    end += 2;
                    continue;
                }
                if (c == '"') {
                    in_quotes = false;
                }
                end++;
                continue;
            }
            if (c == '"') {
                in_quotes = true;
                end++;
                continue;
            }
            if (c == '{') {
                brace_depth++;
                end++;
                continue;
            }
            if (c == '}') {
                if (brace_depth == 0) {
                    break;  // プレースホルダの終端
                }
                brace_depth--;
                end++;
                continue;
            }
            if (c == '[' || c == '(') {
                depth++;
            }
            if (c == ']' || c == ')') {
                depth--;
            }
            if (c == ':' && end + 1 < format_str.length() && format_str[end + 1] == ':') {
                end += 2;  // ::はパス区切りとしてスキップ
                continue;
            }
            if (c == ':' && depth == 0 && brace_depth == 0 && colon_pos == std::string::npos) {
                colon_pos = end;
            }
            end++;
        }
        if (end >= format_str.length()) {
            break;  // 閉じ } がない
        }
        size_t content_end = (colon_pos != std::string::npos) ? colon_pos : end;
        std::string content = format_str.substr(pos + 1, content_end - pos - 1);
        if (!content.empty()) {
            contents.push_back(content);
        }
        pos = end + 1;
    }
    return contents;
}

}  // namespace

namespace {

// パースした部分式ツリーへリテラル位置のSpanを再帰的に刻印する。
// プレースホルダは合成ラッパー関数として再パースされるため、素のSpanは合成ソース上の座標になり診断位置が壊れる
void stamp_spans(ast::Expr& e, const Span& span) {
    e.span = span;
    if (auto* mem = e.as<ast::MemberExpr>()) {
        if (mem->object) {
            stamp_spans(*mem->object, span);
        }
        for (auto& arg : mem->args) {
            if (arg) {
                stamp_spans(*arg, span);
            }
        }
    } else if (auto* bin = e.as<ast::BinaryExpr>()) {
        if (bin->left) {
            stamp_spans(*bin->left, span);
        }
        if (bin->right) {
            stamp_spans(*bin->right, span);
        }
    } else if (auto* un = e.as<ast::UnaryExpr>()) {
        if (un->operand) {
            stamp_spans(*un->operand, span);
        }
    } else if (auto* call = e.as<ast::CallExpr>()) {
        if (call->callee) {
            stamp_spans(*call->callee, span);
        }
        for (auto& arg : call->args) {
            if (arg) {
                stamp_spans(*arg, span);
            }
        }
    } else if (auto* idx = e.as<ast::IndexExpr>()) {
        if (idx->object) {
            stamp_spans(*idx->object, span);
        }
        if (idx->index) {
            stamp_spans(*idx->index, span);
        }
    } else if (auto* sl = e.as<ast::SliceExpr>()) {
        if (sl->object) {
            stamp_spans(*sl->object, span);
        }
    } else if (auto* cast = e.as<ast::CastExpr>()) {
        if (cast->operand) {
            stamp_spans(*cast->operand, span);
        }
    } else if (auto* tern = e.as<ast::TernaryExpr>()) {
        if (tern->condition) {
            stamp_spans(*tern->condition, span);
        }
        if (tern->then_expr) {
            stamp_spans(*tern->then_expr, span);
        }
        if (tern->else_expr) {
            stamp_spans(*tern->else_expr, span);
        }
    }
}

// プレースホルダ内容を式としてパースする（合成ラッパー方式）。パース不能ならnullptr
ast::ExprPtr parse_interp_content(const std::string& content) {
    std::string src = "int __interp_part__() { return (" + content + "); }";
    Lexer lex(src);
    auto tokens = lex.tokenize();
    Parser parser(std::move(tokens));
    auto program = parser.parse();
    if (parser.has_errors()) {
        return nullptr;
    }
    for (auto& decl : program.declarations) {
        auto* func = decl->as<ast::FunctionDecl>();
        if (!func || func->name != "__interp_part__" || func->body.empty()) {
            continue;
        }
        auto* ret = func->body[0]->as<ast::ReturnStmt>();
        if (!ret || !ret->value) {
            continue;
        }
        return std::move(ret->value);
    }
    return nullptr;
}

}  // namespace

// 文字列リテラルの補間プレースホルダを一度だけ実ASTへ脱糖する（type-resolution-simplification 領域1第4段b）。
// 従来はチェッカーの検査用パース・MIRのミニパイプライン・影の型チェッカーが同じテキストを個別に再パースしていたが、脱糖後は本物の推論・loweringが部分式をそのまま消費する。
// match/enumメソッドのscrutinee退避プリパス（match_hoist）が型検査前に部分式を走査できるよう自由関数として公開する
void desugar_string_interpolation(ast::LiteralExpr& lit, const Span& span) {
    if (lit.interp_scanned || !lit.is_string()) {
        return;
    }
    lit.interp_scanned = true;
    for (const auto& content : extract_placeholder_exprs(std::get<std::string>(lit.value))) {
        auto expr = parse_interp_content(content);
        if (!expr) {
            // パース不能な内容は従来どおりリテラル文字として扱う（診断はMIR側のフォールバックが行う）
            continue;
        }
        stamp_spans(*expr, span);
        lit.interp_parts.emplace_back(content, std::shared_ptr<ast::Expr>(std::move(expr)));
    }
}

void TypeChecker::desugar_interpolation_parts(ast::LiteralExpr& lit) {
    desugar_string_interpolation(lit, current_span_);
}

}  // namespace cm
