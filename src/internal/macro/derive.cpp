#include "derive.hpp"

#include "internal/syntax/lexer/lexer.hpp"
#include "internal/syntax/parser/parser.hpp"

#include <string>
#include <utility>
#include <vector>

namespace cm::macro_expand {

namespace {

// ソース展開へ移行済みのトレイト（第1段: Eq。第2段以降でOrd/Clone/Hash/Debug/Display/Cssを追加する）
bool is_source_expanded_trait(const std::string& name) {
    return name == "Eq";
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
    source += "impl " + type_text + " for Eq {\n";
    source += "    operator bool ==(" + type_text + " other) {\n";
    source += body;
    source += "    }\n";
    source += "}\n";
    return source;
}

}  // namespace

std::string synthesize_derive_impls(const ast::Program& program) {
    std::string source;
    for (const auto& decl : program.declarations) {
        if (!decl) {
            continue;
        }
        const auto* st = const_cast<ast::Decl&>(*decl).as<ast::StructDecl>();
        if (!st || st->auto_impls.empty()) {
            continue;
        }
        // ジェネリック構造体は対象外（総称演算子implのモノモーフ化が未対応のため、
        // モノモーフ化後の手組み生成経路を維持する。単一総称implへの移行はmono拡張後）
        if (!st->generic_params.empty()) {
            continue;
        }
        for (const auto& iface : st->auto_impls) {
            if (!is_source_expanded_trait(iface)) {
                continue;
            }
            if (!source.empty()) {
                source += "\n";
            }
            if (iface == "Eq") {
                source += synthesize_eq_impl(*st);
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

    // 展開済みトレイトはauto_implsから除去する。合成implが唯一の実装となり、
    // 型検査の重複impl検出・インターフェース適合・MIRの手組み生成はすべて通常のimplとして扱われる
    for (auto& decl : program.declarations) {
        if (!decl) {
            continue;
        }
        auto* st = decl->as<ast::StructDecl>();
        if (!st || st->auto_impls.empty() || !st->generic_params.empty()) {
            continue;
        }
        std::vector<std::string> remaining;
        for (const auto& iface : st->auto_impls) {
            if (!is_source_expanded_trait(iface)) {
                remaining.push_back(iface);
            }
        }
        st->auto_impls = std::move(remaining);
    }

    // 合成ソースを通常のパーサで検証しつつASTへ変換する（合成が不正ならここで検出される）
    Lexer lexer(source);
    auto tokens = lexer.tokenize();
    Parser parser(std::move(tokens));
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
