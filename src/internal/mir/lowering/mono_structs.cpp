// ============================================================
// 単相化 - 構造体の特殊化（収集・生成・型参照の書き換え）
// ============================================================

#include "internal/base/debug.hpp"
#include "internal/mir/lowering/mono/typekey.hpp"
#include "mono_internal.hpp"
#include "monomorphization.hpp"
#include "monomorphization_utils.hpp"

#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cm::mir {

// ジェネリック構造体のモノモーフィゼーション
void Monomorphization::monomorphize_structs(MirProgram& program) {
    if (!hir_struct_defs)
        return;

    // 必要な構造体特殊化を収集
    // key: 特殊化構造体名, value: (基本名, 型引数ツリーのリスト)
    std::map<std::string, std::pair<std::string, std::vector<hir::TypePtr>>> needed;

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
    std::map<std::string, std::pair<std::string, std::vector<hir::TypePtr>>>& needed) {
    if (!hir_struct_defs || !hir_funcs)
        return;

    // ジェネリック構造体のリストを作成
    std::unordered_set<std::string> generic_structs;

    for (const auto& [name, st] : *hir_struct_defs) {
        if (st && !st->generic_params.empty()) {
            generic_structs.insert(name);
            for (const auto& param : st->generic_params) {
                all_generic_param_names.insert(param.name);
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
                all_generic_param_names.insert(param.name);
            }
        }
    }

    if (generic_structs.empty())
        return;

    // 全関数のローカル変数の型を走査（ジェネリック関数はスキップ）
    for (const auto& func : program.functions) {
        if (!func)
            continue;

        // ジェネリック関数内のローカル変数はスキップ（関数モノモーフィゼーション時に処理される）
        if (generic_func_names.count(func->name) > 0) {
            continue;
        }

        for (auto& local : func->locals) {
            if (!local.type)
                continue;

            // 構造体型でtype_argsがある場合（型引数ツリーを直接引き渡す。C7: 再パース廃止）
            if ((local.type->kind == hir::TypeKind::Struct ||
                 local.type->kind == hir::TypeKind::TypeAlias) &&
                !local.type->type_args.empty() && generic_structs.count(local.type->name) > 0) {
                // 未解決のジェネリック型パラメータが残っている場合はスキップ
                if (tree_has_generic_param(local.type))
                    continue;

                // 置換に使うツリーを正準化（フラット名リーフの復号）
                std::vector<hir::TypePtr> norm_args;
                norm_args.reserve(local.type->type_args.size());
                for (const auto& a : local.type->type_args)
                    norm_args.push_back(normalize_spec_arg_tree(a));
                std::string spec_name = struct_symbol_key(local.type->name, norm_args);
                if (needed.find(spec_name) == needed.end()) {
                    needed[spec_name] = {local.type->name, norm_args};
                    debug_msg("MONO", "Need struct specialization: " + spec_name);
                }
            }

            // 既にマングリング済みの構造体名（Node__intなど）を検出
            if ((local.type->kind == hir::TypeKind::Struct ||
                 local.type->kind == hir::TypeKind::TypeAlias) &&
                (local.type->name.find("__") != std::string::npos ||
                 typekey::is_encoded_key(local.type->name)) &&
                hir_struct_defs->find(local.type->name) == hir_struct_defs->end()) {
                // ユーザー定義に同名の構造体がある場合は特殊化と混同しない（C8）
                // 基本名を抽出（Node__int -> Node。$エンコード名はtypekeyの可逆復号を優先する）
                std::string base_name;
                std::vector<hir::TypePtr> type_args;
                if (typekey::is_encoded_key(local.type->name)) {
                    base_name = typekey::base_name_of(local.type->name);
                    if (generic_structs.count(base_name) > 0) {
                        type_args = typekey::decode_type_args(local.type->name);
                    }
                } else {
                    auto pos = local.type->name.find("__");
                    base_name = local.type->name.substr(0, pos);
                    if (generic_structs.count(base_name) > 0) {
                        type_args =
                            parse_flat_type_args(base_name, local.type->name.substr(pos + 2));
                    }
                }
                {
                    if (!type_args.empty()) {
                        // 置換に使うツリーを正準化（フラット名リーフの復号）
                        for (auto& a : type_args)
                            a = normalize_spec_arg_tree(a);
                        // フラット名産生の全廃に伴い、既存のフラット名ローカルも正準キーへ改名して収束させる
                        // （生成されるMirStructは正準キーで登録されるため、ローカル名との不一致はレイアウト解決欠落になる）
                        const std::string canonical = struct_symbol_key(base_name, type_args);
                        if (canonical != local.type->name) {
                            local.type = std::make_shared<hir::Type>(hir::TypeKind::Struct);
                            local.type->name = canonical;
                        }
                        if (needed.find(canonical) == needed.end()) {
                            needed[canonical] = {base_name, type_args};
                        }
                    }
                }
            }
        }
    }
}

