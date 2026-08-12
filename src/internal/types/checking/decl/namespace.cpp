// ============================================================
// TypeChecker 実装 - 名前空間の登録/検査・マングル名シンボル表・importの解決
// ============================================================

#include "internal/base/i18n.hpp"
#include "internal/types/type_checker.hpp"

#include <string>

namespace cm {

void TypeChecker::register_namespace(ast::ModuleDecl& mod, const std::string& parent_namespace) {
    std::string namespace_name = mod.path.segments.empty() ? "" : mod.path.segments[0];
    std::string full_namespace =
        parent_namespace.empty() ? namespace_name : parent_namespace + "::" + namespace_name;

    debug::tc::log(debug::tc::Id::Resolved, "Processing namespace: " + full_namespace,
                   debug::Level::Debug);

    // 名前空間内の非修飾型名解決のため現在の名前空間を設定（ネストは再帰で退避/復元）
    std::string saved_namespace = current_namespace_;
    current_namespace_ = full_namespace;

    for (auto& inner_decl : mod.declarations) {
        if (auto* nested_mod = inner_decl->as<ast::ModuleDecl>()) {
            register_namespace(*nested_mod, full_namespace);
        } else if (auto* func = inner_decl->as<ast::FunctionDecl>()) {
            std::string original_name = func->name;
            func->name = full_namespace + "::" + original_name;
            register_declaration(*inner_decl);
            func->name = original_name;
        } else if (auto* st = inner_decl->as<ast::StructDecl>()) {
            std::string original_name = st->name;
            st->name = full_namespace + "::" + original_name;
            register_declaration(*inner_decl);
            st->name = original_name;
        } else if (auto* gvar = inner_decl->as<ast::GlobalVarDecl>()) {
            // グローバル変数も修飾名で登録し、import元の同名グローバルとの衝突を防ぐ（非修飾参照は infer_ident の名前空間フォールバックで解決される）
            std::string original_name = gvar->name;
            gvar->name = full_namespace + "::" + original_name;
            register_declaration(*inner_decl);
            gvar->name = original_name;
        } else {
            register_declaration(*inner_decl);
        }
    }

    current_namespace_ = saved_namespace;
}

void TypeChecker::check_namespace(ast::ModuleDecl& mod, const std::string& parent_namespace) {
    std::string namespace_name = mod.path.segments.empty() ? "" : mod.path.segments[0];
    std::string full_namespace =
        parent_namespace.empty() ? namespace_name : parent_namespace + "::" + namespace_name;

    debug::tc::log(debug::tc::Id::Resolved, "Checking namespace: " + full_namespace,
                   debug::Level::Debug);

    // 名前空間内の非修飾型名解決のため現在の名前空間を設定（ネストは再帰で退避/復元）
    std::string saved_namespace = current_namespace_;
    current_namespace_ = full_namespace;

    for (auto& inner_decl : mod.declarations) {
        if (auto* nested_mod = inner_decl->as<ast::ModuleDecl>()) {
            check_namespace(*nested_mod, full_namespace);
        } else if (auto* func = inner_decl->as<ast::FunctionDecl>()) {
            std::string original_name = func->name;
            func->name = full_namespace + "::" + original_name;
            check_declaration(*inner_decl);
            func->name = original_name;
        } else if (auto* st = inner_decl->as<ast::StructDecl>()) {
            std::string original_name = st->name;
            st->name = full_namespace + "::" + original_name;
            check_declaration(*inner_decl);
            st->name = original_name;
        } else {
            check_declaration(*inner_decl);
        }
    }

    current_namespace_ = saved_namespace;
}

// マングル名をシンボルテーブルへ登録し、別由来・別シグネチャの同名があればエラーを発行する（C16）
// モジュールflatten等による同一定義の再出現（由来・シグネチャが完全一致）は許容する
void TypeChecker::register_mangled_symbol(const std::string& name, const std::string& origin,
                                          const std::string& sig, Span span) {
    auto [it, inserted] = mangled_symbols_.emplace(name, MangledSymbolInfo{origin, sig, span});
    if (inserted) {
        return;
    }
    if (it->second.origin == origin && it->second.sig == sig) {
        return;
    }
    error(span,
          i18n::msgf(i18n::MsgId::TypeMangledSymbolCollision, name, it->second.origin, origin));
}

void TypeChecker::check_import(ast::ImportDecl& import) {
    std::string path_str = import.path.to_string();

    // std::io からのインポート
    if (path_str == "std::io") {
        for (const auto& item : import.items) {
            if (item.name == "println" || item.name.empty()) {
                register_println();
            }
            if (item.name == "print" || item.name.empty()) {
                register_print();
            }
        }
    } else if (import.path.segments.size() >= 3 && import.path.segments[0] == "std" &&
               import.path.segments[1] == "io") {
        // std::io::println / std::io::print
        if (import.path.segments[2] == "println") {
            register_println();
        } else if (import.path.segments[2] == "print") {
            register_print();
        }
    }
}

}  // namespace cm
