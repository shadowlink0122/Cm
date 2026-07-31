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
namespace {

// 特殊化名（Box__Box__string等）から型引数を復元する（W5）。
// 型引数が1個の構造体は残り全体を1引数として扱い、ネスト特殊化名も再帰的に解決する
hir::TypePtr decode_specialized_type_name(const std::string& name, const LoweringContext& ctx);

hir::TypePtr make_named_or_primitive(const std::string& name, const LoweringContext& ctx) {
    if (name == "int")
        return hir::make_int();
    if (name == "uint")
        return hir::make_uint();
    if (name == "long")
        return hir::make_long();
    if (name == "ulong")
        return hir::make_ulong();
    if (name == "short")
        return hir::make_short();
    if (name == "ushort")
        return hir::make_ushort();
    if (name == "tiny")
        return hir::make_tiny();
    if (name == "utiny")
        return hir::make_utiny();
    if (name == "float")
        return hir::make_float();
    if (name == "double")
        return hir::make_double();
    if (name == "bool")
        return hir::make_bool();
    if (name == "char")
        return hir::make_char();
    if (name == "string")
        return hir::make_string();
    if (name.find("__") != std::string::npos) {
        return decode_specialized_type_name(name, ctx);
    }
    auto t = std::make_shared<hir::Type>(hir::TypeKind::Struct);
    t->name = name;
    return t;
}

hir::TypePtr decode_specialized_type_name(const std::string& name, const LoweringContext& ctx) {
    auto sep = name.find("__");
    if (sep == std::string::npos || !ctx.struct_defs) {
        return nullptr;
    }
    std::string base = name.substr(0, sep);
    auto it = ctx.struct_defs->find(base);
    if (it == ctx.struct_defs->end() || !it->second) {
        return nullptr;
    }
    const size_t nparams = it->second->generic_params.size();
    std::string rest = name.substr(sep + 2);
    auto t = std::make_shared<hir::Type>(hir::TypeKind::Struct);
    t->name = base;
    if (nparams == 1) {
        t->type_args.push_back(make_named_or_primitive(rest, ctx));
    } else {
        size_t start = 0;
        for (size_t i = 0; i < nparams; ++i) {
            size_t next = rest.find("__", start);
            std::string piece = (i + 1 == nparams || next == std::string::npos)
                                    ? rest.substr(start)
                                    : rest.substr(start, next - start);
            t->type_args.push_back(make_named_or_primitive(piece, ctx));
            start = (next == std::string::npos) ? rest.size() : next + 2;
        }
    }
    return t;
}

// ジェネリック構造体のフィールド型を型引数で具象化する（Box<Box<string>>のv等。W5）
hir::TypePtr substitute_generic_field(const hir::TypePtr& field_type, const hir::HirStruct* sd,
                                      const hir::TypePtr& object_type, const LoweringContext& ctx) {
    if (!field_type || !sd || !object_type || object_type->type_args.empty()) {
        return field_type;
    }
    for (size_t gi = 0; gi < sd->generic_params.size() && gi < object_type->type_args.size();
         ++gi) {
        if (field_type->name == sd->generic_params[gi].name) {
            return object_type->type_args[gi];
        }
    }
    // フォールバック: generic_paramsが未設定のHirStructでも、フィールド型が未知の名前
    // （構造体表になくプリミティブでもない=型パラメータ名）で型引数が1個なら置換する
    if (object_type->type_args.size() == 1 &&
        (field_type->kind == hir::TypeKind::Struct || field_type->kind == hir::TypeKind::Generic) &&
        !field_type->name.empty() && ctx.struct_defs &&
        ctx.struct_defs->count(field_type->name) == 0) {
        return object_type->type_args[0];
    }
    return field_type;
}

// 補間ミニパイプラインの式ツリーへ型を再帰的に補完する（W5）。
// ミニパイプラインは型チェッカーを通らないため、メンバ・添字・メソッド呼び出しの
// チェーンで中間ノードの型が未設定になり、フィールドずれ・__error__*シンボル発行になっていた
void annotate_interp_expr_types(hir::HirExpr& e, LoweringContext& ctx,
                                const std::function<hir::TypePtr(const std::string&)>& call_ret) {
    std::visit(
        [&](auto& node) {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirMember>>) {
                if (node && node->object) {
                    annotate_interp_expr_types(*node->object, ctx, call_ret);
                    auto obj_t = node->object->type;
                    // ポインタは自動デリファレンス（アロー相当）
                    while (obj_t && obj_t->kind == hir::TypeKind::Pointer) {
                        obj_t = obj_t->element_type;
                    }
                    // 特殊化名（Box__Box__string等、type_args空）は名前から型引数を復元する
                    if (obj_t && obj_t->kind == hir::TypeKind::Struct && obj_t->type_args.empty() &&
                        obj_t->name.find("__") != std::string::npos) {
                        if (auto decoded = decode_specialized_type_name(obj_t->name, ctx)) {
                            obj_t = decoded;
                        }
                    }
                    // 既存の型が未解決の型パラメータ名（T等: 構造体表に無い名前）の場合も補完対象
                    const bool type_is_unresolved_param =
                        e.type &&
                        (e.type->kind == hir::TypeKind::Struct ||
                         e.type->kind == hir::TypeKind::Generic) &&
                        !e.type->name.empty() && ctx.struct_defs &&
                        ctx.struct_defs->count(e.type->name) == 0 &&
                        e.type->name.find("__") == std::string::npos;
                    if ((!e.type || e.type->is_error() || type_is_unresolved_param) && obj_t &&
                        obj_t->kind == hir::TypeKind::Struct && ctx.struct_defs) {
                        // 特殊化名（Box__int）は基底名でも引く
                        auto it = ctx.struct_defs->find(obj_t->name);
                        if (it == ctx.struct_defs->end()) {
                            auto base = obj_t->name.substr(0, obj_t->name.find("__"));
                            it = ctx.struct_defs->find(base);
                        }
                        if (it != ctx.struct_defs->end() && it->second) {
                            for (const auto& f : it->second->fields) {
                                if (f.name == node->member) {
                                    e.type =
                                        substitute_generic_field(f.type, it->second, obj_t, ctx);
                                    break;
                                }
                            }
                        }
                    }
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirIndex>>) {
                if (node && node->object) {
                    annotate_interp_expr_types(*node->object, ctx, call_ret);
                    for (auto& ie : node->indices) {
                        if (ie) {
                            annotate_interp_expr_types(*ie, ctx, call_ret);
                        }
                    }
                    if (node->index) {
                        annotate_interp_expr_types(*node->index, ctx, call_ret);
                    }
                    if ((!e.type || e.type->is_error()) && node->object->type &&
                        node->object->type->element_type) {
                        e.type = node->object->type->element_type;
                    }
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirCall>>) {
                if (node) {
                    for (auto& a : node->args) {
                        if (a) {
                            annotate_interp_expr_types(*a, ctx, call_ret);
                        }
                    }
                    // レシーバ型が不明のままマングリングされた呼び出し名（__error__len等）を、
                    // 補完済みのレシーバ型から再解決する（W5(d)。__error__*シンボル発行の禁止）
                    if (node->func_name.rfind("__error__", 0) == 0 && !node->args.empty() &&
                        node->args[0] && node->args[0]->type) {
                        std::string method = node->func_name.substr(9);
                        hir::TypePtr recv = node->args[0]->type;
                        while (recv && recv->kind == hir::TypeKind::Pointer) {
                            recv = recv->element_type;
                        }
                        if (recv && recv->kind == hir::TypeKind::String) {
                            if (method == "len" || method == "size" || method == "length") {
                                node->func_name = "__builtin_string_codepoint_len";
                                e.type = hir::make_uint();
                            } else if (method == "byte_len") {
                                node->func_name = "__builtin_string_len";
                                e.type = hir::make_uint();
                            }
                        } else if (recv && recv->kind == hir::TypeKind::Struct) {
                            node->func_name = interp_specialized_struct_name(recv) + "__" + method;
                        }
                    }
                    if ((!e.type || e.type->is_error()) && call_ret) {
                        if (auto rt = call_ret(node->func_name)) {
                            e.type = rt;
                        }
                    }
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirUnary>>) {
                if (node && node->operand) {
                    annotate_interp_expr_types(*node->operand, ctx, call_ret);
                    if ((!e.type || e.type->is_error()) && node->operand->type) {
                        if (node->op == hir::HirUnaryOp::Deref &&
                            node->operand->type->element_type) {
                            e.type = node->operand->type->element_type;
                        }
                    }
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirBinary>>) {
                if (node) {
                    if (node->lhs) {
                        annotate_interp_expr_types(*node->lhs, ctx, call_ret);
                    }
                    if (node->rhs) {
                        annotate_interp_expr_types(*node->rhs, ctx, call_ret);
                    }
                }
            } else {
                (void)node;
            }
        },
        e.kind);
}

}  // namespace

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

                    // 呼び出し名→戻り値型の解決（ネストしたチェーン補完用。W5）
                    auto resolve_call_return = [&ctx](const std::string& callee) -> hir::TypePtr {
                        if (ctx.hir_func_defs) {
                            auto fit = ctx.hir_func_defs->find(callee);
                            if (fit != ctx.hir_func_defs->end() && fit->second) {
                                return fit->second->return_type;
                            }
                            for (const auto& [fn_name, fn] : *ctx.hir_func_defs) {
                                if (!fn || fn_name.size() <= callee.size() + 1 ||
                                    fn_name.compare(fn_name.size() - callee.size(), callee.size(),
                                                    callee) != 0) {
                                    continue;
                                }
                                size_t sep = fn_name.size() - callee.size();
                                if (sep >= 2 && (fn_name.compare(sep - 2, 2, "__") == 0 ||
                                                 fn_name.compare(sep - 2, 2, "::") == 0)) {
                                    return fn->return_type;
                                }
                            }
                        }
                        if (ctx.interface_method_returns) {
                            auto iit = ctx.interface_method_returns->find(callee);
                            if (iit != ctx.interface_method_returns->end()) {
                                return iit->second;
                            }
                        }
                        // 自動実装メソッド（Debug/Display/CSS）はマングル名（Point__debug等）のMIR直生成でhir_func_defsに無いため、Type__プレフィックスを剥がしたメソッド名部分で判定する
                        auto base = cm::text::strip_namespace(callee);
                        auto tail_sep = base.rfind("__");
                        std::string mtail =
                            (tail_sep != std::string::npos) ? base.substr(tail_sep + 2) : base;
                        if (mtail == "debug" || mtail == "display" || mtail == "to_string" ||
                            mtail == "toString" || mtail == "css" || mtail == "to_css") {
                            return hir::make_string();
                        }
                        if (mtail == "is_css" || mtail == "isCss") {
                            return hir::make_bool();
                        }
                        if (mtail == "len" || mtail == "byte_len") {
                            return hir::make_long();
                        }
                        return nullptr;
                    };
                    // チェーン全体（ネストしたメンバ・添字・呼び出し）の型を底から補完する（W5）
                    annotate_interp_expr_types(ret_expr, ctx, resolve_call_return);
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
                                    // 自動実装メソッドはマングル名でhir_func_defsに無いため、Type__プレフィックスを剥がして判定する
                                    auto base = cm::text::strip_namespace(callee);
                                    auto tail_sep = base.rfind("__");
                                    std::string mtail = (tail_sep != std::string::npos)
                                                            ? base.substr(tail_sep + 2)
                                                            : base;
                                    if (mtail == "debug" || mtail == "display" ||
                                        mtail == "to_string" || mtail == "toString" ||
                                        mtail == "css" || mtail == "to_css") {
                                        ret_expr.type = hir::make_string();
                                    } else if (mtail == "is_css" || mtail == "isCss") {
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
