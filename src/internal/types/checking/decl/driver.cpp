// ============================================================
// TypeChecker 実装 - 検査エントリポイント（check）と構造体表・自動implのアクセサ
// ============================================================

#include "internal/types/checking/match_hoist.hpp"
#include "internal/types/type_checker.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace cm {

TypeChecker::TypeChecker() {
    register_builtin_interfaces();
    register_builtin_types();  // Result<T, E>, Option<T> 組み込み型
}

bool TypeChecker::check(ast::Program& program) {
    debug::tc::log(debug::tc::Id::Start);

    // 組み込みprelude: Result<T, E> / Option<T> を実enum宣言としてプログラム先頭に注入する（TypeChecker内だけの疑似登録ではHIR/MIR/コード生成にenum定義が届かず、関数返却でのペイロード喪失や型IDの分裂を起こしていた）。
    // ユーザーが同名を定義している場合は注入しない
    {
        bool has_result = false;
        bool has_option = false;
        for (const auto& decl : program.declarations) {
            if (const auto* en = decl->as<ast::EnumDecl>()) {
                if (en->name == "Result") {
                    has_result = true;
                }
                if (en->name == "Option") {
                    has_option = true;
                }
            }
        }
        auto make_prelude_enum = [](const std::string& name, std::vector<std::string> params,
                                    std::vector<ast::EnumMember> members) {
            auto en = std::make_unique<ast::EnumDecl>(name, std::move(members));
            en->generic_params = std::move(params);
            // preludeマーカー: register_enumの組み込みメソッド消去（ユーザー再定義向け）を抑止する
            en->attributes.emplace_back("__prelude");
            return std::make_unique<ast::Decl>(std::move(en), Span{});
        };
        if (!has_option) {
            std::vector<ast::EnumMember> members;
            members.emplace_back("Some", std::vector<std::pair<std::string, ast::TypePtr>>{
                                             {"value", ast::make_named("T")}});
            members.emplace_back("None");
            program.declarations.insert(program.declarations.begin(),
                                        make_prelude_enum("Option", {"T"}, std::move(members)));
        }
        if (!has_result) {
            std::vector<ast::EnumMember> members;
            members.emplace_back("Ok", std::vector<std::pair<std::string, ast::TypePtr>>{
                                           {"value", ast::make_named("T")}});
            members.emplace_back("Err", std::vector<std::pair<std::string, ast::TypePtr>>{
                                            {"error", ast::make_named("E")}});
            program.declarations.insert(
                program.declarations.begin(),
                make_prelude_enum("Result", {"T", "E"}, std::move(members)));
        }
    }

    // 式形式matchの呼び出しscrutineeを一時変数へ退避するASTプリパス（HIRの三項演算子脱糖でのクローン多重評価を避け、単一評価を保証する）
    hoist_match_call_scrutinees(program);

    // 属性の検証レジストリ（R7）: タイポ・未実装属性の診断と#[deprecated]収集（Pass 2の呼び出し検査より前に実行する）
    check_attributes(program);

    // Pass 1: 関数シグネチャを登録
    for (auto& decl : program.declarations) {
        register_declaration(*decl);
    }

    // Pass 2: 関数本体をチェック
    for (auto& decl : program.declarations) {
        check_declaration(*decl);
    }

    // 命名規則チェック（check/lint --strict時のみ有効）
    check_naming_conventions(program);

    debug::tc::log(debug::tc::Id::End, std::to_string(diagnostics_.size()) + " issues");
    return !has_errors();
}

bool TypeChecker::has_errors() const {
    for (const auto& d : diagnostics_) {
        if (d.severity == DiagKind::Error)
            return true;
    }
    return false;
}

void TypeChecker::register_struct(const std::string& name, const ast::StructDecl& decl) {
    struct_defs_[name] = &decl;
}

const ast::StructDecl* TypeChecker::get_struct(const std::string& name) const {
    auto it = struct_defs_.find(name);
    if (it != struct_defs_.end()) {
        return it->second;
    }

    // 名前空間内の非修飾名は「現在の名前空間::名前」として解決する
    if (auto qualified = resolve_in_namespace(name)) {
        auto ns_it = struct_defs_.find(*qualified);
        if (ns_it != struct_defs_.end()) {
            return ns_it->second;
        }
    }

    auto td_it = typedef_defs_.find(name);
    if (td_it != typedef_defs_.end() && td_it->second) {
        std::string actual_name = td_it->second->name;
        auto struct_it = struct_defs_.find(actual_name);
        if (struct_it != struct_defs_.end()) {
            return struct_it->second;
        }
    }

    return nullptr;
}

ast::TypePtr TypeChecker::get_default_member_type(const std::string& struct_name) const {
    const ast::StructDecl* decl = get_struct(struct_name);
    if (!decl)
        return nullptr;
    for (const auto& field : decl->fields) {
        if (field.is_default) {
            return field.type;
        }
    }
    return nullptr;
}

std::string TypeChecker::get_default_member_name(const std::string& struct_name) const {
    const ast::StructDecl* decl = get_struct(struct_name);
    if (!decl)
        return "";
    for (const auto& field : decl->fields) {
        if (field.is_default) {
            return field.name;
        }
    }
    return "";
}

bool TypeChecker::has_auto_impl(const std::string& struct_name,
                                const std::string& iface_name) const {
    auto it = auto_impl_info_.find(struct_name);
    if (it != auto_impl_info_.end()) {
        auto iface_it = it->second.find(iface_name);
        if (iface_it != it->second.end()) {
            return iface_it->second;
        }
    }
    return false;
}

}  // namespace cm
