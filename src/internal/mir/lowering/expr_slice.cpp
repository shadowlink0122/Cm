// ============================================================
// MIR lowering - スライスbuiltin（len/cap/push/pop/delete/clear）
// ============================================================

#include "expr.hpp"
#include "internal/base/debug.hpp"
#include "internal/hir/lowering/fwd.hpp"

namespace cm::mir {

// スライスbuiltinを処理した場合はローカルIDを、対象外ならnulloptを返す
std::optional<LocalId> ExprLowering::try_lower_slice_builtin(const hir::HirCall& call,
                                                             const hir::TypePtr& result_type,
                                                             LoweringContext& ctx) {
    (void)result_type;
    // スライスのlen/cap処理
    if (call.func_name == "__builtin_slice_len" || call.func_name == "__builtin_slice_cap") {
        if (!call.args.empty()) {
            auto slice_expr = call.args[0].get();

            // スライス変数を解決（変数参照またはメンバアクセス）
            MirPlace slice_place{0};
            hir::TypePtr slice_type = nullptr;
            bool resolved = false;

            if (auto* var = std::get_if<std::unique_ptr<hir::HirVarRef>>(&slice_expr->kind)) {
                auto slice_local_opt = ctx.resolve_variable((*var)->name);
                if (slice_local_opt.has_value()) {
                    slice_place = MirPlace{*slice_local_opt};
                    resolved = true;
                }
            } else if (auto* mem =
                           std::get_if<std::unique_ptr<hir::HirMember>>(&slice_expr->kind)) {
                // メンバアクセス（c.values のような形式）- 直接参照を取得
                if (get_member_place(**mem, ctx, slice_place, slice_type)) {
                    resolved = true;
                }
            }

            if (resolved) {
                std::string func_name =
                    (call.func_name == "__builtin_slice_len") ? "cm_slice_len" : "cm_slice_cap";

                LocalId result = ctx.new_temp(hir::make_uint());
                BlockId success_block = ctx.new_block();
                std::vector<MirOperandPtr> args;
                args.push_back(MirOperand::copy(slice_place));

                auto call_term = std::make_unique<MirTerminator>();
                call_term->kind = MirTerminator::Call;
                call_term->data = MirTerminator::CallData{MirOperand::function_ref(func_name),
                                                          std::move(args),
                                                          MirPlace{result},
                                                          success_block,
                                                          std::nullopt,
                                                          "",
                                                          "",
                                                          false};
                ctx.set_terminator(std::move(call_term));
                ctx.switch_to_block(success_block);
                return result;
            }
        }
        return ctx.new_temp(hir::make_uint());
    }

    // スライスビルトイン関数の処理 - 通常の関数呼び出しとして変換
    if (call.func_name == "__builtin_slice_push") {
        if (call.args.size() >= 2) {
            auto slice_expr = call.args[0].get();

            // スライス変数を解決（変数参照またはメンバアクセス）
            MirPlace slice_place{0};
            hir::TypePtr slice_type = nullptr;
            bool resolved = false;

            if (auto* var = std::get_if<std::unique_ptr<hir::HirVarRef>>(&slice_expr->kind)) {
                auto slice_local_opt = ctx.resolve_variable((*var)->name);
                if (slice_local_opt) {
                    slice_place = MirPlace{*slice_local_opt};
                    resolved = true;
                    if (*slice_local_opt < ctx.func->locals.size()) {
                        slice_type = ctx.func->locals[*slice_local_opt].type;
                    }
                }
            } else if (auto* mem =
                           std::get_if<std::unique_ptr<hir::HirMember>>(&slice_expr->kind)) {
                // メンバアクセス（c.values のような形式）- 直接参照を取得
                if (get_member_place(**mem, ctx, slice_place, slice_type)) {
                    resolved = true;
                }
            }

            if (resolved) {
                LocalId value_local = lower_expression(*call.args[1], ctx);

                // 要素型に基づいてランタイム関数を選択
                // メンバ経由のslice_typeはtypedefエイリアス未解決のことがあるため解決してから判定
                std::string push_func = "cm_slice_push_i32";
                if (slice_type && slice_type->element_type) {
                    auto resolved_elem = ctx.resolve_typedef(slice_type->element_type);
                    auto elem_kind =
                        resolved_elem ? resolved_elem->kind : slice_type->element_type->kind;
                    if (elem_kind == hir::TypeKind::Char || elem_kind == hir::TypeKind::Bool ||
                        elem_kind == hir::TypeKind::Tiny || elem_kind == hir::TypeKind::UTiny) {
                        push_func = "cm_slice_push_i8";
                    } else if (elem_kind == hir::TypeKind::Long ||
                               elem_kind == hir::TypeKind::ULong) {
                        push_func = "cm_slice_push_i64";
                    } else if (elem_kind == hir::TypeKind::Float) {
                        push_func = "cm_slice_push_f32";
                    } else if (elem_kind == hir::TypeKind::Double) {
                        push_func = "cm_slice_push_f64";
                    } else if (elem_kind == hir::TypeKind::Array) {
                        // 多次元スライス: 内側スライスはポインタとしてpush
                        push_func = "cm_slice_push_slice";
                    } else if (elem_kind == hir::TypeKind::Union ||
                               elem_kind == hir::TypeKind::Struct) {
                        // ユニオン・構造体: blobとして値をインラインコピー
                        push_func = "cm_slice_push_blob";
                    } else if (elem_kind == hir::TypeKind::Pointer ||
                               elem_kind == hir::TypeKind::String) {
                        push_func = "cm_slice_push_ptr";
                    }
                }

                BlockId success_block = ctx.new_block();
                std::vector<MirOperandPtr> args;
                args.push_back(MirOperand::copy(slice_place));
                if (push_func == "cm_slice_push_blob") {
                    // blob pushはデータ先頭へのポインタを受け取る（ユニオン値は集約のため）
                    hir::TypePtr value_type = nullptr;
                    if (value_local < ctx.func->locals.size()) {
                        value_type = ctx.func->locals[value_local].type;
                    }
                    LocalId addr_local =
                        ctx.new_temp(hir::make_pointer(value_type ? value_type : hir::make_int()));
                    ctx.push_statement(MirStatement::assign(
                        MirPlace{addr_local}, MirRvalue::ref(MirPlace{value_local}, false)));
                    args.push_back(MirOperand::copy(MirPlace{addr_local}));
                } else {
                    args.push_back(MirOperand::copy(MirPlace{value_local}));
                }

                auto call_term = std::make_unique<MirTerminator>();
                call_term->kind = MirTerminator::Call;
                call_term->data = MirTerminator::CallData{MirOperand::function_ref(push_func),
                                                          std::move(args),
                                                          std::nullopt,
                                                          success_block,
                                                          std::nullopt,
                                                          "",
                                                          "",
                                                          false};
                ctx.set_terminator(std::move(call_term));
                ctx.switch_to_block(success_block);
                return ctx.new_temp(hir::make_void());
            }
        }
        return ctx.new_temp(hir::make_void());
    }

    if (call.func_name == "__builtin_slice_pop") {
        if (!call.args.empty()) {
            auto slice_expr = call.args[0].get();
            if (auto* var = std::get_if<std::unique_ptr<hir::HirVarRef>>(&slice_expr->kind)) {
                auto slice_local_opt = ctx.resolve_variable((*var)->name);
                if (slice_local_opt.has_value()) {
                    LocalId slice_local = slice_local_opt.value();

                    std::string pop_func = "cm_slice_pop_i32";
                    hir::TypePtr elem_type = hir::make_int();
                    hir::TypePtr slice_type = nullptr;
                    if (slice_local < ctx.func->locals.size()) {
                        slice_type = ctx.func->locals[slice_local].type;
                    }
                    if (slice_type && slice_type->element_type) {
                        elem_type = slice_type->element_type;
                        auto elem_kind = elem_type->kind;
                        if (elem_kind == hir::TypeKind::Char || elem_kind == hir::TypeKind::Bool ||
                            elem_kind == hir::TypeKind::Tiny || elem_kind == hir::TypeKind::UTiny) {
                            pop_func = "cm_slice_pop_i8";
                        } else if (elem_kind == hir::TypeKind::Long ||
                                   elem_kind == hir::TypeKind::ULong) {
                            pop_func = "cm_slice_pop_i64";
                        } else if (elem_kind == hir::TypeKind::Float) {
                            pop_func = "cm_slice_pop_f32";
                        } else if (elem_kind == hir::TypeKind::Double) {
                            pop_func = "cm_slice_pop_f64";
                        } else if (elem_kind == hir::TypeKind::Pointer ||
                                   elem_kind == hir::TypeKind::String ||
                                   elem_kind == hir::TypeKind::Struct) {
                            pop_func = "cm_slice_pop_ptr";
                        }
                    }

                    LocalId result = ctx.new_temp(elem_type);
                    BlockId success_block = ctx.new_block();
                    std::vector<MirOperandPtr> args;
                    args.push_back(MirOperand::copy(MirPlace{slice_local}));

                    auto call_term = std::make_unique<MirTerminator>();
                    call_term->kind = MirTerminator::Call;
                    call_term->data = MirTerminator::CallData{MirOperand::function_ref(pop_func),
                                                              std::move(args),
                                                              MirPlace{result},
                                                              success_block,
                                                              std::nullopt,
                                                              "",
                                                              "",
                                                              false};
                    ctx.set_terminator(std::move(call_term));
                    ctx.switch_to_block(success_block);
                    return result;
                }
            }
        }
        return ctx.new_temp(result_type ? result_type : hir::make_int());
    }

    if (call.func_name == "__builtin_slice_delete") {
        if (call.args.size() >= 2) {
            auto slice_expr = call.args[0].get();
            if (auto* var = std::get_if<std::unique_ptr<hir::HirVarRef>>(&slice_expr->kind)) {
                auto slice_local_opt = ctx.resolve_variable((*var)->name);
                if (slice_local_opt.has_value()) {
                    LocalId slice_local = slice_local_opt.value();
                    LocalId index_local = lower_expression(*call.args[1], ctx);

                    BlockId success_block = ctx.new_block();
                    std::vector<MirOperandPtr> args;
                    args.push_back(MirOperand::copy(MirPlace{slice_local}));
                    args.push_back(MirOperand::copy(MirPlace{index_local}));

                    auto call_term = std::make_unique<MirTerminator>();
                    call_term->kind = MirTerminator::Call;
                    call_term->data =
                        MirTerminator::CallData{MirOperand::function_ref("cm_slice_delete"),
                                                std::move(args),
                                                std::nullopt,
                                                success_block,
                                                std::nullopt,
                                                "",
                                                "",
                                                false};
                    ctx.set_terminator(std::move(call_term));
                    ctx.switch_to_block(success_block);
                    return ctx.new_temp(hir::make_void());
                }
            }
        }
        return ctx.new_temp(hir::make_void());
    }

    if (call.func_name == "__builtin_slice_clear") {
        if (!call.args.empty()) {
            auto slice_expr = call.args[0].get();
            if (auto* var = std::get_if<std::unique_ptr<hir::HirVarRef>>(&slice_expr->kind)) {
                auto slice_local_opt = ctx.resolve_variable((*var)->name);
                if (slice_local_opt.has_value()) {
                    LocalId slice_local = slice_local_opt.value();

                    BlockId success_block = ctx.new_block();
                    std::vector<MirOperandPtr> args;
                    args.push_back(MirOperand::copy(MirPlace{slice_local}));

                    auto call_term = std::make_unique<MirTerminator>();
                    call_term->kind = MirTerminator::Call;
                    call_term->data =
                        MirTerminator::CallData{MirOperand::function_ref("cm_slice_clear"),
                                                std::move(args),
                                                std::nullopt,
                                                success_block,
                                                std::nullopt,
                                                "",
                                                "",
                                                false};
                    ctx.set_terminator(std::move(call_term));
                    ctx.switch_to_block(success_block);
                    return ctx.new_temp(hir::make_void());
                }
            }
        }
        return ctx.new_temp(hir::make_void());
    }

    return std::nullopt;
}

}  // namespace cm::mir
