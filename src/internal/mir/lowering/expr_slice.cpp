// ============================================================
// MIR lowering - スライスbuiltin（len/cap/push/pop/delete/clear）
// ============================================================
// 要素型ごとのランタイム関数選択と呼び出し規約はslice_dispatch.hppのslice_elem_dispatchに集約し、本ファイルは表引きと共通のCall構築ヘルパのみで構成する（type-resolution-simplification 領域4）。
// 従来はpush/pop/get各loweringが「スカラ→幅・Array→slice・Struct/Union→blob・Pointer/String→ptr」の選択と規約を個別に手書きしており、popだけがblob受け取り規約を欠いてW3（構造体pop戻り値のSIGSEGV）を招いた。

#include "expr.hpp"
#include "internal/base/debug.hpp"
#include "internal/base/i18n.hpp"
#include "internal/base/target.hpp"
#include "internal/hir/lowering/fwd.hpp"
#include "internal/hir/slice_dispatch.hpp"
#include "layout.hpp"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cm::mir {

namespace {

// ランタイム関数呼び出しを1ブロックとして発行し、後続ブロックへ切り替える（destなしは戻り値破棄）
void emit_call(LoweringContext& ctx, const std::string& func, std::vector<MirOperandPtr> args,
               std::optional<MirPlace> dest) {
    BlockId next = ctx.new_block();
    auto term = std::make_unique<MirTerminator>();
    term->kind = MirTerminator::Call;
    term->data = MirTerminator::CallData{MirOperand::function_ref(func),
                                         std::move(args),
                                         std::move(dest),
                                         next,
                                         std::nullopt,
                                         "",
                                         "",
                                         false};
    ctx.set_terminator(std::move(term));
    ctx.switch_to_block(next);
}

// 整数定数をローカルへ実体化する
LocalId make_const(LoweringContext& ctx, int64_t v, const hir::TypePtr& type) {
    LocalId l = ctx.new_temp(type);
    MirConstant c;
    c.value = v;
    c.type = type;
    ctx.push_statement(MirStatement::assign(MirPlace{l}, MirRvalue::use(MirOperand::constant(c))));
    return l;
}

// 値ローカルのアドレスを取ってポインタローカルを返す（Blob規約のアドレス渡し）
LocalId make_addr(LoweringContext& ctx, LocalId value_local) {
    hir::TypePtr vt =
        (value_local < ctx.func->locals.size()) ? ctx.func->locals[value_local].type : nullptr;
    LocalId addr = ctx.new_temp(hir::make_pointer(vt ? vt : hir::make_int()));
    ctx.push_statement(
        MirStatement::assign(MirPlace{addr}, MirRvalue::ref(MirPlace{value_local}, false)));
    return addr;
}

// 要素型の確保バイト幅はlayout APIへ一元化（Z2/Y6。手書き表の再複製を避ける）
int64_t elem_size_of(LoweringContext& ctx, const hir::TypePtr& t) {
    return layout::array_elem_stride(ctx, t);
}

}  // namespace

