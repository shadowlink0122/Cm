#include "derive.hpp"

#include "internal/syntax/lexer/lexer.hpp"
#include "internal/syntax/parser/parser.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cm::macro_expand {

namespace {

// enum名 → タグ付きユニオンか（値enumはfalse）。enumフィールドの扱い分岐に使う（R21）
using EnumTaggedMap = std::unordered_map<std::string, bool>;

// フィールド型が値enum（int意味論で比較・整形できる）か
bool is_value_enum_field(const ast::TypePtr& t, const EnumTaggedMap& enums) {
    if (!t || (t->kind != ast::TypeKind::Struct && t->kind != ast::TypeKind::Generic)) {
        return false;
    }
    auto it = enums.find(t->name);
    return it != enums.end() && !it->second;
}

// フィールド型がタグ付きenum（ペイロード付き。derive未対応）か
bool is_tagged_enum_field(const ast::TypePtr& t, const EnumTaggedMap& enums) {
    if (!t || (t->kind != ast::TypeKind::Struct && t->kind != ast::TypeKind::Generic)) {
        return false;
    }
    auto it = enums.find(t->name);
    return it != enums.end() && it->second;
}

// ソース展開へ移行済みのトレイト（Eq/Ord/Clone/Hash/Debug/Display/Css。Copyはマーカーのみで生成物が無いため対象外）
bool is_source_expanded_trait(const std::string& name) {
    return name == "Eq" || name == "Ord" || name == "Clone" || name == "Hash" || name == "Debug" ||
           name == "Display" || name == "Css";
}

// フィールド型がトレイトのderive対象として妥当か（checkerのvalidate（types/checking/auto_impl.cpp）と同一の規則）。
// 不正な場合は展開せずauto_implsへ残し、従来のderive検証診断（Cannot derive ...）を発火させる
bool fields_derivable(const ast::StructDecl& st, const std::string& iface,
                      const EnumTaggedMap& enums) {
    if (iface == "Clone" || iface == "Copy" || iface == "Css") {
        return true;
    }
    for (const auto& field : st.fields) {
        const auto& t = field.type;
        if (!t) {
            continue;
        }
        // タグ付きenumフィールドは未対応（展開せずchecker側のCannot derive診断へ委ねる）
        if (is_tagged_enum_field(t, enums)) {
            return false;
        }
        if (t->kind == ast::TypeKind::Union || t->kind == ast::TypeKind::LiteralUnion) {
            return false;
        }
        if (t->kind == ast::TypeKind::Array) {
            const bool fixed_1d =
                !t->is_multidim_array() && (t->array_size.has_value() || t->dimensions.size() == 1);
            const auto& elem = t->element_type;
            if (iface == "Eq") {
                if (!fixed_1d || !elem || !elem->is_primitive() ||
                    elem->kind == ast::TypeKind::Void) {
                    return false;
                }
            } else if (iface == "Hash") {
                if (!fixed_1d || !elem ||
                    !(elem->is_integer() || elem->kind == ast::TypeKind::Bool ||
                      elem->kind == ast::TypeKind::Char)) {
                    return false;
                }
            } else {
                // Ord / Debug / Display は配列フィールド非対応
                return false;
            }
        } else if (iface == "Hash") {
            if (t->kind == ast::TypeKind::String || t->kind == ast::TypeKind::CString ||
                t->is_floating() || t->kind == ast::TypeKind::Pointer) {
                return false;
            }
        }
    }
    return true;
}

// 構造体の型表記（ジェネリックは型引数付き。例: Pair<T, U>）
std::string struct_type_text(const ast::StructDecl& st) {
    std::string text = st.name;
    if (!st.generic_params.empty()) {
        text += "<";
        for (size_t i = 0; i < st.generic_params.size(); ++i) {
            if (i > 0) {
                text += ", ";
            }
            text += st.generic_params[i];
        }
        text += ">";
    }
    return text;
}

// 1フィールド分の等価比較式を追加する。固定長配列は要素単位の比較へ再帰的に展開する
// （配列全体の==は通常経路に存在しないため。多次元は添字を重ねて展開する）
void append_field_eq(std::string& expr, const ast::TypePtr& type, const std::string& access,
                     bool& first) {
    if (type && type->kind == ast::TypeKind::Array && type->array_size.has_value()) {
        for (uint32_t i = 0; i < type->array_size.value(); ++i) {
            append_field_eq(expr, type->element_type, access + "[" + std::to_string(i) + "]",
                            first);
        }
        return;
    }
    if (!first) {
        expr += " && ";
    }
    first = false;
    expr += "self." + access + " == other." + access;
}

// Eq: 全フィールドの==を&&で連結する（空構造体は常にtrue。手組みMIR実装と同一の意味論）
// 型パラメータ名の集合（ジェネリック構造体の合成でフィールド型が型パラメータかを判定する）
std::set<std::string> generic_param_set(const ast::StructDecl& st) {
    return std::set<std::string>(st.generic_params.begin(), st.generic_params.end());
}

// フィールド型が型パラメータそのもの（T等）か
bool is_param_field(const ast::TypePtr& t, const std::set<std::string>& params) {
    return t && !t->name.empty() && t->type_args.empty() && params.count(t->name) > 0;
}

// ジェネリック構造体の合成implに付けるwhere句（全型パラメータへ境界traitを課す）。
// 型パラメータのフィールドはメソッド形（self.f.hash()等）で合成され、境界経由で解決される
std::string where_clause_for(const ast::StructDecl& st, const std::string& bound_iface) {
    if (st.generic_params.empty() || bound_iface.empty()) {
        return "";
    }
    std::string w = " where ";
    for (size_t i = 0; i < st.generic_params.size(); ++i) {
        if (i > 0) {
            w += ", ";
        }
        w += st.generic_params[i] + ": " + bound_iface;
    }
    return w;
}

std::string synthesize_eq_impl(const ast::StructDecl& st) {
    const std::string type_text = struct_type_text(st);
    std::string body;
    std::string expr;
    bool first = true;
    for (const auto& field : st.fields) {
        append_field_eq(expr, field.type, field.name, first);
    }
    if (expr.empty()) {
        body = "        return true;\n";
    } else {
        body = "        return " + expr + ";\n";
    }
    std::string source;
    if (!st.generic_params.empty()) {
        source += "#[__derived]\n";
    }
    source += "impl " + type_text + " for Eq {\n";
    source += "    operator bool ==(" + type_text + " other) {\n";
    source += body;
    source += "    }\n";
    source += "}\n";
    return source;
}

// Ord: 辞書式比較（各フィールドで<ならtrue・>ならfalse・等しければ次へ。全等はfalse。手組み実装と同一）
std::string synthesize_ord_impl(const ast::StructDecl& st) {
    const std::string type_text = struct_type_text(st);
    std::string body;
    for (const auto& field : st.fields) {
        body += "        if (self." + field.name + " < other." + field.name +
                ") {\n            return true;\n        }\n";
        body += "        if (self." + field.name + " > other." + field.name +
                ") {\n            return false;\n        }\n";
    }
    body += "        return false;\n";
    std::string source;
    if (!st.generic_params.empty()) {
        source += "#[__derived]\n";
    }
    source += "impl " + type_text + " for Ord" + where_clause_for(st, "Ord") + " {\n";
    source += "    operator bool <(" + type_text + " other) {\n";
    source += body;
    source += "    }\n";
    source += "}\n";
    return source;
}

// Clone: 自己の値コピーを返す（構造体代入の集約コピー意味論に委譲。手組み実装と同一）
std::string synthesize_clone_impl(const ast::StructDecl& st) {
    const std::string type_text = struct_type_text(st);
    std::string source;
    if (!st.generic_params.empty()) {
        source += "#[__derived]\n";
    }
    source += "impl " + type_text + " for Clone {\n";
    source += "    " + type_text + " clone() {\n";
    source += "        return self;\n";
    source += "    }\n";
    source += "}\n";
    return source;
}

// Hash: FNV-1a（基数0x811c9dc5・素数16777619。整数/bool/charは値を、ネスト構造体はhash()結果を、
// 固定長配列は要素を順に混合する。基数はi32ビットパターンを保つため負数リテラルで表記）
std::string synthesize_hash_impl(const ast::StructDecl& st, const EnumTaggedMap& enums) {
    const std::string type_text = struct_type_text(st);
    std::string body = "        int h = -2128831035;\n";
    auto mix = [&body](const std::string& value_expr) {
        body += "        h = (h ^ " + value_expr + ") * 16777619;\n";
    };
    const auto params = generic_param_set(st);
    for (const auto& field : st.fields) {
        const auto& t = field.type;
        // 型パラメータのフィールドは一様にhash()メソッド形（境界T: Hash経由で解決。
        // プリミティブ型引数はプリミティブへの一様hash()実装が受ける）
        if (is_param_field(t, params)) {
            mix("self." + field.name + ".hash()");
        } else if (t && t->kind == ast::TypeKind::Struct && !is_value_enum_field(t, enums)) {
            // 値enumフィールドはint値として混合する（.hash()呼び出しはenumのint正規化後に解決不能。R21）
            mix("self." + field.name + ".hash()");
        } else if (t && t->kind == ast::TypeKind::Array && t->array_size.has_value()) {
            for (uint32_t j = 0; j < t->array_size.value(); ++j) {
                mix("(self." + field.name + "[" + std::to_string(j) + "] as int)");
            }
        } else {
            mix("(self." + field.name + " as int)");
        }
    }
    body += "        return h;\n";
    std::string source;
    if (!st.generic_params.empty()) {
        source += "#[__derived]\n";
    }
    source += "impl " + type_text + " for Hash" + where_clause_for(st, "Hash") + " {\n";
    source += "    int hash() {\n";
    if (st.fields.empty()) {
        source += "        return -2128831035;\n";
    } else {
        source += body;
    }
    source += "    }\n";
    source += "}\n";
    return source;
}

// Debug/Display/Cssのフィールド値の連結片。
// 挿入値が波括弧を含みうるもの（文字列フィールド・ネストのdebug/toString/css結果）は
// 補間を使わず直接連結する（補間の逐次置換は挿入値内の{...}を後続プレースホルダと誤認するため）。
// スカラは単独プレースホルダのリテラル（後続置換が無く安全）で整形する
std::string field_value_piece(const ast::Field& field, const std::string& method,
                              const EnumTaggedMap& enums) {
    // 値enumフィールドはint値として整形する（debug()/toString()呼び出しはenumのint正規化後に解決不能。R21）
    if (field.type && field.type->kind == ast::TypeKind::Struct &&
        !is_value_enum_field(field.type, enums)) {
        return "self." + field.name + "." + method + "()";
    }
    if (field.type &&
        (field.type->kind == ast::TypeKind::String || field.type->kind == ast::TypeKind::CString)) {
        return "self." + field.name;
    }
    return "\"{self." + field.name + "}\"";
}

// Debug: "S { f1: v1, f2: v2 }"（空は"S {}"。ネスト構造体はdebug()。手組み実装と同一の書式）
std::string synthesize_debug_impl(const ast::StructDecl& st, const EnumTaggedMap& enums) {
    const std::string type_text = struct_type_text(st);
    std::string expr = "\"" + st.name + " {";
    for (size_t i = 0; i < st.fields.size(); ++i) {
        expr += (i == 0 ? " " : ", ");
        expr += st.fields[i].name + ": \" + " + field_value_piece(st.fields[i], "debug", enums) +
                " + \"";
    }
    expr += st.fields.empty() ? "}\"" : " }\"";
    std::string source;
    if (!st.generic_params.empty()) {
        source += "#[__derived]\n";
    }
    source += "impl " + type_text + " for Debug" + where_clause_for(st, "Debug") + " {\n";
    source += "    string debug() {\n";
    source += "        return " + expr + ";\n";
    source += "    }\n";
    source += "}\n";
    return source;
}

// Display: "(v1, v2)"（ネスト構造体はtoString()。手組み実装と同一の書式）
std::string synthesize_display_impl(const ast::StructDecl& st, const EnumTaggedMap& enums) {
    const std::string type_text = struct_type_text(st);
    std::string expr = "\"(\"";
    for (size_t i = 0; i < st.fields.size(); ++i) {
        if (i > 0) {
            expr += " + \", \"";
        }
        expr += " + " + field_value_piece(st.fields[i], "toString", enums);
    }
    expr += " + \")\"";
    std::string source;
    if (!st.generic_params.empty()) {
        source += "#[__derived]\n";
    }
    source += "impl " + type_text + " for Display" + where_clause_for(st, "Display") + " {\n";
    source += "    string toString() {\n";
    source += "        return " + expr + ";\n";
    source += "    }\n";
    source += "}\n";
    return source;
}

// フィールド名のkebab-case（アンダースコア→ダッシュ。手組みCss実装と同一）
std::string css_key(const std::string& name) {
    std::string result;
    for (char c : name) {
        result += (c == '_') ? '-' : c;
    }
    return result;
}

// Css: kebab名の昇順で "key: value; " を連結（boolは真のとき"key; "のみ・ネスト構造体は"key { 内容 } "。
// 手組み実装と同一の書式）。to_cssはcss()のエイリアス、is_cssは常にtrue
std::string synthesize_css_impl(const ast::StructDecl& st, const EnumTaggedMap& enums) {
    const std::string type_text = struct_type_text(st);
    std::vector<size_t> order(st.fields.size());
    for (size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return css_key(st.fields[a].name) < css_key(st.fields[b].name);
    });

