// ============================================================
// 単相化 - 構造体の特殊化（収集・生成・型参照の書き換え）
// ============================================================

#include "internal/base/debug.hpp"
#include "mono_internal.hpp"
#include "monomorphization.hpp"
#include "monomorphization_utils.hpp"

#include <iostream>

namespace cm::mir {

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

        // ジェネリック関数内のローカル変数はスキップ（関数モノモーフィゼーション時に処理される）
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
    mir_struct->is_extern = base_struct->is_extern;

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
