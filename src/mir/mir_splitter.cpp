// MIRプログラムをモジュール別に分割するユーティリティの実装

#include "mir_splitter.hpp"

#include <set>
#include <unordered_set>

namespace cm::mir {

// モジュール名を正規化（空文字列 → "main"）
std::string MirSplitter::normalize_module_name(const std::string& module_path) {
    if (module_path.empty()) {
        return "main";
    }
    return module_path;
}

// 関数が参照する型名を収集（構造体・enum）
std::vector<std::string> MirSplitter::collect_referenced_types(const MirFunction& func) {
    std::set<std::string> types;

    // ローカル変数の型から参照される構造体名を収集
    for (const auto& local : func.locals) {
        if (local.type) {
            if (local.type->kind == hir::TypeKind::Struct && !local.type->name.empty()) {
                types.insert(local.type->name);
            }
            // ポインタの指す先の型も含める
            if (local.type->kind == hir::TypeKind::Pointer && local.type->element_type) {
                if (local.type->element_type->kind == hir::TypeKind::Struct &&
                    !local.type->element_type->name.empty()) {
                    types.insert(local.type->element_type->name);
                }
            }
        }
    }

    return std::vector<std::string>(types.begin(), types.end());
}

// 関数が呼び出す関数名を収集
std::vector<std::string> MirSplitter::collect_called_functions(const MirFunction& func) {
    std::set<std::string> called;

    for (const auto& bb : func.basic_blocks) {
        if (!bb || !bb->terminator)
            continue;

        const auto& term = *bb->terminator;
        if (term.kind == MirTerminator::Call) {
            const auto& call_data = std::get<MirTerminator::CallData>(term.data);
            // FunctionRef オペランドから関数名を取得
            if (call_data.func && call_data.func->kind == MirOperand::FunctionRef) {
                const auto* func_name = std::get_if<std::string>(&call_data.func->data);
                if (func_name) {
                    called.insert(*func_name);
                }
            }
        }
    }

    return std::vector<std::string>(called.begin(), called.end());
}

// MirProgramをmodule_pathに基づきモジュール別に分割
std::map<std::string, ModuleProgram> MirSplitter::split_by_module(const MirProgram& program) {
    std::map<std::string, ModuleProgram> modules;

    // ============================================================
    // Step 1: 関数をモジュール別にグループ化
    // ============================================================
    for (const auto& func : program.functions) {
        if (!func)
            continue;
        std::string mod_name = normalize_module_name(func->module_path);

        auto& mod = modules[mod_name];
        mod.module_name = mod_name;
        mod.functions.push_back(func.get());
    }

    // ============================================================
    // Step 2: 構造体をモジュール別にグループ化
    // ============================================================
    // 構造体名 → 所属モジュールのマップ
    std::unordered_map<std::string, std::string> struct_owner;

    for (const auto& st : program.structs) {
        if (!st)
            continue;
        std::string mod_name = normalize_module_name(st->module_path);

        auto& mod = modules[mod_name];
        mod.module_name = mod_name;
        mod.structs.push_back(st.get());
        struct_owner[st->name] = mod_name;
    }

    // ============================================================
    // Step 3: enumをモジュール別にグループ化
    // ============================================================
    std::unordered_map<std::string, std::string> enum_owner;

    for (const auto& en : program.enums) {
        if (!en)
            continue;
        std::string mod_name = normalize_module_name(en->module_path);

        auto& mod = modules[mod_name];
        mod.module_name = mod_name;
        mod.enums.push_back(en.get());
        enum_owner[en->name] = mod_name;
    }

    // ============================================================
    // Step 4: 関数名 → 所属モジュールのマップを構築
    // ============================================================
    std::unordered_map<std::string, std::string> func_owner;
    for (const auto& [mod_name, mod] : modules) {
        for (const auto* func : mod.functions) {
            func_owner[func->name] = mod_name;
        }
    }

    // ============================================================
    // Step 5: extern依存を解決
    // 各モジュールが参照するが定義されていない型・関数を特定
    // ============================================================
    for (auto& [mod_name, mod] : modules) {
        // このモジュールに定義されている構造体名・関数名のセット
        std::unordered_set<std::string> local_structs;
        std::unordered_set<std::string> local_enums;
        std::unordered_set<std::string> local_functions;

        for (const auto* st : mod.structs) {
            local_structs.insert(st->name);
        }
        for (const auto* en : mod.enums) {
            local_enums.insert(en->name);
        }
        for (const auto* func : mod.functions) {
            local_functions.insert(func->name);
        }

        // 外部参照を集める
        std::unordered_set<std::string> needed_structs;
        std::unordered_set<std::string> needed_functions;

        for (const auto* func : mod.functions) {
            // 参照される型
            auto types = collect_referenced_types(*func);
            for (const auto& type_name : types) {
                if (local_structs.find(type_name) == local_structs.end() &&
                    struct_owner.find(type_name) != struct_owner.end()) {
                    needed_structs.insert(type_name);
                }
            }

            // 呼び出される関数
            auto calls = collect_called_functions(*func);
            for (const auto& call_name : calls) {
                if (local_functions.find(call_name) == local_functions.end() &&
                    func_owner.find(call_name) != func_owner.end()) {
                    needed_functions.insert(call_name);
                }
            }
        }

        // extern参照を登録
        for (const auto& struct_name : needed_structs) {
            for (const auto& st : program.structs) {
                if (st && st->name == struct_name) {
                    mod.extern_structs.push_back(st.get());
                    break;
                }
            }
        }

        for (const auto& func_name : needed_functions) {
            for (const auto& func : program.functions) {
                if (func && func->name == func_name) {
                    mod.extern_functions.push_back(func.get());
                    break;
                }
            }
        }
    }

    // ============================================================
    // Step 6: 共有データを全モジュールにコピー
    // インターフェース、vtable、グローバル変数は全モジュールで必要
    // ============================================================
    for (auto& [mod_name, mod] : modules) {
        for (const auto& iface : program.interfaces) {
            if (iface) {
                mod.interfaces.push_back(iface.get());
            }
        }
        for (const auto& vt : program.vtables) {
            if (vt) {
                mod.vtables.push_back(vt.get());
            }
        }
        for (const auto& gv : program.global_vars) {
            if (gv) {
                mod.global_vars.push_back(gv.get());
            }
        }
    }

    return modules;
}

}  // namespace cm::mir