    std::string body = "        string s = \"\";\n";
    for (size_t idx : order) {
        const auto& field = st.fields[idx];
        const std::string key = css_key(field.name);
        if (field.type && field.type->kind == ast::TypeKind::Bool) {
            body += "        if (self." + field.name + ") {\n";
            body += "            s = s + \"" + key + "; \";\n";
            body += "        }\n";
        } else if (field.type && field.type->kind == ast::TypeKind::Struct &&
                   !is_value_enum_field(field.type, enums) &&
                   generic_param_set(st).count(field.type->name) == 0) {
            body +=
                "        s = s + \"" + key + " { \" + self." + field.name + ".css() + \" } \";\n";
        } else if (field.type && generic_param_set(st).count(field.type->name) > 0 &&
                   field.type->type_args.empty()) {
            // 型パラメータのフィールドは補間で値整形する（プリミティブ型引数を想定。Css境界は課さない）
            body += "        s = s + \"" + key + ": {self." + field.name + "}; \";\n";
        } else {
            body += "        s = s + \"" + key + ": \" + " +
                    field_value_piece(field, "css", enums) + " + \"; \";\n";
        }
    }
    body += "        return s;\n";

    std::string source;
    if (!st.generic_params.empty()) {
        source += "#[__derived]\n";
    }
    source += "impl " + type_text + " for Css {\n";
    source += "    string css() {\n";
    source += body;
    source += "    }\n";
    source += "    string to_css() {\n";
    source += "        return self.css();\n";
    source += "    }\n";
    source += "    bool is_css() {\n";
    source += "        return true;\n";
    source += "    }\n";
    // 呼び出し側の解決名差（checker登録はisCss・従来関数はis_css）を両名の提供で吸収する
    source += "    bool isCss() {\n";
    source += "        return true;\n";
    source += "    }\n";
    source += "}\n";
    return source;
}

}  // namespace

