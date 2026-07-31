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

                // 多次元スライスへの配列リテラル直接push（X3）:
                // リテラルは固定長配列blobとしてlowerされるため、cm_slice_push_sliceが
                // 期待するCmSliceヘッダでないメモリが渡り壊れた要素が格納されていた。
                // cm_array_to_sliceでヒープスライスへ実体化してからpushする（空リテラルはlen=0の正規ヘッダ）
                if (push_func == "cm_slice_push_slice" && value_local < ctx.func->locals.size()) {
                    hir::TypePtr vt = ctx.func->locals[value_local].type;
                    if (vt && vt->kind == hir::TypeKind::Array && vt->array_size.has_value()) {
                        auto inner_slice_type = ctx.resolve_typedef(slice_type->element_type);
                        const int64_t inner_size = static_cast<int64_t>(vt->array_size.value_or(0));
                        int64_t inner_elem_size = 4;
                        hir::TypePtr inner_et =
                            inner_slice_type ? inner_slice_type->element_type : nullptr;
                        if (vt->element_type) {
                            auto rk = ctx.resolve_typedef(vt->element_type);
                            auto ik = rk ? rk->kind : vt->element_type->kind;
                            if (auto iinfo = slice_scalar_info(ik)) {
                                inner_elem_size = iinfo->elem_size;
                            } else if (ik == hir::TypeKind::Pointer ||
                                       ik == hir::TypeKind::String) {
                                inner_elem_size = cm::target_pointer_size();
                            } else if (ik == hir::TypeKind::Struct || ik == hir::TypeKind::Union) {
                                inner_elem_size = ctx.layout_size(rk ? rk : vt->element_type);
                            } else if (ik == hir::TypeKind::Array) {
                                inner_elem_size = static_cast<int64_t>(sizeof(void*) * 4);
                            }
                        } else if (inner_et) {
                            // 空リテラル []: 要素型はレシーバの内側スライスから取る
                            auto rk = ctx.resolve_typedef(inner_et);
                            auto ik = rk ? rk->kind : inner_et->kind;
                            if (auto iinfo = slice_scalar_info(ik)) {
                                inner_elem_size = iinfo->elem_size;
                            } else if (ik == hir::TypeKind::Pointer ||
                                       ik == hir::TypeKind::String) {
                                inner_elem_size = cm::target_pointer_size();
                            } else if (ik == hir::TypeKind::Struct || ik == hir::TypeKind::Union) {
                                inner_elem_size = ctx.layout_size(rk ? rk : inner_et);
                            } else if (ik == hir::TypeKind::Array) {
                                inner_elem_size = static_cast<int64_t>(sizeof(void*) * 4);
                            }
                        }

                        LocalId addr_local = ctx.new_temp(hir::make_pointer(vt->element_type));
                        ctx.push_statement(MirStatement::assign(
                            MirPlace{addr_local}, MirRvalue::ref(MirPlace{value_local}, false)));
                        LocalId size_local = ctx.new_temp(hir::make_long());
                        MirConstant size_const;
                        size_const.value = inner_size;
                        size_const.type = hir::make_long();
                        ctx.push_statement(
                            MirStatement::assign(MirPlace{size_local},
                                                 MirRvalue::use(MirOperand::constant(size_const))));
                        LocalId ies_local = ctx.new_temp(hir::make_long());
                        MirConstant ies_const;
                        ies_const.value = inner_elem_size;
                        ies_const.type = hir::make_long();
                        ctx.push_statement(MirStatement::assign(
                            MirPlace{ies_local}, MirRvalue::use(MirOperand::constant(ies_const))));

                        LocalId conv = ctx.new_temp(inner_slice_type ? inner_slice_type
                                                                     : slice_type->element_type);
                        BlockId conv_block = ctx.new_block();
                        std::vector<MirOperandPtr> conv_args;
                        conv_args.push_back(MirOperand::copy(MirPlace{addr_local}));
                        conv_args.push_back(MirOperand::copy(MirPlace{size_local}));
                        conv_args.push_back(MirOperand::copy(MirPlace{ies_local}));
                        auto conv_term = std::make_unique<MirTerminator>();
                        conv_term->kind = MirTerminator::Call;
                        conv_term->data =
                            MirTerminator::CallData{MirOperand::function_ref("cm_array_to_slice"),
                                                    std::move(conv_args),
                                                    MirPlace{conv},
                                                    conv_block,
                                                    std::nullopt,
                                                    "",
                                                    "",
                                                    false};
                        ctx.set_terminator(std::move(conv_term));
                        ctx.switch_to_block(conv_block);
                        value_local = conv;
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
                               elem_kind == hir::TypeKind::String) {
                        pop_func = "cm_slice_pop_ptr";
                    } else if (elem_kind == hir::TypeKind::Struct ||
                               elem_kind == hir::TypeKind::Union) {
                        // blob要素: cm_slice_pop_ptrはポインタ幅の戻り値を構造体宛先へ
                        // 格納する型不整合になりSIGSEGVしていた（W3）。
                        // 末尾要素ポインタからderefコピーで受けてからlenを減算する
                        LocalId len_local = ctx.new_temp(hir::make_long());
                        BlockId len_block = ctx.new_block();
                        std::vector<MirOperandPtr> len_args;
                        len_args.push_back(MirOperand::copy(slice_place));
                        auto len_term = std::make_unique<MirTerminator>();
                        len_term->kind = MirTerminator::Call;
                        len_term->data =
                            MirTerminator::CallData{MirOperand::function_ref("cm_slice_len"),
                                                    std::move(len_args),
                                                    MirPlace{len_local},
                                                    len_block,
                                                    std::nullopt,
                                                    "",
                                                    "",
                                                    false};
                        ctx.set_terminator(std::move(len_term));
                        ctx.switch_to_block(len_block);

                        LocalId idx_local = ctx.new_temp(hir::make_long());
                        MirConstant one_const;
                        one_const.value = int64_t{1};
                        one_const.type = hir::make_long();
                        ctx.push_statement(MirStatement::assign(
                            MirPlace{idx_local},
                            MirRvalue::binary(MirBinaryOp::Sub,
                                              MirOperand::copy(MirPlace{len_local}),
                                              MirOperand::constant(one_const), hir::make_long())));

                        LocalId elem_ptr = ctx.new_temp(hir::make_pointer(elem_type));
                        BlockId ptr_block = ctx.new_block();
                        std::vector<MirOperandPtr> ptr_args;
                        ptr_args.push_back(MirOperand::copy(slice_place));
                        ptr_args.push_back(MirOperand::copy(MirPlace{idx_local}));
                        auto ptr_term = std::make_unique<MirTerminator>();
                        ptr_term->kind = MirTerminator::Call;
                        ptr_term->data = MirTerminator::CallData{
                            MirOperand::function_ref("cm_slice_get_element_ptr"),
                            std::move(ptr_args),
                            MirPlace{elem_ptr},
                            ptr_block,
                            std::nullopt,
                            "",
                            "",
                            false};
                        ctx.set_terminator(std::move(ptr_term));
                        ctx.switch_to_block(ptr_block);

                        LocalId blob_result = ctx.new_temp(elem_type);
                        ctx.push_statement(MirStatement::assign(
                            MirPlace{blob_result}, MirRvalue::use(MirOperand::copy(MirPlace{
                                                       elem_ptr, {PlaceProjection::deref()}}))));

                        BlockId pop_block = ctx.new_block();
                        std::vector<MirOperandPtr> pop_args;
                        pop_args.push_back(MirOperand::copy(slice_place));
                        auto pop_term = std::make_unique<MirTerminator>();
                        pop_term->kind = MirTerminator::Call;
                        pop_term->data =
                            MirTerminator::CallData{MirOperand::function_ref("cm_slice_pop_blob"),
                                                    std::move(pop_args),
                                                    std::nullopt,
                                                    pop_block,
                                                    std::nullopt,
                                                    "",
                                                    "",
                                                    false};
                        ctx.set_terminator(std::move(pop_term));
                        ctx.switch_to_block(pop_block);
                        return blob_result;
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
