// ============================================================
// ネスト型宣言の平坦化（hoist）パス
// struct/enum本体内に宣言された型をOuter::Inner名のトップレベル宣言へ展開する。
// 型チェッカ以降は::を含むフラット名をそのまま扱えるため、このパス以降に新しいスコープ概念は不要になる
// ============================================================

#include "nested.hpp"

#include "decl.hpp"
#include "typedef.hpp"

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cm::ast {

namespace {

// 単純名→平坦名の対応（1ネスト階層ぶん）
using Scope = std::unordered_map<std::string, std::string>;

// 型参照名の先頭セグメントをスコープチェーン（内側優先）で平坦名へ書き換える
void rewrite_type_name(std::string& name, const std::vector<Scope>& scopes) {
    if (name.empty()) {
        return;
    }
    auto pos = name.find("::");
    std::string head = pos == std::string::npos ? name : name.substr(0, pos);
    for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
        auto found = it->find(head);
        if (found != it->end()) {
            name = pos == std::string::npos ? found->second : found->second + name.substr(pos);
            return;
        }
    }
}

// 型ツリーを再帰的に歩き、名前付き型参照を平坦名へ書き換える
void rewrite_type(const TypePtr& type, const std::vector<Scope>& scopes) {
    if (!type) {
        return;
    }
    // ユーザー定義型名のみ書き換える（ジェネリックパラメータ名Tなどは対象外）
    if (type->kind == TypeKind::Struct || type->kind == TypeKind::TypeAlias) {
        rewrite_type_name(type->name, scopes);
    }
    rewrite_type(type->element_type, scopes);
    for (const auto& arg : type->type_args) {
        rewrite_type(arg, scopes);
    }
    for (const auto& param : type->param_types) {
        rewrite_type(param, scopes);
    }
    rewrite_type(type->return_type, scopes);
    if (type->kind == TypeKind::Union) {
        if (auto* uni = static_cast<UnionType*>(type.get())) {
            for (auto& variant : uni->variants) {
                for (const auto& field : variant.fields) {
                    rewrite_type(field, scopes);
                }
            }
        }
    }
}

// struct本体のフィールド型参照を書き換える
void rewrite_struct_body(StructDecl& st, const std::vector<Scope>& scopes) {
    for (auto& field : st.fields) {
        rewrite_type(field.type, scopes);
    }
}

// enum本体の連想データ型参照を書き換える
void rewrite_enum_body(EnumDecl& en, const std::vector<Scope>& scopes) {
    for (auto& member : en.members) {
        for (auto& field : member.fields) {
            rewrite_type(field.second, scopes);
        }
    }
}

// 1つの型宣言のネスト型を深さ優先で平坦化し、hoistedへ内側優先の順で追記する
void flatten_type_decl(Decl& decl, std::vector<Scope>& scopes, std::vector<DeclPtr>& hoisted) {
    std::vector<DeclPtr> nested;
    std::string flat_name;
    if (auto* st = decl.as<StructDecl>()) {
        nested = std::move(st->nested_types);
        st->nested_types.clear();
        flat_name = st->name;
    } else if (auto* en = decl.as<EnumDecl>()) {
        nested = std::move(en->nested_types);
        en->nested_types.clear();
        flat_name = en->name;
    } else {
        return;
    }

    // 同一階層のネスト型をスコープへ登録する（兄弟間の相互参照を許すため改名前に一括登録）
    Scope local;
    for (const auto& n : nested) {
        if (const auto* st = n->as<StructDecl>()) {
            local.emplace(st->name, flat_name + "::" + st->name);
        } else if (const auto* en = n->as<EnumDecl>()) {
            local.emplace(en->name, flat_name + "::" + en->name);
        }
    }
    scopes.push_back(std::move(local));

    for (auto& n : nested) {
        if (auto* st = n->as<StructDecl>()) {
            st->name = scopes.back().at(st->name);
        } else if (auto* en = n->as<EnumDecl>()) {
            en->name = scopes.back().at(en->name);
        }
        // さらに深いネストを先に展開する（レイアウト依存の前方参照を避けるため内側が先）
        flatten_type_decl(*n, scopes, hoisted);
        hoisted.push_back(std::move(n));
    }

    // 本体の型参照は自階層＋外側スコープの両方が見える状態で書き換える
    if (auto* st = decl.as<StructDecl>()) {
        rewrite_struct_body(*st, scopes);
    } else if (auto* en = decl.as<EnumDecl>()) {
        rewrite_enum_body(*en, scopes);
    }
    scopes.pop_back();
}

// 宣言リストを走査し、ネスト型を持つstruct/enumを外側宣言の直前へ展開する（mod・export配下も再帰対象）
void hoist_in_decl_list(std::vector<DeclPtr>& decls) {
    for (size_t i = 0; i < decls.size(); ++i) {
        Decl& d = *decls[i];
        if (auto* mod = d.as<ModuleDecl>()) {
            hoist_in_decl_list(mod->declarations);
            continue;
        }
        // export宣言ラッパの内側もhoist対象にする（hoistされた型のexport状態は可視性フィールドで保持される）
        Decl* target = &d;
        if (auto* exp = d.as<ExportDecl>()) {
            if (!exp->declaration) {
                continue;
            }
            target = exp->declaration.get();
        }

        const auto* st = target->as<StructDecl>();
        const auto* en = target->as<EnumDecl>();
        bool has_nested = (st && !st->nested_types.empty()) || (en && !en->nested_types.empty());
        if (!has_nested) {
            continue;
        }

        std::vector<Scope> scopes;
        std::vector<DeclPtr> hoisted;
        flatten_type_decl(*target, scopes, hoisted);
        decls.insert(decls.begin() + static_cast<std::ptrdiff_t>(i),
                     std::make_move_iterator(hoisted.begin()),
                     std::make_move_iterator(hoisted.end()));
        i += hoisted.size();
    }
}

}  // namespace

void hoist_nested_types(Program& program) {
    hoist_in_decl_list(program.declarations);
}

}  // namespace cm::ast