// プログラム中のenum宣言を収集する（enumフィールドの扱い分岐用。R21）
EnumTaggedMap collect_enum_tagged_map(const ast::Program& program) {
    EnumTaggedMap enums;
    for (const auto& decl : program.declarations) {
        if (!decl) {
            continue;
        }
        const auto* en = const_cast<ast::Decl&>(*decl).as<ast::EnumDecl>();
        if (en) {
            enums[en->name] = en->is_tagged_union();
        }
    }
    return enums;
}

// 総称derive合成が使うプリミティブへの一様メソッド実装を合成する。
// 型パラメータのフィールドはメソッド形（self.v.hash()等）で合成されるため、
// int等のプリミティブ型引数の特殊化ではプリミティブ自身がhash()/debug()/toString()を持つ必要がある。
// ユーザーが同じ（プリミティブ, インターフェース）のimplを書いている場合は重複を避けて出力しない
std::string synthesize_primitive_uniform_impls(const ast::Program& program,
                                               const std::set<std::string>& needed_ifaces) {
    if (needed_ifaces.empty()) {
        return "";
    }
    // 既存のユーザーimpl（プリミティブ×インターフェース）を収集
    std::set<std::pair<std::string, std::string>> existing;
    for (const auto& decl : program.declarations) {
        const auto* imp = const_cast<ast::Decl&>(*decl).as<ast::ImplDecl>();
        if (imp && imp->target_type && !imp->interface_name.empty()) {
            existing.insert({ast::type_to_string(*imp->target_type), imp->interface_name});
        }
    }
    static const char* kIntLike[] = {"tiny", "utiny", "short", "ushort", "int",  "uint",
                                     "long", "ulong", "isize", "usize",  "bool", "char"};
    static const char* kFloatLike[] = {"float", "double"};
    std::string source;
    auto emit = [&](const std::string& prim, const std::string& iface, const std::string& body) {
        if (existing.count({prim, iface}) > 0) {
            return;
        }
        source += "impl " + prim + " for " + iface + " {\n" + body + "}\n";
    };
    if (needed_ifaces.count("Hash") > 0) {
        for (const char* prim : kIntLike) {
            emit(prim, "Hash", "    int hash() {\n        return self as int;\n    }\n");
        }
    }
    if (needed_ifaces.count("Debug") > 0) {
        for (const char* prim : kIntLike) {
            emit(prim, "Debug", "    string debug() {\n        return \"{self}\";\n    }\n");
        }
        for (const char* prim : kFloatLike) {
            emit(prim, "Debug", "    string debug() {\n        return \"{self}\";\n    }\n");
        }
        emit("string", "Debug", "    string debug() {\n        return self;\n    }\n");
    }
    if (needed_ifaces.count("Display") > 0) {
        for (const char* prim : kIntLike) {
            emit(prim, "Display", "    string toString() {\n        return \"{self}\";\n    }\n");
        }
        for (const char* prim : kFloatLike) {
            emit(prim, "Display", "    string toString() {\n        return \"{self}\";\n    }\n");
        }
        emit("string", "Display", "    string toString() {\n        return self;\n    }\n");
    }
    return source;
}