// スライスbuiltinを処理した場合はローカルIDを、対象外ならnulloptを返す
std::optional<LocalId> ExprLowering::try_lower_slice_builtin(const hir::HirCall& call,
                                                             const hir::TypePtr& result_type,
                                                             LoweringContext& ctx) {
    const std::string& name = call.func_name;
    const bool is_len = name == "__builtin_slice_len";
    const bool is_cap = name == "__builtin_slice_cap";
    const bool is_push = name == "__builtin_slice_push";
    const bool is_pop = name == "__builtin_slice_pop";
    const bool is_delete = name == "__builtin_slice_delete";
    const bool is_clear = name == "__builtin_slice_clear";
    if (!is_len && !is_cap && !is_push && !is_pop && !is_delete && !is_clear) {
        return std::nullopt;
    }

    // 引数不足・レシーバ解決不能時の返り値（従来の各ビルトインの既定と同じ）
    auto fallback_result = [&]() -> LocalId {
        if (is_len || is_cap) {
            return ctx.new_temp(hir::make_uint());
        }
        if (is_pop) {
            return ctx.new_temp(result_type ? result_type : hir::make_int());
        }
        return ctx.new_temp(hir::make_void());
    };
    const size_t min_args = (is_push || is_delete) ? 2 : 1;
    if (call.args.size() < min_args) {
        return fallback_result();
    }

    // レシーバの場所化（唯一の場所化API lower_place。H10）
    MirPlace slice_place{0};
    hir::TypePtr slice_type = nullptr;
    bool resolved = lower_place(call.args[0].get(), ctx, slice_place, slice_type);

    // len/capは場所を持たない式（make_slice().len() 等の呼び出し戻り値）も一時実体化して読める（H10）
    if (!resolved && (is_len || is_cap)) {
        slice_place = MirPlace{lower_expression(*call.args[0], ctx)};
        resolved = true;
    }
    if (!resolved) {
        // 黙殺禁止: レシーバを解決できない場合はエラー診断として報告しcodegen前に停止する（C11/H10。従来はログのみでコンパイル続行し文ごと欠落していた）
        report_error(call.args[0]->span, i18n::msg(i18n::MsgId::MirSliceReceiverUnresolved));
        return fallback_result();
    }

    if (is_len || is_cap) {
        LocalId result = ctx.new_temp(hir::make_uint());
        std::vector<MirOperandPtr> args;
        args.push_back(MirOperand::copy(slice_place));
        emit_call(ctx, is_len ? "cm_slice_len" : "cm_slice_cap", std::move(args), MirPlace{result});
        return result;
    }

    if (is_delete) {
        LocalId index_local = lower_expression(*call.args[1], ctx);
        std::vector<MirOperandPtr> args;
        args.push_back(MirOperand::copy(slice_place));
        args.push_back(MirOperand::copy(MirPlace{index_local}));
        emit_call(ctx, "cm_slice_delete", std::move(args), std::nullopt);
        return ctx.new_temp(hir::make_void());
    }

    if (is_clear) {
        std::vector<MirOperandPtr> args;
        args.push_back(MirOperand::copy(slice_place));
        emit_call(ctx, "cm_slice_clear", std::move(args), std::nullopt);
        return ctx.new_temp(hir::make_void());
    }

    // 要素型と格納規約を表から引く（メンバ経由のslice_typeはtypedefエイリアス未解決のことがあるため解決してから判定。C4）
    hir::TypePtr elem_type = nullptr;
    if (slice_type && slice_type->element_type) {
        auto r = ctx.resolve_typedef(slice_type->element_type);
        elem_type = r ? r : slice_type->element_type;
    }
    const hir::SliceElemDispatch disp = hir::slice_elem_dispatch(elem_type);

    if (is_push) {
        LocalId value_local = lower_expression(*call.args[1], ctx);

        // 変換統一ドライバ第1段: ユニオン構築（Y3）・numeric・固定長配列→スライスをcoerce_to_expected 1系統で挿入する（多次元リテラルpushの専用経路は後段が処理する）
        value_local = ctx.coerce_to_expected(value_local, elem_type);

        // 多次元スライスへの配列リテラル直接push（X3）: リテラルは固定長配列blobとしてlowerされるためcm_slice_push_sliceが期待するCmSliceヘッダにならない。cm_array_to_sliceでヒープスライスへ実体化してからpushする（空リテラルはlen=0の正規ヘッダ）
        if (disp.cls == hir::SliceElemClass::InnerSlice && value_local < ctx.func->locals.size()) {
            hir::TypePtr vt = ctx.func->locals[value_local].type;
            if (vt && vt->kind == hir::TypeKind::Array && vt->array_size.has_value()) {
                // 空リテラル[]は要素型をレシーバの内側スライスから取る
                hir::TypePtr inner_elem = vt->element_type
                                              ? vt->element_type
                                              : (elem_type ? elem_type->element_type : nullptr);
                LocalId addr = make_addr(ctx, value_local);
                LocalId size_local = make_const(
                    ctx, static_cast<int64_t>(vt->array_size.value_or(0)), hir::make_long());
                LocalId ies_local =
                    make_const(ctx, elem_size_of(ctx, inner_elem), hir::make_long());
                LocalId conv = ctx.new_temp(elem_type ? elem_type : slice_type->element_type);
                std::vector<MirOperandPtr> conv_args;
                conv_args.push_back(MirOperand::copy(MirPlace{addr}));
                conv_args.push_back(MirOperand::copy(MirPlace{size_local}));
                conv_args.push_back(MirOperand::copy(MirPlace{ies_local}));
                emit_call(ctx, "cm_array_to_slice", std::move(conv_args), MirPlace{conv});
                value_local = conv;
            }
        }

        // インターフェイス要素スライスへの具象構造体push: インターフェイス型の一時へ代入してfat pointerを構築してからblob格納する（H1）
        if (disp.cls == hir::SliceElemClass::Blob && elem_type &&
            elem_type->kind == hir::TypeKind::Struct && ctx.interface_names &&
            ctx.interface_names->count(elem_type->name) > 0) {
            hir::TypePtr actual = (value_local < ctx.func->locals.size())
                                      ? ctx.func->locals[value_local].type
                                      : nullptr;
            if (actual && actual->kind == hir::TypeKind::Struct &&
                actual->name != elem_type->name) {
                LocalId iface_tmp = ctx.new_temp(elem_type);
                ctx.push_statement(MirStatement::assign(
                    MirPlace{iface_tmp}, MirRvalue::use(MirOperand::copy(MirPlace{value_local}))));
                value_local = iface_tmp;
            }
        }

        std::vector<MirOperandPtr> args;
        args.push_back(MirOperand::copy(slice_place));
        // Blob規約は値のアドレスを渡す（集約のインラインコピーのため）、それ以外は値渡し
        LocalId value_arg =
            (disp.cls == hir::SliceElemClass::Blob) ? make_addr(ctx, value_local) : value_local;
        args.push_back(MirOperand::copy(MirPlace{value_arg}));
        emit_call(ctx, std::string("cm_slice_push_") + disp.suffix, std::move(args), std::nullopt);
        return ctx.new_temp(hir::make_void());
    }

    if (is_pop) {
        hir::TypePtr result_elem = elem_type ? elem_type : hir::make_int();
        if (disp.cls == hir::SliceElemClass::Blob) {
            // Blob規約: ポインタ幅戻り値のcm_slice_pop_ptrでは構造体宛先への格納が型不整合になるため（W3）、末尾要素ポインタからderefコピーで受けてからlenを減算する
            LocalId len_local = ctx.new_temp(hir::make_long());
            std::vector<MirOperandPtr> len_args;
            len_args.push_back(MirOperand::copy(slice_place));
            emit_call(ctx, "cm_slice_len", std::move(len_args), MirPlace{len_local});

            LocalId idx_local = ctx.new_temp(hir::make_long());
            MirConstant one_const;
            one_const.value = int64_t{1};
            one_const.type = hir::make_long();
            ctx.push_statement(MirStatement::assign(
                MirPlace{idx_local},
                MirRvalue::binary(MirBinaryOp::Sub, MirOperand::copy(MirPlace{len_local}),
                                  MirOperand::constant(one_const), hir::make_long())));

            LocalId elem_ptr = ctx.new_temp(hir::make_pointer(result_elem));
            std::vector<MirOperandPtr> ptr_args;
            ptr_args.push_back(MirOperand::copy(slice_place));
            ptr_args.push_back(MirOperand::copy(MirPlace{idx_local}));
            emit_call(ctx, "cm_slice_get_element_ptr", std::move(ptr_args), MirPlace{elem_ptr});

            LocalId blob_result = ctx.new_temp(result_elem);
            ctx.push_statement(MirStatement::assign(
                MirPlace{blob_result},
                MirRvalue::use(MirOperand::copy(MirPlace{elem_ptr, {PlaceProjection::deref()}}))));

            std::vector<MirOperandPtr> pop_args;
            pop_args.push_back(MirOperand::copy(slice_place));
            emit_call(ctx, "cm_slice_pop_blob", std::move(pop_args), std::nullopt);
            return blob_result;
        }

        // 内側スライス要素のpopはランタイム未実装のため従来既定のi32に落ちる（既知の未対応。導入時はcm_slice_pop_sliceを追加して表のsuffixを使う）
        const std::string pop_suffix =
            (disp.cls == hir::SliceElemClass::InnerSlice) ? "i32" : disp.suffix;
        LocalId result = ctx.new_temp(result_elem);
        std::vector<MirOperandPtr> args;
        args.push_back(MirOperand::copy(slice_place));
        emit_call(ctx, std::string("cm_slice_pop_") + pop_suffix, std::move(args),
                  MirPlace{result});
        return result;
    }

    return std::nullopt;
}

}  // namespace cm::mir
