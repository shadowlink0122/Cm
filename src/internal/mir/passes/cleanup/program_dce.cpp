#include "program_dce.hpp"

#include <set>
#include <string>
#include <unordered_map>

namespace cm::mir::opt {

bool ProgramDeadCodeElimination::run(MirProgram& program) {
    bool changed = false;

    // 1. 使用されている関数を収集
    std::set<std::string> used_functions;
    collect_used_functions(program, used_functions);

    // 2. 未使用関数を削除
    changed |= remove_unused_functions(program, used_functions);

    // 3. 使用されている構造体を収集
    std::set<std::string> used_structs;
    collect_used_structs(program, used_structs, used_functions);

    // 4. 未使用構造体を削除
    changed |= remove_unused_structs(program, used_structs);

    return changed;
}

void ProgramDeadCodeElimination::collect_used_functions(const MirProgram& program,
                                                        std::set<std::string>& used) {
    // エントリポイントは常に使用される
    used.insert("main");
    used.insert("_start");
    used.insert("start_kernel");
    used.insert("efi_main");

    // 組み込み関数は常に使用される
    static const std::set<std::string> builtins = {"println",
                                                   "__println__",
                                                   "print",
                                                   "__print__",
                                                   "printf",
                                                   "sprintf",
                                                   "exit",
                                                   "panic",
                                                   "__builtin_string_len",
                                                   "__builtin_string_codepoint_len",
                                                   "__builtin_string_charAt",
                                                   "__builtin_string_substring",
                                                   "__builtin_string_indexOf",
                                                   "__builtin_string_toUpperCase",
                                                   "__builtin_string_toLowerCase",
                                                   "__builtin_string_trim",
                                                   "__builtin_string_startsWith",
                                                   "__builtin_string_endsWith",
                                                   "__builtin_string_includes",
                                                   "__builtin_string_repeat",
                                                   "__builtin_string_replace",
                                                   "__builtin_array_forEach",
                                                   "__builtin_array_reduce",
                                                   "__builtin_array_some_i32",
                                                   "__builtin_array_every_i32",
                                                   "__builtin_array_findIndex_i32",
                                                   "__builtin_array_indexOf_i32",
                                                   "__builtin_array_includes_i32",
                                                   "cm_format_int",
                                                   "cm_format_double",
                                                   "cm_format_char",
                                                   "cm_string_concat",
                                                   "cm_string_free",
                                                   "cm_slice_free",
                                                   "strcmp",
                                                   "strlen",
                                                   "malloc",
                                                   "free"};

    for (const auto& b : builtins) {
        used.insert(b);
    }

    // export関数は常にusedとして扱う
    // exportされた関数はクロスモジュールリンクで使用されるため削除してはならない
    for (const auto& func : program.functions) {
        if (func && func->is_export) {
            used.insert(func->name);
        }
    }

    // 関数名→MirFunction*の逆引きマップを構築
    // モジュール修飾名（module::func）→単純名（func）の解決にも使用
    std::unordered_map<std::string, const MirFunction*> func_map;
    for (const auto& func : program.functions) {
        if (func) {
            func_map[func->name] = func.get();
        }
    }

    // 呼び出しグラフをたどって使用される関数を収集
    std::queue<std::string> worklist;
    for (const auto& name : used) {
        worklist.push(name);
    }

    while (!worklist.empty()) {
        std::string current = worklist.front();
        worklist.pop();

        // この関数から呼び出される関数を収集
        const MirFunction* func = nullptr;
        auto it = func_map.find(current);
        if (it != func_map.end()) {
            func = it->second;
        }
        if (!func)
            continue;

        // calleeを収集するヘルパー: FunctionRefからcallee名を取得
        auto resolve_callee = [&](const std::string& callee) {
            // 直接一致する関数を探す
            if (func_map.count(callee) > 0) {
                return callee;
            }
            // モジュール修飾名の場合、単純名で再検索
            auto pos = callee.rfind("::");
            if (pos != std::string::npos) {
                std::string simple = callee.substr(pos + 2);
                if (func_map.count(simple) > 0) {
                    return simple;
                }
            }
            return callee;  // そのまま返す
        };

        for (const auto& block : func->basic_blocks) {
            if (!block)
                continue;

            // ステートメントから関数参照を収集
            for (const auto& stmt : block->statements) {
                if (!stmt || stmt->kind != MirStatement::Assign)
                    continue;

                const auto& assign = std::get<MirStatement::AssignData>(stmt->data);
                if (!assign.rvalue)
                    continue;

                if (assign.rvalue->kind == MirRvalue::Use) {
                    const auto& use_data = std::get<MirRvalue::UseData>(assign.rvalue->data);
                    if (use_data.operand && use_data.operand->kind == MirOperand::FunctionRef) {
                        if (const auto* name = std::get_if<std::string>(&use_data.operand->data)) {
                            std::string resolved = resolve_callee(*name);
                            if (used.find(resolved) == used.end()) {
                                used.insert(resolved);
                                worklist.push(resolved);
                            }
                        }
                    }
                }
            }

            // ターミネータの呼び出しから関数名を収集
            if (block->terminator && block->terminator->kind == MirTerminator::Call) {
                const auto& call_data = std::get<MirTerminator::CallData>(block->terminator->data);

                // 関数名を取得
                std::string callee;
                if (call_data.func) {
                    if (call_data.func->kind == MirOperand::FunctionRef) {
                        if (const auto* name = std::get_if<std::string>(&call_data.func->data)) {
                            callee = resolve_callee(*name);
                        }
                    } else if (call_data.func->kind == MirOperand::Constant) {
                        if (const auto* c = std::get_if<MirConstant>(&call_data.func->data)) {
                            if (const auto* s = std::get_if<std::string>(&c->value)) {
                                callee = resolve_callee(*s);
                            }
                        }
                    }
                }

                if (!callee.empty()) {
                    if (used.find(callee) == used.end()) {
                        used.insert(callee);
                        worklist.push(callee);
                    }
                }

                // 呼び出しの引数から関数参照を収集（クロージャサポート）
                for (const auto& arg : call_data.args) {
                    if (!arg)
                        continue;
                    if (arg->kind == MirOperand::FunctionRef) {
                        if (const auto* name = std::get_if<std::string>(&arg->data)) {
                            std::string resolved = resolve_callee(*name);
                            if (used.find(resolved) == used.end()) {
                                used.insert(resolved);
                                worklist.push(resolved);
                            }
                        }
                    }
                }
            }
        }
    }

    // implメソッドの保持: callerがusedなら、そこから呼ばれるimplメソッドも保持
    // TypeName__method パターンの関数を、TypeNameの他メソッドがusedなら保持
    std::set<std::string> used_types;
    for (const auto& name : used) {
        size_t sep = name.find("__");
        if (sep != std::string::npos) {
            used_types.insert(name.substr(0, sep));
        }
    }

    // 使用されている型のメソッドも保持
    for (const auto& func : program.functions) {
        if (!func)
            continue;
        const std::string& name = func->name;
        size_t sep = name.find("__");
        if (sep != std::string::npos) {
            std::string type_name = name.substr(0, sep);
            if (used_types.count(type_name) > 0) {
                used.insert(name);
            }
        }
    }

    // vtableエントリのimpl関数を保護
    // interface dispatch関数（InterfaceName__method）がusedの場合、対応するvtableのimpl関数も保持する
    for (const auto& vt : program.vtables) {
        if (!vt)
            continue;
        for (const auto& entry : vt->entries) {
            // vtableエントリのimpl関数名をusedに追加
            used.insert(entry.impl_function_name);
        }
    }
}

bool ProgramDeadCodeElimination::remove_unused_functions(MirProgram& program,
                                                         const std::set<std::string>& used) {
    bool changed = false;

    auto it = program.functions.begin();
    while (it != program.functions.end()) {
        if (*it && used.find((*it)->name) == used.end()) {
            it = program.functions.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }

    return changed;
}

void ProgramDeadCodeElimination::collect_used_structs(const MirProgram& program,
                                                      std::set<std::string>& used,
                                                      const std::set<std::string>& used_functions) {
    // 使用される関数の引数・戻り値・ローカル変数から構造体を収集
    for (const auto& func : program.functions) {
        if (!func)
            continue;
        if (used_functions.find(func->name) == used_functions.end())
            continue;

        for (const auto& local : func->locals) {
            if (local.type && local.type->kind == ast::TypeKind::Struct) {
                used.insert(local.type->name);
            }
            // 配列の要素型も検査
            if (local.type && local.type->kind == ast::TypeKind::Array &&
                local.type->element_type) {
                if (local.type->element_type->kind == ast::TypeKind::Struct) {
                    used.insert(local.type->element_type->name);
                }
            }
        }
    }

    // 構造体のフィールドからも収集（再帰的）
    std::queue<std::string> worklist;
    for (const auto& s : used) {
        worklist.push(s);
    }

    while (!worklist.empty()) {
        std::string current = worklist.front();
        worklist.pop();

        const MirStruct* st = program.find_struct(current);
        if (!st)
            continue;

        for (const auto& field : st->fields) {
            if (field.type && field.type->kind == ast::TypeKind::Struct) {
                if (used.find(field.type->name) == used.end()) {
                    used.insert(field.type->name);
                    worklist.push(field.type->name);
                }
            }
        }
    }
}

bool ProgramDeadCodeElimination::remove_unused_structs(MirProgram& program,
                                                       const std::set<std::string>& used) {
    bool changed = false;

    auto it = program.structs.begin();
    while (it != program.structs.end()) {
        if (*it && used.find((*it)->name) == used.end()) {
            it = program.structs.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }

    return changed;
}

}  // namespace cm::mir::opt