std::string synthesize_derive_impls(const ast::Program& program) {
    const EnumTaggedMap enums = collect_enum_tagged_map(program);
    std::string source;
    // 総称構造体のderiveが必要とするプリミティブ一様メソッドの対象トレイトを収集
    std::set<std::string> prim_ifaces;
    for (const auto& decl : program.declarations) {
        if (!decl) {
            continue;
        }
        const auto* gst = const_cast<ast::Decl&>(*decl).as<ast::StructDecl>();
        if (!gst || gst->generic_params.empty() || gst->auto_impls.empty()) {
            continue;
        }
        for (const auto& iface : gst->auto_impls) {
            if ((iface == "Hash" || iface == "Debug" || iface == "Display") &&
                is_source_expanded_trait(iface) && fields_derivable(*gst, iface, enums)) {
                prim_ifaces.insert(iface);
            }
        }
    }
    source += synthesize_primitive_uniform_impls(program, prim_ifaces);
    for (const auto& decl : program.declarations) {
        if (!decl) {
            continue;
        }
        const auto* st = const_cast<ast::Decl&>(*decl).as<ast::StructDecl>();
        if (!st || st->auto_impls.empty()) {
            continue;
        }
        // ジェネリック構造体も単一の総称implソースへ合成する（第3段後半）。
        // 型パラメータのフィールドはメソッド形（self.v.hash()等）+ where境界で合成され、
        // プリミティブ型引数は同時に合成されるプリミティブへの一様メソッド実装が受ける
        for (const auto& iface : st->auto_impls) {
            if (!is_source_expanded_trait(iface) || !fields_derivable(*st, iface, enums)) {
                continue;
            }
            if (!source.empty()) {
                source += "\n";
            }
            if (iface == "Eq") {
                source += synthesize_eq_impl(*st);
            } else if (iface == "Ord") {
                source += synthesize_ord_impl(*st);
            } else if (iface == "Clone") {
                source += synthesize_clone_impl(*st);
            } else if (iface == "Hash") {
                source += synthesize_hash_impl(*st, enums);
            } else if (iface == "Debug") {
                source += synthesize_debug_impl(*st, enums);
            } else if (iface == "Display") {
                source += synthesize_display_impl(*st, enums);
            } else if (iface == "Css") {
                source += synthesize_css_impl(*st, enums);
            }
        }
    }
    return source;
}

