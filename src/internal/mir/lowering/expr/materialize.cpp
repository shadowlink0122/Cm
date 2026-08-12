// MIR lowering - スライスリテラルの実体化（cm_slice_new確保+要素pushループの唯一の発行箇所）
// let初期化・構造体リテラルフィールドの配列リテラル→スライス経路が共有する。
// push関数の選択はslice_elem_dispatchの正準表、要素の暗黙変換はcoerce_to_expected、
// 内側固定長配列の実体化はmaterialize_array_to_sliceへ委譲し、格納規約の再実装を持たない

#include "internal/base/debug.hpp"
#include "internal/hir/slice_dispatch.hpp"
#include "internal/mir/lowering/expr.hpp"
#include "internal/mir/lowering/layout.hpp"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cm::mir {

LocalId ExprLowering::materialize_slice_literal(const std::vector<hir::HirExprPtr>& elements,
                                                const hir::TypePtr& slice_type,
                                                LoweringContext& ctx,
                                                std::optional<MirPlace> dest) {
    // 要素型はtypedefエイリアスを解決してから要素サイズ・push規約を決める
    hir::TypePtr elem_type = ctx.resolve_typedef(
        slice_type && slice_type->element_type ? slice_type->element_type : hir::make_int());
    const int64_t elem_size = layout::slice_elem_stride(ctx, elem_type);

    // cm_slice_new(elem_size, initial_capacity) を呼び出し（容量は要素数）
    LocalId elem_size_local = ctx.new_temp(hir::make_long());
    MirConstant elem_size_const;
    elem_size_const.value = elem_size;
    elem_size_const.type = hir::make_long();
    ctx.push_statement(MirStatement::assign(MirPlace{elem_size_local},
                                            MirRvalue::use(MirOperand::constant(elem_size_const))));

    LocalId init_cap_local = ctx.new_temp(hir::make_long());
    MirConstant init_cap_const;
    init_cap_const.value = static_cast<int64_t>(elements.size());
    init_cap_const.type = hir::make_long();
    ctx.push_statement(MirStatement::assign(MirPlace{init_cap_local},
                                            MirRvalue::use(MirOperand::constant(init_cap_const))));

    // 宛先が指定されていればそこへ、なければスライス型の一時へ格納する
    MirPlace slice_place = dest ? *dest : MirPlace{ctx.new_temp(slice_type)};
    BlockId new_block = ctx.new_block();
    std::vector<MirOperandPtr> new_args;
    new_args.push_back(MirOperand::copy(MirPlace{elem_size_local}));
    new_args.push_back(MirOperand::copy(MirPlace{init_cap_local}));

    auto new_term = std::make_unique<MirTerminator>();
    new_term->kind = MirTerminator::Call;
    new_term->data = MirTerminator::CallData{MirOperand::function_ref("cm_slice_new"),
                                             std::move(new_args),
                                             slice_place,
                                             new_block,
                                             std::nullopt,
                                             "",
                                             "",
                                             false};
    ctx.set_terminator(std::move(new_term));
    ctx.switch_to_block(new_block);

    // push関数はslice_elem_dispatchの表引きで選ぶ（Z1/Y6: 固定長配列要素はN×要素のインラインblob、スライス要素はヘッダ格納）
    const hir::SliceElemDispatch elem_disp = hir::slice_elem_dispatch(elem_type);
    const std::string push_func = std::string("cm_slice_push_") + elem_disp.suffix;

    // 各要素をpushで追加
    for (const auto& elem : elements) {
        LocalId elem_value;

        // 要素がスライス（内側スライス格納）で値が固定長配列リテラルの場合のみヒープスライスへ実体化する。
        // 固定長配列要素（Blobクラス）はそのままblobアドレス渡しで格納する（Y6）
        if (elem_disp.cls == hir::SliceElemClass::InnerSlice && elem->type &&
            elem->type->array_size.has_value()) {
            LocalId arr_value = lower_expression(*elem, ctx);
            elem_value = ctx.materialize_array_to_slice(
                MirPlace{arr_value}, elem->type, elem_type, std::nullopt,
                elem_type ? elem_type->element_type : nullptr);
        } else {
            elem_value = lower_expression(*elem, ctx);

            // ユニオン要素スライスの変種値要素はユニオン構築Cast経由でタグ+ペイロードを揃え、
            // インターフェイス要素スライスへの具象構造体要素はfat pointer構築（iface_upcast）を発行する
            elem_value = ctx.coerce_to_expected(elem_value, elem_type);

            // floatスライスへのdouble要素の場合、floatにキャスト（浮動小数点リテラルはデフォルトでdouble）
            if (elem_type->kind == hir::TypeKind::Float) {
                hir::TypePtr actual_elem_type = nullptr;
                if (elem_value < ctx.func->locals.size()) {
                    actual_elem_type = ctx.func->locals[elem_value].type;
                }
                if (actual_elem_type && actual_elem_type->kind == hir::TypeKind::Double) {
                    LocalId casted = ctx.new_temp(hir::make_float());
                    ctx.push_statement(MirStatement::assign(
                        MirPlace{casted}, MirRvalue::cast(MirOperand::copy(MirPlace{elem_value}),
                                                          hir::make_float())));
                    elem_value = casted;
                }
            }
        }

        BlockId success_block = ctx.new_block();
        std::vector<MirOperandPtr> args;
        args.push_back(MirOperand::copy(slice_place));
        if (elem_disp.cls == hir::SliceElemClass::Blob) {
            // blob pushはデータ先頭へのポインタを受け取る（ランタイムがelem_sizeバイトをインラインコピー）
            hir::TypePtr value_type = nullptr;
            if (elem_value < ctx.func->locals.size()) {
                value_type = ctx.func->locals[elem_value].type;
            }
            LocalId addr_local =
                ctx.new_temp(hir::make_pointer(value_type ? value_type : hir::make_int()));
            ctx.push_statement(MirStatement::assign(MirPlace{addr_local},
                                                    MirRvalue::ref(MirPlace{elem_value}, false)));
            args.push_back(MirOperand::copy(MirPlace{addr_local}));
        } else {
            args.push_back(MirOperand::copy(MirPlace{elem_value}));
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
    }

    return slice_place.local;
}

}  // namespace cm::mir
