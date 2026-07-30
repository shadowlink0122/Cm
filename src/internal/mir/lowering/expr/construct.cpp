// MIR lowering - 構築式（構造体リテラル・配列リテラル・enum構築/ペイロード抽出）

#include "internal/base/debug.hpp"
#include "internal/base/target.hpp"
#include "internal/mir/lowering/expr.hpp"
#include "internal/mir/lowering/slice_dispatch.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cm::mir {

// 構造体リテラルのlowering
LocalId ExprLowering::lower_struct_literal(const hir::HirStructLiteral& lit,
                                           const hir::TypePtr& expr_type, LoweringContext& ctx) {
    debug_msg("MIR", "Lowering struct literal: " + lit.type_name);

    // 構造体の型を作成
    hir::TypePtr struct_type = std::make_shared<hir::Type>(hir::TypeKind::Struct);
    struct_type->name = lit.type_name;

    // ジェネリック特殊化のリテラル（typedef IntPair = Pair<int,int>;由来）は型引数付きのHIR型を一時変数へ引き継ぎ、モノモーフィゼーションの特殊化・型書き換え対象にする（B8）
    if (expr_type && expr_type->kind == hir::TypeKind::Struct && expr_type->name == lit.type_name &&
        !expr_type->type_args.empty()) {
        struct_type = expr_type;
    }

    // 結果用の変数を作成
    LocalId result = ctx.new_temp(struct_type);

    // 構造体定義を取得
    const hir::HirStruct* struct_def = nullptr;
    if (ctx.struct_defs && ctx.struct_defs->count(lit.type_name)) {
        struct_def = ctx.struct_defs->at(lit.type_name);
    }

    // 各フィールドを初期化（名前付き初期化のみ）
    for (const auto& field : lit.fields) {
        // フィールドインデックスを名前から検索
        size_t field_idx = 0;
        hir::TypePtr field_type = nullptr;
        if (struct_def) {
            auto idx = ctx.get_field_index(lit.type_name, field.name);
            if (idx) {
                field_idx = *idx;
                if (*idx < struct_def->fields.size()) {
                    field_type = struct_def->fields[*idx].type;
                }
            }
        }

        // スライスフィールドへの配列リテラル代入をチェック
        bool is_slice_field = field_type && field_type->kind == hir::TypeKind::Array &&
                              !field_type->array_size.has_value();

        bool is_array_literal = false;
        const hir::HirArrayLiteral* arr_lit = nullptr;
        if (auto* arr_lit_ptr =
                std::get_if<std::unique_ptr<hir::HirArrayLiteral>>(&field.value->kind)) {
            if (*arr_lit_ptr) {
                is_array_literal = true;
                arr_lit = arr_lit_ptr->get();
            }
        }

        LocalId field_value;

        if (is_slice_field && is_array_literal && arr_lit) {
            // 配列リテラルからスライスを作成（typedefエイリアスは解決してから判定）
            hir::TypePtr elem_type = ctx.resolve_typedef(
                field_type->element_type ? field_type->element_type : hir::make_int());

            // 要素サイズを取得
            int64_t elem_size = 4;  // デフォルトはint
            auto elem_kind = elem_type->kind;
            if (elem_kind == hir::TypeKind::Char || elem_kind == hir::TypeKind::Bool ||
                elem_kind == hir::TypeKind::Tiny || elem_kind == hir::TypeKind::UTiny) {
                elem_size = 1;
            } else if (elem_kind == hir::TypeKind::Short || elem_kind == hir::TypeKind::UShort) {
                elem_size = 2;
            } else if (elem_kind == hir::TypeKind::Long || elem_kind == hir::TypeKind::ULong ||
                       elem_kind == hir::TypeKind::Double) {
                elem_size = 8;
            } else if (elem_kind == hir::TypeKind::Float) {
                elem_size = 4;
            } else if (elem_kind == hir::TypeKind::Pointer || elem_kind == hir::TypeKind::String) {
                elem_size = 8;
            } else if (elem_kind == hir::TypeKind::Struct || elem_kind == hir::TypeKind::Union) {
                // 構造体・ユニオンはblob（値のインラインコピー）として格納する
                elem_size = ctx.layout_size(elem_type);
            } else if (elem_kind == hir::TypeKind::Array) {
                // 多次元スライス: 要素はCmSlice構造体（data/len/cap/elem_size）のインライン格納
                elem_size = static_cast<int64_t>(sizeof(void*) * 4);
            }

            // スライス用の一時変数を作成
            field_value = ctx.new_temp(field_type);

            // cm_slice_new(elem_size, initial_capacity) を呼び出し
            LocalId elem_size_local = ctx.new_temp(hir::make_long());
            MirConstant elem_size_const;
            elem_size_const.value = static_cast<int64_t>(elem_size);
            elem_size_const.type = hir::make_long();
            ctx.push_statement(MirStatement::assign(
                MirPlace{elem_size_local}, MirRvalue::use(MirOperand::constant(elem_size_const))));

            LocalId init_cap_local = ctx.new_temp(hir::make_long());
            MirConstant init_cap_const;
            init_cap_const.value = static_cast<int64_t>(arr_lit->elements.size());
            init_cap_const.type = hir::make_long();
            ctx.push_statement(MirStatement::assign(
                MirPlace{init_cap_local}, MirRvalue::use(MirOperand::constant(init_cap_const))));

            // cm_slice_new呼び出し
            BlockId new_block = ctx.new_block();
            std::vector<MirOperandPtr> new_args;
            new_args.push_back(MirOperand::copy(MirPlace{elem_size_local}));
            new_args.push_back(MirOperand::copy(MirPlace{init_cap_local}));

            auto new_term = std::make_unique<MirTerminator>();
            new_term->kind = MirTerminator::Call;
            new_term->data = MirTerminator::CallData{MirOperand::function_ref("cm_slice_new"),
                                                     std::move(new_args),
                                                     MirPlace{field_value},
                                                     new_block,
                                                     std::nullopt,
                                                     "",
                                                     "",
                                                     false};
            ctx.set_terminator(std::move(new_term));
            ctx.switch_to_block(new_block);

            // push関数名を決定
            std::string push_func = "cm_slice_push_i32";
            if (auto info = slice_scalar_info(elem_kind)) {
                // スカラ型: 幅サフィックスをslice_dispatchから取得（elem_sizeと整合。C4）
                push_func = std::string("cm_slice_push_") + info->width;
            } else if (elem_kind == hir::TypeKind::Pointer || elem_kind == hir::TypeKind::String ||
                       elem_kind == hir::TypeKind::Struct) {
                push_func = "cm_slice_push_ptr";
            } else if (elem_kind == hir::TypeKind::Array) {
                // 多次元スライス: 内側スライスのヘッダをインラインコピー
                push_func = "cm_slice_push_slice";
            }

            // 各要素をpushで追加
            for (const auto& elem : arr_lit->elements) {
                LocalId elem_value = lower_expression(*elem, ctx);

                // 内側要素が固定長配列（ネストした配列リテラル等）の場合はスライスへ実体化してからpushする
                // （Grid{cells: [[7]]}が変換未配線でLLVM検証エラーになっていた。let経路と同じ変換）
                if (elem_kind == hir::TypeKind::Array && elem->type &&
                    elem->type->array_size.has_value()) {
                    const int64_t inner_size =
                        static_cast<int64_t>(elem->type->array_size.value_or(0));
                    int64_t inner_elem_size = 4;
                    if (elem->type->element_type) {
                        auto inner_ek = ctx.resolve_typedef(elem->type->element_type);
                        auto ik = inner_ek ? inner_ek->kind : elem->type->element_type->kind;
                        if (auto iinfo = slice_scalar_info(ik)) {
                            inner_elem_size = iinfo->elem_size;
                        } else if (ik == hir::TypeKind::Pointer || ik == hir::TypeKind::String) {
                            inner_elem_size = cm::target_pointer_size();
                        }
                    }
                    LocalId addr_local = ctx.new_temp(hir::make_pointer(elem->type->element_type));
                    ctx.push_statement(MirStatement::assign(
                        MirPlace{addr_local}, MirRvalue::ref(MirPlace{elem_value}, false)));
                    LocalId size_local = ctx.new_temp(hir::make_long());
                    MirConstant size_const;
                    size_const.value = inner_size;
                    size_const.type = hir::make_long();
                    ctx.push_statement(MirStatement::assign(
                        MirPlace{size_local}, MirRvalue::use(MirOperand::constant(size_const))));
                    LocalId ies_local = ctx.new_temp(hir::make_long());
                    MirConstant ies_const;
                    ies_const.value = inner_elem_size;
                    ies_const.type = hir::make_long();
                    ctx.push_statement(MirStatement::assign(
                        MirPlace{ies_local}, MirRvalue::use(MirOperand::constant(ies_const))));

                    LocalId inner_slice = ctx.new_temp(elem_type);
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
                                                MirPlace{inner_slice},
                                                conv_block,
                                                std::nullopt,
                                                "",
                                                "",
                                                false};
                    ctx.set_terminator(std::move(conv_term));
                    ctx.switch_to_block(conv_block);
                    elem_value = inner_slice;
                }

                BlockId success_block = ctx.new_block();
                std::vector<MirOperandPtr> push_args;
                push_args.push_back(MirOperand::copy(MirPlace{field_value}));
                push_args.push_back(MirOperand::copy(MirPlace{elem_value}));

                auto call_term = std::make_unique<MirTerminator>();
                call_term->kind = MirTerminator::Call;
                call_term->data = MirTerminator::CallData{MirOperand::function_ref(push_func),
                                                          std::move(push_args),
                                                          std::nullopt,
                                                          success_block,
                                                          std::nullopt,
                                                          "",
                                                          "",
                                                          false};
                ctx.set_terminator(std::move(call_term));
                ctx.switch_to_block(success_block);
            }
        } else {
            // 通常の処理
            field_value = lower_expression(*field.value, ctx);
        }

        // 整数値を浮動小数フィールドへ入れる場合はsitofp/uitofp相当のCastを挿入する（B2）
        field_value = ctx.coerce_to_float_context(field_value, field_type);

        // フィールドへの代入を生成
        MirPlace place{result};
        place.projections.push_back(PlaceProjection::field(field_idx));

        ctx.push_statement(
            MirStatement::assign(place, MirRvalue::use(MirOperand::copy(MirPlace{field_value}))));
    }

    return result;
}