// 特殊化構造体を生成（型引数はhir::Typeツリー。ネストした特殊化も再帰的に生成する）
void Monomorphization::generate_specialized_struct(MirProgram& program,
                                                   const std::string& base_name,
                                                   const std::vector<hir::TypePtr>& type_args) {
    if (!hir_struct_defs)
        return;

    std::string spec_name = struct_symbol_key(base_name, type_args);

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

    // 再帰的なネスト生成での無限ループを防ぐため先に登録する
    generated_struct_specializations.insert(spec_name);

    // 型パラメータ→具体的な型のマッピングを作成（ツリーを直接使用）
    std::unordered_map<std::string, hir::TypePtr> type_subst;
    for (size_t i = 0; i < base_struct->generic_params.size() && i < type_args.size(); ++i) {
        const auto& param_name = base_struct->generic_params[i].name;
        type_subst[param_name] = type_args[i];
        debug_msg("MONO", "Struct type substitution: " + param_name + " -> " +
                              typekey::display_name(type_args[i]));
    }

    // 特殊化構造体を生成
    auto mir_struct = std::make_unique<MirStruct>();
    mir_struct->name = spec_name;
    mir_struct->is_css = base_struct->is_css;
    mir_struct->is_extern = base_struct->is_extern;

    // フィールドの型を特殊化する。
    // 注: レイアウト（サイズ・アライメント・オフセット）はここでは計算しない（M13。
    // レイアウトはLoweringContext::layout_size/layout_alignとLLVMのDataLayoutが唯一の情報源。
    // スライス要素等の個別サイズが必要な箇所はcalculate_specialized_type_sizeを直接使う）
    for (const auto& field : base_struct->fields) {
        MirStructField mir_field;
        mir_field.name = field.name;

        // 構造を保ったまま型パラメータを置換する
        hir::TypePtr size_tree = substitute_type_tree(field.type, type_subst);

        // ネストしたジェネリックインスタンスの特殊化を生成し、シンボル名参照へ正規化する
        mir_field.type = to_symbol_type(program, size_tree);

        debug_msg("MONO", "  Field: " + field.name + " -> " +
                              (mir_field.type ? hir::type_to_string(*mir_field.type) : "unknown"));
        mir_struct->fields.push_back(std::move(mir_field));
    }

    // プログラムに追加
    program.structs.push_back(std::move(mir_struct));

    debug_msg("MONO", "Generated specialized struct: " + spec_name);
}

