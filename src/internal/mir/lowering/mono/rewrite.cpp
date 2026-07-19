// 単相化 - 型引数文字列の抽出とジェネリック呼び出しの特殊化呼び出しへの書き換え

#include "internal/base/debug.hpp"
#include "internal/base/target.hpp"
#include "internal/mir/lowering/mono_internal.hpp"
#include "internal/mir/lowering/monomorphization.hpp"
#include "internal/mir/lowering/monomorphization_utils.hpp"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cm::mir {

// 型からtype_argsを文字列として抽出
std::vector<std::string> Monomorphization::extract_type_args_strings(const hir::TypePtr& type) {
    std::vector<std::string> result;
    if (!type)
        return result;

    for (const auto& arg : type->type_args) {
        if (arg) {
            result.push_back(get_type_name(arg));
        }
    }
    return result;
}

// ジェネリック関数呼び出しを特殊化関数呼び出しに書き換え
void Monomorphization::rewrite_generic_calls(
    MirProgram& program, const std::map<std::pair<std::string, std::vector<std::string>>,
                                        std::vector<std::tuple<std::string, size_t>>>& needed) {
    // (元の関数名, 型引数) -> 特殊化関数名 のマッピングを構築
    std::map<std::pair<std::string, std::vector<std::string>>, std::string> rewrite_map;
    for (const auto& [key, _] : needed) {
        const auto& [func_name, type_args] = key;
        std::string specialized_name = make_specialized_name(func_name, type_args);
        rewrite_map[key] = specialized_name;
    }

    // 単純なジェネリック関数名 -> 特殊化関数名のマップも構築
    // (例: "create_node" -> "create_node__int")
    std::map<std::string, std::string> simple_rewrite_map;
    for (const auto& [key, specialized_name] : rewrite_map) {
        const auto& [func_name, type_args] = key;
        // 単純な関数名（メソッドではない）の場合のみ
        if (func_name.find("__") == std::string::npos && func_name.find("<") == std::string::npos) {
            simple_rewrite_map[func_name] = specialized_name;
        }
    }

    // 全関数の呼び出しを書き換え
    for (auto& func : program.functions) {
        if (!func)
            continue;

        for (auto& block : func->basic_blocks) {
            if (!block || !block->terminator)
                continue;

            if (block->terminator->kind == MirTerminator::Call) {
                auto& call_data = std::get<MirTerminator::CallData>(block->terminator->data);

                if (!call_data.func || call_data.func->kind != MirOperand::FunctionRef)
                    continue;

                auto& func_name = std::get<std::string>(call_data.func->data);

                // 0. ポインタ型を含む関数名を正規化
                // PtrContainer__*int__init -> PtrContainer__ptr_int__init
                // 再帰的に __* を __ptr_ に置換（ネストしたポインタ対応）
                {
                    std::string normalized = func_name;
                    size_t pos = 0;
                    while ((pos = normalized.find("__*", pos)) != std::string::npos) {
                        normalized.replace(pos, 3, "__ptr_");
                        pos += 6;  // "__ptr_".length()
                    }
                    // <*type> 形式も正規化
                    pos = 0;
                    while ((pos = normalized.find("<*", pos)) != std::string::npos) {
                        normalized.replace(pos, 2, "<ptr_");
                        pos += 5;  // "<ptr_".length()
                    }
                    if (normalized != func_name) {
                        func_name = normalized;
                        debug_msg("MONO", "Normalized pointer type in call: " + func_name);
                    }
                }

                // 1. 単純なジェネリック関数呼び出し（create_node -> create_node__int）
                auto simple_it = simple_rewrite_map.find(func_name);
                if (simple_it != simple_rewrite_map.end()) {
                    func_name = simple_it->second;
                    continue;
                }

                // 2. デストラクタ呼び出し（XXX__dtor形式）の書き換え
                // Vector__dtor を Vector__TrackedObject__dtor に書き換える
                if (func_name.size() > 6 && func_name.substr(func_name.size() - 6) == "__dtor") {
                    // デストラクタ呼び出しを検出
                    std::string base_type = func_name.substr(0, func_name.size() - 6);

                    // 引数からポインタ型を取得し、型名を推論
                    if (!call_data.args.empty() && call_data.args[0]) {
                        // 引数はポインタ型のはずなので、Place型から型情報を取得
                        if (call_data.args[0]->kind == MirOperand::Copy ||
                            call_data.args[0]->kind == MirOperand::Move) {
                            const auto& place = std::get<MirPlace>(call_data.args[0]->data);
                            LocalId local_id = place.local;

                            // ローカル変数の型を取得
                            if (local_id < func->locals.size()) {
                                const auto& local_type = func->locals[local_id].type;
                                if (local_type && local_type->kind == hir::TypeKind::Pointer) {
                                    // ポインタの要素型を取得
                                    const auto& elem_type = local_type->element_type;
                                    if (elem_type && !elem_type->name.empty()) {
                                        std::string actual_type = elem_type->name;
                                        // ネストジェネリック型名の正規化（Vector<int> →
                                        // Vector__int）
                                        if (actual_type.find('<') != std::string::npos) {
                                            std::string result;
                                            for (char c : actual_type) {
                                                if (c == '<' || c == '>') {
                                                    if (c == '<')
                                                        result += "__";
                                                } else if (c == ',' || c == ' ') {
                                                } else {
                                                    result += c;
                                                }
                                            }
                                            actual_type = result;
                                        }
                                        // 特殊化された型名からデストラクタ名を構築
                                        std::string specialized_dtor = actual_type + "__dtor";

                                        // MIRに特殊化デストラクタが存在するか確認
                                        bool found = false;
                                        for (const auto& mir_func : program.functions) {
                                            if (mir_func && mir_func->name == specialized_dtor) {
                                                found = true;
                                                break;
                                            }
                                        }

                                        if (found && specialized_dtor != func_name) {
                                            debug_msg("MONO",
                                                      "Rewriting destructor call: " + func_name +
                                                          " -> " + specialized_dtor);
                                            func_name = specialized_dtor;
                                            continue;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // 3. Container<int>__print のような形式を検出
                auto pos = func_name.find("<");
                if (pos == std::string::npos)
                    continue;

                auto end_pos = func_name.find(">__");
                if (end_pos == std::string::npos)
                    continue;

                // 型引数を抽出
                std::string type_args_str = func_name.substr(pos + 1, end_pos - pos - 1);

                // 複数型引数をパース（カンマで分割）
                std::vector<std::string> type_args;
                size_t start = 0;
                size_t comma_pos;
                while ((comma_pos = type_args_str.find(", ", start)) != std::string::npos) {
                    type_args.push_back(type_args_str.substr(start, comma_pos - start));
                    start = comma_pos + 2;
                }
                type_args.push_back(type_args_str.substr(start));

                // 元のジェネリック関数名を再構築
                std::string base_name = func_name.substr(0, pos);           // "Container"
                std::string method_suffix = func_name.substr(end_pos + 1);  // "__print"

                // Container<T>__print のような形式に変換
                std::string generic_func_name = base_name + "<";
                // 型パラメータ名は不明なので、T, U, V... と仮定
                const char* param_names[] = {"T", "U", "V", "W"};
                for (size_t i = 0; i < type_args.size(); ++i) {
                    if (i > 0)
                        generic_func_name += ", ";
                    generic_func_name += (i < 4) ? param_names[i] : "T" + std::to_string(i);
                }
                generic_func_name += ">" + method_suffix;

                // rewrite_mapから特殊化関数名を取得
                auto key = std::make_pair(generic_func_name, type_args);
                auto it = rewrite_map.find(key);
                if (it != rewrite_map.end()) {
                    func_name = it->second;
                    debug_msg("MONO", "Rewrote call in " + func->name + ": " +
                                          std::get<std::string>(call_data.func->data) + " -> " +
                                          func_name);
                } else {
                    // フォールバック: Container<int>__ctor_1 -> Container__int__ctor_1 に直接変換
                    // 型パラメータ名がT以外（Vなど）の場合に対応
                    std::string args_str;
                    for (const auto& arg : type_args) {
                        args_str += "__" + normalize_type_arg(arg);
                    }
                    std::string direct_name = base_name + args_str + method_suffix;

                    // MIRに特殊化関数が存在するか確認
                    bool found = false;
                    for (const auto& mir_func : program.functions) {
                        if (mir_func && mir_func->name == direct_name) {
                            found = true;
                            break;
                        }
                    }
                    if (found) {
                        func_name = direct_name;
                        debug_msg("MONO", "Rewrote call (fallback) in " + func->name + ": " +
                                              std::get<std::string>(call_data.func->data) + " -> " +
                                              func_name);
                    }
                }
            }
        }
    }
}

}  // namespace cm::mir
