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
        int ternary_pending = 0;
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
            // 三項演算子の '?'。対応する ':' はフォーマット指定子ではない（局所処理調査D2。MIRのexpr_interpと同一規則）
            if (c == '?' && depth == 0 && brace_depth == 0) {
                ternary_pending++;
                end++;
                continue;
            }
            if (c == ':' && end + 1 < format_str.length() && format_str[end + 1] == ':') {
                end += 2;  // ::はパス区切りとしてスキップ
                continue;
            }
            if (c == ':' && depth == 0 && brace_depth == 0 && colon_pos == std::string::npos) {
                if (ternary_pending > 0) {
                    ternary_pending--;  // 三項のコロン
                    end++;
                    continue;
                }
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
    // 合成ラッパー__interp_part__自身が'__'予約識別子検査に落ちないようにする
    parser.set_allow_reserved_idents(true);
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
            // パース不能な内容はリテラル文字として出力される（MIR側フォールバック）。リテラルへ記録しcheckerのdesugar_interpolation_partsが警告する（従来は無診断でエラー型ローカルの未初期化値が出力されていた）
            lit.interp_parse_failures.push_back(content);
            continue;
        }
        stamp_spans(*expr, span);
        lit.interp_parts.emplace_back(content, std::shared_ptr<ast::Expr>(std::move(expr)));
    }
}

// 集約型のprint/補間直接整形の診断（局所処理調査G3）。
// 従来は共有規則が無く各バックエンドが即興整形しており、配列/スライスの補間はinterp/nativeが空・ゴミバイト、JSが"1,2,3"、
// 構造体はinterp/nativeが空、JSが"[object Object]"、直接引数println(arr)はnative/jitがLLVM検証失敗でクラッシュと分裂していた。
// 要素の個別出力またはdebug()等のstringを返すメソッドへ誘導する。
// 対象は非bit配列/スライスと登録済み構造体のみ: bitベクタは整数として、ユニオンはタグで実バリアントを整形する対応済み機能（v0.16.0）のため対象外。
// 未登録名の構造体型（ジェネリック型パラメータT等）はモノモーフ化前の見かけの型のため対象外とする
void TypeChecker::check_print_aggregate(const ast::TypePtr& type, const Span& span) {
    if (!type) {
        return;
    }
    auto resolved = resolve_typedef(type);
    if (!resolved) {
        return;
    }
    const bool is_bits_vector = resolved->kind == ast::TypeKind::Array && resolved->element_type &&
                                resolved->element_type->kind == ast::TypeKind::Bit;
    const bool is_plain_array = resolved->kind == ast::TypeKind::Array && !is_bits_vector;
    const bool is_known_struct =
        resolved->kind == ast::TypeKind::Struct && get_struct(resolved->name) != nullptr;
    if (is_plain_array || is_known_struct) {
        error(span,
              i18n::msgf(i18n::MsgId::TcPrintAggregateUnsupported, type_to_string(*resolved)));
    }
}

void TypeChecker::desugar_interpolation_parts(ast::LiteralExpr& lit) {
    desugar_string_interpolation(lit, current_span_);
    // 脱糖はプリパス（match_hoist）が先行することがあるため、記録済みの失敗をここで一度だけ報告する
    if (!lit.interp_failures_reported && !lit.interp_parse_failures.empty()) {
        lit.interp_failures_reported = true;
        for (const auto& content : lit.interp_parse_failures) {
            const std::string msg =
                i18n::msgf(i18n::MsgId::TcInterpPlaceholderNotExpression, content);
            // Z5と同じ運用: 通常は警告、--strictではエラーへ昇格する
            if (enable_naming_check_) {
                error(current_span_, msg);
            } else {
                warning(current_span_, msg);
            }
        }
    }
}

}  // namespace cm