// 配列リテラル
LocalId ExprLowering::lower_array_literal(const hir::HirArrayLiteral& lit,
                                          const hir::TypePtr& expected_type, LoweringContext& ctx) {
    debug_msg("MIR",
              "Lowering array literal with " + std::to_string(lit.elements.size()) + " elements");

    // 要素の型を推論（期待される型があればそれを使用）
    hir::TypePtr elem_type = hir::make_int();  // デフォルト
    if (expected_type && expected_type->kind == hir::TypeKind::Array &&
        expected_type->element_type) {
        // 期待される配列型から要素型を取得
        elem_type = expected_type->element_type;
    } else if (!lit.elements.empty() && lit.elements[0]->type) {
        // フォールバック: 最初の要素の型を使用
        elem_type = lit.elements[0]->type;
    }

    // 配列型を作成
    hir::TypePtr array_type = hir::make_array(elem_type, lit.elements.size());

    // 結果用の変数を作成（new_tempでtypedefが解決される）
    LocalId result = ctx.new_temp(array_type);

    // elem_typeも解決する（new_tempと同じ解決パスを通る）
    // expected_typeのelement_typeはtypedef未解決のStruct型("Value")の場合があるため、正確な型比較のためにlocals経由で解決済みの型を取得する
    if (result < ctx.func->locals.size() && ctx.func->locals[result].type &&
        ctx.func->locals[result].type->kind == hir::TypeKind::Array &&
        ctx.func->locals[result].type->element_type) {
        elem_type = ctx.func->locals[result].type->element_type;
    }

    // 各要素を初期化
    for (size_t i = 0; i < lit.elements.size(); ++i) {
        LocalId elem_value = lower_expression(*lit.elements[i], ctx);

        // 要素の型が期待される型と異なる場合、型変換が必要
        hir::TypePtr actual_elem_type = nullptr;
        if (elem_value < ctx.func->locals.size()) {
            actual_elem_type = ctx.func->locals[elem_value].type;
        }

        // 型変換が必要かチェック
        bool needs_cast = false;
        if (actual_elem_type && elem_type) {
            // floatとdoubleの変換
            if ((elem_type->kind == hir::TypeKind::Float &&
                 actual_elem_type->kind == hir::TypeKind::Double) ||
                (elem_type->kind == hir::TypeKind::Double &&
                 actual_elem_type->kind == hir::TypeKind::Float)) {
                needs_cast = true;
            }
            // intとlongの変換など
            else if (elem_type->kind != actual_elem_type->kind) {
                needs_cast = true;
            }
        }

        // 型変換が必要な場合はcastを挿入
        if (needs_cast) {
            LocalId casted = ctx.new_temp(elem_type);
            ctx.push_statement(MirStatement::assign(
                MirPlace{casted},
                MirRvalue::cast(MirOperand::copy(MirPlace{elem_value}), elem_type)));
            elem_value = casted;
        }

        // インデックス用の定数を変数に格納
        LocalId idx_local = ctx.new_temp(hir::make_int());
        MirConstant idx_const;
        idx_const.value = static_cast<int64_t>(i);
        idx_const.type = hir::make_int();
        ctx.push_statement(MirStatement::assign(MirPlace{idx_local},
                                                MirRvalue::use(MirOperand::constant(idx_const))));

        // 配列要素への代入を生成
        MirPlace place{result};
        place.projections.push_back(PlaceProjection::index(idx_local));

        ctx.push_statement(
            MirStatement::assign(place, MirRvalue::use(MirOperand::copy(MirPlace{elem_value}))));
    }

    return result;
}

