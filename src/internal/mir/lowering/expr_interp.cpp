// ============================================================
// MIR lowering - 文字列補間（f"..." / プレースホルダ抽出）
// ============================================================

#include "expr.hpp"
#include "internal/base/debug.hpp"

#include <cctype>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cm::mir {

std::pair<std::vector<std::string>, std::string> ExprLowering::extract_named_placeholders(
    const std::string& format_str, [[maybe_unused]] LoweringContext& ctx) {
    std::vector<std::string> var_names;
    std::string converted_format;

    size_t pos = 0;
    while (pos < format_str.length()) {
        if (pos + 1 < format_str.length() && format_str[pos] == '$' && format_str[pos + 1] == '{') {
            // "${{"は「リテラル$ + エスケープ波括弧」（\${...}の脱糖形。R5）。$を出力へ残す
            if (pos + 2 < format_str.length() && format_str[pos + 2] == '{') {
                converted_format += '$';
                pos++;
                continue;
            }
            // ${...} は補間として扱う（閉じ波括弧がある場合のみ）
            size_t close_pos = format_str.find('}', pos + 2);
            if (close_pos != std::string::npos) {
                pos++;
                continue;
            }
        }
        if (pos + 1 < format_str.length() && format_str[pos] == '{' && format_str[pos + 1] == '{') {
            // エスケープされた {{ を処理
            converted_format += "{{";
            pos += 2;
            continue;
        }
        if (pos + 1 < format_str.length() && format_str[pos] == '}' && format_str[pos + 1] == '}') {
            // エスケープされた }} を処理
            converted_format += "}}";
            pos += 2;
            continue;
        }

        if (format_str[pos] == '{') {
            size_t end = pos + 1;
            // :: は変数名の一部として扱う（enum値のため）。
            // [] / () 内の ':' はビットスライス（x[3:0]）等の式の一部であり
            // フォーマット指定子ではない。
            // L2: ネストした波括弧（構造体リテラル Key{id: 1} 等）と文字列リテラル内の
            // } { : は式の一部として扱い、深度0・引用符外の } のみを終端とする
            int fspec_depth = 0;
            int brace_depth = 0;
            int ternary_pending = 0;
            bool in_quotes = false;
            while (end < format_str.length()) {
                char fc = format_str[end];
                if (in_quotes) {
                    if (fc == '\\' && end + 1 < format_str.length()) {
                        end += 2;
                        continue;
                    }
                    if (fc == '"') {
                        in_quotes = false;
                    }
                    end++;
                    continue;
                }
                if (fc == '"') {
                    in_quotes = true;
                    end++;
                    continue;
                }
                if (fc == '{') {
                    brace_depth++;
                    end++;
                    continue;
                }
                if (fc == '}') {
                    if (brace_depth == 0) {
                        break;  // プレースホルダの終端
                    }
                    brace_depth--;
                    end++;
                    continue;
                }
                if (fc == '[' || fc == '(') {
                    fspec_depth++;
                    end++;
                    continue;
                }
                if (fc == ']' || fc == ')') {
                    fspec_depth--;
                    end++;
                    continue;
                }
                // 三項演算子の '?'（括弧・ネスト波括弧の外）。対応する ':' はフォーマット指定子ではない（局所処理調査D2）
                if (fc == '?' && fspec_depth == 0 && brace_depth == 0) {
                    ternary_pending++;
                    end++;
                    continue;
                }
                if (fc == ':' && end + 1 < format_str.length() && format_str[end + 1] == ':') {
                    end += 2;  // :: はパス区切りとしてスキップ
                    continue;
                }
                if (fc == ':' && fspec_depth == 0 && brace_depth == 0) {
                    if (ternary_pending > 0) {
                        ternary_pending--;  // 三項のコロン。フォーマット指定子ではない
                        end++;
                        continue;
                    }
                    break;  // フォーマット指定子の開始
                }
                end++;
            }

            if (end < format_str.length() || end == format_str.length()) {
                // フォーマット指定子のコロン位置は上の走査結果（end）から導出する。
                // endはフォーマット指定子コロン（あれば）か終端 } を指すため、二重走査（regression温床）を排して両者の一致を保証する
                size_t colon_pos =
                    (end < format_str.length() && format_str[end] == ':') ? end : std::string::npos;
                // プレースホルダの終端は深度考慮の走査結果を使う（find('}')はネストで壊れる）。
                // フォーマット指定子がある場合、指定子部分に波括弧は現れないためコロン以降のfindで良い
                size_t close_pos;
                if (colon_pos != std::string::npos) {
                    close_pos = format_str.find('}', colon_pos);
                } else {
                    close_pos = (end < format_str.length() && format_str[end] == '}')
                                    ? end
                                    : std::string::npos;
                }

                if (close_pos != std::string::npos) {
                    if (colon_pos != std::string::npos && colon_pos < close_pos) {
                        // {name:format} の形式
                        std::string var_name = format_str.substr(pos + 1, colon_pos - pos - 1);
                        std::string format_spec =
                            format_str.substr(colon_pos, close_pos - colon_pos + 1);

                        // &variable パターンをチェック
                        if (!var_name.empty() && var_name[0] == '&') {
                            // &variable形式 - アドレス取得 (後でサポート)
                            std::string actual_var = var_name.substr(1);
                            if (!actual_var.empty() &&
                                (std::isalpha(actual_var[0]) || actual_var[0] == '_')) {
                                var_names.push_back("&" + actual_var);  // &付きで格納
                                // フォーマット指定子をそのまま維持
                                converted_format += "{" + format_spec;
                            } else {
                                // 無効な&フォーマット - そのまま処理
                                converted_format += format_str.substr(pos, close_pos - pos + 1);
                            }
                        } else if (!var_name.empty() && var_name[0] == '*') {
                            // *variable形式 - ポインタのデリファレンス（**pp等も許可）
                            std::string ptr_var = var_name.substr(1);
                            if (!ptr_var.empty() &&
                                (std::isalpha(ptr_var[0]) || ptr_var[0] == '_' ||
                                 ptr_var[0] == '(' || ptr_var[0] == '*')) {
                                var_names.push_back("*" + ptr_var);  // *付きで格納
                                // フォーマット指定子をそのまま維持
                                converted_format += "{" + format_spec;
                            } else {
                                // 無効な*フォーマット - そのまま処理
                                converted_format += format_str.substr(pos, close_pos - pos + 1);
                            }
                        } else if (!var_name.empty()) {
                            // 先頭文字ホワイトリストを撤廃し任意の式を受理する（局所処理調査D1/D3）。
                            // checkerが本物の式パーサでプレースホルダを検証・脱糖済みで、無効な内容は警告済み＋リテラルへフォールバックするため、
                            // 数値始まり{2 + 3}・配列リテラル始まり{[1,2,3].len()}・文字列リテラル始まり{"s".len()}も式として評価される
                            var_names.push_back(var_name);
                            converted_format += "{" + format_spec;  // {:x} のような形式に変換
                        } else {
                            // 空プレースホルダ {} はそのまま出力（変数名が無い）
                            converted_format += format_str.substr(pos, close_pos - pos + 1);
                        }
                        pos = close_pos + 1;
                    } else {
                        // {name} の形式
                        std::string var_name = format_str.substr(pos + 1, close_pos - pos - 1);

                        // &variable パターンをチェック
                        if (!var_name.empty() && var_name[0] == '&') {
                            // &variable形式 - アドレス取得 (後でサポート)
                            std::string actual_var = var_name.substr(1);
                            if (!actual_var.empty() &&
                                (std::isalpha(actual_var[0]) || actual_var[0] == '_')) {
                                var_names.push_back("&" + actual_var);  // &付きで格納
                                // アドレス表示用: プレースホルダーのみ（プレフィックスなし）
                                converted_format += "{}";
                            } else {
                                // 無効な&フォーマット - そのまま処理
                                converted_format += format_str.substr(pos, close_pos - pos + 1);
                            }
                        } else if (!var_name.empty() && var_name[0] == '*') {
                            // *variable形式 - ポインタのデリファレンス
                            std::string ptr_var = var_name.substr(1);
                            // (*ptr).x 形式と **pp（多重デリファレンス）もサポート
                            if (!ptr_var.empty() &&
                                (std::isalpha(ptr_var[0]) || ptr_var[0] == '_' ||
                                 ptr_var[0] == '(' || ptr_var[0] == '*')) {
                                var_names.push_back("*" + ptr_var);  // *付きで格納
                                converted_format += "{}";
                            } else {
                                // 無効な*フォーマット - そのまま処理
                                converted_format += format_str.substr(pos, close_pos - pos + 1);
                            }
                        } else if (!var_name.empty()) {
                            // 先頭文字ホワイトリストを撤廃し任意の式を受理する（局所処理調査D1/D3）。
                            // checkerが本物の式パーサでプレースホルダを検証・脱糖済みで、無効な内容は警告済み＋リテラルへフォールバックする
                            debug_msg("MIR", "Extracted placeholder: " + var_name);
                            var_names.push_back(var_name);
                            converted_format += "{}";  // 位置プレースホルダに変換
                        } else {
                            // 位置プレースホルダは無視（変数名ではないので処理しない）
                            // 空のままにしてエラーにする
                            return {var_names, format_str};  // エラー：変換せずに元の文字列を返す
                        }
                        pos = close_pos + 1;
                    }
                } else {
                    converted_format += format_str[pos];
                    pos++;
                }
            } else {
                converted_format += format_str[pos];
                pos++;
            }
        } else {
            converted_format += format_str[pos];
            pos++;
        }
    }

    return {var_names, converted_format};
}

