#include "../../common/debug.hpp"
#include "monomorphization.hpp"
#include "monomorphization_utils.hpp"

namespace cm::mir {

// デバッグ出力用
#ifdef CM_DEBUG_MONOMORPHIZATION
#include <iostream>
#define MONO_DEBUG(msg) std::cerr << "[MONO] " << msg << std::endl
#else
#define MONO_DEBUG(msg)
#endif

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

            // ジェネリック関数かチェック
            // まず直接チェック
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
                // generic_name = "Container<T>__print"
                // func_name = "Container<int>__print"
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

        // 1. 単純な型パラメータの場合（T → int）
        for (const auto& generic_param : callee->generic_params) {
            if (param.type->name == generic_param.name) {
                inferred_map[generic_param.name] = arg_type_name;
                debug_msg("MONO", "Inferred " + generic_param.name + " = " + arg_type_name +
                                      " from simple param");
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
                        if (!dest_type_name.empty() &&
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

// 型内の型パラメータを再帰的に置換するヘルパー関数
static hir::TypePtr substitute_type_in_type(
    const hir::TypePtr& type, const std::unordered_map<std::string, hir::TypePtr>& type_subst,
    Monomorphization* mono) {
    if (!type)
        return nullptr;

    // 0. type_argsを再帰的に置換（重要: T -> int を正しく処理）
    std::vector<hir::TypePtr> substituted_type_args;
    bool type_args_changed = false;
    for (const auto& arg : type->type_args) {
        if (arg) {
            auto substituted_arg = substitute_type_in_type(arg, type_subst, mono);
            substituted_type_args.push_back(substituted_arg);
            if (substituted_arg != arg ||
                (substituted_arg && arg && substituted_arg->name != arg->name)) {
                type_args_changed = true;
            }
        } else {
            substituted_type_args.push_back(nullptr);
        }
    }

    // 1. 単純な型パラメータの場合（T → int）
    auto it = type_subst.find(type->name);
    if (it != type_subst.end()) {
        return it->second;
    }

    // 1.5 type_argsが置換された場合、新しい構造体型を作成
    // ただし、既にマングリング済みの名前（__を含む）はスキップ
    if (type_args_changed &&
        (type->kind == hir::TypeKind::Struct || type->kind == hir::TypeKind::Generic)) {
        // 既にマングリング済みの名前の場合はスキップ（二重マングリング防止）
        if (type->name.find("__") != std::string::npos) {
            // 既にマングリング済み: そのまま元の名前を使用して新しい型を作成
            auto new_type = std::make_shared<hir::Type>(hir::TypeKind::Struct);
            new_type->name = type->name;
            new_type->type_args = substituted_type_args;
            return new_type;
        }

        auto new_type = std::make_shared<hir::Type>(hir::TypeKind::Struct);
        // 新しい名前を生成（Node -> Node__Item）
        std::string new_name = type->name;
        for (const auto& arg : substituted_type_args) {
            if (arg) {
                new_name += "__" + get_type_name(arg);
            }
        }
        new_type->name = new_name;
        new_type->type_args = substituted_type_args;
        // 注: 構造体の登録は呼び出し元で行う
        return new_type;
    }

    // 2. ポインタ型の場合（Container<T>* → Container__int*）
    if (type->kind == hir::TypeKind::Pointer) {
        // まずelement_typeをチェック（より信頼性が高い）
        if (type->element_type) {
            auto substituted_elem = substitute_type_in_type(type->element_type, type_subst, mono);
            if (substituted_elem && (substituted_elem != type->element_type ||
                                     substituted_elem->name != type->element_type->name)) {
                auto new_ptr_type = std::make_shared<hir::Type>(hir::TypeKind::Pointer);
                new_ptr_type->element_type = substituted_elem;
                new_ptr_type->name =
                    substituted_elem->name.empty() ? "" : (substituted_elem->name + "*");
                // element_typeにtype_argsがない場合、元の型から継承（重要!）
                if (substituted_elem->type_args.empty() && !type->element_type->type_args.empty()) {
                    // 元のtype_argsを置換して設定
                    for (const auto& orig_arg : type->element_type->type_args) {
                        if (orig_arg) {
                            auto subst_arg = substitute_type_in_type(orig_arg, type_subst, mono);
                            if (subst_arg) {
                                new_ptr_type->element_type->type_args.push_back(subst_arg);
                            }
                        }
                    }
                }
                debug_msg("MONO", "Substituted pointer element_type: " +
                                      (type->element_type ? type->element_type->name : "null") +
                                      " -> " +
                                      (substituted_elem ? substituted_elem->name : "null"));
                return new_ptr_type;
            }
        }

        // フォールバック: 名前から推測（旧ロジック）
        std::string pointed_name = type->name;
        if (!pointed_name.empty() && pointed_name.back() == '*') {
            pointed_name.pop_back();  // '*'を削除

            // 指される型を作成して再帰的に置換
            auto pointed_type = std::make_shared<hir::Type>(hir::TypeKind::Struct);
            pointed_type->name = pointed_name;
            auto substituted_pointed = substitute_type_in_type(pointed_type, type_subst, mono);

            if (substituted_pointed && substituted_pointed->name != pointed_name) {
                auto new_ptr_type = std::make_shared<hir::Type>(hir::TypeKind::Pointer);
                new_ptr_type->name = substituted_pointed->name + "*";
                return new_ptr_type;
            }
        }
    }

    // 3. 構造体名に型パラメータが埋め込まれている場合（Container__T → Container__int）
    if (type->kind == hir::TypeKind::Struct || type->kind == hir::TypeKind::TypeAlias) {
        std::string type_name = type->name;
        size_t underscore_pos = type_name.find("__");

        if (underscore_pos != std::string::npos) {
            std::string base_name = type_name.substr(0, underscore_pos);
            std::string params_str = type_name.substr(underscore_pos + 2);

            // 型パラメータを "__" で分割して置換
            std::vector<std::string> new_params;
            bool any_substituted = false;
            size_t start = 0;

            while (true) {
                size_t next_pos = params_str.find("__", start);
                std::string param;
                if (next_pos != std::string::npos) {
                    param = params_str.substr(start, next_pos - start);
                } else {
                    param = params_str.substr(start);
                }

                // この型パラメータを置換できるか
                auto subst_it = type_subst.find(param);
                if (subst_it != type_subst.end()) {
                    new_params.push_back(get_type_name(subst_it->second));
                    any_substituted = true;
                } else {
                    new_params.push_back(param);
                }

                if (next_pos == std::string::npos)
                    break;
                start = next_pos + 2;
            }

            if (any_substituted) {
                // 新しい構造体名を生成
                std::string new_name = base_name;
                std::vector<hir::TypePtr> resolved_type_args;
                for (const auto& p : new_params) {
                    new_name += "__" + p;
                    // type_argsを設定（LLVMコード生成でマングリング名生成に必要）
                    auto arg_type = std::make_shared<hir::Type>(hir::TypeKind::Struct);
                    arg_type->name = p;
                    // プリミティブ型の場合はTypeKindを設定
                    if (p == "int")
                        arg_type->kind = hir::TypeKind::Int;
                    else if (p == "uint")
                        arg_type->kind = hir::TypeKind::UInt;
                    else if (p == "long")
                        arg_type->kind = hir::TypeKind::Long;
                    else if (p == "ulong")
                        arg_type->kind = hir::TypeKind::ULong;
                    else if (p == "float")
                        arg_type->kind = hir::TypeKind::Float;
                    else if (p == "double")
                        arg_type->kind = hir::TypeKind::Double;
                    else if (p == "bool")
                        arg_type->kind = hir::TypeKind::Bool;
                    else if (p == "char")
                        arg_type->kind = hir::TypeKind::Char;
                    else if (p == "string")
                        arg_type->kind = hir::TypeKind::String;
                    resolved_type_args.push_back(arg_type);
                }

                auto new_type = std::make_shared<hir::Type>(hir::TypeKind::Struct);
                new_type->name = new_name;
                new_type->type_args = resolved_type_args;
                return new_type;
            }
        }
    }

    // 4. 構造体名に型パラメータが<>で括られている場合（Container<T> → Container__int）
    // または Generic 型で型名にジェネリクス情報が含まれる場合
    if (type->kind == hir::TypeKind::Struct || type->kind == hir::TypeKind::TypeAlias ||
        type->kind == hir::TypeKind::Pointer || type->kind == hir::TypeKind::Generic) {
        std::string type_name = type->name;
        size_t angle_pos = type_name.find("<");

        if (angle_pos != std::string::npos) {
            std::string base_name = type_name.substr(0, angle_pos);  // "Container"
            size_t end_angle = type_name.rfind(">");
            if (end_angle != std::string::npos && end_angle > angle_pos) {
                std::string params_str =
                    type_name.substr(angle_pos + 1, end_angle - angle_pos - 1);  // "T"

                // 複数の型パラメータを処理（カンマ区切り）
                std::vector<std::string> new_params;
                bool any_substituted = false;
                size_t start = 0;

                while (true) {
                    size_t comma_pos = params_str.find(",", start);
                    std::string param;
                    if (comma_pos != std::string::npos) {
                        param = params_str.substr(start, comma_pos - start);
                    } else {
                        param = params_str.substr(start);
                    }

                    // 空白をトリム
                    while (!param.empty() && param.front() == ' ')
                        param.erase(0, 1);
                    while (!param.empty() && param.back() == ' ')
                        param.pop_back();

                    // この型パラメータを置換できるか
                    auto subst_it = type_subst.find(param);
                    if (subst_it != type_subst.end()) {
                        new_params.push_back(get_type_name(subst_it->second));
                        any_substituted = true;
                    } else {
                        new_params.push_back(param);
                    }

                    if (comma_pos == std::string::npos)
                        break;
                    start = comma_pos + 1;
                }

                if (any_substituted) {
                    // 新しい構造体名を生成（Container__int 形式）
                    std::string new_name = base_name;
                    std::vector<hir::TypePtr> resolved_type_args;
                    for (const auto& p : new_params) {
                        new_name += "__" + p;
                        // type_argsを設定（LLVMコード生成でマングリング名生成に必要）
                        auto arg_type = std::make_shared<hir::Type>(hir::TypeKind::Struct);
                        arg_type->name = p;
                        // プリミティブ型の場合はTypeKindを設定
                        if (p == "int")
                            arg_type->kind = hir::TypeKind::Int;
                        else if (p == "uint")
                            arg_type->kind = hir::TypeKind::UInt;
                        else if (p == "long")
                            arg_type->kind = hir::TypeKind::Long;
                        else if (p == "ulong")
                            arg_type->kind = hir::TypeKind::ULong;
                        else if (p == "float")
                            arg_type->kind = hir::TypeKind::Float;
                        else if (p == "double")
                            arg_type->kind = hir::TypeKind::Double;
                        else if (p == "bool")
                            arg_type->kind = hir::TypeKind::Bool;
                        else if (p == "char")
                            arg_type->kind = hir::TypeKind::Char;
                        else if (p == "string")
                            arg_type->kind = hir::TypeKind::String;
                        resolved_type_args.push_back(arg_type);
                    }

                    // 重要: モノモーフィック化後の型はStructになる
                    auto new_type = std::make_shared<hir::Type>(hir::TypeKind::Struct);
                    new_type->name = new_name;
                    new_type->type_args = resolved_type_args;
                    debug_msg("MONO", "Substituted angle-bracket type: " + type_name + " -> " +
                                          new_name + " (kind: Generic->Struct)");
                    return new_type;
                }
            }
        }

        // 5. Generic型で名前のみ（Container）の場合、type_argsから推論
        if (type->kind == hir::TypeKind::Generic && !type->name.empty() &&
            type->name.find("<") == std::string::npos) {
            // ジェネリック構造体名（Container）で、型引数がある場合
            // type_substからすべての型引数を適用
            std::string new_name = type->name;
            bool applied = false;
            for (const auto& [param_name, param_type] : type_subst) {
                (void)param_name;  // 使用しない
                new_name += "__" + get_type_name(param_type);
                applied = true;
            }
            if (applied) {
                // type_argsを設定
                std::vector<hir::TypePtr> resolved_type_args;
                for (const auto& [param_name, param_type] : type_subst) {
                    (void)param_name;
                    resolved_type_args.push_back(param_type);
                }
                auto new_type = std::make_shared<hir::Type>(hir::TypeKind::Struct);
                new_type->name = new_name;
                new_type->type_args = resolved_type_args;
                debug_msg("MONO", "Substituted generic type: " + type->name + " -> " + new_name);
                return new_type;
            }
        }
    }

    // 変更なしの場合は元の型を返す
    return type;
}

// ジェネリック関数の特殊化を生成
void Monomorphization::generate_generic_specializations(
    MirProgram& program,
    const std::unordered_map<std::string, const hir::HirFunction*>& hir_functions,
    const std::map<std::pair<std::string, std::vector<std::string>>,
                   std::vector<std::tuple<std::string, size_t>>>& needed) {
    // 生成済みの特殊化を追跡
    std::unordered_set<std::string> generated;

    for (const auto& [key, call_sites] : needed) {
        const auto& [func_name, type_args] = key;

        // 特殊化関数名を生成
        std::string specialized_name = make_specialized_name(func_name, type_args);

        // すでに生成済みならスキップ
        if (generated.count(specialized_name) > 0)
            continue;
        generated.insert(specialized_name);

        debug_msg("MONO", "Generating specialization: " + specialized_name);

        // 元のMIR関数を検索
        MirFunction* original_mir = nullptr;
        for (auto& func : program.functions) {
            if (func && func->name == func_name) {
                original_mir = func.get();
                break;
            }
        }

        if (!original_mir)
            continue;

        // HIR関数から型パラメータ名を取得
        auto hir_it = hir_functions.find(func_name);
        if (hir_it == hir_functions.end())
            continue;
        const auto* hir_func = hir_it->second;

        // 型置換マップを作成（TypePtr版）
        std::unordered_map<std::string, hir::TypePtr> type_subst;
        // 型名置換マップ（string版 - メソッド呼び出し書き換え用）
        std::unordered_map<std::string, std::string> type_name_subst;
        for (size_t i = 0; i < hir_func->generic_params.size() && i < type_args.size(); ++i) {
            const auto& param_name = hir_func->generic_params[i].name;
            type_subst[param_name] = make_type_from_name(type_args[i]);
            type_name_subst[param_name] = type_args[i];
            debug_msg("MONO", "Type substitution: " + param_name + " -> " + type_args[i]);
        }

        // 特殊化関数を生成（MIR関数をコピーして型を置換）
        auto specialized = std::make_unique<MirFunction>();
        specialized->name = specialized_name;
        specialized->entry_block = original_mir->entry_block;
        specialized->return_local = original_mir->return_local;
        specialized->arg_locals = original_mir->arg_locals;

        // ローカル変数をコピーして型を置換
        // ジェネリックimplメソッドの場合、self型を関数名から推測
        std::string inferred_self_type;
        auto angle_pos = func_name.find("<");
        auto dunder_pos = func_name.find(">__");
        if (angle_pos != std::string::npos && dunder_pos != std::string::npos) {
            // func_name = "Container<T>__get" -> base = "Container"
            std::string base_struct = func_name.substr(0, angle_pos);
            // 置換後の構造体名を生成: Container__int
            inferred_self_type = base_struct;
            for (const auto& arg : type_args) {
                inferred_self_type += "__" + arg;
            }
        }

        for (const auto& local : original_mir->locals) {
            LocalDecl new_local = local;
            if (new_local.type) {
                // selfパラメータで型名が空のPointer型の場合、推論した型を使用
                if (local.name == "self" && new_local.type->kind == hir::TypeKind::Pointer &&
                    new_local.type->name.empty() && !inferred_self_type.empty()) {
                    // Container__intへのポインタ型を作成
                    auto struct_type = std::make_shared<hir::Type>(hir::TypeKind::Struct);
                    struct_type->name = inferred_self_type;
                    auto new_ptr_type = std::make_shared<hir::Type>(hir::TypeKind::Pointer);
                    new_ptr_type->element_type = struct_type;
                    new_ptr_type->name = inferred_self_type + "*";
                    new_local.type = new_ptr_type;

                } else {
                    auto old_type = new_local.type;
                    new_local.type = substitute_type_in_type(new_local.type, type_subst, this);

                    // ✅ 置換後の型がジェネリック構造体の具象化を含む場合、構造体を生成
                    // 例: Node<T> -> Node<Item> の場合、Node__Itemを生成
                    auto ensure_struct_specialization = [&](const hir::TypePtr& t) {
                        if (!t)
                            return;
                        hir::TypePtr target = t;
                        // ポインタ型の場合は要素型をチェック
                        if (t->kind == hir::TypeKind::Pointer && t->element_type) {
                            target = t->element_type;
                        }
                        // 構造体型でマングリング済みの名前（__を含む）を持つ場合
                        if (target && target->kind == hir::TypeKind::Struct &&
                            target->name.find("__") != std::string::npos) {
                            // 基本名と型引数を抽出
                            auto pos = target->name.find("__");
                            std::string base_name = target->name.substr(0, pos);
                            std::vector<std::string> struct_type_args;
                            std::string remainder = target->name.substr(pos + 2);
                            size_t arg_pos = 0;
                            while (arg_pos < remainder.size()) {
                                auto next = remainder.find("__", arg_pos);
                                if (next == std::string::npos) {
                                    struct_type_args.push_back(remainder.substr(arg_pos));
                                    break;
                                }
                                struct_type_args.push_back(
                                    remainder.substr(arg_pos, next - arg_pos));
                                arg_pos = next + 2;
                            }
                            // 構造体特殊化を生成
                            if (!struct_type_args.empty()) {
                                generate_specialized_struct(program, base_name, struct_type_args);
                            }
                        }
                    };
                    ensure_struct_specialization(new_local.type);
                }
            }
            specialized->locals.push_back(new_local);
        }

        // 基本ブロックをコピー
        for (const auto& block : original_mir->basic_blocks) {
            if (!block)
                continue;

            auto new_block = std::make_unique<BasicBlock>(block->id);

            // 文をコピー（クローン関数を使用）
            for (const auto& stmt : block->statements) {
                if (stmt) {
                    new_block->statements.push_back(clone_statement(stmt));
                }
            }

            // 終端命令をコピー（型置換を適用してメソッド呼び出しを書き換え）
            if (block->terminator) {
                new_block->terminator =
                    clone_terminator_with_subst(block->terminator, type_name_subst);
            }

            specialized->basic_blocks.push_back(std::move(new_block));
        }

        // プロジェクション内の型を置換
        auto substitute_place_types = [&](MirPlace& place) {
            for (auto& proj : place.projections) {
                if (proj.result_type) {
                    proj.result_type = substitute_type_in_type(proj.result_type, type_subst, this);
                }
                if (proj.pointee_type) {
                    proj.pointee_type =
                        substitute_type_in_type(proj.pointee_type, type_subst, this);
                }
            }
            if (place.type) {
                place.type = substitute_type_in_type(place.type, type_subst, this);
            }
            if (place.pointee_type) {
                place.pointee_type = substitute_type_in_type(place.pointee_type, type_subst, this);
            }
        };

        auto substitute_operand_types = [&](MirOperandPtr& op) {
            if (!op)
                return;
            if (op->kind == MirOperand::Copy || op->kind == MirOperand::Move) {
                auto* place = std::get_if<MirPlace>(&op->data);
                if (place) {
                    substitute_place_types(*place);
                }
            }
            if (op->type) {
                op->type = substitute_type_in_type(op->type, type_subst, this);
            }
        };

        for (auto& block : specialized->basic_blocks) {
            if (!block)
                continue;
            for (auto& stmt : block->statements) {
                if (!stmt || stmt->kind != MirStatement::Assign)
                    continue;
                auto& assign_data = std::get<MirStatement::AssignData>(stmt->data);
                // place側のプロジェクション
                substitute_place_types(assign_data.place);

                // rvalue側のオペランド
                if (assign_data.rvalue) {
                    switch (assign_data.rvalue->kind) {
                        case MirRvalue::Use: {
                            auto& use_data = std::get<MirRvalue::UseData>(assign_data.rvalue->data);
                            substitute_operand_types(use_data.operand);
                            break;
                        }
                        case MirRvalue::BinaryOp: {
                            auto& bin_data =
                                std::get<MirRvalue::BinaryOpData>(assign_data.rvalue->data);
                            substitute_operand_types(bin_data.lhs);
                            substitute_operand_types(bin_data.rhs);
                            if (bin_data.result_type) {
                                bin_data.result_type =
                                    substitute_type_in_type(bin_data.result_type, type_subst, this);
                            }
                            break;
                        }
                        case MirRvalue::UnaryOp: {
                            auto& unary_data =
                                std::get<MirRvalue::UnaryOpData>(assign_data.rvalue->data);
                            substitute_operand_types(unary_data.operand);
                            break;
                        }
                        case MirRvalue::Ref: {
                            auto& ref_data = std::get<MirRvalue::RefData>(assign_data.rvalue->data);
                            substitute_place_types(ref_data.place);
                            break;
                        }
                        case MirRvalue::Cast: {
                            auto& cast_data =
                                std::get<MirRvalue::CastData>(assign_data.rvalue->data);
                            substitute_operand_types(cast_data.operand);
                            if (cast_data.target_type) {
                                cast_data.target_type = substitute_type_in_type(
                                    cast_data.target_type, type_subst, this);
                            }
                            break;
                        }
                        case MirRvalue::Aggregate: {
                            auto& agg_data =
                                std::get<MirRvalue::AggregateData>(assign_data.rvalue->data);
                            for (auto& operand : agg_data.operands) {
                                substitute_operand_types(operand);
                            }
                            break;
                        }
                        default:
                            break;
                    }
                }
            }
        }

        // メソッド呼び出しの self 引数に対する参照修正
        // 構造体メソッド呼び出しで、第1引数が値型の場合に &ref を追加
        for (auto& block : specialized->basic_blocks) {
            if (!block || !block->terminator)
                continue;
            if (block->terminator->kind != MirTerminator::Call)
                continue;

            auto& call_data = std::get<MirTerminator::CallData>(block->terminator->data);
            if (!call_data.func || call_data.func->kind != MirOperand::FunctionRef)
                continue;

            const auto& func_name_ref = std::get<std::string>(call_data.func->data);

            // 関数名が TypeName__method の形式かチェック
            auto dunder_pos = func_name_ref.find("__");
            if (dunder_pos == std::string::npos || call_data.args.empty())
                continue;

            // 型名を抽出
            std::string type_name = func_name_ref.substr(0, dunder_pos);

            // この型名に対応する構造体が存在するか確認（hir_struct_defsから）
            if (!hir_struct_defs || hir_struct_defs->find(type_name) == hir_struct_defs->end())
                continue;

            // 第1引数が値型（非ポインタ）かチェック
            auto& first_arg = call_data.args[0];
            if (!first_arg || first_arg->kind != MirOperand::Copy)
                continue;

            auto& place = std::get<MirPlace>(first_arg->data);
            if (place.local >= specialized->locals.size())
                continue;

            auto& local_type = specialized->locals[place.local].type;
            if (!local_type)
                continue;

            // 既にポインタ型ならスキップ
            if (local_type->kind == hir::TypeKind::Pointer)
                continue;

            // 構造体型またはエイリアス型であれば、参照を取る必要がある
            if (local_type->kind == hir::TypeKind::Struct ||
                local_type->kind == hir::TypeKind::TypeAlias || local_type->name == type_name ||
                local_type->name.find(type_name + "__") == 0) {
                // 新しいローカル変数を追加（ポインタ型）
                LocalId ref_id = static_cast<LocalId>(specialized->locals.size());
                std::string ref_name = "_ref_" + std::to_string(ref_id);
                auto ref_type = hir::make_pointer(local_type);
                specialized->locals.emplace_back(ref_id, ref_name, ref_type, false, false);

                // Ref文を追加
                auto ref_stmt = MirStatement::assign(MirPlace{ref_id},
                                                     MirRvalue::ref(place, false));  // 不変参照
                block->statements.push_back(std::move(ref_stmt));

                // 呼び出しの第1引数を参照に変更
                call_data.args[0] = MirOperand::copy(MirPlace{ref_id});

                debug_msg("MONO", "Added self-ref fixup for " + func_name_ref +
                                      " in specialized function " + specialized_name);
            }
        }

        // 特殊化関数内の自己再帰呼び出しを特殊化版に書き換え
        for (auto& block : specialized->basic_blocks) {
            if (!block || !block->terminator)
                continue;
            if (block->terminator->kind != MirTerminator::Call)
                continue;

            auto& call_data = std::get<MirTerminator::CallData>(block->terminator->data);
            if (!call_data.func || call_data.func->kind != MirOperand::FunctionRef)
                continue;

            auto& called_func_name = std::get<std::string>(call_data.func->data);

            // 自己再帰呼び出しのみを書き換え
            if (called_func_name == func_name) {
                call_data.func = MirOperand::function_ref(specialized_name);
                debug_msg("MONO",
                          "Rewrote recursive call: " + func_name + " -> " + specialized_name);
            }
        }

        program.functions.push_back(std::move(specialized));

        // 呼び出し箇所を書き換え
        for (const auto& [caller_name, block_idx] : call_sites) {
            for (auto& func : program.functions) {
                if (func && func->name == caller_name) {
                    if (block_idx < func->basic_blocks.size()) {
                        auto& block = func->basic_blocks[block_idx];
                        if (block && block->terminator &&
                            block->terminator->kind == MirTerminator::Call) {
                            auto& call_data =
                                std::get<MirTerminator::CallData>(block->terminator->data);
                            // 関数名を取得して比較
                            if (call_data.func && call_data.func->kind == MirOperand::FunctionRef) {
                                auto& current_func_name =
                                    std::get<std::string>(call_data.func->data);
                                if (current_func_name == func_name) {
                                    // 関数名を特殊化された名前に変更
                                    call_data.func = MirOperand::function_ref(specialized_name);
                                }
                            }
                        }
                    }
                    break;
                }
            }
        }
    }
}

// ジェネリック関数を削除
void Monomorphization::cleanup_generic_functions(
    MirProgram& program, const std::unordered_set<std::string>& generic_funcs) {
    // ジェネリック関数を削除（特殊化されたものに置き換えられたため）
    auto it = program.functions.begin();
    while (it != program.functions.end()) {
        if (*it && generic_funcs.count((*it)->name) > 0) {
            debug_msg("MONO", "Removing generic function: " + (*it)->name);
            it = program.functions.erase(it);
        } else {
            ++it;
        }
    }
}

// ============================================================
// 旧実装（インターフェース特殊化用）- 互換性のため残す
// ============================================================

void Monomorphization::scan_function_calls(
    MirFunction* func, const std::string& /* caller_name */,
    const std::unordered_map<std::string, const hir::HirFunction*>& /* hir_functions */,
    std::unordered_map<std::string, std::vector<std::tuple<std::string, size_t, std::string>>>&
    /* needed */) {
    if (!func)
        return;
    // 旧実装は使用しない
}

void Monomorphization::generate_specializations(
    MirProgram& /* program */,
    const std::unordered_map<std::string, const hir::HirFunction*>& /* hir_functions */,
    const std::unordered_map<
        std::string, std::vector<std::tuple<std::string, size_t, std::string>>>& /* needed */) {
    // 旧実装は使用しない
}

MirFunctionPtr Monomorphization::generate_specialized_function(
    const hir::HirFunction& /* original */, const std::string& /* actual_type */,
    size_t /* param_idx */) {
    return nullptr;  // 旧実装は使用しない
}

void Monomorphization::cleanup_generic_functions(
    MirProgram& /* program */,
    const std::unordered_map<
        std::string, std::vector<std::tuple<std::string, size_t, std::string>>>& /* needed */) {
    // 旧実装は使用しない
}

// ============================================================
// 構造体モノモーフィゼーション
// ============================================================

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

// ジェネリック構造体のモノモーフィゼーション
void Monomorphization::monomorphize_structs(MirProgram& program) {
    if (!hir_struct_defs)
        return;

    // 必要な構造体特殊化を収集
    // key: 特殊化構造体名, value: (基本名, 型引数リスト)
    std::map<std::string, std::pair<std::string, std::vector<std::string>>> needed;

    collect_struct_specializations(program, needed);

    if (needed.empty()) {
        debug_msg("MONO", "No struct specializations needed");
        return;
    }

    debug_msg("MONO", "Found " + std::to_string(needed.size()) + " struct specializations needed");

    // 特殊化構造体を生成
    for (const auto& [spec_name, info] : needed) {
        const auto& [base_name, type_args] = info;
        generate_specialized_struct(program, base_name, type_args);
    }

    // MIR内の型参照を更新
    update_type_references(program);
}

// MIR内の全型を走査し、必要な構造体特殊化を収集
void Monomorphization::collect_struct_specializations(
    MirProgram& program,
    std::map<std::string, std::pair<std::string, std::vector<std::string>>>& needed) {
    if (!hir_struct_defs || !hir_funcs)
        return;

    // ジェネリック構造体のリストを作成
    std::unordered_set<std::string> generic_structs;
    // 全ジェネリック型パラメータ名を収集
    std::unordered_set<std::string> all_generic_params;

    for (const auto& [name, st] : *hir_struct_defs) {
        if (st && !st->generic_params.empty()) {
            generic_structs.insert(name);
            for (const auto& param : st->generic_params) {
                all_generic_params.insert(param.name);
            }
            debug_msg("MONO", "Found generic struct: " + name + " with " +
                                  std::to_string(st->generic_params.size()) + " type params");
        }
    }

    // ジェネリック関数の型パラメータも収集
    std::unordered_set<std::string> generic_func_names;
    for (const auto& [name, func] : *hir_funcs) {
        if (func && !func->generic_params.empty()) {
            generic_func_names.insert(name);
            for (const auto& param : func->generic_params) {
                all_generic_params.insert(param.name);
            }
        }
    }

    if (generic_structs.empty())
        return;

    // 全関数のローカル変数の型を走査（ジェネリック関数はスキップ）
    for (const auto& func : program.functions) {
        if (!func)
            continue;

        // ジェネリック関数内のローカル変数はスキップ
        // （関数モノモーフィゼーション時に処理される）
        if (generic_func_names.count(func->name) > 0) {
            continue;
        }

        for (const auto& local : func->locals) {
            if (!local.type)
                continue;

            // 構造体型でtype_argsがある場合
            if ((local.type->kind == hir::TypeKind::Struct ||
                 local.type->kind == hir::TypeKind::TypeAlias) &&
                !local.type->type_args.empty() && generic_structs.count(local.type->name) > 0) {
                auto type_args = extract_type_args_strings(local.type);
                if (type_args.empty())
                    continue;

                // type_argsにジェネリック型パラメータが含まれている場合はスキップ
                bool has_generic_param = false;
                for (const auto& arg : type_args) {
                    if (all_generic_params.count(arg) > 0) {
                        has_generic_param = true;
                        break;
                    }
                }
                if (has_generic_param)
                    continue;

                std::string spec_name = make_specialized_struct_name(local.type->name, type_args);
                if (needed.find(spec_name) == needed.end()) {
                    needed[spec_name] = {local.type->name, type_args};
                    debug_msg("MONO", "Need struct specialization: " + spec_name);
                }
            }

            // 既にマングリング済みの構造体名（Node__intなど）を検出
            if ((local.type->kind == hir::TypeKind::Struct ||
                 local.type->kind == hir::TypeKind::TypeAlias) &&
                local.type->name.find("__") != std::string::npos) {
                // 基本名を抽出（Node__int -> Node）
                auto pos = local.type->name.find("__");
                std::string base_name = local.type->name.substr(0, pos);

                // 基本名がジェネリック構造体かチェック
                if (generic_structs.count(base_name) > 0) {
                    // 型引数を抽出（Node__int -> ["int"]）
                    std::vector<std::string> type_args;
                    std::string remainder = local.type->name.substr(pos + 2);

                    // __で区切られた型引数を抽出
                    size_t arg_pos = 0;
                    while (arg_pos < remainder.size()) {
                        auto next_pos = remainder.find("__", arg_pos);
                        if (next_pos == std::string::npos) {
                            type_args.push_back(remainder.substr(arg_pos));
                            break;
                        }
                        type_args.push_back(remainder.substr(arg_pos, next_pos - arg_pos));
                        arg_pos = next_pos + 2;
                    }

                    if (!type_args.empty()) {
                        std::string spec_name = local.type->name;
                        if (needed.find(spec_name) == needed.end()) {
                            needed[spec_name] = {base_name, type_args};
                        }
                    }
                }
            }
        }
    }
}

// 特殊化構造体を生成
void Monomorphization::generate_specialized_struct(MirProgram& program,
                                                   const std::string& base_name,
                                                   const std::vector<std::string>& type_args) {
    if (!hir_struct_defs)
        return;

    std::string spec_name = make_specialized_struct_name(base_name, type_args);

    // 既に生成済みならスキップ
    if (generated_struct_specializations.count(spec_name) > 0) {
        return;
    }

    // 元の構造体定義を取得
    auto it = hir_struct_defs->find(base_name);
    if (it == hir_struct_defs->end() || !it->second) {
        debug_msg("MONO", "WARNING: Base struct not found: " + base_name);
        return;
    }

    const hir::HirStruct* base_struct = it->second;

    // 型パラメータ→具体的な型のマッピングを作成
    std::unordered_map<std::string, hir::TypePtr> type_subst;
    for (size_t i = 0; i < base_struct->generic_params.size() && i < type_args.size(); ++i) {
        const auto& param_name = base_struct->generic_params[i].name;
        type_subst[param_name] = make_type_from_name(type_args[i]);
        debug_msg("MONO", "Struct type substitution: " + param_name + " -> " + type_args[i]);
    }

    // 特殊化構造体を生成
    auto mir_struct = std::make_unique<MirStruct>();
    mir_struct->name = spec_name;
    mir_struct->is_css = base_struct->is_css;

    // フィールドとレイアウトを計算
    uint32_t current_offset = 0;
    uint32_t max_align = 1;

    for (const auto& field : base_struct->fields) {
        MirStructField mir_field;
        mir_field.name = field.name;

        // フィールドの型を置換（再帰的に適用）
        hir::TypePtr field_type = field.type;
        if (field_type) {
            // ✅ substitute_type_in_typeを使用して再帰的に型を置換
            // これによりT=ItemのようなStruct型も正しく置換される
            field_type = substitute_type_in_type(field_type, type_subst, this);
        }
        mir_field.type = field_type;

        // 型のサイズとアライメントを取得
        uint32_t size = 8, align = 8;  // デフォルト
        if (field_type) {
            switch (field_type->kind) {
                case hir::TypeKind::Bool:
                case hir::TypeKind::Tiny:
                case hir::TypeKind::UTiny:
                case hir::TypeKind::Char:
                    size = 1;
                    align = 1;
                    break;
                case hir::TypeKind::Short:
                case hir::TypeKind::UShort:
                    size = 2;
                    align = 2;
                    break;
                case hir::TypeKind::Int:
                case hir::TypeKind::UInt:
                case hir::TypeKind::Float:
                    size = 4;
                    align = 4;
                    break;
                case hir::TypeKind::Long:
                case hir::TypeKind::ULong:
                case hir::TypeKind::Double:
                case hir::TypeKind::Pointer:
                    size = 8;
                    align = 8;
                    break;
                case hir::TypeKind::String:
                    size = 16;
                    align = 8;
                    break;
                default:
                    size = 8;
                    align = 8;
                    break;
            }
        }

        // アライメント調整
        if (current_offset % align != 0) {
            current_offset += align - (current_offset % align);
        }
        mir_field.offset = current_offset;
        current_offset += size;
        if (align > max_align)
            max_align = align;

        mir_struct->fields.push_back(std::move(mir_field));
        debug_msg("MONO", "  Field: " + field.name + " -> " +
                              (field_type ? hir::type_to_string(*field_type) : "unknown"));
    }

    // 最終的なサイズとアライメントを設定
    if (current_offset % max_align != 0) {
        current_offset += max_align - (current_offset % max_align);
    }
    mir_struct->size = current_offset;
    mir_struct->align = max_align;

    // プログラムに追加
    program.structs.push_back(std::move(mir_struct));
    generated_struct_specializations.insert(spec_name);

    debug_msg("MONO", "Generated specialized struct: " + spec_name +
                          " (size=" + std::to_string(current_offset) +
                          ", align=" + std::to_string(max_align) + ")");
}

// MIR内の型参照を更新（Pair → Pair__int など）
void Monomorphization::update_type_references(MirProgram& program) {
    if (!hir_struct_defs)
        return;

    // ジェネリック構造体のリスト
    std::unordered_set<std::string> generic_structs;
    // 各ジェネリック構造体の型パラメータ名のリスト
    std::unordered_map<std::string, std::vector<std::string>> struct_type_params;
    for (const auto& [name, st] : *hir_struct_defs) {
        if (st && !st->generic_params.empty()) {
            generic_structs.insert(name);
            std::vector<std::string> params;
            for (const auto& param : st->generic_params) {
                params.push_back(param.name);
            }
            struct_type_params[name] = params;
        }
    }

    // 全関数のローカル変数の型を更新
    for (auto& func : program.functions) {
        if (!func)
            continue;

        // まず、どの特殊化構造体が使用されているかを追跡
        // localId -> (base_struct_name, type_args)
        std::unordered_map<LocalId, std::pair<std::string, std::vector<std::string>>> struct_info;

        for (size_t i = 0; i < func->locals.size(); ++i) {
            auto& local = func->locals[i];
            if (!local.type)
                continue;

            // ジェネリック構造体型の場合
            if ((local.type->kind == hir::TypeKind::Struct ||
                 local.type->kind == hir::TypeKind::TypeAlias) &&
                !local.type->type_args.empty() && generic_structs.count(local.type->name) > 0) {
                auto type_args = extract_type_args_strings(local.type);
                if (!type_args.empty()) {
                    std::string spec_name =
                        make_specialized_struct_name(local.type->name, type_args);
                    struct_info[i] = {local.type->name, type_args};

                    // 型名を更新（type_argsはクリア）
                    local.type = std::make_shared<hir::Type>(hir::TypeKind::Struct);
                    local.type->name = spec_name;

                    debug_msg("MONO", "Updated type reference in " + func->name + ": " +
                                          local.name + " -> " + spec_name);
                }
            }

            // Option__T 形式の型名を処理（type_argsが空の場合）
            // これは型パラメータTが具体型に置換されるべきケース
            else if (local.type->kind == hir::TypeKind::Struct && local.type->type_args.empty()) {
                std::string type_name = local.type->name;
                size_t underscore_pos = type_name.find("__");
                if (underscore_pos != std::string::npos) {
                    std::string base_name = type_name.substr(0, underscore_pos);
                    std::string param_name = type_name.substr(underscore_pos + 2);

                    // ベース名がジェネリック構造体かチェック
                    if (generic_structs.count(base_name) > 0) {
                        // param_nameが型パラメータ名かチェック
                        auto params_it = struct_type_params.find(base_name);
                        if (params_it != struct_type_params.end()) {
                            for (const auto& type_param : params_it->second) {
                                if (param_name == type_param) {
                                    // これはまだ具体化されていない型
                                    // 関数の呼び出しコンテキストから具体型を推論する必要がある
                                    // 現時点では警告を出す
                                    debug_msg("MONO", "WARNING: Unresolved generic type in " +
                                                          func->name + ": " + local.name +
                                                          " has type " + type_name);
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }

        // フィールドアクセスの結果として使用される一時変数の型を更新
        // MIRのstatementを解析して、フィールドアクセスのソース型を特定
        for (auto& bb : func->basic_blocks) {
            if (!bb)
                continue;

            for (auto& stmt : bb->statements) {
                if (!stmt || stmt->kind != MirStatement::Assign)
                    continue;

                auto& assign = std::get<MirStatement::AssignData>(stmt->data);
                if (!assign.rvalue || assign.rvalue->kind != MirRvalue::Use)
                    continue;

                auto& use_data = std::get<MirRvalue::UseData>(assign.rvalue->data);
                if (!use_data.operand || use_data.operand->kind != MirOperand::Copy)
                    continue;

                auto* place = std::get_if<MirPlace>(&use_data.operand->data);
                if (!place || place->projections.empty())
                    continue;

                // フィールドアクセスの場合
                if (place->projections[0].kind == ProjectionKind::Field) {
                    LocalId source_local = place->local;
                    LocalId dest_local = assign.place.local;
                    FieldId field_id = place->projections[0].field_id;

                    // ソースローカルの型情報を取得
                    auto info_it = struct_info.find(source_local);
                    if (info_it != struct_info.end()) {
                        const auto& [base_name, type_args] = info_it->second;

                        // 元の構造体定義からフィールドの型を取得
                        auto struct_it = hir_struct_defs->find(base_name);
                        if (struct_it != hir_struct_defs->end() && struct_it->second) {
                            const auto* st = struct_it->second;
                            if (field_id < st->fields.size()) {
                                auto field_type = st->fields[field_id].type;

                                // フィールド型がジェネリック型パラメータなら置換
                                if (field_type && (field_type->kind == hir::TypeKind::Generic ||
                                                   struct_type_params[base_name].size() > 0)) {
                                    auto params_it = struct_type_params.find(base_name);
                                    if (params_it != struct_type_params.end()) {
                                        for (size_t pi = 0;
                                             pi < params_it->second.size() && pi < type_args.size();
                                             ++pi) {
                                            if (field_type->name == params_it->second[pi]) {
                                                // 型を置換
                                                auto new_type = make_type_from_name(type_args[pi]);
                                                func->locals[dest_local].type = new_type;
                                                debug_msg("MONO",
                                                          "Updated field access type in " +
                                                              func->name + ": " +
                                                              func->locals[dest_local].name +
                                                              " -> " + type_args[pi]);
                                                break;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
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

                // 1. 単純なジェネリック関数呼び出し（create_node -> create_node__int）
                auto simple_it = simple_rewrite_map.find(func_name);
                if (simple_it != simple_rewrite_map.end()) {
                    func_name = simple_it->second;
                    continue;
                }

                // 2. Container<int>__print のような形式を検出
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
                }
            }
        }
    }
}

// 構造体メソッドのself引数を参照に修正
// 呼び出し側で構造体のコピーではなくアドレスを渡すように変更
void Monomorphization::fix_struct_method_self_args(MirProgram& program) {
    for (auto& func : program.functions) {
        if (!func)
            continue;

        // まず、全ブロックのCopy代入をスキャンしてコピー元マップを構築
        // copy_sources[dest_local] = source_local
        std::unordered_map<LocalId, LocalId> copy_sources;

        for (auto& block : func->basic_blocks) {
            if (!block)
                continue;
            for (auto& stmt : block->statements) {
                if (!stmt || stmt->kind != MirStatement::Assign)
                    continue;

                auto& assign_data = std::get<MirStatement::AssignData>(stmt->data);
                if (!assign_data.rvalue || assign_data.rvalue->kind != MirRvalue::Use)
                    continue;

                auto& use_data = std::get<MirRvalue::UseData>(assign_data.rvalue->data);
                if (!use_data.operand || use_data.operand->kind != MirOperand::Copy)
                    continue;

                // dest = copy source の形式
                auto& source_place = std::get<MirPlace>(use_data.operand->data);
                if (assign_data.place.projections.empty() && source_place.projections.empty()) {
                    // 単純なlocal-to-localコピー
                    copy_sources[assign_data.place.local] = source_place.local;
                }
            }
        }

        // 次に、構造体メソッド呼び出しを処理
        for (auto& block : func->basic_blocks) {
            if (!block || !block->terminator)
                continue;

            if (block->terminator->kind != MirTerminator::Call)
                continue;

            auto& call_data = std::get<MirTerminator::CallData>(block->terminator->data);
            if (!call_data.func || call_data.func->kind != MirOperand::FunctionRef)
                continue;

            const auto& func_name_ref = std::get<std::string>(call_data.func->data);

            // 構造体メソッド呼び出し（TypeName__method 形式）をチェック
            auto dunder_pos = func_name_ref.find("__");
            if (dunder_pos == std::string::npos || call_data.args.empty())
                continue;

            // 型名を抽出
            std::string type_name = func_name_ref.substr(0, dunder_pos);

            // この型名に対応する構造体が存在するか確認
            if (!hir_struct_defs || hir_struct_defs->find(type_name) == hir_struct_defs->end())
                continue;

            // 第1引数が値型（Copy）かチェック
            auto& first_arg = call_data.args[0];
            if (!first_arg || first_arg->kind != MirOperand::Copy)
                continue;

            auto& place = std::get<MirPlace>(first_arg->data);
            if (place.local >= func->locals.size())
                continue;

            // コピー元を追跡してオリジナルを見つける
            LocalId original_local = place.local;
            int chain_depth = 0;
            while (copy_sources.count(original_local) > 0 && chain_depth < 10) {
                original_local = copy_sources[original_local];
                chain_depth++;
            }

            if (original_local >= func->locals.size())
                continue;

            auto& local_type = func->locals[original_local].type;
            if (!local_type)
                continue;

            // 既にポインタ型ならスキップ
            if (local_type->kind == hir::TypeKind::Pointer)
                continue;

            // 構造体型であれば参照を取る
            if (local_type->kind == hir::TypeKind::Struct ||
                local_type->kind == hir::TypeKind::Generic ||
                local_type->kind == hir::TypeKind::TypeAlias || local_type->name == type_name ||
                local_type->name.find(type_name + "__") == 0) {
                // 新しいローカル変数を追加（ポインタ型）
                LocalId ref_id = static_cast<LocalId>(func->locals.size());
                std::string ref_name = "_self_ref_" + std::to_string(ref_id);
                auto ref_type = hir::make_pointer(local_type);
                func->locals.emplace_back(ref_id, ref_name, ref_type, false, false);

                // **オリジナル**への参照を作成
                auto ref_stmt = MirStatement::assign(
                    MirPlace{ref_id}, MirRvalue::ref(MirPlace{original_local}, false));
                block->statements.push_back(std::move(ref_stmt));

                // 呼び出しの第1引数を参照に変更
                call_data.args[0] = MirOperand::copy(MirPlace{ref_id});

                debug_msg("MONO", "Fixed self-ref for " + func_name_ref + " in " + func->name +
                                      " (traced " + std::to_string(place.local) + " -> " +
                                      std::to_string(original_local) + ")");
            }
        }
    }
}

}  // namespace cm::mir
