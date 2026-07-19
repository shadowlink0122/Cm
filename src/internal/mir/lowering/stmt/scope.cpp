// MIR lowering - スコープ後始末（defer登録・デストラクタ呼び出し・mustブロック）

#include "internal/base/debug.hpp"
#include "internal/mir/lowering/stmt.hpp"
#include "internal/mir/passes/scalar/const_eval.hpp"

#include <cinttypes>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cm::mir {

// defer文のlowering
void StmtLowering::lower_defer(const hir::HirDefer& defer_stmt, LoweringContext& ctx) {
    // defer文を現在のスコープに登録
    // defer文は、スコープ終了時に逆順で実行される
    if (defer_stmt.body) {
        ctx.add_defer(defer_stmt.body.get());
    }
}

// スコープ終了時のデストラクタ呼び出しを生成
void StmtLowering::emit_scope_destructors(LoweringContext& ctx) {
    auto destructor_vars = ctx.get_current_scope_destructor_vars();
    for (const auto& [local_id, type_name] : destructor_vars) {
        // 登録時の型名を優先使用（ジェネリック型の場合、マングル済み名が登録されている）
        // ローカル変数の型名はモノモフィゼーション前なので不正確な場合がある
        std::string actual_type_name = type_name;

        // 登録時の型名がマングル済み（__を含む）の場合はそのまま使用そうでない場合はローカル変数の型名を確認
        if (type_name.find("__") == std::string::npos && local_id < ctx.func->locals.size()) {
            const auto& local_decl = ctx.func->locals[local_id];
            if (local_decl.type && !local_decl.type->name.empty() &&
                local_decl.type->name.find("__") != std::string::npos) {
                // ローカル変数の型名がマングル済みならそれを使用
                actual_type_name = local_decl.type->name;
            }
        }

        // ネストジェネリック型名の正規化（Vector<int> → Vector__int）
        if (actual_type_name.find('<') != std::string::npos) {
            std::string result;
            for (char c : actual_type_name) {
                if (c == '<' || c == '>') {
                    if (c == '<')
                        result += "__";
                } else if (c == ',' || c == ' ') {
                    // カンマと空白は省略
                } else {
                    result += c;
                }
            }
            actual_type_name = result;
        }

        std::string dtor_name = actual_type_name + "__dtor";

        // デストラクタ呼び出しを生成（selfはポインタとして渡す）
        hir::TypePtr local_type = std::make_shared<hir::Type>(hir::TypeKind::Struct);
        local_type->name = actual_type_name;
        LocalId ref_temp = ctx.new_temp(hir::make_pointer(local_type));
        ctx.push_statement(
            MirStatement::assign(MirPlace{ref_temp}, MirRvalue::ref(MirPlace{local_id}, false)));

        std::vector<MirOperandPtr> args;
        args.push_back(MirOperand::copy(MirPlace{ref_temp}));

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
                                                  false};
        ctx.set_terminator(std::move(call_term));
        ctx.switch_to_block(success_block);
    }
}

// must {} ブロックのlowering（最適化禁止）
void StmtLowering::lower_must_block(const hir::HirMustBlock& must_block, LoweringContext& ctx) {
    debug_msg("mir_must", "[MIR] lower_must_block");

    // mustブロック開始：最適化禁止フラグをON
    ctx.in_must_block = true;

    // mustブロック内の各文をlowering
    for (const auto& stmt : must_block.body) {
        lower_statement(*stmt, ctx);
    }

    // mustブロック終了：最適化禁止フラグをOFF
    ctx.in_must_block = false;
}

}  // namespace cm::mir
