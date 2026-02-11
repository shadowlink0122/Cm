#include "generator.hpp"

namespace cm::mir {

// ============================================================
// 非ジェネリック構造体の自動実装を生成
// ============================================================
void AutoImplGenerator::generate_for_struct(const hir::HirStruct& st) {
    if (st.auto_impls.empty())
        return;

    // ジェネリック構造体はモノモーフィゼーション時に処理
    if (!st.generic_params.empty()) {
        register_generic_auto_impls(st.name, st.auto_impls);
        return;
    }

    for (const auto& iface_name : st.auto_impls) {
        if (iface_name == "Eq") {
            generate_builtin_eq_operator(st);
        } else if (iface_name == "Ord") {
            generate_builtin_lt_operator(st);
        } else if (iface_name == "Copy") {
            ctx_.impl_info[st.name]["Copy"] = "";
        } else if (iface_name == "Clone") {
            generate_builtin_clone_method(st);
        } else if (iface_name == "Hash") {
            generate_builtin_hash_method(st);
        } else if (iface_name == "Debug") {
            generate_builtin_debug_method(st);
        } else if (iface_name == "Display") {
            generate_builtin_display_method(st);
        } else if (iface_name == "Css") {
            generate_builtin_css_method(st);
            generate_builtin_to_css_method(st);
            generate_builtin_is_css_method(st);
        } else {
            // ユーザー定義インターフェース
            auto iface_it = ctx_.interface_defs.find(iface_name);
            if (iface_it != ctx_.interface_defs.end()) {
                const auto* iface = iface_it->second;
                for (const auto& op : iface->operators) {
                    generate_auto_operator_impl(st, *iface, op);
                }
            }
        }
    }
}

// ============================================================
// モノモーフィゼーション後の自動実装を生成
// ============================================================
void AutoImplGenerator::generate_monomorphized_auto_impls() {
    for (const auto& mir_struct : ctx_.program.structs) {
        if (!mir_struct)
            continue;

        const std::string& struct_name = mir_struct->name;

        // 元のジェネリック構造体名を抽出
        std::string base_name = struct_name;
        auto underscore_pos = struct_name.find("__");
        if (underscore_pos != std::string::npos) {
            base_name = struct_name.substr(0, underscore_pos);
        }

        auto it = generic_struct_auto_impls_.find(base_name);
        if (it == generic_struct_auto_impls_.end())
            continue;

        for (const auto& iface_name : it->second) {
            if (iface_name == "Eq") {
                generate_builtin_eq_operator_for_monomorphized(*mir_struct);
            } else if (iface_name == "Ord") {
                generate_builtin_lt_operator_for_monomorphized(*mir_struct);
            } else if (iface_name == "Copy") {
                ctx_.impl_info[struct_name]["Copy"] = "";
            } else if (iface_name == "Clone") {
                generate_builtin_clone_method_for_monomorphized(*mir_struct);
            } else if (iface_name == "Hash") {
                generate_builtin_hash_method_for_monomorphized(*mir_struct);
            } else if (iface_name == "Debug") {
                generate_builtin_debug_method_for_monomorphized(*mir_struct);
            } else if (iface_name == "Display") {
                generate_builtin_display_method_for_monomorphized(*mir_struct);
            } else if (iface_name == "Css") {
                generate_builtin_css_method_for_monomorphized(*mir_struct);
                generate_builtin_to_css_method_for_monomorphized(*mir_struct);
                generate_builtin_is_css_method_for_monomorphized(*mir_struct);
            }
        }
    }
}

// ============================================================
// ヘルパー: kebab-case変換
// ============================================================
std::string AutoImplGenerator::to_kebab_case(const std::string& name) {
    std::string result;
    for (size_t i = 0; i < name.size(); ++i) {
        if (isupper(name[i])) {
            if (i > 0)
                result += '-';
            result += static_cast<char>(tolower(name[i]));
        } else {
            result += name[i];
        }
    }
    return result;
}

// ============================================================
// ユーザー定義演算子の自動実装
// ============================================================
void AutoImplGenerator::generate_auto_operator_impl(const hir::HirStruct& st,
                                                    const hir::HirInterface& iface,
                                                    const hir::HirOperatorSig& op) {
    // 演算子の自動実装ロジック
    // TODO: lowering.hppから完全実装を移動
    (void)st;
    (void)iface;
    (void)op;
}

}  // namespace cm::mir
