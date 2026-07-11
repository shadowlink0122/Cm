// ============================================================
// MIR lowering - 文字列補間（f"..." / プレースホルダ抽出）
// ============================================================

#include "../../common/debug.hpp"
#include "../../frontend/lexer/lexer.hpp"
#include "../../frontend/parser/parser.hpp"
#include "../../hir/lowering/fwd.hpp"
#include "expr.hpp"
#include "interp_internal.hpp"

#include <functional>

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
            // フォーマット指定子ではない
            int fspec_depth = 0;
            while (end < format_str.length() && format_str[end] != '}') {
                char fc = format_str[end];
                if (fc == '[' || fc == '(') {
                    fspec_depth++;
                }
                if (fc == ']' || fc == ')') {
                    fspec_depth--;
                }
                // フォーマット指定子のコロンをチェック（:: ではなく、括弧の外）
                if (fc == ':' && fspec_depth == 0 &&
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

                // フォーマット指定子を探す（:: と括弧内はスキップ）
                size_t colon_pos = std::string::npos;
                int cdepth = 0;
                for (size_t i = pos + 1; i < format_str.length(); ++i) {
                    char cc = format_str[i];
                    if (cc == '}') {
                        break;
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
                size_t close_pos = format_str.find('}', pos + 1);

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
// 内容を返り値とするダミー関数を本物のフロントエンド
// （Lexer→Parser→HirLowering）でHIRに変換し、そのreturn式を
// 現在の関数コンテキストで通常の式loweringに掛ける
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
        // 補間式内で Color::Blue 等のenumバリアントを解決できるよう、
        // 元プログラムのenum定義を引き継ぐ
        if (ctx.enum_defs) {
            hir_lowering.seed_enum_values(*ctx.enum_defs);
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
                    return lower_expression(*(*ret)->value, ctx);
                }
            }
        }
    } catch (...) {
        // パース不能な内容は従来のパターン処理へフォールバック
    }
    return std::nullopt;
}

}  // namespace cm::mir