// enumバリアントコンストラクタのlowering
// Tagged Union: {tag, payload}構造体を生成
// field[0] = タグ値（i32）、field[1] = ペイロード（型に応じて）
LocalId ExprLowering::lower_enum_construct(const hir::HirEnumConstruct& ec, LoweringContext& ctx) {
    debug_msg("MIR", "Lowering enum construct: " + ec.enum_name + "::" + ec.variant_name);

    // Tagged Union型（2フィールド構造体）を作成
    // 型名: "__TaggedUnion_{enum_name}"
    std::string tagged_union_name = "__TaggedUnion_" + ec.enum_name;
    hir::TypePtr union_type = std::make_shared<hir::Type>(hir::TypeKind::Struct);
    union_type->name = tagged_union_name;

    // 結果用の変数を作成
    LocalId result = ctx.new_temp(union_type);

    // field[0] = タグ値
    MirConstant tag_const;
    tag_const.type = hir::make_int();
    tag_const.value = ec.tag_value;

    MirPlace tag_place{result};
    tag_place.projections.push_back(PlaceProjection::field(0));
    ctx.push_statement(
        MirStatement::assign(tag_place, MirRvalue::use(MirOperand::constant(tag_const))));

    // field[1] = ペイロード値（ペイロードがある場合）
    if (ec.payload) {
        LocalId payload_local = lower_expression(*ec.payload, ctx);

        MirPlace payload_place{result};
        payload_place.projections.push_back(PlaceProjection::field(1));
        ctx.push_statement(MirStatement::assign(
            payload_place, MirRvalue::use(MirOperand::copy(MirPlace{payload_local}))));
    } else {
        // ペイロードがない場合はデフォルト値（0）を設定
        MirConstant zero_const;
        zero_const.type = hir::make_int();
        zero_const.value = int64_t(0);

        MirPlace payload_place{result};
        payload_place.projections.push_back(PlaceProjection::field(1));
        ctx.push_statement(
            MirStatement::assign(payload_place, MirRvalue::use(MirOperand::constant(zero_const))));
    }

    return result;
}

// enumペイロード抽出のlowering
// match式でバインディング変数に代入するペイロード値を取得
// scrutinee.field[1]からペイロードを抽出
LocalId ExprLowering::lower_enum_payload(const hir::HirEnumPayload& ep, LoweringContext& ctx) {
    debug_msg("MIR", "Lowering enum payload extract for variant: " + ep.variant_name);

    // scrutineeをlowering
    LocalId scrutinee_local = lower_expression(*ep.scrutinee, ctx);

    // ペイロード型で結果を作成
    LocalId result = ctx.new_temp(ep.payload_type);

    // scrutinee.field[1]（ペイロード）を抽出
    MirPlace payload_place{scrutinee_local};
    payload_place.projections.push_back(PlaceProjection::field(1));
    ctx.push_statement(
        MirStatement::assign(MirPlace{result}, MirRvalue::use(MirOperand::copy(payload_place))));

    return result;
}

}  // namespace cm::mir
