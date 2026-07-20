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

// 式ツリー内の識別子参照を収集する（メンバ名・enumパスは対象外）
void collect_ident_refs(const ast::Expr& e, std::vector<std::string>& out) {
    if (const auto* id = e.as<ast::IdentExpr>()) {
        out.push_back(id->name);
        return;
    }
    if (const auto* mem = e.as<ast::MemberExpr>()) {
        if (mem->object) {
            collect_ident_refs(*mem->object, out);
        }
        for (const auto& arg : mem->args) {
            if (arg) {
                collect_ident_refs(*arg, out);
            }
        }
        return;
    }
    if (const auto* bin = e.as<ast::BinaryExpr>()) {
        if (bin->left) {
            collect_ident_refs(*bin->left, out);
        }
        if (bin->right) {
            collect_ident_refs(*bin->right, out);
        }
        return;
    }
    if (const auto* un = e.as<ast::UnaryExpr>()) {
        if (un->operand) {
            collect_ident_refs(*un->operand, out);
        }
        return;
    }
    if (const auto* call = e.as<ast::CallExpr>()) {
        if (call->callee) {
            collect_ident_refs(*call->callee, out);
        }
        for (const auto& arg : call->args) {
            if (arg) {
                collect_ident_refs(*arg, out);
            }
        }
        return;
    }
    if (const auto* idx = e.as<ast::IndexExpr>()) {
        if (idx->object) {
            collect_ident_refs(*idx->object, out);
        }
        if (idx->index) {
            collect_ident_refs(*idx->index, out);
        }
        return;
    }
    if (const auto* sl = e.as<ast::SliceExpr>()) {
        if (sl->object) {
            collect_ident_refs(*sl->object, out);
        }
        // ビットスライスの範囲は定数式なので対象外
        return;
    }
    if (const auto* cast = e.as<ast::CastExpr>()) {
        if (cast->operand) {
            collect_ident_refs(*cast->operand, out);
        }
        return;
    }
    if (const auto* tern = e.as<ast::TernaryExpr>()) {
        if (tern->condition) {
            collect_ident_refs(*tern->condition, out);
        }
        if (tern->then_expr) {
            collect_ident_refs(*tern->then_expr, out);
        }
        if (tern->else_expr) {
            collect_ident_refs(*tern->else_expr, out);
        }
        return;
    }
}

}  // namespace

void TypeChecker::check_interpolation_scope(const std::string& format_str) {
    for (const auto& content : extract_placeholder_exprs(format_str)) {
        // プレースホルダ内容を式としてパースする（MIRの補間ミニパイプラインと同じ手法）
        std::string src = "int __interp_scope_check__() { return (" + content + "); }";
        Lexer lex(src);
        auto tokens = lex.tokenize();
        Parser parser(std::move(tokens));
        auto program = parser.parse();
        if (parser.has_errors()) {
            continue;  // パース不能な内容はMIR側の処理に委ねる
        }
        for (auto& decl : program.declarations) {
            const auto* func = decl->as<ast::FunctionDecl>();
            if (!func || func->name != "__interp_scope_check__" || func->body.empty()) {
                continue;
            }
            const auto* ret = func->body[0]->as<ast::ReturnStmt>();
            if (!ret || !ret->value) {
                continue;
            }
            std::vector<std::string> names;
            collect_ident_refs(*ret->value, names);
            for (const auto& name : names) {
                if (name.find("::") != std::string::npos || name == "null" || name == "true" ||
                    name == "false") {
                    continue;
                }
                if (scopes_.current().lookup(name)) {
                    // 補間内の参照は変数の使用としてマークする（W001誤検出防止）
                    scopes_.current().mark_used(name);
                    // 補間経由のmove後使用を診断する（H12: 従来は補間内がすり抜けていた）
                    check_use_after_move(name, current_span_);
                    continue;
                }
                if (enum_names_.count(name) || struct_defs_.count(name) ||
                    typedef_defs_.count(name) || generic_functions_.count(name) ||
                    interface_names_.count(name)) {
                    continue;
                }
                error(current_span_, "Undefined variable '" + name +
                                         "' in interpolation placeholder '{" + content + "}'");
            }
        }
    }
}

// 文字列リテラル内の補間プレースホルダが参照する変数を使用としてマークする（check_interpolation_scopeと違いエラーは出さない。あらゆるstringリテラルで呼ばれる）
void TypeChecker::mark_interpolation_uses(const std::string& format_str) {
    for (const auto& content : extract_placeholder_exprs(format_str)) {
        std::string src = "int __interp_scope_check__() { return (" + content + "); }";
        Lexer lex(src);
        auto tokens = lex.tokenize();
        Parser parser(std::move(tokens));
        auto program = parser.parse();
        if (parser.has_errors()) {
            continue;
        }
        for (auto& decl : program.declarations) {
            const auto* func = decl->as<ast::FunctionDecl>();
            if (!func || func->name != "__interp_scope_check__" || func->body.empty()) {
                continue;
            }
            const auto* ret = func->body[0]->as<ast::ReturnStmt>();
            if (!ret || !ret->value) {
                continue;
            }
            std::vector<std::string> names;
            collect_ident_refs(*ret->value, names);
            for (const auto& name : names) {
                if (scopes_.current().lookup(name)) {
                    scopes_.current().mark_used(name);
                    mark_variable_initialized(name);
                }
            }
        }
    }
}

}  // namespace cm
