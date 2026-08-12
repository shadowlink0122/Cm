// MIR lowering - 構築式（構造体リテラル・配列リテラル・enum構築/ペイロード抽出）

#include "internal/base/debug.hpp"
#include "internal/mir/lowering/expr.hpp"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
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

        // ジェネリック構造体のリテラル: フィールド型の型パラメータをリテラルの具象型引数で置換する。
        // 置換しないとフィールド T v（T=int[]）がスライスフィールドと判定されず、固定配列blobが
        // 生格納されて不正なCmSlice*になり、内容比較等の参照でSIGSEGVしていた
        if (field_type && struct_def && !struct_def->generic_params.empty() &&
            struct_type->type_args.size() == struct_def->generic_params.size()) {
            std::unordered_map<std::string, hir::TypePtr> subst;
            for (size_t gi = 0; gi < struct_def->generic_params.size(); ++gi) {
                subst[struct_def->generic_params[gi].name] = struct_type->type_args[gi];
            }
            field_type = ast::substitute_type_params(field_type, subst);
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
            // 配列リテラルからスライスフィールド値を実体化する（cm_slice_new+push。正準ヘルパへ委譲）
            field_value = materialize_slice_literal(arr_lit->elements, field_type, ctx);
        } else {
            // 通常の処理
            field_value = lower_expression(*field.value, ctx);
        }

        // 変換統一ドライバ第1段: numeric/ユニオン構築/固定長配列→スライスをcoerce_to_expected 1系統で挿入する（B2/Y1）
        field_value = ctx.coerce_to_expected(field_value, field_type);

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

        // 変換統一ドライバ: numeric/ユニオン/インターフェイスupcast（fat pointer構築）を1系統で挿入する
        // （旧来はkind不一致の生Castのみで、interface要素スロットはバックエンドの射影assign認識頼みだった）
        elem_value = ctx.coerce_to_expected(elem_value, elem_type);

        // 要素の型が期待される型と異なる場合、型変換が必要
        hir::TypePtr actual_elem_type = nullptr;
        if (elem_value < ctx.func->locals.size()) {
            actual_elem_type = ctx.func->locals[elem_value].type;
        }

        // 型変換が必要かチェック（ドライバが扱わない残余のkind不一致の生Castフォールバック）
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
    // Q5: selfポインタ経由のmatch（enumのinherent implメソッド内等）はderefしてからペイロードを射影する（タグ読みのlower_memberは自動derefするがこちらは手組み射影のため明示する）
    hir::TypePtr scrutinee_type = (scrutinee_local < ctx.func->locals.size())
                                      ? ctx.func->locals[scrutinee_local].type
                                      : nullptr;
    if (scrutinee_type && scrutinee_type->kind == hir::TypeKind::Pointer) {
        payload_place.projections.push_back(PlaceProjection::deref());
    }
    payload_place.projections.push_back(PlaceProjection::field(1));
    ctx.push_statement(
        MirStatement::assign(MirPlace{result}, MirRvalue::use(MirOperand::copy(payload_place))));

    return result;
}

}  // namespace cm::mir