int expand_derives(ast::Program& program) {
    const std::string source = synthesize_derive_impls(program);
    if (source.empty()) {
        return 0;
    }
    const EnumTaggedMap enums = collect_enum_tagged_map(program);

    // 展開済みトレイトはauto_implsから除去する。合成implが唯一の実装となり、
    // 型検査の重複impl検出・インターフェース適合・MIRの手組み生成はすべて通常のimplとして扱われる
    for (auto& decl : program.declarations) {
        if (!decl) {
            continue;
        }
        auto* st = decl->as<ast::StructDecl>();
        if (!st || st->auto_impls.empty()) {
            continue;
        }
        std::vector<std::string> remaining;
        for (const auto& iface : st->auto_impls) {
            if (!is_source_expanded_trait(iface) || !fields_derivable(*st, iface, enums)) {
                remaining.push_back(iface);
            }
        }
        st->auto_impls = std::move(remaining);
    }

    // 合成ソースを通常のパーサで検証しつつASTへ変換する（合成が不正ならここで検出される）
    Lexer lexer(source);
    auto tokens = lexer.tokenize();
    Parser parser(std::move(tokens));
    // コンパイラ合成ソースは'__'予約識別子の検査対象外
    parser.set_allow_reserved_idents(true);
    ast::Program generated = parser.parse();
    if (parser.has_errors()) {
        // 合成コードの構文エラーは展開器のバグ（ユーザー入力では発生しない）。
        // 黙殺せず0件展開として返し、以降の型検査・MIRの自動実装欠落として顕在化させる
        return 0;
    }

    int added = 0;
    for (auto& decl : generated.declarations) {
        if (!decl) {
            continue;
        }
        program.declarations.push_back(std::move(decl));
        ++added;
    }
    return added;
}

}  // namespace cm::macro_expand
