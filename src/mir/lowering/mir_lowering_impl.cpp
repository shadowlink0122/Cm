#include "../../common/debug.hpp"
#include "lowering_context.hpp"
#include "mir_lowering.hpp"

namespace cm::mir {

// 関数のlowering - モジュラーコンポーネントを使用
std::unique_ptr<MirFunction> MirLowering::lower_function(const hir::HirFunction& func) {
    debug::log(debug::Stage::Mir, debug::Level::Info, "Lowering function: " + func.name);

    // MirFunctionを作成
    auto mir_func = std::make_unique<MirFunction>();
    mir_func->name = func.name;

    // 戻り値用のローカル変数（typedefを解決）
    mir_func->return_local = 0;
    auto resolved_return_type = resolve_typedef(func.return_type);
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

        // 構造体やvoid型はデフォルト値代入をスキップ
        bool skip_default_assign = false;
        auto resolved_return_type = resolve_typedef(func.return_type);
        if (resolved_return_type) {
            if (resolved_return_type->kind == hir::TypeKind::Struct ||
                resolved_return_type->kind == hir::TypeKind::Void) {
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
        call_term->data = MirTerminator::CallData{std::move(func_operand), std::move(args),
                                                  std::nullopt,  // void戻り値
                                                  success_block, std::nullopt};
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
            if (!method->generic_params.empty()) {
                hir_functions[mir_func->name] = method.get();
                debug_msg("MIR", "Registered generic impl method: " + mir_func->name);
            }

            mir_program.functions.push_back(std::move(mir_func));
        }
    }
}

// デストラクタを生成（構造体用）
void MirLowering::generate_destructor([[maybe_unused]] const std::string& type_name,
                                      [[maybe_unused]] LoweringContext& ctx) {
    // TODO: デストラクタの実装
    // 現在はプレースホルダー
}

}  // namespace cm::mir