// 置換済みツリー内のジェネリックインスタンスを特殊化生成し、シンボル名参照へ書き換える
hir::TypePtr Monomorphization::to_symbol_type(MirProgram& program, const hir::TypePtr& type) {
    if (!type)
        return nullptr;

    // ポインタ・参照・配列は要素型を再帰的に正規化する
    if (type->kind == hir::TypeKind::Pointer || type->kind == hir::TypeKind::Reference ||
        type->kind == hir::TypeKind::Array) {
        auto elem = to_symbol_type(program, type->element_type);
        if (elem == type->element_type)
            return type;
        auto result = std::make_shared<hir::Type>(*type);
        result->element_type = elem;
        if (type->kind == hir::TypeKind::Pointer)
            result->name = "ptr_" + get_type_name(elem);
        return result;
    }

    // ジェネリックインスタンス（type_argsあり）はシンボルキーへ正規化する
    if ((type->kind == hir::TypeKind::Struct || type->kind == hir::TypeKind::Generic ||
         type->kind == hir::TypeKind::TypeAlias) &&
        !type->type_args.empty()) {
        // 未解決の型パラメータが残る場合はそのまま（関数特殊化時に解決される）
        if (tree_has_generic_param(type))
            return type;

        std::string base = type->name;
        auto lt = base.find('<');
        if (lt != std::string::npos)
            base = base.substr(0, lt);

        // 既にマングリング済みの名前はそのままシンボルとし、特殊化の存在のみ保証する
        if (base.find("__") != std::string::npos || base.find('$') != std::string::npos) {
            if (hir_struct_defs && hir_struct_defs->find(base) == hir_struct_defs->end()) {
                // $エンコード名はtypekeyの可逆復号を優先し、フラット名のみ逆算ヒューリスティックへ渡す
                std::string flat_base;
                std::vector<hir::TypePtr> parsed;
                if (typekey::is_encoded_key(base)) {
                    flat_base = typekey::base_name_of(base);
                    parsed = typekey::decode_type_args(base);
                } else {
                    flat_base = base.substr(0, base.find("__"));
                    parsed = parse_flat_type_args(flat_base, base.substr(flat_base.size() + 2));
                }
                auto def_it = hir_struct_defs->find(flat_base);
                if (def_it != hir_struct_defs->end() && def_it->second &&
                    !def_it->second->generic_params.empty() && !parsed.empty()) {
                    generate_specialized_struct(program, flat_base, parsed);
                }
            }
            auto result = std::make_shared<hir::Type>(hir::TypeKind::Struct);
            result->name = base;
            return result;
        }

        // 基底がジェネリック構造体なら特殊化を生成する（ネスト分の再帰）
        if (hir_struct_defs) {
            auto def_it = hir_struct_defs->find(base);
            if (def_it != hir_struct_defs->end() && def_it->second &&
                !def_it->second->generic_params.empty()) {
                generate_specialized_struct(program, base, type->type_args);
            }
        }

        auto result = std::make_shared<hir::Type>(hir::TypeKind::Struct);
        result->name = struct_symbol_key(base, type->type_args);
        // シンボル名に型引数を埋め込んだためtype_argsは保持しない（二重マングリング防止）
        return result;
    }

    return type;
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
        // localId -> (base_struct_name, 型引数ツリー)
        std::unordered_map<LocalId, std::pair<std::string, std::vector<hir::TypePtr>>> struct_info;

        for (size_t i = 0; i < func->locals.size(); ++i) {
            auto& local = func->locals[i];
            if (!local.type)
                continue;

            // ジェネリック構造体型の場合（型引数ツリーからシンボルキーを生成。C7）
            if ((local.type->kind == hir::TypeKind::Struct ||
                 local.type->kind == hir::TypeKind::TypeAlias) &&
                !local.type->type_args.empty() && generic_structs.count(local.type->name) > 0) {
                if (!tree_has_generic_param(local.type)) {
                    std::string spec_name =
                        struct_symbol_key(local.type->name, local.type->type_args);
                    struct_info[i] = {local.type->name, local.type->type_args};

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
                    std::vector<hir::TypePtr> type_args;
                    bool has_struct_info = false;

                    if (info_it != struct_info.end()) {
                        has_struct_info = true;
                        base_name = info_it->second.first;
                        type_args = info_it->second.second;
                    } else {
                        // struct_infoにないが、マングリング済み構造体名を持つローカルの場合
                        // 例: _4: Iterator__int / Box$1$3$int
                        if (source_local < func->locals.size()) {
                            auto& local_type = func->locals[source_local].type;
                            if (local_type && local_type->kind == hir::TypeKind::Struct &&
                                hir_struct_defs->find(local_type->name) == hir_struct_defs->end()) {
                                std::string type_name = local_type->name;
                                if (typekey::is_encoded_key(type_name)) {
                                    // '$'エンコード名から基底名と型引数を復元
                                    base_name = typekey::base_name_of(type_name);
                                    type_args = typekey::decode_type_args(type_name);
                                    if (!type_args.empty() &&
                                        generic_structs.count(base_name) > 0) {
                                        has_struct_info = true;
                                    }
                                } else {
                                    size_t pos = type_name.find("__");
                                    if (pos != std::string::npos) {
                                        base_name = type_name.substr(0, pos);
                                        // 型引数を抽出（1パラメータ基底はセグメントを結合）
                                        type_args = parse_flat_type_args(base_name,
                                                                         type_name.substr(pos + 2));
                                        if (generic_structs.count(base_name) > 0) {
                                            has_struct_info = true;
                                        }
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
                        std::vector<hir::TypePtr> current_type_args = type_args;
                        bool is_final_type_resolved = false;

                        // 具体型ツリーへ遷移する際に基底名と型引数を更新する共通処理
                        auto descend_into = [&](const hir::TypePtr& t) {
                            if (!t || t->kind != hir::TypeKind::Struct)
                                return;
                            std::string next_base = t->name;
                            auto lt = next_base.find('<');
                            if (lt != std::string::npos)
                                next_base = next_base.substr(0, lt);
                            current_struct_name = next_base;
                            current_type_args = t->type_args;
                        };

                        for (const auto& proj : place->projections) {
                            // 配列フィールドの要素アクセス（.items[i]）は要素型へ降下する。
                            // 従来はここでbreakし、フィールドの配列型（未置換のT[4]）が
                            // そのまま要素destの型として上書きされていた（JIT O0で
                            // [4 x %T]のallocaが生成されスタック隣接領域を壊す）
                            if (proj.kind == ProjectionKind::Index) {
                                if (current_field_type &&
                                    current_field_type->kind == hir::TypeKind::Array &&
                                    current_field_type->element_type) {
                                    current_field_type = current_field_type->element_type;
                                    if (current_field_type->kind == hir::TypeKind::Struct) {
                                        descend_into(current_field_type);
                                    }
                                    continue;
                                }
                                // 要素型を解決できない場合はloweringが付けた型を維持する
                                current_field_type = nullptr;
                                break;
                            }
                            if (proj.kind != ProjectionKind::Field) {
                                // Deref等は従来どおりここまでに解決したフィールド型を適用する
                                // （挙動を変えるとOption等の既存チェーンが退行するため、
                                // 上書き抑止はIndex射影の解決不能ケースに限定する）
                                break;
                            }

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
                                    // 直接型パラメータ名と一致する場合（実引数ツリーをそのまま採用）
                                    if (field_type->name == params_it->second[pi]) {
                                        current_field_type = current_type_args[pi];
                                        // 置換後の型が構造体の場合、次のフィールドアクセスのために情報を更新
                                        descend_into(current_field_type);
                                        is_final_type_resolved = true;
                                        break;
                                    }
                                    // 配列型でelement_typeが型パラメータの場合 (T[4] → short[4])
                                    if (field_type->kind == hir::TypeKind::Array &&
                                        field_type->element_type &&
                                        field_type->element_type->name == params_it->second[pi]) {
                                        auto arr = std::make_shared<hir::Type>(*field_type);
                                        arr->element_type = current_type_args[pi];
                                        current_field_type = arr;
                                        is_final_type_resolved = true;
                                        break;
                                    }
                                    // ポインタ型でelement_typeが型パラメータの場合 (*T → *int)
                                    if (field_type->kind == hir::TypeKind::Pointer &&
                                        field_type->element_type &&
                                        field_type->element_type->name == params_it->second[pi]) {
                                        // ポインタ要素型を置換
                                        current_field_type =
                                            hir::make_pointer(current_type_args[pi]);
                                        is_final_type_resolved = true;
                                        break;
                                    }
                                }
                            }

                            // フィールド型がジェネリックパラメータでない場合
                            if (!is_final_type_resolved || current_field_type == nullptr) {
                                current_field_type = field_type;
                                if (field_type->kind == hir::TypeKind::Struct) {
                                    descend_into(field_type);
                                }
                            }
                            is_final_type_resolved = false;  // 次のプロジェクションのためにリセット
                        }

                        // 最終的なフィールド型が得られた場合、dest_localの型を更新
                        // （ジェネリックインスタンスは特殊化を生成してシンボル名参照へ正規化する）
                        if (current_field_type) {
                            func->locals[dest_local].type =
                                to_symbol_type(program, current_field_type);
                            debug_msg("MONO",
                                      "Updated field access type in " + func->name + ": " +
                                          func->locals[dest_local].name + " -> " +
                                          hir::type_to_string(*func->locals[dest_local].type));
                        }
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
        // Bug#10修正: Deref経由のコピー元マップ
        // deref_sources[dest_local] = ptr_local （_tmp = Use(Copy(Deref(ptr)))）
        std::unordered_map<LocalId, LocalId> deref_sources;

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
                } else if (assign_data.place.projections.empty() &&
                           source_place.projections.size() == 1 &&
                           source_place.projections[0].kind == ProjectionKind::Deref) {
                    // Bug#10: _tmp = Use(Copy(Deref(ptr))) パターン
                    // ポインタ経由のimplメソッド呼出し（ptr->method()）で発生
                    deref_sources[assign_data.place.local] = source_place.local;
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

            // Bug#10修正: コピーチェーン末端がderef_sourcesにある場合
            // ptr->method() パターン: original_localはDeref(ptr)で作られた一時変数
            // Derefコピー先(original_local)への参照を作成して渡す
            // メソッド呼び出し後にoriginal_localの内容をptr先に書き戻す
            if (deref_sources.count(original_local) > 0) {
                LocalId ptr_local = deref_sources[original_local];
                if (ptr_local < func->locals.size()) {
                    auto& ptr_type = func->locals[ptr_local].type;
                    if (ptr_type && ptr_type->kind == hir::TypeKind::Pointer &&
                        ptr_type->element_type &&
                        (ptr_type->element_type->name == type_name ||
                         ptr_type->element_type->name.find(type_name + "__") == 0)) {
                        // original_local（Derefで作られた構造体コピー）への参照を作成
                        auto& deref_local_type = func->locals[original_local].type;
                        if (deref_local_type) {
                            LocalId ref_id = static_cast<LocalId>(func->locals.size());
                            std::string ref_name = "_self_ref_" + std::to_string(ref_id);
                            auto ref_type = hir::make_pointer(deref_local_type);
                            func->locals.emplace_back(ref_id, ref_name, ref_type, false, false);

                            // Derefコピー先への参照を作成
                            auto ref_stmt = MirStatement::assign(
                                MirPlace{ref_id}, MirRvalue::ref(MirPlace{original_local}, false));
                            block->statements.push_back(std::move(ref_stmt));

                            // 呼び出しの第1引数を参照に変更
                            call_data.args[0] = MirOperand::copy(MirPlace{ref_id});

                            // メソッド呼び出し後にoriginal_localの内容をptr先に書き戻す
                            // success blockにStore(Deref(ptr), Copy(original_local))を追加
                            if (call_data.success != mir::INVALID_BLOCK) {
                                for (auto& succ_block : func->basic_blocks) {
                                    if (succ_block && succ_block->id == call_data.success) {
                                        // Deref(ptr)への代入: *ptr = original_local
                                        MirPlace deref_place{ptr_local};
                                        deref_place.projections.push_back(PlaceProjection::deref());
                                        auto writeback_stmt = MirStatement::assign(
                                            deref_place, MirRvalue::use(MirOperand::copy(
                                                             MirPlace{original_local})));
                                        // 成功ブロックの先頭に挿入
                                        succ_block->statements.insert(
                                            succ_block->statements.begin(),
                                            std::move(writeback_stmt));
                                        break;
                                    }
                                }
                            }

                            debug_msg("MONO", "Bug#10: ptr->method() fixed for " + func_name_ref +
                                                  " in " + func->name + " (deref_source: local " +
                                                  std::to_string(original_local) +
                                                  " -> ref local " + std::to_string(ref_id) +
                                                  ", writeback to ptr local " +
                                                  std::to_string(ptr_local) + ")");
                        }
                        continue;
                    }
                }
            }

            if (original_local >= func->locals.size())
                continue;

            auto& local_type = func->locals[original_local].type;
            if (!local_type)
                continue;

            // Bug#10修正: ネスト呼出しでselfが既にポインタ型の場合
            // Ref経由で作られたポインタ（expr_call.cppのDeref+Ref処理）は正しく構築済みなのでそのまま維持する。
            // 元のポインタ変数（Pointer型ローカル）が直接渡された場合のみ
            // 引数を維持してスキップする。
            if (local_type->kind == hir::TypeKind::Pointer) {
                // Ref経由のポインタ、または既にptr->method修正済みの場合は変更不要
                // そのままスキップ（引数を変更しない）
                continue;
            }

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
