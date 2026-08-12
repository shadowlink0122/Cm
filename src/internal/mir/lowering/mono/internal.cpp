// ============================================================
// 単相化内部ヘルパーの実装
// ============================================================
// internal.hpp で宣言されたヘルパー関数の実装（mono/ 配下の各TUで共有）

#include "internal/mir/lowering/mono/internal.hpp"

#include "internal/syntax/ast/typekey.hpp"

#include <optional>

namespace cm::mir {

// マングルされた型引数名（"int"/"short"/"double"等）からプリミティブTypeKindを復元する。
// 単相化で型引数の種別を復元する2箇所で共有する。short/ushort/tiny/utiny/isize/usizeを
// 取りこぼすとStruct扱いになり、Box<short>の配列フィールドが消える等の実害が出る（M11）。
static std::optional<hir::TypeKind> primitive_kind_from_name(const std::string& p) {
    if (p == "int")
        return hir::TypeKind::Int;
    if (p == "uint")
        return hir::TypeKind::UInt;
    if (p == "tiny")
        return hir::TypeKind::Tiny;
    if (p == "utiny")
        return hir::TypeKind::UTiny;
    if (p == "short")
        return hir::TypeKind::Short;
    if (p == "ushort")
        return hir::TypeKind::UShort;
    if (p == "long")
        return hir::TypeKind::Long;
    if (p == "ulong")
        return hir::TypeKind::ULong;
    if (p == "isize")
        return hir::TypeKind::ISize;
    if (p == "usize")
        return hir::TypeKind::USize;
    if (p == "float")
        return hir::TypeKind::Float;
    if (p == "double")
        return hir::TypeKind::Double;
    if (p == "bool")
        return hir::TypeKind::Bool;
    if (p == "char")
        return hir::TypeKind::Char;
    if (p == "string")
        return hir::TypeKind::String;
    return std::nullopt;
}

// 型内の型パラメータを再帰的に置換するヘルパー関数
hir::TypePtr substitute_type_in_type(
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
        if (type->name.find("__") != std::string::npos ||
            ast::typekey::is_encoded_key(type->name)) {
            // 基本名を正準関数で抽出し、substituted_type_argsから正準キーを生成（フラット名産生の全廃）
            std::string base_name = ast::typekey::spec_base_name(type->name);
            auto new_type = std::make_shared<hir::Type>(hir::TypeKind::Struct);
            new_type->name =
                mono ? mono->struct_symbol_key(base_name, substituted_type_args) : type->name;
            // マングリング済みの名前なのでtype_argsはクリア
            new_type->type_args.clear();
            return new_type;
        }

        auto new_type = std::make_shared<hir::Type>(hir::TypeKind::Struct);
        // 新しい名前（正準キー）を生成（Node -> Node$1$...）
        std::string new_name =
            mono ? mono->struct_symbol_key(type->name, substituted_type_args) : type->name;
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

    // 2.5 配列/スライス型の場合（K[] → int[]、T[N] → int[N]）
    // element_type を再帰的に置換する。これがないとジェネリックなスライス/配列フィールドの
    // 要素型が未解決（K のまま）で残り、要素サイズ計算が誤る（例: int を12バイトblobとして扱う）。
    // その結果 push/index の stride が食い違い、確保バッファに余裕のないwasmで木構造が壊れる。
    if (type->kind == hir::TypeKind::Array && type->element_type) {
        auto substituted_elem = substitute_type_in_type(type->element_type, type_subst, mono);
        if (substituted_elem && (substituted_elem != type->element_type ||
                                 substituted_elem->name != type->element_type->name)) {
            // 元の型の他フィールド（array_size 等）を保ったまま element_type だけ差し替える
            auto new_arr_type = std::make_shared<hir::Type>(*type);
            new_arr_type->element_type = substituted_elem;
            return new_arr_type;
        }
    }

    // 3a. $エンコード名に型パラメータが埋め込まれている場合（Container$1$1$T → 復号・置換・再エンコード）
    if ((type->kind == hir::TypeKind::Struct || type->kind == hir::TypeKind::TypeAlias) &&
        ast::typekey::is_encoded_key(type->name)) {
        auto base = ast::typekey::base_name_of(type->name);
        auto args = ast::typekey::decode_type_args(type->name);
        bool any = false;
        for (auto& a : args) {
            if (a) {
                auto sit = type_subst.find(a->name);
                if (sit != type_subst.end()) {
                    a = sit->second;
                    any = true;
                } else {
                    auto rec = substitute_type_in_type(a, type_subst, mono);
                    if (rec && rec->name != a->name) {
                        a = rec;
                        any = true;
                    }
                }
            }
        }
        if (any && mono && !args.empty()) {
            auto new_type = std::make_shared<hir::Type>(hir::TypeKind::Struct);
            new_type->name = mono->struct_symbol_key(base, args);
            new_type->type_args = args;
            return new_type;
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
                // 新しい構造体名（正準キー）を生成
                std::vector<hir::TypePtr> resolved_type_args;
                for (const auto& p : new_params) {
                    auto arg_type = std::make_shared<hir::Type>(hir::TypeKind::Struct);
                    arg_type->name = p;
                    // プリミティブ型の場合はTypeKindを復元（M11）
                    if (auto k = primitive_kind_from_name(p))
                        arg_type->kind = *k;
                    resolved_type_args.push_back(arg_type);
                }

                auto new_type = std::make_shared<hir::Type>(hir::TypeKind::Struct);
                new_type->name =
                    mono ? mono->struct_symbol_key(base_name, resolved_type_args) : type_name;
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
                    // 新しい構造体名（正準キー）を生成
                    std::vector<hir::TypePtr> resolved_type_args;
                    for (const auto& p : new_params) {
                        auto arg_type = std::make_shared<hir::Type>(hir::TypeKind::Struct);
                        arg_type->name = p;
                        // プリミティブ型の場合はTypeKindを復元（M11）
                        if (auto k = primitive_kind_from_name(p))
                            arg_type->kind = *k;
                        resolved_type_args.push_back(arg_type);
                    }

                    // 重要: モノモーフィック化後の型はStructになる
                    auto new_type = std::make_shared<hir::Type>(hir::TypeKind::Struct);
                    new_type->name =
                        mono ? mono->struct_symbol_key(base_name, resolved_type_args) : type_name;
                    new_type->type_args = resolved_type_args;
                    debug_msg("MONO", "Substituted angle-bracket type: " + type_name + " -> " +
                                          new_type->name + " (kind: Generic->Struct)");
                    return new_type;
                }
            }
        }

        // 5. Generic型で名前のみ（Container）の場合、type_argsから推論
        if (type->kind == hir::TypeKind::Generic && !type->name.empty() &&
            type->name.find("<") == std::string::npos) {
            // ジェネリック構造体名（Container）で、型引数がある場合
            // type_substからすべての型引数を適用
            std::vector<hir::TypePtr> resolved_type_args;
            for (const auto& [param_name, param_type] : type_subst) {
                (void)param_name;
                resolved_type_args.push_back(param_type);
            }
            if (!resolved_type_args.empty()) {
                auto new_type = std::make_shared<hir::Type>(hir::TypeKind::Struct);
                // 新しい構造体名（正準キー）を生成
                new_type->name =
                    mono ? mono->struct_symbol_key(type->name, resolved_type_args) : type->name;
                new_type->type_args = resolved_type_args;
                debug_msg("MONO",
                          "Substituted generic type: " + type->name + " -> " + new_type->name);
                return new_type;
            }
        }
    }

    // 変更なしの場合は元の型を返す
    return type;
}

}  // namespace cm::mir
