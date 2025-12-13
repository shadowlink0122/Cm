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

    // 戻り値用のローカル変数
    mir_func->return_local = 0;
    mir_func->locals.emplace_back(0, "@return", func.return_type, true, false);

    // エントリーブロックを作成
    mir_func->entry_block = 0;
    mir_func->basic_blocks.push_back(std::make_unique<BasicBlock>(0));

    // LoweringContextを作成
    LoweringContext ctx(mir_func.get());
    ctx.enum_defs = &enum_defs;
    ctx.typedef_defs = &typedef_defs;

    // 関数パラメータをローカル変数として登録
    for (const auto& param : func.params) {
        LocalId param_id = ctx.new_local(param.name, param.type, false);
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
        MirConstant default_return;
        default_return.type = func.return_type;
        default_return.value = int64_t(0);

        ctx.push_statement(MirStatement::assign(
            MirPlace{0}, MirRvalue::use(MirOperand::constant(default_return))));
        ctx.set_terminator(MirTerminator::return_value());
    }

    return mir_func;
}

// impl内のメソッドをlowering
void MirLowering::lower_impl(const hir::HirImpl& impl) {
    if (impl.target_type.empty())
        return;

    std::string type_name = impl.target_type;

    // 各メソッドをlowering
    for (const auto& method : impl.methods) {
        std::string impl_method_name = type_name + "__" + method->name;

        // メソッドを関数として処理（参照を使用、名前だけ変更）
        auto mir_func = lower_function(*method);
        if (mir_func) {
            mir_func->name = impl_method_name;
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