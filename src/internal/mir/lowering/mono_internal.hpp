#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// ============================================================
// 単相化内部ヘルパー（monomorphization_impl / mono_structs で共有）
// ============================================================

#include "monomorphization.hpp"
#include "monomorphization_utils.hpp"

namespace cm::mir {

// 型内の型パラメータを再帰的に置換するヘルパー関数
inline hir::TypePtr substitute_type_in_type(
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

}  // namespace cm::mir
