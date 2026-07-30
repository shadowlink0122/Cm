// ============================================================
// MIR lowering - 文字列補間（f"..." / プレースホルダ抽出）
// ============================================================

#include "expr.hpp"
#include "internal/base/debug.hpp"
#include "internal/base/text_utils.hpp"
#include "internal/hir/lowering/fwd.hpp"
#include "internal/syntax/lexer/lexer.hpp"
#include "internal/syntax/parser/parser.hpp"
#include "interp_internal.hpp"

#include <cctype>
#include <functional>
#include <memory>
#include <optional>
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
                }
                if (fc == ']' || fc == ')') {
                    fspec_depth--;
                }
                // フォーマット指定子のコロンをチェック（:: ではなく、括弧・ネスト波括弧の外）
                if (fc == ':' && fspec_depth == 0 && brace_depth == 0 &&
                    (end + 1 >= format_str.length() || format_str[end + 1] != ':')) {
                    break;  // フォーマット指定子の開始
                }
                if (fc == ':' && end + 1 < format_str.length() && format_str[end + 1] == ':') {
                    end += 2;  // :: をスキップ
                } else {
                    end++;
                }
            }

            if (end < format_str.length() || end == format_str.length()) {
                std::string content = format_str.substr(pos + 1, end - pos - 1);

                // フォーマット指定子を探す（:: と括弧・ネスト波括弧・引用符内はスキップ。L2）
                size_t colon_pos = std::string::npos;
                int cdepth = 0;
                int cbrace = 0;
                bool cquotes = false;
                for (size_t i = pos + 1; i < format_str.length(); ++i) {
                    char cc = format_str[i];
                    if (cquotes) {
                        if (cc == '\\' && i + 1 < format_str.length()) {
                            i++;
                            continue;
                        }
                        if (cc == '"') {
                            cquotes = false;
                        }
                        continue;
                    }
                    if (cc == '"') {
                        cquotes = true;
                        continue;
                    }
                    if (cc == '{') {
                        cbrace++;
                        continue;
                    }
                    if (cc == '}') {
                        if (cbrace == 0) {
                            break;
                        }
                        cbrace--;
                        continue;
                    }
                    if (cc == '[' || cc == '(') {
                        cdepth++;
                    }
                    if (cc == ']' || cc == ')') {
                        cdepth--;
                    }
                    if (cc == ':' && cdepth == 0 &&
                        (i + 1 >= format_str.length() || format_str[i + 1] != ':')) {
                        colon_pos = i;
                        break;
                    }
                    if (cc == ':' && i + 1 < format_str.length() && format_str[i + 1] == ':') {
                        i++;  // :: をスキップ
                    }
                }
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
                            if (!actual_var.empty() && std::isalpha(actual_var[0])) {
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
                            if (!ptr_var.empty() && (std::isalpha(ptr_var[0]) ||
                                                     ptr_var[0] == '(' || ptr_var[0] == '*')) {
                                var_names.push_back("*" + ptr_var);  // *付きで格納
                                // フォーマット指定子をそのまま維持
                                converted_format += "{" + format_spec;
                            } else {
                                // 無効な*フォーマット - そのまま処理
                                converted_format += format_str.substr(pos, close_pos - pos + 1);
                            }
                        } else if (!var_name.empty() &&
                                   (std::isalpha(var_name[0]) || var_name[0] == '!' ||
                                    var_name[0] == '*' ||  // デリファレンスを許可
                                    var_name.substr(0, 5) == "self." ||
                                    var_name.find("::") != std::string::npos)) {
                            // 変数名、メンバーアクセス、メソッド呼び出し、enum値、または否定演算子として有効
                            // self.x, p.field, r.area(), Color::Red, !true のような形式も許可
                            var_names.push_back(var_name);
                            converted_format += "{" + format_spec;  // {:x} のような形式に変換
                        } else {
                            // 位置プレースホルダは無視（変数名ではないので処理しない）
                            // 空のままにしてエラーにする
                            return {var_names, format_str};  // エラー：変換せずに元の文字列を返す
                        }
                        pos = close_pos + 1;
                    } else {
                        // {name} の形式
                        std::string var_name = format_str.substr(pos + 1, close_pos - pos - 1);

                        // &variable パターンをチェック
                        if (!var_name.empty() && var_name[0] == '&') {
                            // &variable形式 - アドレス取得 (後でサポート)
                            std::string actual_var = var_name.substr(1);
                            if (!actual_var.empty() && std::isalpha(actual_var[0])) {
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
                            if (!ptr_var.empty() && (std::isalpha(ptr_var[0]) ||
                                                     ptr_var[0] == '(' || ptr_var[0] == '*')) {
                                var_names.push_back("*" + ptr_var);  // *付きで格納
                                converted_format += "{}";
                            } else {
                                // 無効な*フォーマット - そのまま処理
                                converted_format += format_str.substr(pos, close_pos - pos + 1);
                            }
                        } else if (!var_name.empty() &&
                                   (std::isalpha(var_name[0]) || var_name[0] == '!' ||
                                    var_name[0] == '*' ||  // デリファレンスを許可
                                    var_name[0] == '(' ||  // (*ptr).x 形式を許可
                                    var_name.substr(0, 5) == "self." ||
                                    var_name.find("::") != std::string::npos ||
                                    var_name.find("->") !=
                                        std::string::npos)) {  // ptr->x 形式を許可
                            // 変数名、メンバーアクセス、メソッド呼び出し、enum値、または否定演算子として有効
                            // self.x, p.field, r.area(), Color::Red, !true のような形式も許可
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

// 補間プレースホルダの内容を式としてパースしMIRへ降下する。
// 内容を返り値とするダミー関数を本物のフロントエンド（Lexer→Parser→HirLowering）でHIRに変換し、そのreturn式を現在の関数コンテキストで通常の式loweringに掛ける
std::optional<LocalId> ExprLowering::lower_interp_expression(const std::string& content,
                                                             LoweringContext& ctx) {
    try {
        std::string src = "int __interp_expr__() { return (" + content + "); }";
        Lexer lex(src);
        auto tokens = lex.tokenize();
        Parser parser(std::move(tokens));
        auto program = parser.parse();
        if (parser.has_errors()) {
            return std::nullopt;
        }

        hir::HirLowering hir_lowering;
        // 補間式内で Color::Blue 等のenumバリアントを解決できるよう、元プログラムのenum定義を引き継ぐ
        if (ctx.enum_defs) {
            hir_lowering.seed_enum_values(*ctx.enum_defs);
        }
        // 構造体フィールドも引き継ぐ（{c.values[0]} 等のメンバ型解決のため）
        if (ctx.struct_defs) {
            std::unordered_map<std::string, std::vector<std::pair<std::string, hir::TypePtr>>>
                struct_fields;
            for (const auto& [struct_name, st] : *ctx.struct_defs) {
                if (!st) {
                    continue;
                }
                auto& fields = struct_fields[struct_name];
                for (const auto& field : st->fields) {
                    fields.push_back({field.name, field.type});
                }
            }
            hir_lowering.seed_struct_fields(std::move(struct_fields));
        }
        // 変数の型も引き継ぐ（ビットスライス等の型依存脱糖のため）
        {
            std::unordered_map<std::string, hir::TypePtr> var_types;
            if (ctx.func) {
                for (const auto& local : ctx.func->locals) {
                    if (!local.name.empty() && local.type) {
                        var_types.emplace(local.name, local.type);
                    }
                }
            }
            hir_lowering.seed_variable_types(std::move(var_types));
        }
        auto hir_program = hir_lowering.lower(program);

        for (auto& decl : hir_program.declarations) {
            if (!decl) {
                continue;
            }
            auto* func = std::get_if<std::unique_ptr<hir::HirFunction>>(&decl->kind);
            if (!func || !*func || (*func)->name != "__interp_expr__") {
                continue;
            }
            for (auto& stmt : (*func)->body) {
                if (!stmt) {
                    continue;
                }
                auto* ret = std::get_if<std::unique_ptr<hir::HirReturn>>(&stmt->kind);
                if (ret && *ret && (*ret)->value) {
                    // ミニパイプラインは型チェッカーを通らないため、メソッド呼び出しの
                    // 戻り型が未設定になる。既知の関数定義（末尾一致でも探索）から補完する
                    auto& ret_expr = *(*ret)->value;
                    if (!ret_expr.type || ret_expr.type->is_error()) {
                        if (auto* call_expr =
                                std::get_if<std::unique_ptr<hir::HirCall>>(&ret_expr.kind)) {
                            if (*call_expr) {
                                const std::string& callee = (*call_expr)->func_name;
                                if (ctx.hir_func_defs) {
                                    auto fit = ctx.hir_func_defs->find(callee);
                                    if (fit == ctx.hir_func_defs->end()) {
                                        // メソッドはマングル名（Type__method / ns::method）で登録されているため境界つき末尾一致で探索する（budget が get に誤マッチする類を防ぐ）
                                        for (const auto& [fn_name, fn] : *ctx.hir_func_defs) {
                                            if (!fn || fn_name.size() <= callee.size() + 1 ||
                                                fn_name.compare(fn_name.size() - callee.size(),
                                                                callee.size(), callee) != 0) {
                                                continue;
                                            }
                                            size_t sep = fn_name.size() - callee.size();
                                            bool at_boundary =
                                                (sep >= 2 &&
                                                 (fn_name.compare(sep - 2, 2, "__") == 0 ||
                                                  fn_name.compare(sep - 2, 2, "::") == 0));
                                            if (at_boundary) {
                                                ret_expr.type = fn->return_type;
                                                break;
                                            }
                                        }
                                    } else if (fit->second) {
                                        ret_expr.type = fit->second->return_type;
                                    }
                                }
                                // インターフェイスメソッド（Animal__name等）はimplが具象名（Dog__name）で登録されるためhir_func_defsでは解決できない（B7）。
                                // HIRのインターフェイス宣言からシードした戻り値型マップで補完する
                                if ((!ret_expr.type || ret_expr.type->is_error()) &&
                                    ctx.interface_method_returns) {
                                    auto iit = ctx.interface_method_returns->find(callee);
                                    if (iit != ctx.interface_method_returns->end()) {
                                        ret_expr.type = iit->second;
                                    } else {
                                        // モノモーフ化前のジェネリック関数本体ではcalleeが`T__method`になり型パラメータ名では直接引けない。
                                        // メソッド名部分の境界つき末尾一致で任意のインターフェイス宣言から戻り値型を引く（呼び出し名の具象化はモノモーフ化のMIR書き換えが行う）
                                        auto sep = callee.find("__");
                                        if (sep != std::string::npos && sep > 0 &&
                                            sep + 2 < callee.size()) {
                                            std::string method = callee.substr(sep + 2);
                                            for (const auto& [sig_name, sig_ret] :
                                                 *ctx.interface_method_returns) {
                                                if (sig_name.size() > method.size() + 2 &&
                                                    sig_name.compare(
                                                        sig_name.size() - method.size(),
                                                        method.size(), method) == 0 &&
                                                    sig_name.compare(
                                                        sig_name.size() - method.size() - 2, 2,
                                                        "__") == 0) {
                                                    ret_expr.type = sig_ret;
                                                    break;
                                                }
                                            }
                                        }
                                    }
                                }
                                // 自動実装メソッドの既知シグネチャ（Debug/Display/CSS）
                                if (!ret_expr.type || ret_expr.type->is_error()) {
                                    auto base = cm::text::strip_namespace(callee);
                                    if (base == "debug" || base == "display" ||
                                        base == "to_string" || base == "toString" ||
                                        base == "css" || base == "to_css") {
                                        ret_expr.type = hir::make_string();
                                    } else if (base == "is_css" || base == "isCss") {
                                        ret_expr.type = hir::make_bool();
                                    }
                                }
                            }
                        }
                    }
                    return lower_expression(ret_expr, ctx);
                }
            }
        }
    } catch (...) {
        // パース不能な内容は従来のパターン処理へフォールバック
    }
    return std::nullopt;
}

// 補間プレースホルダの内容を値ローカルへ解決する。
// 単純な識別子は直接参照し、それ以外は本物の式パーサ＋既存のlowering経路（lower_interp_expression）で評価する。
// 解決不能な内容は従来どおりエラー型のダミー値を返す
LocalId ExprLowering::resolve_interp_placeholder(const std::string& content, LoweringContext& ctx) {
    bool plain = !content.empty() &&
                 (std::isalpha(static_cast<unsigned char>(content[0])) || content[0] == '_');
    if (plain) {
        for (char c : content) {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
                plain = false;
                break;
            }
        }
    }
    if (plain) {
        if (auto var_id = ctx.resolve_variable(content)) {
            return *var_id;
        }
    }
    if (auto expr_local = lower_interp_expression(content, ctx)) {
        return *expr_local;
    }
    if (auto var_id = ctx.resolve_variable(content)) {
        return *var_id;
    }
    return ctx.new_temp(hir::make_error());
}

}  // namespace cm::mir
