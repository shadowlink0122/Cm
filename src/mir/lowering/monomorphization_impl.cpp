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
                // generic_name = "Vector<T>__init" または "HashMap<K, V>__put"
                // func_name = "Vector__int__init" または "HashMap__int__int__put"

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
                // ネストジェネリクス対応: Vector__Vector__int__dtor -> type_args = [Vector__int]
                // remaining = "Vector__int__dtor" (base_name "Vector" は既に除去済み)
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

    // 1.1 カンマ区切りの複数型パラメータの場合
    // 例: "K, V" → "int__int" (置換後マングリング)
    // 例: "int, int" → "int__int" (具象型のマングリング)
    if (type->name.find(',') != std::string::npos) {
        auto params = split_type_args(type->name);
        std::vector<std::string> result_params;
        for (const auto& param : params) {
            auto param_it = type_subst.find(param);
            if (param_it != type_subst.end()) {
                // 型パラメータの場合は置換
                result_params.push_back(get_type_name(param_it->second));
            } else {
                // 既に具象型の場合はそのまま使用
                result_params.push_back(param);
            }
        }
        if (!result_params.empty()) {
            // 名前を構築（"int__int"形式でマングリング）
            auto new_type = std::make_shared<hir::Type>(type->kind);
            std::string new_name;
            for (size_t i = 0; i < result_params.size(); ++i) {
                if (i > 0)
                    new_name += "__";
                new_name += result_params[i];
            }
            new_type->name = new_name;
            new_type->type_args = type->type_args;  // 元のtype_argsを継承
            debug_msg("MONO", "Normalized comma-separated type: " + type->name + " -> " + new_name);
            return new_type;
        }
    }

    // 1.5 type_argsが置換された場合、新しい構造体型を作成
    // ただし、既にマングリング済みの名前（__を含む）はスキップ
    if (type_args_changed &&
        (type->kind == hir::TypeKind::Struct || type->kind == hir::TypeKind::Generic)) {
        // 既にマングリング済みの名前の場合でも、substituted_type_argsを使用して正しい名前を生成
        if (type->name.find("__") != std::string::npos) {
            // 基本名を抽出してsubstituted_type_argsから新しい名前を生成
            auto pos = type->name.find("__");
            std::string base_name = type->name.substr(0, pos);
            std::string new_name = base_name;
            for (const auto& arg : substituted_type_args) {
                if (arg) {
                    new_name += "__" + get_type_name(arg);
                }
            }
            auto new_type = std::make_shared<hir::Type>(hir::TypeKind::Struct);
            new_type->name = new_name;
            // マングリング済みの名前なのでtype_argsはクリア
            new_type->type_args.clear();
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
        // 重要: マングリング済みの名前（__を含む）の場合、type_argsはクリア
        // これにより二重マングリング（例: QueueNode__int<int>）を防止
        // type_argsは元の未マングリング名（QueueNode<T>）にのみ設定されるべき
        new_type->type_args.clear();
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
                // 重要: マングリング済み名前（__を含む）のelement_typeにはtype_argsをクリア
                // 二重マングリング（例: QueueNode__int<int>）を防止
                if (substituted_elem->name.find("__") != std::string::npos) {
                    substituted_elem->type_args.clear();
                }
                new_ptr_type->element_type = substituted_elem;
                // ptr_xxx形式で一貫した名前を生成
                new_ptr_type->name = "ptr_" + get_type_name(substituted_elem);
                // 注: マングリング済みのelement_typeにはtype_argsを追加しない
                // 型引数は名前でマングリング済み（例: QueueNode__int）
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
                // ptr_xxx形式で一貫した名前を生成
                new_ptr_type->name = "ptr_" + get_type_name(substituted_pointed);
                new_ptr_type->element_type = substituted_pointed;  // ← 重要: element_typeを設定
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
        const auto& [func_name, type_args_raw] = key;

        // type_argsを正規化（カンマ区切りの1要素を分割）
        // 例: ["int, int"] -> ["int", "int"]
        std::vector<std::string> type_args;
        for (const auto& arg : type_args_raw) {
            if (arg.find(',') != std::string::npos) {
                auto split_args = split_type_args(arg);
                for (const auto& split_arg : split_args) {
                    type_args.push_back(split_arg);
                }
            } else {
                type_args.push_back(arg);
            }
        }

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

        if (!hir_func->generic_params.empty()) {
            // 通常のジェネリック関数: generic_paramsから型パラメータを取得
            for (size_t i = 0; i < hir_func->generic_params.size() && i < type_args.size(); ++i) {
                const auto& param_name = hir_func->generic_params[i].name;
                type_subst[param_name] = make_type_from_name(type_args[i]);
                type_name_subst[param_name] = type_args[i];
                debug_msg("MONO", "Type substitution: " + param_name + " -> " + type_args[i]);
            }
        } else if (func_name.find('<') != std::string::npos) {
            // ジェネリックimplメソッドの場合: 関数名Vector<T>__methodから型パラメータを推論
            // type_args[0]が実際の型（例: "int"）、Tがパラメータ名
            auto angle_start = func_name.find('<');
            auto angle_end = func_name.find('>');
            if (angle_start != std::string::npos && angle_end != std::string::npos) {
                std::string params_str =
                    func_name.substr(angle_start + 1, angle_end - angle_start - 1);
                // 型パラメータを抽出（カンマ区切り）
                std::vector<std::string> param_names;
                std::string current;
                int depth = 0;
                for (char c : params_str) {
                    if (c == '<')
                        depth++;
                    else if (c == '>')
                        depth--;
                    else if (c == ',' && depth == 0) {
                        if (!current.empty()) {
                            // 空白をトリム
                            size_t start = current.find_first_not_of(" ");
                            size_t end = current.find_last_not_of(" ");
                            if (start != std::string::npos) {
                                param_names.push_back(current.substr(start, end - start + 1));
                            }
                            current.clear();
                        }
                        continue;
                    }
                    current += c;
                }
                if (!current.empty()) {
                    size_t start = current.find_first_not_of(" ");
                    size_t end = current.find_last_not_of(" ");
                    if (start != std::string::npos) {
                        param_names.push_back(current.substr(start, end - start + 1));
                    }
                }

                // type_argsと対応付け
                for (size_t i = 0; i < param_names.size() && i < type_args.size(); ++i) {
                    type_subst[param_names[i]] = make_type_from_name(type_args[i]);
                    type_name_subst[param_names[i]] = type_args[i];
                    debug_msg("MONO", "Impl method type substitution: " + param_names[i] + " -> " +
                                          type_args[i]);
                }
            }
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

            // sizeof_for_Tマーカー型を持つ定数オペランドの値を再計算
            // ジェネリック型パラメータのsizeofがHIR段階でマーカー型として保存され、
            // モノモフィゼーション時に実際の型サイズに置換される
            if (op->kind == MirOperand::Constant) {
                auto* const_data = std::get_if<MirConstant>(&op->data);
                if (!const_data)
                    goto normal_type_subst;

                // MirConstant.typeからマーカーを検出
                hir::TypePtr marker_type = nullptr;
                if (const_data->type && const_data->type->kind == hir::TypeKind::Generic &&
                    const_data->type->name.find("sizeof_for_") == 0) {
                    marker_type = const_data->type;
                }
                // op->typeからもマーカーを検出（両方チェック）
                else if (op->type && op->type->kind == hir::TypeKind::Generic &&
                         op->type->name.find("sizeof_for_") == 0) {
                    marker_type = op->type;
                }

                if (marker_type) {
                    // "sizeof_for_T" から "T" を抽出
                    std::string type_param_name = marker_type->name.substr(11);

                    // type_substから置換後の型を取得
                    auto param_it = type_subst.find(type_param_name);
                    if (param_it != type_subst.end()) {
                        // 置換後の型のサイズを計算
                        int64_t actual_size = calculate_specialized_type_size(param_it->second);

                        // 定数オペランドの値を更新
                        const_data->value = actual_size;
                        const_data->type = hir::make_long();
                    }
                    // 型を通常の整数型に変更（マーカーは不要に）
                    op->type = hir::make_long();
                    goto normal_type_subst;
                }
            }

        normal_type_subst:
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

        // ========== デストラクタループ挿入（Vector<T>等の要素デストラクタ呼び出し） ==========
        // 関数名が__dtorで終わり、type_argsが存在し、要素型にデストラクタがある場合
        if (specialized_name.find("__dtor") != std::string::npos && !type_args.empty()) {
            // 要素型のデストラクタ名を構築（ネストジェネリックの場合は正規化）
            std::string element_type = normalize_type_arg(type_args[0]);
            std::string element_dtor_name = element_type + "__dtor";

            // 要素型にデストラクタが存在するかチェック
            bool has_element_dtor = false;
            for (const auto& func : program.functions) {
                if (func && func->name == element_dtor_name) {
                    has_element_dtor = true;
                    break;
                }
            }

            // デストラクタがある場合のみループを挿入
            if (has_element_dtor) {
                debug_msg("MONO", "Inserting destructor loop for " + specialized_name +
                                      " with element dtor " + element_dtor_name);

                // 元のentry blockを保存
                BlockId original_entry = specialized->entry_block;

                // 新しいローカル変数を追加
                LocalId loop_idx_id = static_cast<LocalId>(specialized->locals.size());
                specialized->locals.emplace_back(loop_idx_id, "_loop_idx", hir::make_ulong(), false,
                                                 false);

                LocalId elem_size_id = static_cast<LocalId>(specialized->locals.size());
                specialized->locals.emplace_back(elem_size_id, "_elem_size", hir::make_ulong(),
                                                 false, false);

                LocalId loop_cond_id = static_cast<LocalId>(specialized->locals.size());
                specialized->locals.emplace_back(loop_cond_id, "_loop_cond", hir::make_bool(),
                                                 false, false);

                // 要素型のポインタ型を作成
                auto element_type_ptr = make_type_from_name(element_type);
                auto element_ptr_type = hir::make_pointer(element_type_ptr);

                LocalId data_ptr_id = static_cast<LocalId>(specialized->locals.size());
                specialized->locals.emplace_back(data_ptr_id, "_data_ptr", element_ptr_type, false,
                                                 false);

                LocalId elem_ptr_id = static_cast<LocalId>(specialized->locals.size());
                specialized->locals.emplace_back(elem_ptr_id, "_elem_ptr", element_ptr_type, false,
                                                 false);

                // ブロックIDを割り当て（現在のサイズから順番に）
                BlockId loop_init_id = static_cast<BlockId>(specialized->basic_blocks.size());
                BlockId loop_header_id = loop_init_id + 1;
                BlockId loop_body_id = loop_init_id + 2;
                BlockId after_dtor_id = loop_init_id + 3;

                // ====== loop_init ブロック ======
                auto loop_init = std::make_unique<BasicBlock>(loop_init_id);

                // _loop_idx = 0
                MirConstant zero_const;
                zero_const.type = hir::make_ulong();
                zero_const.value = int64_t{0};
                loop_init->statements.push_back(MirStatement::assign(
                    MirPlace{loop_idx_id}, MirRvalue::use(MirOperand::constant(zero_const))));

                // _elem_size = (*self).size (field index 1 for Vector)
                // self は LocalId(1)
                // 注意: size フィールドは int 型なので、まずint型で読み込んでからulongにキャスト
                MirPlace self_place{LocalId(1)};
                MirPlace self_deref = self_place;
                self_deref.projections.push_back(PlaceProjection::deref());
                MirPlace size_field = self_deref;
                size_field.projections.push_back(PlaceProjection::field(1));  // size is field 1

                // int型の一時変数を作成してsizeを読み込む
                LocalId size_int_id = static_cast<LocalId>(specialized->locals.size());
                specialized->locals.emplace_back(size_int_id, "_size_int", hir::make_int(), false,
                                                 false);
                loop_init->statements.push_back(MirStatement::assign(
                    MirPlace{size_int_id}, MirRvalue::use(MirOperand::copy(size_field))));

                // int から ulong へのキャスト
                loop_init->statements.push_back(MirStatement::assign(
                    MirPlace{elem_size_id},
                    MirRvalue::cast(MirOperand::copy(MirPlace{size_int_id}), hir::make_ulong())));

                // _data_ptr = (*self).data (field index 0 for Vector)
                MirPlace data_field = self_deref;
                data_field.projections.push_back(PlaceProjection::field(0));  // data is field 0
                loop_init->statements.push_back(MirStatement::assign(
                    MirPlace{data_ptr_id}, MirRvalue::use(MirOperand::copy(data_field))));

                // goto loop_header
                loop_init->terminator = MirTerminator::goto_block(loop_header_id);
                loop_init->successors = {loop_header_id};
                specialized->basic_blocks.push_back(std::move(loop_init));

                // ====== loop_header ブロック ======
                auto loop_header = std::make_unique<BasicBlock>(loop_header_id);

                // _loop_cond = _loop_idx < _elem_size
                loop_header->statements.push_back(MirStatement::assign(
                    MirPlace{loop_cond_id},
                    MirRvalue::binary(MirBinaryOp::Lt, MirOperand::copy(MirPlace{loop_idx_id}),
                                      MirOperand::copy(MirPlace{elem_size_id}))));

                // switch_int _loop_cond: true -> loop_body, false -> original_entry
                loop_header->terminator = MirTerminator::switch_int(
                    MirOperand::copy(MirPlace{loop_cond_id}),
                    {{1, loop_body_id}},  // true -> loop_body
                    original_entry        // false -> original_entry (free処理など)
                );
                loop_header->successors = {loop_body_id, original_entry};
                specialized->basic_blocks.push_back(std::move(loop_header));

                // ====== loop_body ブロック ======
                auto loop_body = std::make_unique<BasicBlock>(loop_body_id);

                // _elem_ptr = &(_data_ptr[_loop_idx]) using PlaceProjection::index
                MirPlace indexed_elem{data_ptr_id};
                indexed_elem.projections.push_back(PlaceProjection::deref());
                indexed_elem.projections.push_back(PlaceProjection::index(loop_idx_id));
                loop_body->statements.push_back(MirStatement::assign(
                    MirPlace{elem_ptr_id}, MirRvalue::ref(indexed_elem, false)  // immutable ref
                    ));

                // Call element_dtor(_elem_ptr) -> after_dtor
                auto dtor_call_term = std::make_unique<MirTerminator>();
                dtor_call_term->kind = MirTerminator::Call;
                std::vector<MirOperandPtr> dtor_args;
                dtor_args.push_back(MirOperand::copy(MirPlace{elem_ptr_id}));
                dtor_call_term->data = MirTerminator::CallData{
                    MirOperand::function_ref(element_dtor_name),
                    std::move(dtor_args),
                    std::nullopt,  // 戻り値なし（void）
                    after_dtor_id,
                    std::nullopt,  // unwind無し
                    "",
                    "",
                    false  // 通常の関数呼び出し
                };
                loop_body->terminator = std::move(dtor_call_term);
                loop_body->successors = {after_dtor_id};
                specialized->basic_blocks.push_back(std::move(loop_body));

                // ====== after_dtor ブロック ======
                auto after_dtor = std::make_unique<BasicBlock>(after_dtor_id);

                // _loop_idx = _loop_idx + 1
                MirConstant one_const;
                one_const.type = hir::make_ulong();
                one_const.value = int64_t{1};
                after_dtor->statements.push_back(MirStatement::assign(
                    MirPlace{loop_idx_id},
                    MirRvalue::binary(MirBinaryOp::Add, MirOperand::copy(MirPlace{loop_idx_id}),
                                      MirOperand::constant(one_const))));

                // goto loop_header
                after_dtor->terminator = MirTerminator::goto_block(loop_header_id);
                after_dtor->successors = {loop_header_id};
                specialized->basic_blocks.push_back(std::move(after_dtor));

                // entry_blockをloop_initに変更
                specialized->entry_block = loop_init_id;

                debug_msg("MONO", "Destructor loop inserted: entry_block now " +
                                      std::to_string(loop_init_id) + ", blocks=" +
                                      std::to_string(specialized->basic_blocks.size()));
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
        bool should_remove = false;
        if (*it) {
            const std::string& func_name = (*it)->name;
            // 1. 明示的なジェネリック関数リストに含まれる
            if (generic_funcs.count(func_name) > 0) {
                should_remove = true;
            }
            // 2. 関数名に未置換の型パラメータパターン(__T__)が含まれる
            // 例: Queue__T__clear, Container__T__method
            else if (func_name.find("__T__") != std::string::npos ||
                     func_name.find("__K__") != std::string::npos ||
                     func_name.find("__V__") != std::string::npos) {
                should_remove = true;
                debug_msg("MONO", "Removing unspecialized generic function: " + func_name);
            }
        }
        if (should_remove) {
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
                                                   const std::vector<std::string>& type_args_raw) {
    if (!hir_struct_defs)
        return;

    // type_argsを正規化（カンマ区切りの1要素を分割）
    // 例: ["int, int"] -> ["int", "int"]
    std::vector<std::string> type_args;
    for (const auto& arg : type_args_raw) {
        if (arg.find(',') != std::string::npos) {
            auto split_args = split_type_args(arg);
            for (const auto& split_arg : split_args) {
                type_args.push_back(split_arg);
            }
        } else {
            type_args.push_back(arg);
        }
    }

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

            // ポインタ型のelement_typeのtype_argsをクリア（二重マングリング防止）
            if (field_type && field_type->kind == hir::TypeKind::Pointer &&
                field_type->element_type && !field_type->element_type->type_args.empty()) {
                field_type->element_type->type_args.clear();
            }
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

    // まず、すべてのMIRローカル変数の型名を正規化
    // PtrContainer__*int -> PtrContainer__ptr_int
    // また、ポインタ型のelement_type名も正規化
    for (auto& func : program.functions) {
        if (!func)
            continue;
        for (auto& local : func->locals) {
            if (!local.type)
                continue;
            // 型名にポインタ表記が含まれている場合は正規化
            if (local.type->name.find("__*") != std::string::npos) {
                std::string normalized = local.type->name;
                size_t pos = 0;
                while ((pos = normalized.find("__*", pos)) != std::string::npos) {
                    normalized.replace(pos, 3, "__ptr_");
                    pos += 6;
                }
                debug_msg("MONO",
                          "Normalized type name: " + local.type->name + " -> " + normalized);
                local.type->name = normalized;
            }
            // ポインタ型の場合、element_type名も再帰的に正規化
            if (local.type->kind == hir::TypeKind::Pointer && local.type->element_type) {
                auto& elem = local.type->element_type;
                if (elem->name.find("__*") != std::string::npos) {
                    std::string normalized = elem->name;
                    size_t pos = 0;
                    while ((pos = normalized.find("__*", pos)) != std::string::npos) {
                        normalized.replace(pos, 3, "__ptr_");
                        pos += 6;
                    }
                    debug_msg("MONO", "Normalized pointer element type: " + elem->name + " -> " +
                                          normalized);
                    elem->name = normalized;
                }
            }
        }
    }

    // MIR構造体名も正規化
    for (auto& st : program.structs) {
        if (!st)
            continue;
        if (st->name.find("__*") != std::string::npos) {
            std::string normalized = st->name;
            size_t pos = 0;
            while ((pos = normalized.find("__*", pos)) != std::string::npos) {
                normalized.replace(pos, 3, "__ptr_");
                pos += 6;
            }
            debug_msg("MONO", "Normalized struct name: " + st->name + " -> " + normalized);
            st->name = normalized;
        }
    }

    // MIR関数名も正規化
    for (auto& func : program.functions) {
        if (!func)
            continue;
        if (func->name.find("__*") != std::string::npos) {
            std::string normalized = func->name;
            size_t pos = 0;
            while ((pos = normalized.find("__*", pos)) != std::string::npos) {
                normalized.replace(pos, 3, "__ptr_");
                pos += 6;
            }
            debug_msg("MONO", "Normalized function name: " + func->name + " -> " + normalized);
            func->name = normalized;
        }

        // 関数内の呼び出しも正規化
        for (auto& bb : func->basic_blocks) {
            if (!bb || !bb->terminator)
                continue;
            if (bb->terminator->kind == MirTerminator::Call) {
                auto& call_data = std::get<MirTerminator::CallData>(bb->terminator->data);
                if (call_data.func && call_data.func->kind == MirOperand::FunctionRef) {
                    auto& fn_name = std::get<std::string>(call_data.func->data);
                    if (fn_name.find("__*") != std::string::npos) {
                        std::string normalized = fn_name;
                        size_t pos = 0;
                        while ((pos = normalized.find("__*", pos)) != std::string::npos) {
                            normalized.replace(pos, 3, "__ptr_");
                            pos += 6;
                        }
                        debug_msg("MONO",
                                  "Normalized call target: " + fn_name + " -> " + normalized);
                        fn_name = normalized;
                    }
                }
            }
        }
    }

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

                if (place->projections[0].kind == ProjectionKind::Field) {
                    LocalId source_local = place->local;
                    LocalId dest_local = assign.place.local;
                    (void)place->projections[0].field_id;  // 未使用警告を抑制

                    // ソースローカルの型情報を取得
                    auto info_it = struct_info.find(source_local);

                    // struct_infoにある場合とない場合で分岐
                    std::string base_name;
                    std::vector<std::string> type_args;
                    bool has_struct_info = false;

                    if (info_it != struct_info.end()) {
                        has_struct_info = true;
                        base_name = info_it->second.first;
                        type_args = info_it->second.second;
                    } else {
                        // struct_infoにないが、マングリング済み構造体名を持つローカルの場合
                        // 例: _4: Iterator__int
                        if (source_local < func->locals.size()) {
                            auto& local_type = func->locals[source_local].type;
                            if (local_type && local_type->kind == hir::TypeKind::Struct) {
                                std::string type_name = local_type->name;
                                size_t pos = type_name.find("__");
                                if (pos != std::string::npos) {
                                    base_name = type_name.substr(0, pos);
                                    // 型引数を抽出
                                    std::string remainder = type_name.substr(pos + 2);
                                    size_t arg_pos = 0;
                                    while (arg_pos < remainder.size()) {
                                        auto next_pos = remainder.find("__", arg_pos);
                                        if (next_pos == std::string::npos) {
                                            type_args.push_back(remainder.substr(arg_pos));
                                            break;
                                        }
                                        type_args.push_back(
                                            remainder.substr(arg_pos, next_pos - arg_pos));
                                        arg_pos = next_pos + 2;
                                    }
                                    if (generic_structs.count(base_name) > 0) {
                                        has_struct_info = true;
                                    }
                                }
                            }
                        }
                    }

                    if (has_struct_info && !base_name.empty()) {
                        // プロジェクションチェーン全体を辿って最終的なフィールド型を取得
                        // 例: node.data.value → Node.data(=T→Item) → Item.value(=int)
                        hir::TypePtr current_field_type = nullptr;
                        std::string current_struct_name = base_name;
                        std::vector<std::string> current_type_args = type_args;
                        bool is_final_type_resolved = false;

                        for (const auto& proj : place->projections) {
                            if (proj.kind != ProjectionKind::Field)
                                break;

                            FieldId fid = proj.field_id;

                            // 現在の構造体定義を取得
                            auto struct_it = hir_struct_defs->find(current_struct_name);
                            if (struct_it == hir_struct_defs->end() || !struct_it->second)
                                break;

                            const auto* st = struct_it->second;
                            if (fid >= st->fields.size())
                                break;

                            auto field_type = st->fields[fid].type;
                            if (!field_type)
                                break;

                            // フィールド型がジェネリック型パラメータの場合、置換
                            auto params_it = struct_type_params.find(current_struct_name);
                            if (params_it != struct_type_params.end()) {
                                for (size_t pi = 0;
                                     pi < params_it->second.size() && pi < current_type_args.size();
                                     ++pi) {
                                    // 直接型パラメータ名と一致する場合
                                    if (field_type->name == params_it->second[pi]) {
                                        // 型パラメータを具体型に置換
                                        current_field_type =
                                            make_type_from_name(current_type_args[pi]);
                                        // 置換後の型が構造体の場合、次のフィールドアクセスのために情報を更新
                                        if (current_field_type &&
                                            current_field_type->kind == hir::TypeKind::Struct) {
                                            current_struct_name = current_field_type->name;
                                            current_type_args
                                                .clear();  // 具体型なのでtype_argsはクリア
                                        }
                                        is_final_type_resolved = true;
                                        break;
                                    }
                                    // ポインタ型でelement_typeが型パラメータの場合 (*T → *int)
                                    if (field_type->kind == hir::TypeKind::Pointer &&
                                        field_type->element_type &&
                                        field_type->element_type->name == params_it->second[pi]) {
                                        // ポインタ要素型を置換
                                        auto concrete_elem =
                                            make_type_from_name(current_type_args[pi]);
                                        current_field_type = hir::make_pointer(concrete_elem);
                                        is_final_type_resolved = true;
                                        break;
                                    }
                                }
                            }

                            // フィールド型がジェネリックパラメータでない場合
                            if (!is_final_type_resolved || current_field_type == nullptr) {
                                current_field_type = field_type;
                                if (field_type->kind == hir::TypeKind::Struct) {
                                    current_struct_name = field_type->name;
                                    // type_argsを抽出
                                    current_type_args = extract_type_args_strings(field_type);
                                }
                            }
                            is_final_type_resolved = false;  // 次のプロジェクションのためにリセット
                        }

                        // 最終的なフィールド型が得られた場合、dest_localの型を更新
                        if (current_field_type) {
                            func->locals[dest_local].type = current_field_type;
                            debug_msg("MONO", "Updated field access type in " + func->name + ": " +
                                                  func->locals[dest_local].name + " -> " +
                                                  hir::type_to_string(*current_field_type));
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

// プログラム全体のモノモーフィゼーション
void Monomorphization::monomorphize(
    MirProgram& program,
    const std::unordered_map<std::string, const hir::HirFunction*>& hir_functions,
    const std::unordered_map<std::string, const hir::HirStruct*>& hir_structs) {
    hir_funcs = &hir_functions;
    hir_struct_defs = &hir_structs;

    // 構造体のモノモーフィゼーション（関数より先に実行）
    monomorphize_structs(program);

    // ジェネリック関数を特定
    std::unordered_set<std::string> generic_funcs;
    for (const auto& [name, func] : hir_functions) {
        if (!func)
            continue;
        bool is_generic = !func->generic_params.empty() || name.find('<') != std::string::npos;
        if (is_generic) {
            generic_funcs.insert(name);
            debug_msg("MONO", "Found generic function: " + name + " with " +
                                  std::to_string(func->generic_params.size()) + " type params" +
                                  (name.find('<') != std::string::npos ? " (impl method)" : ""));
        }
    }

    if (generic_funcs.empty()) {
        debug_msg("MONO", "No generic functions found");
        return;
    }

    for (const auto& gf : generic_funcs) {
        debug_msg("MONO", "Generic func in set: " + gf);
    }

    // 反復処理：新しい特殊化が生成されなくなるまで繰り返す
    std::unordered_set<std::string> all_generated_specializations;
    const int MAX_ITERATIONS = 10;

    for (int iteration = 0; iteration < MAX_ITERATIONS; ++iteration) {
        std::map<std::pair<std::string, std::vector<std::string>>,
                 std::vector<std::tuple<std::string, size_t>>>
            needed;

        for (auto& func : program.functions) {
            if (func) {
                scan_generic_calls(func.get(), generic_funcs, hir_functions, needed);
            }
        }

        std::map<std::pair<std::string, std::vector<std::string>>,
                 std::vector<std::tuple<std::string, size_t>>>
            new_needed;
        for (const auto& [key, call_sites] : needed) {
            std::string specialized_name = make_specialized_name(key.first, key.second);
            if (all_generated_specializations.count(specialized_name) == 0) {
                new_needed[key] = call_sites;
            }
        }

        if (new_needed.empty()) {
            debug_msg("MONO",
                      "Iteration " + std::to_string(iteration) + ": No new specializations needed");
            break;
        }

        debug_msg("MONO", "Iteration " + std::to_string(iteration) + ": Found " +
                              std::to_string(new_needed.size()) + " new specializations needed");

        generate_generic_specializations(program, hir_functions, new_needed);

        for (const auto& [key, _] : new_needed) {
            std::string specialized_name = make_specialized_name(key.first, key.second);
            all_generated_specializations.insert(specialized_name);
        }

        rewrite_generic_calls(program, new_needed);
    }

    monomorphize_structs(program);
    fix_struct_method_self_args(program);
    cleanup_generic_functions(program, generic_funcs);
}

// ポインタ型名を正規化
std::string Monomorphization::normalize_type_arg(const std::string& type_arg) {
    if (type_arg.empty())
        return type_arg;

    // *intのような形式をptr_intに変換
    if (type_arg[0] == '*') {
        return "ptr_" + normalize_type_arg(type_arg.substr(1));
    }

    // ネストジェネリクス対応
    auto lt_pos = type_arg.find('<');
    if (lt_pos != std::string::npos) {
        auto gt_pos = type_arg.rfind('>');
        if (gt_pos != std::string::npos && gt_pos > lt_pos) {
            std::string base_name = type_arg.substr(0, lt_pos);
            std::string type_args_str = type_arg.substr(lt_pos + 1, gt_pos - lt_pos - 1);

            // カンマで分割（ネストを考慮）
            std::vector<std::string> type_args;
            int depth = 0;
            size_t start = 0;
            for (size_t i = 0; i < type_args_str.size(); ++i) {
                if (type_args_str[i] == '<') {
                    depth++;
                } else if (type_args_str[i] == '>') {
                    depth--;
                } else if (type_args_str[i] == ',' && depth == 0) {
                    std::string arg = type_args_str.substr(start, i - start);
                    while (!arg.empty() && arg.front() == ' ')
                        arg.erase(0, 1);
                    while (!arg.empty() && arg.back() == ' ')
                        arg.pop_back();
                    type_args.push_back(arg);
                    start = i + 1;
                }
            }
            std::string last_arg = type_args_str.substr(start);
            while (!last_arg.empty() && last_arg.front() == ' ')
                last_arg.erase(0, 1);
            while (!last_arg.empty() && last_arg.back() == ' ')
                last_arg.pop_back();
            if (!last_arg.empty()) {
                type_args.push_back(last_arg);
            }

            std::string result = base_name;
            for (const auto& arg : type_args) {
                result += "__" + normalize_type_arg(arg);
            }
            return result;
        }
    }

    return type_arg;
}

// 型名から特殊化構造体名を生成
std::string Monomorphization::make_specialized_struct_name(
    const std::string& base_name, const std::vector<std::string>& type_args) {
    std::string result = base_name;
    for (const auto& arg : type_args) {
        result += "__" + normalize_type_arg(arg);
    }
    return result;
}

// 型名から特殊化関数名を生成
std::string Monomorphization::make_specialized_name(const std::string& base_name,
                                                    const std::vector<std::string>& type_args) {
    auto pos = base_name.find("<");
    auto end_pos = base_name.find(">__");

    if (pos != std::string::npos && end_pos != std::string::npos && !type_args.empty()) {
        std::string prefix = base_name.substr(0, pos);
        std::string suffix = base_name.substr(end_pos + 1);

        std::string args_str;
        for (size_t i = 0; i < type_args.size(); ++i) {
            args_str += "__" + normalize_type_arg(type_args[i]);
        }

        return prefix + args_str + suffix;
    }

    std::string result = base_name;
    for (const auto& arg : type_args) {
        result += "__" + normalize_type_arg(arg);
    }
    return result;
}

// インターフェース型かチェック
bool Monomorphization::is_interface_type(const std::string& type_name) const {
    return interface_names.count(type_name) > 0;
}

// 特殊化された型のサイズを計算
int64_t Monomorphization::calculate_specialized_type_size(const hir::TypePtr& type) const {
    if (!type)
        return 8;

    switch (type->kind) {
        case hir::TypeKind::Bool:
        case hir::TypeKind::Tiny:
        case hir::TypeKind::UTiny:
        case hir::TypeKind::Char:
            return 1;
        case hir::TypeKind::Short:
        case hir::TypeKind::UShort:
            return 2;
        case hir::TypeKind::Int:
        case hir::TypeKind::UInt:
        case hir::TypeKind::Float:
        case hir::TypeKind::UFloat:
            return 4;
        case hir::TypeKind::Long:
        case hir::TypeKind::ULong:
        case hir::TypeKind::Double:
        case hir::TypeKind::UDouble:
            return 8;
        case hir::TypeKind::Pointer:
        case hir::TypeKind::Reference:
        case hir::TypeKind::String:
            return 8;
        case hir::TypeKind::Struct: {
            if (hir_struct_defs && hir_struct_defs->count(type->name)) {
                const auto* st = hir_struct_defs->at(type->name);
                int64_t size = static_cast<int64_t>(st->fields.size()) * 8;
                return size > 0 ? size : 8;
            }
            if (type->name.find("__") != std::string::npos) {
                std::string base = type->name.substr(0, type->name.find("__"));
                if (hir_struct_defs && hir_struct_defs->count(base)) {
                    const auto* st = hir_struct_defs->at(base);
                    int64_t size = static_cast<int64_t>(st->fields.size()) * 8;
                    return size > 0 ? size : 8;
                }
            }
            return 8;
        }
        case hir::TypeKind::Array:
            if (type->element_type && type->array_size.has_value()) {
                return calculate_specialized_type_size(type->element_type) *
                       type->array_size.value();
            }
            return 8;
        default:
            return 8;
    }
}

}  // namespace cm::mir
