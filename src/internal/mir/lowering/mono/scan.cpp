// 単相化 - ジェネリック呼び出しのスキャンと型引数の推論

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

// ジェネリック関数呼び出しをスキャン
void Monomorphization::scan_generic_calls(
    MirFunction* func, const std::unordered_set<std::string>& generic_funcs,
    const std::unordered_map<std::string, const hir::HirFunction*>& hir_functions,
    std::map<std::pair<std::string, std::vector<std::string>>,
             std::vector<std::tuple<std::string, size_t>>>& needed) {
    if (!func)
        return;

    // 各ブロックの終端命令をチェック
    for (size_t block_idx = 0; block_idx < func->basic_blocks.size(); ++block_idx) {
        auto& block = func->basic_blocks[block_idx];
        if (!block || !block->terminator)
            continue;

        if (block->terminator->kind == MirTerminator::Call) {
            auto& call_data = std::get<MirTerminator::CallData>(block->terminator->data);

            // 関数名を取得
            if (!call_data.func || call_data.func->kind != MirOperand::FunctionRef)
                continue;

            const auto& func_name = std::get<std::string>(call_data.func->data);

            // ジェネリック関数かチェックまず直接チェック
            if (generic_funcs.count(func_name) > 0) {
                // 型引数を推論
                auto it = hir_functions.find(func_name);
                if (it != hir_functions.end()) {
                    auto type_args = infer_type_args(func, call_data, it->second);
                    if (!type_args.empty()) {
                        auto key = std::make_pair(func_name, type_args);
                        needed[key].push_back(std::make_tuple(func->name, block_idx));
                        debug_msg("MONO", "Scanned call in " + func->name + " to " + func_name +
                                              " with type args: " + type_args[0]);
                    } else {
                        debug_msg("MONO", "WARNING: Could not infer type args for " + func_name +
                                              " in " + func->name);
                    }
                }
                continue;
            }

            // func_name が "Container<int>__print" のような形式の場合
            // "Container<T>__print" にマッチするジェネリック関数を探す
            for (const auto& generic_name : generic_funcs) {
                // generic_name = "Container<T>__print" func_name = "Container<int>__print"
                // パターンマッチングで型引数を抽出
                auto pos = generic_name.find("<");
                if (pos == std::string::npos)
                    continue;

                auto end_pos = generic_name.find(">__");
                if (end_pos == std::string::npos)
                    continue;

                std::string base_name = generic_name.substr(0, pos);  // "Container"
                std::string method_suffix =
                    generic_name.substr(end_pos + 2);  // "__print" (skip ">_")

                // func_nameも同じパターンかチェック
                auto func_pos = func_name.find("<");
                if (func_pos == std::string::npos)
                    continue;
                if (func_name.substr(0, func_pos) != base_name)
                    continue;

                auto func_end_pos = func_name.find(">__");
                if (func_end_pos == std::string::npos)
                    continue;

                std::string func_method_suffix = func_name.substr(func_end_pos + 2);  // "__print"
                if (func_method_suffix != method_suffix)
                    continue;

                // 型引数を抽出
                std::string type_arg = func_name.substr(func_pos + 1, func_end_pos - func_pos - 1);

                // HIR関数を取得
                auto it = hir_functions.find(generic_name);
                if (it == hir_functions.end())
                    continue;

                // 特殊化が必要な呼び出しを記録
                std::vector<std::string> type_args = {type_arg};
                auto key = std::make_pair(generic_name, type_args);
                needed[key].push_back(std::make_tuple(func->name, block_idx));

                debug_msg("MONO", "Found call to " + func_name + " matching generic " +
                                      generic_name + " with type arg: " + type_arg);
                break;
            }

            // func_name が "HashMap<int, int>__ctor_1" や "Pair<int, int>__dtor" のような形式の場合
            // "HashMap<K, V>__ctor_1" / "Pair<K, V>__dtor" にマッチするジェネリック関数を探す
            // パターン: Base<TypeArg1, TypeArg2>__ctor_N -> Base<K, V>__ctor_N
            for (const auto& generic_name : generic_funcs) {
                // コンストラクタ/デストラクタのサフィックスをチェック
                auto ctor_pos = generic_name.find(">__ctor");
                auto dtor_pos = generic_name.find(">__dtor");
                if (ctor_pos == std::string::npos && dtor_pos == std::string::npos)
                    continue;

                auto suffix_pos = (ctor_pos != std::string::npos) ? ctor_pos : dtor_pos;
                std::string ctor_suffix =
                    generic_name.substr(suffix_pos + 1);  // "__ctor_1" or "__dtor"

                // generic_name から基本名を抽出: "HashMap<K, V>" -> "HashMap"
                auto angle_pos = generic_name.find("<");
                if (angle_pos == std::string::npos)
                    continue;
                std::string base_name = generic_name.substr(0, angle_pos);  // "HashMap"

                // generic_nameから型パラメータを抽出
                std::string generic_params_str =
                    generic_name.substr(angle_pos + 1, suffix_pos - angle_pos - 1);
                std::vector<std::string> generic_params = split_type_args(generic_params_str);

                // func_name が同じ基本名とサフィックスを持つかチェック
                // func_name = "HashMap<int, int>__ctor_1"
                auto func_angle_pos = func_name.find("<");
                if (func_angle_pos == std::string::npos)
                    continue;
                if (func_name.substr(0, func_angle_pos) != base_name)
                    continue;

                auto func_suffix_pos = func_name.find(">__ctor");
                if (func_suffix_pos == std::string::npos)
                    func_suffix_pos = func_name.find(">__dtor");
                if (func_suffix_pos == std::string::npos)
                    continue;

                std::string func_suffix = func_name.substr(func_suffix_pos + 1);
                if (func_suffix != ctor_suffix)
                    continue;

                // 型引数を抽出: "HashMap<int, int>__ctor_1" -> "int, int"
                std::string type_arg_str =
                    func_name.substr(func_angle_pos + 1, func_suffix_pos - func_angle_pos - 1);
                std::vector<std::string> type_args = split_type_args(type_arg_str);

                // 型パラメータ数のチェック
                if (type_args.size() != generic_params.size())
                    continue;

                // HIR関数を取得
                auto it = hir_functions.find(generic_name);
                if (it == hir_functions.end())
                    continue;

                // 特殊化が必要な呼び出しを記録
                auto key = std::make_pair(generic_name, type_args);
                needed[key].push_back(std::make_tuple(func->name, block_idx));

                // デバッグ出力
                std::string type_args_debug;
                for (const auto& arg : type_args) {
                    if (!type_args_debug.empty())
                        type_args_debug += ", ";
                    type_args_debug += arg;
                }
                debug_msg("MONO", "Found generic ctor/dtor call to " + func_name +
                                      " matching generic " + generic_name + " with type args: [" +
                                      type_args_debug + "]");
                break;
            }
            // "Vector<T>__init" や "HashMap<K, V>__put" にマッチするジェネリック関数を探す
            // パターン: Base__TypeArg1__TypeArg2__method -> Base<T, U>__method
            for (const auto& generic_name : generic_funcs) {
                // generic_name = "Vector<T>__init" または "HashMap<K, V>__put" func_name = "Vector__int__init" または "HashMap__int__int__put"

                auto angle_pos = generic_name.find("<");
                if (angle_pos == std::string::npos)
                    continue;

                auto angle_close = generic_name.find(">__");
                if (angle_close == std::string::npos)
                    continue;

                std::string base_name = generic_name.substr(0, angle_pos);  // "Vector" or "HashMap"
                std::string method_name = generic_name.substr(angle_close + 3);  // "init" or "put"

                // func_nameがBase__で始まるかチェック
                if (func_name.substr(0, base_name.length() + 2) != base_name + "__")
                    continue;

                // generic_nameから型パラメータの数を取得
                std::string generic_params_str =
                    generic_name.substr(angle_pos + 1, angle_close - angle_pos - 1);
                std::vector<std::string> generic_params = split_type_args(generic_params_str);
                size_t num_params = generic_params.size();

                // func_nameからメソッド名と型引数を抽出
                // "HashMap__int__int__put" -> メソッド名 "put", 型引数 ["int", "int"]
                std::string remaining =
                    func_name.substr(base_name.length() + 2);  // "int__int__put"

                // remainingを__で分割
                std::vector<std::string> parts;
                size_t pos = 0;
                while (pos < remaining.size()) {
                    auto next = remaining.find("__", pos);
                    if (next == std::string::npos) {
                        parts.push_back(remaining.substr(pos));
                        break;
                    }
                    parts.push_back(remaining.substr(pos, next - pos));
                    pos = next + 2;
                }

                // 型引数の数 + メソッド名の数が必要
                if (parts.size() < num_params + 1)
                    continue;

                // 最後の部分がメソッド名
                std::string func_method = parts.back();
                if (func_method != method_name)
                    continue;

                // メソッド名を除いた残りの部分を型引数として構築
                // ネストジェネリクス対応: Vector__Vector__int__dtor -> type_args = [Vector__int] remaining = "Vector__int__dtor" (base_name "Vector" は既に除去済み)
                // parts = [Vector, int, dtor]
                // メソッド名 "dtor" を除いた全てを1つの型引数として連結 -> "Vector__int"
                std::vector<std::string> type_args;
                size_t type_parts_count = parts.size() - 1;  // メソッド名を除く

                if (type_parts_count > 0 && num_params == 1) {
                    // 型パラメータが1つの場合：メソッド名以外の全部を1つの型引数として連結
                    std::string arg;
                    for (size_t j = 0; j < type_parts_count; ++j) {  // j=0から開始
                        if (!arg.empty())
                            arg += "__";
                        arg += parts[j];
                    }
                    type_args.push_back(arg);
                } else if (type_parts_count >= num_params) {
                    // 複数型パラメータの場合：各型パラメータに1つずつ割り当て
                    for (size_t i = 0; i < num_params; ++i) {
                        type_args.push_back(parts[i]);
                    }
                }

                // HIR関数を取得
                auto it = hir_functions.find(generic_name);
                if (it == hir_functions.end())
                    continue;

                // 特殊化が必要な呼び出しを記録
                auto key = std::make_pair(generic_name, type_args);
                needed[key].push_back(std::make_tuple(func->name, block_idx));

                // デバッグ出力
                std::string type_args_debug;
                for (const auto& arg : type_args) {
                    if (!type_args_debug.empty())
                        type_args_debug += ", ";
                    type_args_debug += arg;
                }
                debug_msg("MONO", "Found mangled call to " + func_name + " matching generic " +
                                      generic_name + " with type args: [" + type_args_debug + "]");
                break;
            }
        }
    }
}

