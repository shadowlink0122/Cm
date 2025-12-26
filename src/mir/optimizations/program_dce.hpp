#pragma once

#include "../nodes.hpp"

#include <algorithm>
#include <queue>
#include <set>
#include <string>

namespace cm::mir::opt {

// ============================================================
// プログラム全体のデッドコード除去
// 未使用の関数・構造体を削除する
// ============================================================
class ProgramDeadCodeElimination {
   public:
    bool run(MirProgram& program) {
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

   private:
    // 使用されている関数を収集
    void collect_used_functions(const MirProgram& program, std::set<std::string>& used) {
        // mainとエントリポイントは常に使用される
        used.insert("main");
        used.insert("_start");

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
                                                       "strcmp",
                                                       "strlen",
                                                       "malloc",
                                                       "free"};

        for (const auto& b : builtins) {
            used.insert(b);
        }

        // 呼び出しグラフをたどって使用される関数を収集
        std::queue<std::string> worklist;
        worklist.push("main");

        // インターフェースメソッド呼び出しを記録（Interface__method形式）
        std::set<std::string> interface_methods;

        while (!worklist.empty()) {
            std::string current = worklist.front();
            worklist.pop();

            // この関数から呼び出される関数を収集
            const MirFunction* func = program.find_function(current);
            if (!func)
                continue;

            for (const auto& block : func->basic_blocks) {
                if (!block)
                    continue;

                // ステートメントから関数参照を収集
                // （関数ポインタへの代入: _2 = add; など）
                for (const auto& stmt : block->statements) {
                    if (!stmt || stmt->kind != MirStatement::Assign)
                        continue;

                    const auto& assign = std::get<MirStatement::AssignData>(stmt->data);
                    if (!assign.rvalue)
                        continue;

                    if (assign.rvalue->kind == MirRvalue::Use) {
                        const auto& use_data = std::get<MirRvalue::UseData>(assign.rvalue->data);
                        if (use_data.operand && use_data.operand->kind == MirOperand::FunctionRef) {
                            if (const auto* name =
                                    std::get_if<std::string>(&use_data.operand->data)) {
                                if (used.find(*name) == used.end()) {
                                    used.insert(*name);
                                    worklist.push(*name);
                                }
                            }
                        }
                    }
                }

                // ターミネータの呼び出しから関数名を収集
                if (block->terminator && block->terminator->kind == MirTerminator::Call) {
                    const auto& call_data =
                        std::get<MirTerminator::CallData>(block->terminator->data);

                    // 関数名を取得
                    std::string callee;
                    if (call_data.func) {
                        if (call_data.func->kind == MirOperand::FunctionRef) {
                            if (const auto* name =
                                    std::get_if<std::string>(&call_data.func->data)) {
                                callee = *name;
                            }
                        } else if (call_data.func->kind == MirOperand::Constant) {
                            // 定数として関数名が格納されている場合
                            if (const auto* c = std::get_if<MirConstant>(&call_data.func->data)) {
                                if (const auto* s = std::get_if<std::string>(&c->value)) {
                                    callee = *s;
                                }
                            }
                        }
                    }

                    if (!callee.empty()) {
                        // インターフェースメソッド呼び出しを記録
                        // Interface__method形式の場合、メソッド名を抽出
                        size_t sep = callee.find("__");
                        if (sep != std::string::npos) {
                            std::string method_name = callee.substr(sep);  // "__method"
                            interface_methods.insert(method_name);
                        }

                        if (used.find(callee) == used.end()) {
                            used.insert(callee);
                            worklist.push(callee);
                        }
                    }
                }
            }
        }

        // インターフェースメソッドの実装を保持
        // Interface__method形式の呼び出しがある場合、Type__method形式の関数も保持
        for (const auto& func : program.functions) {
            if (!func)
                continue;

            const std::string& name = func->name;
            size_t sep = name.find("__");
            if (sep != std::string::npos) {
                std::string method_suffix = name.substr(sep);  // "__method"
                if (interface_methods.find(method_suffix) != interface_methods.end()) {
                    // このメソッド名に対応するインターフェースメソッド呼び出しがある
                    used.insert(name);
                }
            }
        }
    }

    // 未使用関数を削除
    bool remove_unused_functions(MirProgram& program, const std::set<std::string>& used) {
        bool changed = false;

        auto it = program.functions.begin();
        while (it != program.functions.end()) {
            if (*it && used.find((*it)->name) == used.end()) {
                // デバッグ出力
                // std::cerr << "[DCE] Removing unused function: " << (*it)->name << std::endl;
                it = program.functions.erase(it);
                changed = true;
            } else {
                ++it;
            }
        }

        return changed;
    }

    // 使用されている構造体を収集
    void collect_used_structs(const MirProgram& program, std::set<std::string>& used,
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

    // 未使用構造体を削除
    bool remove_unused_structs(MirProgram& program, const std::set<std::string>& used) {
        bool changed = false;

        auto it = program.structs.begin();
        while (it != program.structs.end()) {
            if (*it && used.find((*it)->name) == used.end()) {
                // デバッグ出力
                // std::cerr << "[DCE] Removing unused struct: " << (*it)->name << std::endl;
                it = program.structs.erase(it);
                changed = true;
            } else {
                ++it;
            }
        }

        return changed;
    }
};

}  // namespace cm::mir::opt