// 補間プレースホルダの内容を値ローカルへ解決する（脱糖済み部分式が無い場合の最終手段）。
// 識別子は変数として直接参照し、解決不能な内容は「{内容}」のリテラル文字列を返す
// （従来はエラー型ダミーで未初期化値がそのまま出力されていた。checkerが脱糖時に警告済み）
LocalId ExprLowering::resolve_interp_placeholder(const std::string& content, LoweringContext& ctx) {
    if (auto var_id = ctx.resolve_variable(content)) {
        return *var_id;
    }
    LocalId tmp = ctx.new_temp(hir::make_string());
    MirConstant c;
    c.type = hir::make_string();
    c.value = "{" + content + "}";
    ctx.push_statement(
        MirStatement::assign(MirPlace{tmp}, MirRvalue::use(MirOperand::constant(std::move(c)))));
    return tmp;
}

// 脱糖済みの補間部分式から各プレースホルダを値ローカルへ解決する（type-resolution-simplification 領域1第4段b）。
// 補間式は型検査時に一度だけ実ASTへ脱糖済みで、ここでは型検査済みのHIR式を通常のloweringへ渡すだけになる
std::vector<LocalId> ExprLowering::lower_interp_arg_values(
    const hir::HirLiteral& lit, const std::vector<std::string>& var_names, LoweringContext& ctx) {
    // 同一内容の重複（{x} {x}）に対応するため、内容ごとの出現キューで先頭から消費する
    std::unordered_map<std::string, std::deque<hir::HirExpr*>> parts;
    for (const auto& [content, pe] : lit.interp_parts) {
        if (pe) {
            parts[content].push_back(pe.get());
        }
    }
    std::vector<LocalId> out;
    out.reserve(var_names.size());
    for (const auto& name : var_names) {
        auto it = parts.find(name);
        if (it != parts.end() && !it->second.empty()) {
            hir::HirExpr* pe = it->second.front();
            it->second.pop_front();
            out.push_back(lower_expression(*pe, ctx));
            continue;
        }
        // 部分式が無い内容（スキャナ差分）は識別子直接参照で解決する（全スイート掃引で到達0件の防衛経路）
        debug_msg("MIR", "interp fallback to identifier resolution: " + name);
        out.push_back(resolve_interp_placeholder(name, ctx));
    }
    return out;
}

}  // namespace cm::mir