// 引数の型から型パラメータを推論
std::vector<std::string> Monomorphization::infer_type_args(const MirFunction* caller,
                                                           const MirTerminator::CallData& call_data,
                                                           const hir::HirFunction* callee) {
    std::vector<std::string> result;
    if (!callee || callee->generic_params.empty())
        return result;

    // 型パラメータ名 -> 推論された型のマッピング
    std::unordered_map<std::string, std::string> inferred_map;

    // 各パラメータから型を推論
    for (size_t i = 0; i < callee->params.size() && i < call_data.args.size(); ++i) {
        const auto& param = callee->params[i];
        if (!param.type)
            continue;

        // 引数の型を取得
        std::string arg_type_name;
        const auto& arg = call_data.args[i];
        if (arg && arg->kind == MirOperand::Copy) {
            if (auto* place = std::get_if<MirPlace>(&arg->data)) {
                if (place->local < caller->locals.size()) {
                    auto& local = caller->locals[place->local];
                    arg_type_name = get_type_name(local.type);
                }
            }
        } else if (arg && arg->kind == MirOperand::Constant) {
            if (auto* constant = std::get_if<MirConstant>(&arg->data)) {
                arg_type_name = get_type_name(constant->type);
            }
        }

        if (arg_type_name.empty())
            continue;

        // 推論結果が型パラメータ名そのもの（T = T）にならないようにするガード。
        // 呼び出し元の一時がジェネリック型のまま（戻り値先のT等）だと無意味な自己推論になり、
        // 無置換の特殊化（first_of__T）が生成されて要素アクセスが不定値になっていた
        auto is_generic_param_name = [&](const std::string& name) {
            for (const auto& gp : callee->generic_params) {
                if (name == gp.name) {
                    return true;
                }
            }
            return false;
        };

        if (is_generic_param_name(arg_type_name))
            continue;

        // 1. 単純な型パラメータの場合（T → int）
        for (const auto& generic_param : callee->generic_params) {
            if (param.type->name == generic_param.name) {
                inferred_map[generic_param.name] = arg_type_name;
                debug_msg("MONO", "Inferred " + generic_param.name + " = " + arg_type_name +
                                      " from simple param");
            }
        }

        // 1b. スライス/配列パラメータの場合（T[] → int[] から T = int）
        if (param.type->kind == hir::TypeKind::Array && param.type->element_type) {
            for (const auto& generic_param : callee->generic_params) {
                if (param.type->element_type->name == generic_param.name &&
                    arg_type_name.size() > 2 &&
                    arg_type_name.compare(arg_type_name.size() - 2, 2, "[]") == 0) {
                    std::string elem_name = arg_type_name.substr(0, arg_type_name.size() - 2);
                    if (!elem_name.empty() && !is_generic_param_name(elem_name)) {
                        inferred_map[generic_param.name] = elem_name;
                        debug_msg("MONO", "Inferred " + generic_param.name + " = " + elem_name +
                                              " from slice param");
                    }
                }
            }
        }

        // 2. ジェネリック構造体パラメータの場合（Pair<T, U> → Pair__int__string）
        // または ポインタ型のelement_typeがジェネリック構造体の場合（Node<T>* → Node__Item*）
        hir::TypePtr struct_type = param.type;
        std::string struct_arg_type_name = arg_type_name;

        // ポインタ型の場合、element_typeを使用
        if (param.type->kind == hir::TypeKind::Pointer && param.type->element_type) {
            struct_type = param.type->element_type;
            // 引数の型名からも*を除去
            if (!struct_arg_type_name.empty() && struct_arg_type_name.back() == '*') {
                struct_arg_type_name.pop_back();
            }
        }

        if (struct_type && !struct_type->type_args.empty() && hir_struct_defs) {
            // パラメータ型の構造体定義を取得
            auto struct_it = hir_struct_defs->find(struct_type->name);
            if (struct_it != hir_struct_defs->end() && struct_it->second) {
                // 引数の型名からtype_argsを抽出（Pair__int__string → [int, string]）
                std::string base_name = struct_type->name;
                size_t underscore_pos = struct_arg_type_name.find("__");

                if (underscore_pos != std::string::npos &&
                    struct_arg_type_name.substr(0, underscore_pos) == base_name) {
                    // 型引数を抽出
                    std::vector<std::string> extracted_args;
                    std::string remaining = struct_arg_type_name.substr(underscore_pos + 2);

                    size_t start = 0;
                    while (true) {
                        size_t next_pos = remaining.find("__", start);
                        if (next_pos != std::string::npos) {
                            extracted_args.push_back(remaining.substr(start, next_pos - start));
                            start = next_pos + 2;
                        } else {
                            extracted_args.push_back(remaining.substr(start));
                            break;
                        }
                    }

                    // 型引数とジェネリックパラメータをマッチング
                    for (size_t j = 0;
                         j < struct_type->type_args.size() && j < extracted_args.size(); ++j) {
                        const auto& type_arg = struct_type->type_args[j];
                        if (type_arg) {
                            // このtype_argがジェネリックパラメータ名なら推論
                            for (const auto& generic_param : callee->generic_params) {
                                if (type_arg->name == generic_param.name) {
                                    inferred_map[generic_param.name] = extracted_args[j];
                                    debug_msg("MONO", "Inferred " + generic_param.name + " = " +
                                                          extracted_args[j] +
                                                          " from struct param " +
                                                          struct_type->name);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // 3. 戻り値型から推論（Item got = get_data(node) → T = Item）
    if (callee->return_type && call_data.destination) {
        // 戻り値型がジェネリックパラメータの場合
        for (const auto& generic_param : callee->generic_params) {
            if (callee->return_type->name == generic_param.name) {
                // destination（呼び出し結果の格納先）からローカル変数の型を取得
                if (call_data.destination->local < caller->locals.size()) {
                    const auto& dest_local = caller->locals[call_data.destination->local];
                    if (dest_local.type) {
                        std::string dest_type_name = get_type_name(dest_local.type);
                        // 格納先がジェネリック型のまま（T等）の自己推論は無意味なので除外する
                        bool dest_is_generic_name = false;
                        for (const auto& gp : callee->generic_params) {
                            if (dest_type_name == gp.name) {
                                dest_is_generic_name = true;
                                break;
                            }
                        }
                        if (!dest_type_name.empty() && !dest_is_generic_name &&
                            inferred_map.find(generic_param.name) == inferred_map.end()) {
                            inferred_map[generic_param.name] = dest_type_name;
                            debug_msg("MONO", "Inferred " + generic_param.name + " = " +
                                                  dest_type_name + " from return type");
                        }
                    }
                }
            }
        }
    }

    // 各型パラメータの推論結果を収集
    for (const auto& generic_param : callee->generic_params) {
        auto it = inferred_map.find(generic_param.name);
        if (it != inferred_map.end()) {
            result.push_back(it->second);
        } else {
            // 推論できなかった場合、デフォルトとしてintを使用
            result.push_back("int");
            debug_msg("MONO",
                      "WARNING: Could not infer " + generic_param.name + ", defaulting to int");
        }
    }

    return result;
}

}  // namespace cm::mir
