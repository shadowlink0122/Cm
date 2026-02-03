#include "../../common/debug.hpp"
#include "context.hpp"
#include "lowering.hpp"

namespace cm::mir {

// 演算子実装のlowering
std::unique_ptr<MirFunction> MirLowering::lower_operator(const hir::HirOperatorImpl& op_impl,
                                                         const std::string& type_name) {
    std::string op_name;
    switch (op_impl.op) {
        case hir::HirOperatorKind::Eq:
            op_name = "op_eq";
            break;
        case hir::HirOperatorKind::Ne:
            op_name = "op_ne";
            break;
        case hir::HirOperatorKind::Lt:
            op_name = "op_lt";
            break;
        case hir::HirOperatorKind::Gt:
            op_name = "op_gt";
            break;
        case hir::HirOperatorKind::Le:
            op_name = "op_le";
            break;
        case hir::HirOperatorKind::Ge:
            op_name = "op_ge";
            break;
        case hir::HirOperatorKind::Add:
            op_name = "op_add";
            break;
        case hir::HirOperatorKind::Sub:
            op_name = "op_sub";
            break;
        case hir::HirOperatorKind::Mul:
            op_name = "op_mul";
            break;
        case hir::HirOperatorKind::Div:
            op_name = "op_div";
            break;
        case hir::HirOperatorKind::Mod:
            op_name = "op_mod";
            break;
        default:
            op_name = "op_unknown";
            break;
    }

    debug::log(debug::Stage::Mir, debug::Level::Info,
               "Lowering operator: " + type_name + "__" + op_name);

    auto mir_func = std::make_unique<MirFunction>();
    mir_func->name = type_name + "__" + op_name;

    // 戻り値用のローカル変数
    mir_func->return_local = 0;
    auto resolved_return_type = resolve_typedef(op_impl.return_type);
    mir_func->locals.emplace_back(0, "@return", resolved_return_type, true, false);

    // エントリーブロックを作成
    mir_func->entry_block = 0;
    mir_func->basic_blocks.push_back(std::make_unique<BasicBlock>(0));

    // LoweringContextを作成
    LoweringContext ctx(mir_func.get());
    ctx.enum_defs = &enum_defs;
    ctx.typedef_defs = &typedef_defs;
    ctx.struct_defs = &struct_defs;
    ctx.interface_names = &interface_names;
    ctx.global_const_values = &global_const_values;

    // selfパラメータを登録（値型として - 呼び出し側が参照を渡す）
    auto self_type = hir::make_named(type_name);
    LocalId self_id = ctx.new_local("self", self_type, false);
    mir_func->arg_locals.push_back(self_id);
    ctx.register_variable("self", self_id);

    // 他のパラメータを登録
    for (const auto& param : op_impl.params) {
        auto resolved_param_type = resolve_typedef(param.type);
        LocalId param_id = ctx.new_local(param.name, resolved_param_type, false);
        mir_func->arg_locals.push_back(param_id);
        ctx.register_variable(param.name, param_id);
    }

    // 文を処理
    for (const auto& stmt : op_impl.body) {
        if (stmt) {
            stmt_lowering.lower_statement(*stmt, ctx);
        }
    }

    // デフォルトのreturn
    auto* current = ctx.get_current_block();
    if (current && !current->terminator) {
        // 戻り値がvoid、構造体、配列（動的スライス含む）型でない場合のみデフォルト値を設定
        if (resolved_return_type && resolved_return_type->kind != hir::TypeKind::Void &&
            resolved_return_type->kind != hir::TypeKind::Struct &&
            resolved_return_type->kind != hir::TypeKind::Array) {
            MirConstant default_return;
            default_return.type = resolved_return_type;
            if (resolved_return_type->is_floating()) {
                default_return.value = 0.0;
            } else {
                // すべての型で整数0を使用（LLVMコード生成側で適切に変換）
                default_return.value = int64_t(0);
            }

            ctx.push_statement(MirStatement::assign(
                MirPlace{0}, MirRvalue::use(MirOperand::constant(default_return))));
        }
        ctx.set_terminator(MirTerminator::return_value());
    }

    return mir_func;
}

// 関数のlowering - モジュラーコンポーネントを使用
std::unique_ptr<MirFunction> MirLowering::lower_function(const hir::HirFunction& func) {
    debug::log(debug::Stage::Mir, debug::Level::Info, "Lowering function: " + func.name);

    // MirFunctionを作成
    auto mir_func = std::make_unique<MirFunction>();
    mir_func->name = func.name;
    mir_func->module_path = current_module_path;  // モジュールパスを設定
    mir_func->is_export = func.is_export;         // エクスポートフラグを設定
    mir_func->is_extern = func.is_extern;         // externフラグを設定
    mir_func->is_variadic = func.is_variadic;     // 可変長引数フラグを設定

    // 戻り値用のローカル変数（typedefを解決）
    mir_func->return_local = 0;
    auto resolved_return_type = resolve_typedef(func.return_type);
    mir_func->locals.emplace_back(0, "@return", resolved_return_type, true, false);

    // extern関数は宣言のみでボディなし
    if (func.is_extern) {
        // パラメータを記録
        for (const auto& param : func.params) {
            auto resolved_param_type = resolve_typedef(param.type);
            LocalId param_id = static_cast<LocalId>(mir_func->locals.size());
            mir_func->locals.emplace_back(param_id, param.name, resolved_param_type, false, false);
            mir_func->arg_locals.push_back(param_id);
        }
        return mir_func;
    }

    // エントリーブロックを作成
    mir_func->entry_block = 0;
    mir_func->basic_blocks.push_back(std::make_unique<BasicBlock>(0));

    // LoweringContextを作成
    LoweringContext ctx(mir_func.get());
    ctx.enum_defs = &enum_defs;
    ctx.typedef_defs = &typedef_defs;
    ctx.struct_defs = &struct_defs;
    ctx.interface_names = &interface_names;
    ctx.global_const_values = &global_const_values;

    // デストラクタを持つ型の情報をコンテキストに渡す
    for (const auto& type_name : types_with_destructor) {
        ctx.register_type_with_destructor(type_name);
    }

    // 関数パラメータをローカル変数として登録（typedefを解決）
    for (const auto& param : func.params) {
        auto resolved_param_type = resolve_typedef(param.type);
        LocalId param_id = ctx.new_local(param.name, resolved_param_type, false);
        mir_func->arg_locals.push_back(param_id);
        ctx.register_variable(param.name, param_id);

        debug::log(
            debug::Stage::Mir, debug::Level::Debug,
            "Registered parameter '" + param.name + "' as local " + std::to_string(param_id));
    }

    // 文を処理（モジュラーコンポーネントを使用）
    for (const auto& stmt : func.body) {
        if (stmt) {
            stmt_lowering.lower_statement(*stmt, ctx);
        }
    }

    // デフォルト値で戻る（return文がない場合）
    auto* current = ctx.get_current_block();
    if (current && !current->terminator) {
        // デストラクタを呼び出す
        emit_destructors(ctx);

        // 構造体、void、配列（動的スライス含む）型はデフォルト値代入をスキップ
        bool skip_default_assign = false;
        auto resolved_return_type = resolve_typedef(func.return_type);
        if (resolved_return_type) {
            if (resolved_return_type->kind == hir::TypeKind::Struct ||
                resolved_return_type->kind == hir::TypeKind::Void ||
                resolved_return_type->kind == hir::TypeKind::Array) {
                skip_default_assign = true;
            }
        }

        if (!skip_default_assign) {
            MirConstant default_return;
            default_return.type = resolved_return_type;
            // 型に応じたデフォルト値を設定
            if (resolved_return_type && resolved_return_type->is_floating()) {
                default_return.value = 0.0;
            } else {
                default_return.value = int64_t(0);
            }

            ctx.push_statement(MirStatement::assign(
                MirPlace{0}, MirRvalue::use(MirOperand::constant(default_return))));
        }
        ctx.set_terminator(MirTerminator::return_value());
    }

    // デバッグ: 最終的なbb0の内容を確認
    if (mir_func->name == "main" && !mir_func->basic_blocks.empty()) {
        auto* bb0 = mir_func->basic_blocks[0].get();
        if (bb0) {
            debug_msg("mir_final_bb0", "[MIR] Final bb0 for main has " +
                                           std::to_string(bb0->statements.size()) + " statements");
            for (size_t i = 0; i < bb0->statements.size(); i++) {
                if (bb0->statements[i] && bb0->statements[i]->kind == MirStatement::Assign) {
                    auto& assign = std::get<MirStatement::AssignData>(bb0->statements[i]->data);
                    debug_msg("mir_final_bb0", "[MIR]   Statement " + std::to_string(i) +
                                                   ": assign to local " +
                                                   std::to_string(assign.place.local));
                }
            }
        }
    }

    return mir_func;
}

// デストラクタ呼び出しを生成
void MirLowering::emit_destructors(LoweringContext& ctx) {
    auto destructor_vars = ctx.get_all_destructor_vars();
    for (const auto& [local_id, type_name] : destructor_vars) {
        std::string dtor_name = type_name + "__dtor";

        // デストラクタ呼び出しを生成
        std::vector<MirOperandPtr> args;
        args.push_back(MirOperand::copy(MirPlace{local_id}));

        BlockId success_block = ctx.new_block();

        auto func_operand = MirOperand::function_ref(dtor_name);
        auto call_term = std::make_unique<MirTerminator>();
        call_term->kind = MirTerminator::Call;
        call_term->data = MirTerminator::CallData{std::move(func_operand),
                                                  std::move(args),
                                                  std::nullopt,  // void戻り値
                                                  success_block,
                                                  std::nullopt,
                                                  "",
                                                  "",
                                                  false};  // 通常の関数呼び出し
        ctx.set_terminator(std::move(call_term));
        ctx.switch_to_block(success_block);
    }
}

// impl内のメソッドをlowering
void MirLowering::lower_impl(const hir::HirImpl& impl) {
    if (impl.target_type.empty())
        return;

    std::string type_name = impl.target_type;

    // 各メソッドをlowering
    for (const auto& method : impl.methods) {
        // メソッドを関数として処理
        auto mir_func = lower_function(*method);
        if (mir_func) {
            // コンストラクタ/デストラクタは既にマングル化された名前を持っている
            if (method->is_constructor || method->is_destructor) {
                mir_func->name = method->name;
            } else {
                // 通常のメソッドは type__method_name 形式にする
                mir_func->name = type_name + "__" + method->name;
            }

            // ジェネリックパラメータがある場合、hir_functionsに登録（モノモーフィゼーション用）
            // メソッド自体のジェネリックパラメータ、impl自体のジェネリックパラメータ、
            // またはtype_nameがジェネリック型（<を含む）の場合に登録
            bool has_generic = !method->generic_params.empty() || !impl.generic_params.empty() ||
                               type_name.find('<') != std::string::npos;
            if (has_generic) {
                hir_functions[mir_func->name] = method.get();
                debug_msg("MIR",
                          "Registered generic impl method: " + mir_func->name +
                              " (method params: " + std::to_string(method->generic_params.size()) +
                              ", impl params: " + std::to_string(impl.generic_params.size()) +
                              ", type_name: " + type_name + ")");
            }

            mir_program.functions.push_back(std::move(mir_func));
        }
    }

    // 各演算子実装をlowering
    for (const auto& op_impl : impl.operators) {
        if (!op_impl)
            continue;

        // 専用のlowering関数を使用
        auto mir_func = lower_operator(*op_impl, type_name);
        if (mir_func) {
            debug_msg("MIR", "Lowered operator: " + mir_func->name);

            // impl_infoに登録
            if (op_impl->op == hir::HirOperatorKind::Eq) {
                impl_info[type_name]["Eq"] = mir_func->name;
            } else if (op_impl->op == hir::HirOperatorKind::Lt) {
                impl_info[type_name]["Ord"] = mir_func->name;
            }

            mir_program.functions.push_back(std::move(mir_func));
        }
    }
}

}  // namespace cm::mir