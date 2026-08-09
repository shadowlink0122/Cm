// 単相化 - ジェネリック関数の特殊化生成と元関数の削除（旧実装スタブを含む）

#include "internal/base/debug.hpp"
#include "internal/base/target.hpp"
#include "internal/mir/lowering/mono/typekey.hpp"
#include "internal/mir/lowering/mono_internal.hpp"
#include "internal/mir/lowering/monomorphization.hpp"
#include "internal/mir/lowering/monomorphization_utils.hpp"

#include <cstdio>
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

// ジェネリック関数の特殊化を生成
void Monomorphization::generate_generic_specializations(
    MirProgram& program,
    const std::unordered_map<std::string, const hir::HirFunction*>& hir_functions,
    const SpecRequests& needed) {
    for (const auto& [specialized_name, req] : needed) {
        const std::string& func_name = req.generic_name;
        const std::vector<hir::TypePtr>& type_args = req.type_args;

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

        // HIR関数から型パラメータ名を取得（総称演算子implはHIR関数(HirFunction)を持たないためnull許容。
        // その場合は総称シンボル名の<...>部から型パラメータ名を復元する）
        auto hir_it = hir_functions.find(func_name);
        const hir::HirFunction* hir_func =
            (hir_it != hir_functions.end()) ? hir_it->second : nullptr;

        // 型置換マップを作成（パラメータ名→型引数ツリー。名前からの型復元は行わない）
        std::unordered_map<std::string, hir::TypePtr> type_subst;
        // 型名置換マップ（クローン本体内のマングル済み呼び出し名の書き換え用。
        // arg_symbol_keyのフラット規約でエンコードし、次パスのスキャンが同じ規約で復元する）
        std::unordered_map<std::string, std::string> type_name_subst;

        // 型パラメータ名の取得: 総称関数はgeneric_params、implメソッドは総称シンボル名の<...>部
        std::vector<std::string> param_names;
        if (hir_func && !hir_func->generic_params.empty()) {
            for (const auto& gp : hir_func->generic_params) {
                param_names.push_back(gp.name);
            }
        } else {
            auto angle_start = func_name.find('<');
            auto angle_end = func_name.find('>');
            if (angle_start != std::string::npos && angle_end != std::string::npos) {
                for (auto& p : split_type_args(
                         func_name.substr(angle_start + 1, angle_end - angle_start - 1))) {
                    param_names.push_back(std::move(p));
                }
            }
        }
        for (size_t i = 0; i < param_names.size() && i < type_args.size(); ++i) {
            type_subst[param_names[i]] = type_args[i];
            type_name_subst[param_names[i]] = arg_symbol_key(type_args[i]);
            debug_msg("MONO", "Type substitution: " + param_names[i] + " -> " +
                                  get_type_name(type_args[i]));
        }

        // 特殊化関数を生成（MIR関数をコピーして型を置換）
        auto specialized = std::make_unique<MirFunction>();
        specialized->name = specialized_name;
        specialized->entry_block = original_mir->entry_block;
        specialized->return_local = original_mir->return_local;
        specialized->arg_locals = original_mir->arg_locals;

        // ローカル変数をコピーして型を置換
        // ジェネリックimplメソッドの場合、self型は基底構造体名+型引数ツリーのシンボルキーで確定する
        std::string inferred_self_type;
        auto angle_pos = func_name.find("<");
        auto dunder_pos = func_name.find(">__");
        if (angle_pos != std::string::npos && dunder_pos != std::string::npos) {
            inferred_self_type = struct_symbol_key(func_name.substr(0, angle_pos), type_args);
        }
        // 総称演算子impl（Foo<T>__op_eq等）は、self/other等の値パラメータが基底構造体名（型引数なし）で
        // 型付けされている。substitute_type_in_typeでは基底名は変化しないため、特殊化構造体キーへ明示的に再型付けする
        const bool is_operator_fn = func_name.find(">__op_") != std::string::npos;
        const std::string op_base =
            (angle_pos != std::string::npos) ? func_name.substr(0, angle_pos) : std::string();

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
                        // 型引数ツリーを持つ場合はそのまま特殊化を生成（C7: 再パース不要）
                        if (target && target->kind == hir::TypeKind::Struct &&
                            !target->type_args.empty() && !tree_has_generic_param(target)) {
                            std::string base = target->name;
                            auto lt = base.find('<');
                            if (lt != std::string::npos)
                                base = base.substr(0, lt);
                            generate_specialized_struct(program, base, target->type_args);
                        }
                        // 構造体型でマングリング済みの名前（__を含む）を持つ場合
                        // （ユーザー定義に同名の構造体がある場合は特殊化と混同しない。C8）
                        else if (target && target->kind == hir::TypeKind::Struct &&
                                 (target->name.find("__") != std::string::npos ||
                                  typekey::is_encoded_key(target->name)) &&
                                 (!hir_struct_defs ||
                                  hir_struct_defs->find(target->name) == hir_struct_defs->end())) {
                            // 基本名と型引数を抽出（$エンコード名はtypekeyの可逆復号を優先。フラット1パラメータ基底はセグメントを結合）
                            std::string base_name;
                            std::vector<hir::TypePtr> struct_type_args;
                            if (typekey::is_encoded_key(target->name)) {
                                base_name = typekey::base_name_of(target->name);
                                struct_type_args = typekey::decode_type_args(target->name);
                            } else {
                                auto pos = target->name.find("__");
                                base_name = target->name.substr(0, pos);
                                struct_type_args =
                                    parse_flat_type_args(base_name, target->name.substr(pos + 2));
                            }
                            // 構造体特殊化を生成
                            if (!struct_type_args.empty()) {
                                generate_specialized_struct(program, base_name, struct_type_args);
                            }
                        }
                    };
                    ensure_struct_specialization(new_local.type);

                    // 総称演算子implの値パラメータ等（型引数なしの基底構造体名）を特殊化構造体キーへ再型付け
                    if (is_operator_fn && !inferred_self_type.empty() && new_local.type &&
                        new_local.type->kind == hir::TypeKind::Struct &&
                        new_local.type->name == op_base && new_local.type->type_args.empty()) {
                        auto spec_struct_type = std::make_shared<hir::Type>(hir::TypeKind::Struct);
                        spec_struct_type->name = inferred_self_type;
                        new_local.type = spec_struct_type;
                    }
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
            // ジェネリック型パラメータのsizeofがHIR段階でマーカー型として保存され、モノモフィゼーション時に実際の型サイズに置換される
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
                    } else if (type_param_name.find('<') != std::string::npos) {
                        // 複合ジェネリック型（sizeof_for_QueueNode<T> 等）:
                        // 型引数をtype_substで置換し、置換後フィールドの自然アライメントレイアウトでサイズを再計算する
                        std::string base = type_param_name.substr(0, type_param_name.find('<'));
                        std::string args_str = type_param_name.substr(base.size() + 1);
                        if (!args_str.empty() && args_str.back() == '>') {
                            args_str.pop_back();
                        }
                        const hir::HirStruct* base_struct = nullptr;
                        if (hir_struct_defs && hir_struct_defs->count(base)) {
                            base_struct = hir_struct_defs->at(base);
                        }
                        if (base_struct) {
                            auto arg_names = split_type_args(args_str);
                            std::unordered_map<std::string, hir::TypePtr> field_subst;
                            for (size_t ai = 0;
                                 ai < base_struct->generic_params.size() && ai < arg_names.size();
                                 ++ai) {
                                auto sit = type_subst.find(arg_names[ai]);
                                field_subst[base_struct->generic_params[ai].name] =
                                    (sit != type_subst.end()) ? sit->second
                                                              : make_type_from_name(arg_names[ai]);
                            }
                            auto align_to = [](int64_t offset, int64_t align) {
                                return (offset + align - 1) / align * align;
                            };
                            int64_t offset = 0;
                            int64_t max_align = 1;
                            for (const auto& f : base_struct->fields) {
                                auto ft = substitute_type_in_type(f.type, field_subst, this);
                                int64_t fa = calculate_specialized_type_align(ft);
                                int64_t fs = calculate_specialized_type_size(ft);
                                max_align = std::max(max_align, fa);
                                offset = align_to(offset, fa) + fs;
                            }
                            int64_t total = align_to(offset, max_align);
                            const_data->value = total > 0 ? total : 8;
                            const_data->type = hir::make_long();
                        }
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
            // 要素型のデストラクタ名を型引数ツリーのシンボルキーから構築する
            // 要素の構造体正準キー（$）を関数名ドメイン（base__argkey）へ変換してdtor名を組む
            std::string element_type = arg_symbol_key(type_args[0]);
            std::string element_dtor_name = typekey::spec_fn_prefix(element_type) + "__dtor";

            // 要素型にデストラクタが存在するかチェック
            bool has_element_dtor = false;
            for (const auto& func : program.functions) {
                if (func && func->name == element_dtor_name) {
                    has_element_dtor = true;
                    break;
                }
            }

            // M15: 要素型自身がジェネリック特殊化（Vector__int等）の場合、その特殊化デストラクタは
            // この時点では未生成のことがある。基底のジェネリックデストラクタ（Vector<T>__dtor）が
            // 存在すれば呼び出しを挿入してよい（不動点ループが本関数の呼び出しをスキャンして
            // 特殊化を連鎖生成するため、多段ネストの各段で要素データが解放される）
            if (!has_element_dtor && (element_type.find("__") != std::string::npos ||
                                      typekey::is_encoded_key(element_type))) {
                std::string elem_base = typekey::spec_base_name(element_type);
                for (const auto& func : program.functions) {
                    if (func && func->name.rfind(elem_base + "<", 0) == 0 &&
                        func->name.size() > 6 &&
                        func->name.substr(func->name.size() - 6) == "__dtor") {
                        has_element_dtor = true;
                        break;
                    }
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

        // 置換完了の検証（monomorphization-typed-instantiation 第3段）:
        // 生成した特殊化関数のローカル型に未置換のジェネリック型パラメータが残っていないことを検査する。
        // 残存は無置換特殊化（N2の根因）であり、警告出力でテストスイートが検出する
        for (size_t li = 0; li < specialized->locals.size(); ++li) {
            const auto& lt = specialized->locals[li].type;
            if (lt && tree_has_generic_param(lt)) {
                std::fprintf(stderr,
                             "[MONO] WARNING: unsubstituted generic type remains in %s "
                             "(local %zu '%s': %s)\n",
                             specialized_name.c_str(), li, specialized->locals[li].name.c_str(),
                             get_type_name(lt).c_str());
            }
        }

        program.functions.push_back(std::move(specialized));

        // 呼び出し箇所を書き換え
        for (const auto& [caller_name, block_idx] : req.call_sites) {
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
                                    // 呼び出し結果ローカルの未置換ジェネリック型（T・T[]）を具体型へ差し替える（N2）。
                                    // 残ったままだと後続利用の型解決がptrtointへフォールバックし、値でなくアドレスが流れる
                                    if (call_data.destination && hir_func) {
                                        LocalId dl = call_data.destination->local;
                                        if (dl < func->locals.size() && func->locals[dl].type) {
                                            auto& dtype = func->locals[dl].type;
                                            const auto& gps = hir_func->generic_params;
                                            bool patched = false;
                                            for (size_t gi = 0;
                                                 gi < gps.size() && gi < type_args.size(); ++gi) {
                                                if (dtype->name == gps[gi].name) {
                                                    func->locals[dl].type = type_args[gi];
                                                    patched = true;
                                                    break;
                                                }
                                                if (dtype->kind == hir::TypeKind::Array &&
                                                    dtype->element_type &&
                                                    dtype->element_type->name == gps[gi].name) {
                                                    auto nt = std::make_shared<hir::Type>(*dtype);
                                                    nt->element_type = type_args[gi];
                                                    func->locals[dl].type = nt;
                                                    patched = true;
                                                    break;
                                                }
                                            }
                                            // println/print系のディスパッチは単相化前のT型（int既定）で
                                            // 焼き付いているため、パッチしたローカルを引数に取る出力呼び出しを
                                            // 具体型に合わせて選び直す
                                            if (patched) {
                                                fixup_println_dispatch(func.get(), dl);
                                            }
                                        }
                                    }
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

// 単相化で型が確定したローカルを引数に取るprintln/print系呼び出しのディスパッチを選び直す（N2）。
// println(first_of(s))のようにT型のまま既定のcm_println_intへloweringされた呼び出しが対象
void Monomorphization::fixup_println_dispatch(MirFunction* caller, LocalId local_id) {
    if (!caller || local_id >= caller->locals.size() || !caller->locals[local_id].type) {
        return;
    }
    const auto kind = caller->locals[local_id].type->kind;
    std::string suffix;
    switch (kind) {
        case hir::TypeKind::String:
            suffix = "string";
            break;
        case hir::TypeKind::Double:
        case hir::TypeKind::Float:
            suffix = "double";
            break;
        case hir::TypeKind::Long:
        case hir::TypeKind::ISize:
            suffix = "long";
            break;
        case hir::TypeKind::ULong:
        case hir::TypeKind::USize:
            suffix = "ulong";
            break;
        case hir::TypeKind::UInt:
            suffix = "uint";
            break;
        case hir::TypeKind::Bool:
            suffix = "bool";
            break;
        default:
            return;  // int系はそのまま
    }
    for (auto& block : caller->basic_blocks) {
        if (!block || !block->terminator || block->terminator->kind != MirTerminator::Call) {
            continue;
        }
        auto& cd = std::get<MirTerminator::CallData>(block->terminator->data);
        if (!cd.func || cd.func->kind != MirOperand::FunctionRef || cd.args.size() != 1) {
            continue;
        }
        const auto& cname = std::get<std::string>(cd.func->data);
        if (cname != "cm_println_int" && cname != "cm_print_int") {
            continue;
        }
        const auto& arg = cd.args[0];
        if (!arg || (arg->kind != MirOperand::Copy && arg->kind != MirOperand::Move)) {
            continue;
        }
        const auto* pl = std::get_if<MirPlace>(&arg->data);
        if (!pl || pl->local != local_id || !pl->projections.empty()) {
            continue;
        }
        const std::string prefix = (cname == "cm_println_int") ? "cm_println_" : "cm_print_";
        cd.func = MirOperand::function_ref(prefix + suffix);
        debug_msg("MONO", "Fixed println dispatch for local " + std::to_string(local_id) + " -> " +
                              prefix + suffix);
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
            // 3. 総称演算子impl原本（Foo<T>__op_eq等。特殊化済みで到達不能）。
            // ジェネリック関数集合(generic_funcs)はhir_functions由来で演算子を含まないため個別に除去する
            else if (func_name.find('<') != std::string::npos &&
                     func_name.find(">__op_") != std::string::npos) {
                should_remove = true;
                debug_msg("MONO", "Removing unspecialized generic operator: " + func_name);
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

// 生成済み構造体特殊化を起点に、総称演算子implの特殊化要求を種蒔きする。
// 演算子（==/<等）の呼び出しは生のBinaryOpのままPass 6まで残り、scan_generic_callsに現れない。
// そのため呼び出しサイト駆動では特殊化できず、構造体特殊化集合（Foo__int等）から要求を組み立てる
void Monomorphization::seed_operator_specializations(MirProgram& program, SpecRequests& needed) {
    // 総称演算子impl関数（名前に'<'と">__op_"を含む）を基底名・接尾辞で収集する
    struct GenOp {
        std::string func_name;  // 例: Foo<T>__op_eq
        std::string base;       // 例: Foo
        std::string suffix;     // 例: op_eq
    };
    std::vector<GenOp> gen_ops;
    for (const auto& func : program.functions) {
        if (!func)
            continue;
        const std::string& n = func->name;
        auto lt = n.find('<');
        auto op_pos = n.find(">__op_");
        if (lt == std::string::npos || op_pos == std::string::npos)
            continue;
        // ">__op_" の ">__" 3文字を飛ばした残り("op_...")が接尾辞
        gen_ops.push_back({n, n.substr(0, lt), n.substr(op_pos + 3)});
    }
    if (gen_ops.empty())
        return;

    // 演算子接尾辞→impl_infoキー（Pass 6のrewrite_struct_comparison_operatorsが引き当てる名称）
    auto op_kind_of = [](const std::string& suffix) -> std::string {
        static const std::unordered_map<std::string, std::string> kMap = {
            {"op_eq", "Eq"},         {"op_lt", "Ord"},        {"op_add", "Add"},
            {"op_sub", "Sub"},       {"op_mul", "Mul"},       {"op_div", "Div"},
            {"op_mod", "Mod"},       {"op_bitand", "BitAnd"}, {"op_bitor", "BitOr"},
            {"op_bitxor", "BitXor"}, {"op_shl", "Shl"},       {"op_shr", "Shr"}};
        auto it = kMap.find(suffix);
        return it != kMap.end() ? it->second : std::string();
    };

    for (const auto& struct_name : generated_struct_specializations) {
        // 特殊化構造体名（Foo__int / Foo$1$...）を基底名+型引数ツリーへ復元する
        auto decoded = decode_type_name(struct_name);
        if (!decoded || decoded->type_args.empty())
            continue;
        for (const auto& go : gen_ops) {
            if (go.base != decoded->name)
                continue;
            std::string kind = op_kind_of(go.suffix);
            if (kind.empty())
                continue;
            std::string spec_name = make_specialized_name(go.func_name, decoded->type_args);
            // 固定点反復での重複種蒔きを防ぐ（生成済み判定は駆動側のall_generatedが担う）
            if (!seeded_operator_specs_.insert(spec_name).second)
                continue;
            SpecRequest& req = needed[spec_name];
            if (req.generic_name.empty()) {
                req.generic_name = go.func_name;
                req.type_args = decoded->type_args;
            }
            // MirLoweringがモノモーフ化後にimpl_info[struct_name][kind]=spec_nameとして登録する
            specialized_operators_.push_back({struct_name, kind, spec_name});
            debug_msg("MONO", "Seeded operator specialization: " + spec_name + " for struct " +
                                  struct_name + " (" + kind + ")");
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

}  // namespace cm::mir
