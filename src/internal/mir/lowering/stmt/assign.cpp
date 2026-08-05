// MIR lowering - 代入文（左辺値の解決と代入の展開）

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

// 代入文のlowering
void StmtLowering::lower_assign(const hir::HirAssign& assign, LoweringContext& ctx) {
    if (!assign.target || !assign.value) {
        return;
    }

    // Bug#14修正: 右辺が配列リテラルの場合、temp経由のcopyを避けて
    // 直接ターゲット変数の各インデックスに要素を書き込む。
    // temp経由copyでは構造体要素の配列で正しくコピーされない問題がある。
    if (auto* arr_lit_ptr =
            std::get_if<std::unique_ptr<hir::HirArrayLiteral>>(&assign.value->kind)) {
        const auto& arr_lit = **arr_lit_ptr;

        // 配列リテラルの要素を直接ターゲットに書き込むヘルパー
        auto lower_array_literal_to_place = [&](MirPlace base_place) {
            for (size_t i = 0; i < arr_lit.elements.size(); ++i) {
                LocalId elem_value = expr_lowering->lower_expression(*arr_lit.elements[i], ctx);

                // インデックス用の定数を変数に格納
                LocalId idx_local = ctx.new_temp(hir::make_int());
                MirConstant idx_const;
                idx_const.value = static_cast<int64_t>(i);
                idx_const.type = hir::make_int();
                ctx.push_statement(MirStatement::assign(
                    MirPlace{idx_local}, MirRvalue::use(MirOperand::constant(idx_const))));

                // ターゲットの配列要素への代入を生成
                MirPlace elem_place = base_place;
                elem_place.projections.push_back(PlaceProjection::index(idx_local));
                ctx.push_statement(MirStatement::assign(
                    elem_place, MirRvalue::use(MirOperand::copy(MirPlace{elem_value}))));
            }
        };

        // 左辺が単純な変数参照の場合
        if (auto* var_ref = std::get_if<std::unique_ptr<hir::HirVarRef>>(&assign.target->kind)) {
            auto lhs_opt = ctx.resolve_variable((*var_ref)->name);
            if (lhs_opt) {
                lower_array_literal_to_place(MirPlace{*lhs_opt});
                return;
            }
        }

        // 左辺が複雑な式（メンバーアクセス等）の場合もbuild_lvalue_placeで対応
        // この場合はフォールスルーして通常パスの build_lvalue_place を使う
        // ただし配列リテラルの各要素を直接書き込むために、ここでplaceを構築（フォールスルーさせると通常のtemp copyパスを通ってしまう）
    }

    // 右辺値をlowering
    // 配列リテラルRHSは代入先の型を期待型として渡す（`h.vs = []` のような空リテラルが要素型int既定に落ちるのを防ぐ）
    hir::TypePtr assign_target_type = assign.target ? assign.target->type : nullptr;
    if ((!assign_target_type || assign_target_type->kind != hir::TypeKind::Array) &&
        assign.target) {
        // メンバ代入で式型が未設定の場合、struct定義からフィールド型を引く
        if (auto* mem = std::get_if<std::unique_ptr<hir::HirMember>>(&assign.target->kind)) {
            const auto& obj = (*mem)->object;
            if (obj && obj->type && obj->type->kind == hir::TypeKind::Struct && ctx.struct_defs &&
                ctx.struct_defs->count(obj->type->name)) {
                const auto* struct_def = ctx.struct_defs->at(obj->type->name);
                for (const auto& f : struct_def->fields) {
                    if (f.name == (*mem)->member) {
                        assign_target_type = f.type;
                        break;
                    }
                }
            }
        }
    }

    LocalId rhs_value;
    if (auto* rhs_arr_lit = std::get_if<std::unique_ptr<hir::HirArrayLiteral>>(&assign.value->kind);
        rhs_arr_lit && assign_target_type && assign_target_type->kind == hir::TypeKind::Array) {
        rhs_value = expr_lowering->lower_array_literal(**rhs_arr_lit, assign_target_type, ctx);
    } else {
        rhs_value = expr_lowering->lower_expression(*assign.value, ctx);
    }

    // 左辺値の場所化は唯一のAPI lower_place へ委譲する（type-resolution-simplification 領域2）。
    // 従来ここに並行実装されていたbuild_projectionsは、スライス降下の修正が読み経路と共有されずW2の再発源になっていた
    auto build_lvalue_place = [&](const hir::HirExpr* expr, MirPlace& place,
                                  hir::TypePtr& current_type) -> bool {
        return expr_lowering->lower_place(expr, ctx, place, current_type);
    };

    // 左辺値の種類に応じて処理
    if (auto* var_ref = std::get_if<std::unique_ptr<hir::HirVarRef>>(&assign.target->kind)) {
        // 単純な変数代入
        auto lhs_opt = ctx.resolve_variable((*var_ref)->name);
        if (lhs_opt) {
            // ユニオン型変数への変種値の再代入はCast（ユニオン構築）を経由して
            // タグ+ペイロードを書き込む（letの初期化と同じ扱い）
            hir::TypePtr lhs_type = (*lhs_opt < ctx.func->locals.size())
                                        ? ctx.resolve_typedef(ctx.func->locals[*lhs_opt].type)
                                        : nullptr;
            hir::TypePtr rhs_type = (rhs_value < ctx.func->locals.size())
                                        ? ctx.resolve_typedef(ctx.func->locals[rhs_value].type)
                                        : nullptr;
            debug_msg("mir_union_assign",
                      "[MIR] assign lhs kind=" +
                          (lhs_type ? std::to_string(static_cast<int>(lhs_type->kind))
                                    : std::string("null")) +
                          " rhs kind=" +
                          (rhs_type ? std::to_string(static_cast<int>(rhs_type->kind))
                                    : std::string("null")));
            if (lhs_type && lhs_type->kind == hir::TypeKind::Union &&
                (!rhs_type || rhs_type->kind != hir::TypeKind::Union)) {
                ctx.push_statement(MirStatement::assign(
                    MirPlace{*lhs_opt},
                    MirRvalue::cast(MirOperand::copy(MirPlace{rhs_value}), lhs_type)));
            } else {
                // 整数右辺を浮動小数変数へ代入する場合はsitofp/uitofp相当のCastを挿入する（B2）
                rhs_value = ctx.coerce_numeric_context(rhs_value, lhs_type);
                ctx.push_statement(MirStatement::assign(
                    MirPlace{*lhs_opt}, MirRvalue::use(MirOperand::copy(MirPlace{rhs_value}))));
            }
        }
    } else if (std::get_if<std::unique_ptr<hir::HirMember>>(&assign.target->kind) ||
               std::get_if<std::unique_ptr<hir::HirIndex>>(&assign.target->kind) ||
               std::get_if<std::unique_ptr<hir::HirUnary>>(&assign.target->kind)) {
        // 複雑な左辺値: メンバーアクセス、インデックスアクセス、またはデリファレンス
        // これには c.values[0], points[0].x, arr[i], obj.field, *ptr, (*ptr).x などが含まれる
        MirPlace place{0};
        hir::TypePtr current_type;

        if (build_lvalue_place(assign.target.get(), place, current_type)) {
            // 整数右辺を浮動小数フィールド・配列要素へ代入する場合もCastを挿入する（B2）
            rhs_value = ctx.coerce_numeric_context(rhs_value, current_type);
            ctx.push_statement(
                MirStatement::assign(place, MirRvalue::use(MirOperand::copy(MirPlace{rhs_value}))));
        }
    }
    // その他の左辺値タイプは未対応
}

}  // namespace cm::mir
