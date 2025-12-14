#include "../../common/debug.hpp"
#include "monomorphization.hpp"

namespace cm::mir {

// デバッグ出力用
#ifdef CM_DEBUG_MONOMORPHIZATION
#include <iostream>
#define MONO_DEBUG(msg) std::cerr << "[MONO] " << msg << std::endl
#else
#define MONO_DEBUG(msg)
#endif

// ヘルパー関数：MirOperandのクローン
static MirOperandPtr clone_operand(const MirOperandPtr& op) {
    if (!op)
        return nullptr;

    auto result = std::make_unique<MirOperand>();
    result->kind = op->kind;
    result->data = op->data;  // variant内の値をコピー（参照型がないのでOK）
    return result;
}

// ヘルパー関数：MirRvalueのクローン
static MirRvaluePtr clone_rvalue(const MirRvaluePtr& rv) {
    if (!rv)
        return nullptr;

    auto result = std::make_unique<MirRvalue>();
    result->kind = rv->kind;

    switch (rv->kind) {
        case MirRvalue::Use: {
            auto& use_data = std::get<MirRvalue::UseData>(rv->data);
            result->data = MirRvalue::UseData{clone_operand(use_data.operand)};
            break;
        }
        case MirRvalue::BinaryOp: {
            auto& bin_data = std::get<MirRvalue::BinaryOpData>(rv->data);
            result->data = MirRvalue::BinaryOpData{bin_data.op, clone_operand(bin_data.lhs),
                                                   clone_operand(bin_data.rhs)};
            break;
        }
        case MirRvalue::UnaryOp: {
            auto& un_data = std::get<MirRvalue::UnaryOpData>(rv->data);
            result->data = MirRvalue::UnaryOpData{un_data.op, clone_operand(un_data.operand)};
            break;
        }
        case MirRvalue::Ref: {
            auto& ref_data = std::get<MirRvalue::RefData>(rv->data);
            result->data = ref_data;
            break;
        }
        case MirRvalue::Aggregate: {
            auto& agg_data = std::get<MirRvalue::AggregateData>(rv->data);
            std::vector<MirOperandPtr> cloned_ops;
            for (const auto& op : agg_data.operands) {
                cloned_ops.push_back(clone_operand(op));
            }
            result->data = MirRvalue::AggregateData{agg_data.kind, std::move(cloned_ops)};
            break;
        }
        case MirRvalue::Cast: {
            auto& cast_data = std::get<MirRvalue::CastData>(rv->data);
            result->data =
                MirRvalue::CastData{clone_operand(cast_data.operand), cast_data.target_type};
            break;
        }
        case MirRvalue::FormatConvert: {
            auto& fmt_data = std::get<MirRvalue::FormatConvertData>(rv->data);
            result->data =
                MirRvalue::FormatConvertData{clone_operand(fmt_data.operand), fmt_data.format_spec};
            break;
        }
    }

    return result;
}

// ヘルパー関数：MirStatementのクローン
static MirStatementPtr clone_statement(const MirStatementPtr& stmt) {
    if (!stmt)
        return nullptr;

    auto result = std::make_unique<MirStatement>();
    result->kind = stmt->kind;
    result->span = stmt->span;

    switch (stmt->kind) {
        case MirStatement::Assign: {
            auto& assign_data = std::get<MirStatement::AssignData>(stmt->data);
            result->data =
                MirStatement::AssignData{assign_data.place, clone_rvalue(assign_data.rvalue)};
            break;
        }
        case MirStatement::StorageLive:
        case MirStatement::StorageDead: {
            auto& storage_data = std::get<MirStatement::StorageData>(stmt->data);
            result->data = storage_data;
            break;
        }
        case MirStatement::Nop:
            result->data = std::monostate{};
            break;
    }

    return result;
}

// ヘルパー関数：MirTerminatorのクローン
static MirTerminatorPtr clone_terminator(const MirTerminatorPtr& term) {
    if (!term)
        return nullptr;

    auto result = std::make_unique<MirTerminator>();
    result->kind = term->kind;
    result->span = term->span;

    switch (term->kind) {
        case MirTerminator::Goto: {
            auto& goto_data = std::get<MirTerminator::GotoData>(term->data);
            result->data = goto_data;
            break;
        }
        case MirTerminator::SwitchInt: {
            auto& switch_data = std::get<MirTerminator::SwitchIntData>(term->data);
            result->data = MirTerminator::SwitchIntData{clone_operand(switch_data.discriminant),
                                                        switch_data.targets, switch_data.otherwise};
            break;
        }
        case MirTerminator::Call: {
            auto& call_data = std::get<MirTerminator::CallData>(term->data);
            std::vector<MirOperandPtr> cloned_args;
            for (const auto& arg : call_data.args) {
                cloned_args.push_back(clone_operand(arg));
            }
            result->data = MirTerminator::CallData{clone_operand(call_data.func),
                                                   std::move(cloned_args),
                                                   call_data.destination,
                                                   call_data.success,
                                                   call_data.unwind,
                                                   call_data.interface_name,
                                                   call_data.method_name,
                                                   call_data.is_virtual};
            break;
        }
        case MirTerminator::Return:
        case MirTerminator::Unreachable:
            result->data = std::monostate{};
            break;
    }

    return result;
}

// ヘルパー関数：型置換マップを使ってMirTerminatorをクローンし、メソッド呼び出しを書き換え
static MirTerminatorPtr clone_terminator_with_subst(
    const MirTerminatorPtr& term,
    const std::unordered_map<std::string, std::string>& type_name_subst) {
    if (!term)
        return nullptr;

    auto result = std::make_unique<MirTerminator>();
    result->kind = term->kind;
    result->span = term->span;

    switch (term->kind) {
        case MirTerminator::Goto: {
            auto& goto_data = std::get<MirTerminator::GotoData>(term->data);
            result->data = goto_data;
            break;
        }
        case MirTerminator::SwitchInt: {
            auto& switch_data = std::get<MirTerminator::SwitchIntData>(term->data);
            result->data = MirTerminator::SwitchIntData{clone_operand(switch_data.discriminant),
                                                        switch_data.targets, switch_data.otherwise};
            break;
        }
        case MirTerminator::Call: {
            auto& call_data = std::get<MirTerminator::CallData>(term->data);
            std::vector<MirOperandPtr> cloned_args;
            for (const auto& arg : call_data.args) {
                cloned_args.push_back(clone_operand(arg));
            }

            // 関数名を書き換え（T__method -> ConcreteType__method）
            auto cloned_func = clone_operand(call_data.func);
            if (cloned_func && cloned_func->kind == MirOperand::FunctionRef) {
                auto& func_name = std::get<std::string>(cloned_func->data);
                // 関数名が "TypeParam__method" の形式かチェック
                for (const auto& [type_param, concrete_type] : type_name_subst) {
                    std::string prefix = type_param + "__";
                    if (func_name.find(prefix) == 0) {
                        // TypeParam__method -> ConcreteType__method
                        func_name = concrete_type + func_name.substr(type_param.length());
                        debug_msg("MONO",
                                  "Rewriting method call: " + type_param + "__* -> " + func_name);
                        break;
                    }
                }
            }

            result->data = MirTerminator::CallData{std::move(cloned_func), std::move(cloned_args),
                                                   call_data.destination,  call_data.success,
                                                   call_data.unwind,       call_data.interface_name,
                                                   call_data.method_name,  call_data.is_virtual};
            break;
        }
        case MirTerminator::Return:
        case MirTerminator::Unreachable:
            result->data = std::monostate{};
            break;
    }

    return result;
}

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

// 型から型名文字列を取得するヘルパー
static std::string get_type_name(const hir::TypePtr& type) {
    if (!type)
        return "";

    // kindに基づいて名前を返す
    switch (type->kind) {
        case hir::TypeKind::Int:
            return "int";
        case hir::TypeKind::UInt:
            return "uint";
        case hir::TypeKind::Long:
            return "long";
        case hir::TypeKind::ULong:
            return "ulong";
        case hir::TypeKind::Short:
            return "short";
        case hir::TypeKind::UShort:
            return "ushort";
        case hir::TypeKind::Tiny:
            return "tiny";
        case hir::TypeKind::UTiny:
            return "utiny";
        case hir::TypeKind::Float:
            return "float";
        case hir::TypeKind::Double:
            return "double";
        case hir::TypeKind::Char:
            return "char";
        case hir::TypeKind::Bool:
            return "bool";
        case hir::TypeKind::String:
            return "string";
        case hir::TypeKind::Void:
            return "void";
        case hir::TypeKind::Struct:
        case hir::TypeKind::Interface:
        case hir::TypeKind::Generic:
            return type->name;
        default:
            return type->name.empty() ? "" : type->name;
    }
}

// 型名から正しいTypePtr型を作成するヘルパー
static hir::TypePtr make_type_from_name(const std::string& name) {
    if (name == "int")
        return hir::make_int();
    if (name == "uint")
        return hir::make_uint();
    if (name == "long")
        return hir::make_long();
    if (name == "ulong")
        return hir::make_ulong();
    if (name == "short")
        return hir::make_short();
    if (name == "ushort")
        return hir::make_ushort();
    if (name == "tiny")
        return hir::make_tiny();
    if (name == "utiny")
        return hir::make_utiny();
    if (name == "float")
        return hir::make_float();
    if (name == "double")
        return hir::make_double();
    if (name == "char")
        return hir::make_char();
    if (name == "bool")
        return hir::make_bool();
    if (name == "string")
        return hir::make_string();
    if (name == "void")
        return hir::make_void();
    // ユーザー定義型（構造体など）
    return hir::make_named(name);
}

// 引数の型から型パラメータを推論
std::vector<std::string> Monomorphization::infer_type_args(const MirFunction* caller,
                                                           const MirTerminator::CallData& call_data,
                                                           const hir::HirFunction* callee) {
    std::vector<std::string> result;
    if (!callee || callee->generic_params.empty())
        return result;

    // 各型パラメータに対して推論
    for (const auto& generic_param : callee->generic_params) {
        std::string inferred_type;

        // パラメータから型を推論
        for (size_t i = 0; i < callee->params.size() && i < call_data.args.size(); ++i) {
            const auto& param = callee->params[i];

            // パラメータ型がジェネリック型パラメータと一致するか
            if (param.type && param.type->name == generic_param.name) {
                // 引数の型を取得
                const auto& arg = call_data.args[i];
                if (arg && arg->kind == MirOperand::Copy) {
                    if (auto* place = std::get_if<MirPlace>(&arg->data)) {
                        if (place->local < caller->locals.size()) {
                            auto& local = caller->locals[place->local];
                            std::string type_name = get_type_name(local.type);
                            if (!type_name.empty()) {
                                inferred_type = type_name;
                                break;
                            }
                        }
                    }
                } else if (arg && arg->kind == MirOperand::Constant) {
                    if (auto* constant = std::get_if<MirConstant>(&arg->data)) {
                        std::string type_name = get_type_name(constant->type);
                        if (!type_name.empty()) {
                            inferred_type = type_name;
                            break;
                        }
                    }
                }
            }
        }

        if (inferred_type.empty()) {
            // 推論できなかった場合、デフォルトとしてintを使用
            inferred_type = "int";
        }

        result.push_back(inferred_type);
    }

    return result;
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
        for (const auto& local : original_mir->locals) {
            LocalDecl new_local = local;
            if (new_local.type) {
                // 単純な型パラメータの置換
                auto it = type_subst.find(new_local.type->name);
                if (it != type_subst.end()) {
                    new_local.type = it->second;
                }
                // 構造体型でtype_argsがある場合（Box<T> → Box<int>など）
                else if ((new_local.type->kind == hir::TypeKind::Struct ||
                          new_local.type->kind == hir::TypeKind::TypeAlias) &&
                         !new_local.type->type_args.empty()) {
                    debug_msg("MONO", "  Checking struct with type_args: " + new_local.type->name +
                                          " (" + std::to_string(new_local.type->type_args.size()) +
                                          " args)");
                    // type_args内の型パラメータを置換
                    bool needs_update = false;
                    std::vector<std::string> new_type_args;
                    for (const auto& arg : new_local.type->type_args) {
                        if (arg) {
                            debug_msg("MONO", "    type_arg: " + arg->name + " (kind=" +
                                                  std::to_string(static_cast<int>(arg->kind)) +
                                                  ")");
                            auto subst_it = type_subst.find(arg->name);
                            if (subst_it != type_subst.end()) {
                                new_type_args.push_back(get_type_name(subst_it->second));
                                needs_update = true;
                                debug_msg("MONO", "      -> substituted to: " +
                                                      get_type_name(subst_it->second));
                            } else {
                                new_type_args.push_back(get_type_name(arg));
                                debug_msg("MONO", "      -> kept as: " + get_type_name(arg));
                            }
                        }
                    }

                    if (needs_update && !new_type_args.empty()) {
                        // 特殊化された構造体名を生成
                        std::string spec_struct_name =
                            make_specialized_struct_name(new_local.type->name, new_type_args);

                        // 特殊化構造体が必要なら即座に生成
                        if (hir_struct_defs &&
                            generated_struct_specializations.count(spec_struct_name) == 0) {
                            generate_specialized_struct(program, new_local.type->name,
                                                        new_type_args);
                        }

                        // 型を更新
                        new_local.type = std::make_shared<hir::Type>(hir::TypeKind::Struct);
                        new_local.type->name = spec_struct_name;
                        debug_msg("MONO", "  Updated generic struct type: " + local.name + " -> " +
                                              spec_struct_name);
                    }
                }
                // 構造体型で名前がジェネリック構造体の特殊化形式の場合（Box__T → Box__int, Pair__T__U → Pair__int__string）
                else if (new_local.type->kind == hir::TypeKind::Struct &&
                         new_local.type->type_args.empty()) {
                    // 名前が "Base__Param1__Param2..." 形式かチェック
                    std::string type_name = new_local.type->name;
                    size_t underscore_pos = type_name.find("__");
                    if (underscore_pos != std::string::npos) {
                        std::string base_name = type_name.substr(0, underscore_pos);
                        
                        // HIR構造体定義を確認して型パラメータ数を取得
                        if (hir_struct_defs) {
                            auto hir_it = hir_struct_defs->find(base_name);
                            if (hir_it != hir_struct_defs->end() && hir_it->second) {
                                const auto* struct_def = hir_it->second;
                                if (!struct_def->generic_params.empty()) {
                                    // 型パラメータ名を順番に抽出して置換
                                    std::vector<std::string> new_type_args;
                                    std::string remaining = type_name.substr(underscore_pos + 2);
                                    
                                    for (const auto& param : struct_def->generic_params) {
                                        // パラメータ名が置換マップにあるか
                                        auto subst_it = type_subst.find(param.name);
                                        if (subst_it != type_subst.end()) {
                                            new_type_args.push_back(get_type_name(subst_it->second));
                                        } else {
                                            // 置換マップにない場合は元の名前を使う
                                            // remaining から次の "__" までを抽出
                                            size_t next_pos = remaining.find("__");
                                            if (next_pos != std::string::npos) {
                                                new_type_args.push_back(remaining.substr(0, next_pos));
                                                remaining = remaining.substr(next_pos + 2);
                                            } else {
                                                new_type_args.push_back(remaining);
                                                remaining.clear();
                                            }
                                        }
                                    }
                                    
                                    if (!new_type_args.empty()) {
                                        std::string spec_struct_name =
                                            make_specialized_struct_name(base_name, new_type_args);
                                        
                                        // 特殊化構造体が必要なら生成
                                        if (generated_struct_specializations.count(spec_struct_name) == 0) {
                                            generate_specialized_struct(program, base_name, new_type_args);
                                        }
                                        
                                        new_local.type = std::make_shared<hir::Type>(hir::TypeKind::Struct);
                                        new_local.type->name = spec_struct_name;
                                        debug_msg("MONO", "  Updated multi-param struct: " + type_name +
                                                              " -> " + spec_struct_name);
                                    }
                                    continue;  // 処理済みなので次のケースへ
                                }
                            }
                        }
                        
                        // 単一型パラメータの従来の処理
                        std::string param_name = type_name.substr(underscore_pos + 2);
                        auto subst_it = type_subst.find(param_name);
                        if (subst_it != type_subst.end()) {
                            std::string new_param = get_type_name(subst_it->second);
                            std::string spec_struct_name = base_name + "__" + new_param;

                            // 特殊化構造体が必要なら生成
                            if (hir_struct_defs &&
                                generated_struct_specializations.count(spec_struct_name) == 0) {
                                generate_specialized_struct(program, base_name, {new_param});
                            }

                            new_local.type = std::make_shared<hir::Type>(hir::TypeKind::Struct);
                            new_local.type->name = spec_struct_name;
                            debug_msg("MONO", "  Updated struct name: " + type_name + " -> " +
                                                  spec_struct_name);
                        }
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

    // フィールドとレイアウトを計算
    uint32_t current_offset = 0;
    uint32_t max_align = 1;

    for (const auto& field : base_struct->fields) {
        MirStructField mir_field;
        mir_field.name = field.name;

        // フィールドの型を置換
        hir::TypePtr field_type = field.type;
        if (field_type) {
            // ジェネリック型パラメータならば置換
            if (field_type->kind == hir::TypeKind::Generic ||
                type_subst.count(field_type->name) > 0) {
                auto subst_it = type_subst.find(field_type->name);
                if (subst_it != type_subst.end()) {
                    field_type = subst_it->second;
                }
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
    MirProgram& program,
    const std::map<std::pair<std::string, std::vector<std::string>>,
                   std::vector<std::tuple<std::string, size_t>>>& needed) {
    
    // (元の関数名, 型引数) -> 特殊化関数名 のマッピングを構築
    std::map<std::pair<std::string, std::vector<std::string>>, std::string> rewrite_map;
    for (const auto& [key, _] : needed) {
        const auto& [func_name, type_args] = key;
        std::string specialized_name = make_specialized_name(func_name, type_args);
        rewrite_map[key] = specialized_name;
    }
    
    // 全関数の呼び出しを書き換え
    for (auto& func : program.functions) {
        if (!func) continue;
        
        for (auto& block : func->basic_blocks) {
            if (!block || !block->terminator) continue;
            
            if (block->terminator->kind == MirTerminator::Call) {
                auto& call_data = std::get<MirTerminator::CallData>(block->terminator->data);
                
                if (!call_data.func || call_data.func->kind != MirOperand::FunctionRef) continue;
                
                auto& func_name = std::get<std::string>(call_data.func->data);
                
                // Container<int>__print のような形式を検出
                auto pos = func_name.find("<");
                if (pos == std::string::npos) continue;
                
                auto end_pos = func_name.find(">__");
                if (end_pos == std::string::npos) continue;
                
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
                std::string base_name = func_name.substr(0, pos);  // "Container"
                std::string method_suffix = func_name.substr(end_pos + 1);  // "__print"
                
                // Container<T>__print のような形式に変換
                std::string generic_func_name = base_name + "<";
                // 型パラメータ名は不明なので、T, U, V... と仮定
                const char* param_names[] = {"T", "U", "V", "W"};
                for (size_t i = 0; i < type_args.size(); ++i) {
                    if (i > 0) generic_func_name += ", ";
                    generic_func_name += (i < 4) ? param_names[i] : "T" + std::to_string(i);
                }
                generic_func_name += ">" + method_suffix;
                
                // rewrite_mapから特殊化関数名を取得
                auto key = std::make_pair(generic_func_name, type_args);
                auto it = rewrite_map.find(key);
                if (it != rewrite_map.end()) {
                    func_name = it->second;
                    debug_msg("MONO", "Rewrote call in " + func->name + ": " + 
                             std::get<std::string>(call_data.func->data) + " -> " + func_name);
                }
            }
        }
    }
}

}  // namespace cm::mir
