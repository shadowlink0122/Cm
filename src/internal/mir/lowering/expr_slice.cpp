// ============================================================
// MIR lowering - スライスbuiltin（len/cap/push/pop/delete/clear）
// ============================================================

#include "expr.hpp"
#include "internal/base/debug.hpp"
#include "internal/hir/lowering/fwd.hpp"
#include "slice_dispatch.hpp"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

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

            // スライスレシーバの場所を解決する（VarRef/Member/固定長配列Index。H10）
            MirPlace slice_place{0};
            hir::TypePtr slice_type = nullptr;
            bool resolved = resolve_receiver_place(slice_expr, ctx, slice_place, slice_type);

            // 場所を持たない式（make_slice().len() 等の呼び出し戻り値）は一時ローカルへ
            // 実体化して読み取る（H10: 従来は診断なしで空tempを返し黙って欠落していた）
            if (!resolved) {
                LocalId materialized = lower_expression(*slice_expr, ctx);
                slice_place = MirPlace{materialized};
                resolved = true;
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

            // スライスレシーバの場所を解決する（VarRef/Member/固定長配列Index。H10）
            MirPlace slice_place{0};
            hir::TypePtr slice_type = nullptr;
            bool resolved = resolve_receiver_place(slice_expr, ctx, slice_place, slice_type);

            if (resolved) {
                LocalId value_local = lower_expression(*call.args[1], ctx);

                // 要素型に基づいてランタイム関数を選択
                // メンバ経由のslice_typeはtypedefエイリアス未解決のことがあるため解決してから判定
                std::string push_func = "cm_slice_push_i32";
                if (slice_type && slice_type->element_type) {
                    auto resolved_elem = ctx.resolve_typedef(slice_type->element_type);
                    auto elem_kind =
                        resolved_elem ? resolved_elem->kind : slice_type->element_type->kind;
                    if (auto info = slice_scalar_info(elem_kind)) {
                        // スカラ型: 幅サフィックスをslice_dispatchから取得（elem_sizeと整合。C4）
                        push_func = std::string("cm_slice_push_") + info->width;
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

                // インターフェイス要素スライスへの具象構造体push:
                // インターフェイス型の一時へ代入してfat pointerを構築してからblob格納する（H1）
                if (push_func == "cm_slice_push_blob" && slice_type && slice_type->element_type &&
                    ctx.interface_names) {
                    auto resolved_elem = ctx.resolve_typedef(slice_type->element_type);
                    if (resolved_elem && resolved_elem->kind == hir::TypeKind::Struct &&
                        ctx.interface_names->count(resolved_elem->name) > 0) {
                        hir::TypePtr actual_type = nullptr;
                        if (value_local < ctx.func->locals.size()) {
                            actual_type = ctx.func->locals[value_local].type;
                        }
                        if (actual_type && actual_type->kind == hir::TypeKind::Struct &&
                            actual_type->name != resolved_elem->name) {
                            LocalId iface_tmp = ctx.new_temp(resolved_elem);
                            ctx.push_statement(MirStatement::assign(
                                MirPlace{iface_tmp},
                                MirRvalue::use(MirOperand::copy(MirPlace{value_local}))));
                            value_local = iface_tmp;
                        }
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

            // 黙殺禁止: レシーバを解決できない場合は診断を出す（H10。従来pushは診断なしで
            // 文ごと欠落していた）
            debug::log(debug::Stage::Mir, debug::Level::Error,
                       "slice push(): レシーバのスライス場所を解決できませんでした");
        }
        return ctx.new_temp(hir::make_void());
    }

    if (call.func_name == "__builtin_slice_pop") {
        if (!call.args.empty()) {
            auto slice_expr = call.args[0].get();

            // スライスレシーバの場所を解決する（VarRef/Member/固定長配列Index。C11/H10）
            MirPlace slice_place{0};
            hir::TypePtr slice_type = nullptr;
            bool resolved = resolve_receiver_place(slice_expr, ctx, slice_place, slice_type);

            if (resolved) {
                std::string pop_func = "cm_slice_pop_i32";
                hir::TypePtr elem_type = hir::make_int();
                // メンバ経由のslice_typeはtypedefエイリアス未解決のことがあるため解決してから判定
                if (slice_type && slice_type->element_type) {
                    auto resolved_elem = ctx.resolve_typedef(slice_type->element_type);
                    elem_type = resolved_elem ? resolved_elem : slice_type->element_type;
                    auto elem_kind = elem_type->kind;
                    if (auto info = slice_scalar_info(elem_kind)) {
                        // スカラ型: 幅サフィックスをslice_dispatchから取得（elem_sizeと整合。C4）
                        pop_func = std::string("cm_slice_pop_") + info->width;
                    } else if (elem_kind == hir::TypeKind::Pointer ||
                               elem_kind == hir::TypeKind::String ||
                               elem_kind == hir::TypeKind::Struct) {
                        pop_func = "cm_slice_pop_ptr";
                    }
                }

                LocalId result = ctx.new_temp(elem_type);
                BlockId success_block = ctx.new_block();
                std::vector<MirOperandPtr> args;
                args.push_back(MirOperand::copy(slice_place));

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

            // 黙殺禁止: レシーバを解決できない場合は診断を出す（C11）
            debug::log(debug::Stage::Mir, debug::Level::Error,
                       "slice pop(): レシーバのスライス場所を解決できませんでした");
        }
        return ctx.new_temp(result_type ? result_type : hir::make_int());
    }

    if (call.func_name == "__builtin_slice_delete") {
        if (call.args.size() >= 2) {
            auto slice_expr = call.args[0].get();

            // スライスレシーバの場所を解決する（VarRef/Member/固定長配列Index。C11/H10）
            MirPlace slice_place{0};
            hir::TypePtr slice_type = nullptr;
            bool resolved = resolve_receiver_place(slice_expr, ctx, slice_place, slice_type);

            if (resolved) {
                LocalId index_local = lower_expression(*call.args[1], ctx);

                BlockId success_block = ctx.new_block();
                std::vector<MirOperandPtr> args;
                args.push_back(MirOperand::copy(slice_place));
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

            // 黙殺禁止: レシーバを解決できない場合は診断を出す（C11）
            debug::log(debug::Stage::Mir, debug::Level::Error,
                       "slice delete(): レシーバのスライス場所を解決できませんでした");
        }
        return ctx.new_temp(hir::make_void());
    }

    if (call.func_name == "__builtin_slice_clear") {
        if (!call.args.empty()) {
            auto slice_expr = call.args[0].get();

            // スライスレシーバの場所を解決する（VarRef/Member/固定長配列Index。C11/H10）
            MirPlace slice_place{0};
            hir::TypePtr slice_type = nullptr;
            bool resolved = resolve_receiver_place(slice_expr, ctx, slice_place, slice_type);

            if (resolved) {
                BlockId success_block = ctx.new_block();
                std::vector<MirOperandPtr> args;
                args.push_back(MirOperand::copy(slice_place));

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

            // 黙殺禁止: レシーバを解決できない場合は診断を出す（C11）
            debug::log(debug::Stage::Mir, debug::Level::Error,
                       "slice clear(): レシーバのスライス場所を解決できませんでした");
        }
        return ctx.new_temp(hir::make_void());
    }

    return std::nullopt;
}

}  // namespace cm::mir
