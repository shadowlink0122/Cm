// ============================================================
// HIR lowering - 要素アクセス式（インデックス・スライス）
// ============================================================

#include "internal/hir/lowering/expr/internal.hpp"
#include "internal/hir/lowering/fwd.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cm::hir {

// 配列アクセス
HirExprPtr HirLowering::lower_index(ast::IndexExpr& idx, TypePtr type) {
    debug::hir::log(debug::hir::Id::IndexLower, "", debug::Level::Debug);

    // 多次元配列最適化: 連鎖するIndexExprを検出して単一のHirIndexに統合
    // a[i][j][k] → HirIndex { object: a, indices: [i, j, k] }
    // これにより一時変数の生成を回避し、LLVMのベクトル化が可能になる

    std::vector<ast::Expr*> index_chain;
    ast::Expr* base_obj = idx.object.get();

    // 現在の IndexExpr のインデックスを追加
    index_chain.push_back(idx.index.get());

    // IndexExprの連鎖を逆順に収集（object側に辿る）
    while (auto* inner_idx = base_obj->as<ast::IndexExpr>()) {
        index_chain.push_back(inner_idx->index.get());
        base_obj = inner_idx->object.get();
    }

    // チェーンを正順に戻す（a[i][j] では i が先）
    std::reverse(index_chain.begin(), index_chain.end());

    auto obj_hir = lower_expr(*base_obj);
    TypePtr obj_type = obj_hir->type;

    // 文字列インデックスの場合（連鎖は想定しない）
    if (obj_type && obj_type->kind == ast::TypeKind::String && index_chain.size() == 1) {
        debug::hir::log(debug::hir::Id::IndexLower, "String index access", debug::Level::Debug);
        auto hir = std::make_unique<HirCall>();
        hir->func_name = "__builtin_string_charAt";
        hir->args.push_back(std::move(obj_hir));
        hir->args.push_back(lower_expr(*index_chain[0]));
        return std::make_unique<HirExpr>(std::move(hir), ast::make_char());
    }

    // 配列/ポインタインデックス
    auto hir = std::make_unique<HirIndex>();
    debug::hir::log(debug::hir::Id::IndexBase, "Evaluating base", debug::Level::Trace);
    hir->object = std::move(obj_hir);

    if (index_chain.size() == 1) {
        // 単一インデックス（後方互換性）
        debug::hir::log(debug::hir::Id::IndexValue, "Single index", debug::Level::Trace);
        hir->index = lower_expr(*index_chain[0]);
    } else {
        // 多次元配列: 全インデックスを収集
        debug::hir::log(debug::hir::Id::IndexValue,
                        "Multi-dim index: " + std::to_string(index_chain.size()) + " indices",
                        debug::Level::Trace);
        for (auto* idx_expr : index_chain) {
            hir->indices.push_back(lower_expr(*idx_expr));
        }
    }
    // 型注釈が無い場合（型チェッカーを通らない文字列補間ミニパイプライン等）は
    // 基底の型から要素型を導出する。これをしないと arr[0].method() の受信型が
    // <error> になり、__error__method という未定義シンボルへ黙って解決されていた
    if (!type || type->is_error()) {
        TypePtr derived = hir->object ? hir->object->type : nullptr;
        for (size_t i = 0; i < index_chain.size() && derived; ++i) {
            derived = derived->element_type;
        }
        if (derived) {
            type = derived;
        }
    }
    return std::make_unique<HirExpr>(std::move(hir), type);
}

// スライス式
HirExprPtr HirLowering::lower_slice(ast::SliceExpr& slice, TypePtr type) {
    debug::hir::log(debug::hir::Id::IndexLower, "Slice expression", debug::Level::Debug);

    auto obj_hir = lower_expr(*slice.object);
    TypePtr obj_type = obj_hir->type;
    if (!obj_type && slice.object) {
        obj_type = slice.object->type;
    }
    // ビットスライスの読み取り（v0.16.0）:
    // x[hi:lo] → (x >> lo) & ((1<<w)-1) / x[base +: w] → (x >> base) & ((1<<w)-1) /
    // x[base -: w] → (x >> (base-(w-1))) & ((1<<w)-1)
    // 非SVはシフト+マスク脱糖、SVターゲットはnative part-select出力用のビルトイン呼び出しへ落とす（SV-N1）
    if (is_bits_type(obj_type) &&
        (slice.is_part_select || (slice.start && slice.end && !slice.step))) {
        int64_t width = 0;
        HirExprPtr shift_amount;
        std::optional<int64_t> const_hi;
        std::optional<int64_t> const_lo;
        if (slice.is_part_select) {
            auto w = slice_lit(slice.end);
            if (w) {
                width = *w;
                if (slice.part_select_down) {
                    // 下降方向: 選択範囲は [base : base-w+1] のためシフト量は base-(w-1)
                    auto sub = std::make_unique<HirBinary>();
                    sub->op = HirBinaryOp::Sub;
                    sub->lhs = lower_expr(*slice.start);
                    sub->rhs = make_int_lit(width - 1, ast::make_int());
                    shift_amount = std::make_unique<HirExpr>(std::move(sub), ast::make_int());
                } else {
                    shift_amount = lower_expr(*slice.start);
                }
            }
        } else {
            auto hi = slice_lit(slice.start);
            auto lo = slice_lit(slice.end);
            if (hi && lo && *hi >= *lo) {
                width = *hi - *lo + 1;
                const_hi = hi;
                const_lo = lo;
                shift_amount = make_int_lit(*lo, ast::make_int());
            }
        }
        if (width > 0 && shift_amount) {
            ast::TypePtr result_type =
                ast::make_array(ast::make_bit(), static_cast<uint32_t>(width));
            // SVターゲット: native part-select（x[hi:lo]・x[base +: w]・x[base -: w]）を
            // 出力するためのビルトイン呼び出しへ落とす（テストベンチ等のHIR直接消費文脈は除く）
            if (sv_target_ && !hir_retained_context_) {
                auto hir = std::make_unique<HirCall>();
                if (const_hi && const_lo) {
                    hir->func_name = "__builtin_sv_range_select";
                    hir->args.push_back(std::move(obj_hir));
                    hir->args.push_back(make_int_lit(*const_hi, ast::make_int()));
                    hir->args.push_back(make_int_lit(*const_lo, ast::make_int()));
                } else {
                    hir->func_name = slice.part_select_down ? "__builtin_sv_part_select_down"
                                                            : "__builtin_sv_part_select";
                    hir->args.push_back(std::move(obj_hir));
                    // 基点はソースの式そのもの（下降方向の -(w-1) 補正はSV側の -: が担う）
                    hir->args.push_back(lower_expr(*slice.start));
                    hir->args.push_back(make_int_lit(width, ast::make_int()));
                }
                return std::make_unique<HirExpr>(std::move(hir), result_type);
            }
            int64_t mask = (width >= 64) ? -1 : ((int64_t{1} << width) - 1);
            // (obj >> shift)
            auto shr = std::make_unique<HirBinary>();
            shr->op = HirBinaryOp::Shr;
            shr->lhs = std::move(obj_hir);
            shr->rhs = std::move(shift_amount);
            auto shr_expr = std::make_unique<HirExpr>(std::move(shr), obj_type);
            // ... & mask
            auto band = std::make_unique<HirBinary>();
            band->op = HirBinaryOp::BitAnd;
            band->lhs = std::move(shr_expr);
            band->rhs = make_int_lit(mask, obj_type);
            return std::make_unique<HirExpr>(std::move(band), result_type);
        }
    }

    // 文字列スライス
    if (obj_type && obj_type->kind == ast::TypeKind::String) {
        auto hir = std::make_unique<HirCall>();
        hir->func_name = "__builtin_string_substring";
        hir->args.push_back(std::move(obj_hir));

        if (slice.start) {
            hir->args.push_back(lower_expr(*slice.start));
        } else {
            auto zero = std::make_unique<HirLiteral>();
            zero->value = int64_t{0};
            hir->args.push_back(std::make_unique<HirExpr>(std::move(zero), ast::make_long()));
        }

        if (slice.end) {
            hir->args.push_back(lower_expr(*slice.end));
        } else {
            auto neg_one = std::make_unique<HirLiteral>();
            neg_one->value = int64_t{-1};
            hir->args.push_back(std::make_unique<HirExpr>(std::move(neg_one), ast::make_long()));
        }

        if (slice.step) {
            debug::hir::log(debug::hir::Id::Warning, "String slice step not yet supported",
                            debug::Level::Warn);
        }

        return std::make_unique<HirExpr>(std::move(hir), ast::make_string());
    }

    // 配列スライス
    if (obj_type && obj_type->kind == ast::TypeKind::Array) {
        bool is_dynamic_slice = !obj_type->array_size.has_value();

        if (is_dynamic_slice) {
            // 動的スライスの場合は専用関数を使用
            debug::hir::log(debug::hir::Id::IndexLower, "Dynamic slice subslice",
                            debug::Level::Debug);
            auto hir = std::make_unique<HirCall>();
            hir->func_name = "cm_slice_subslice";
            hir->args.push_back(std::move(obj_hir));

            if (slice.start) {
                hir->args.push_back(lower_expr(*slice.start));
            } else {
                auto zero = std::make_unique<HirLiteral>();
                zero->value = int64_t{0};
                hir->args.push_back(std::make_unique<HirExpr>(std::move(zero), ast::make_long()));
            }

            if (slice.end) {
                hir->args.push_back(lower_expr(*slice.end));
            } else {
                // endが省略された場合は-1を渡して関数内で処理
                auto neg_one = std::make_unique<HirLiteral>();
                neg_one->value = int64_t{-1};
                hir->args.push_back(
                    std::make_unique<HirExpr>(std::move(neg_one), ast::make_long()));
            }

            return std::make_unique<HirExpr>(std::move(hir), type);
        }

        // 固定配列の場合
        debug::hir::log(debug::hir::Id::IndexLower, "Array slice", debug::Level::Debug);
        auto hir = std::make_unique<HirCall>();
        hir->func_name = "__builtin_array_slice";

        hir->args.push_back(std::move(obj_hir));

        int64_t elem_size = 8;
        if (obj_type->element_type) {
            switch (obj_type->element_type->kind) {
                case ast::TypeKind::Tiny:
                case ast::TypeKind::UTiny:
                case ast::TypeKind::Char:
                case ast::TypeKind::Bool:
                    elem_size = 1;
                    break;
                case ast::TypeKind::Short:
                case ast::TypeKind::UShort:
                    elem_size = 2;
                    break;
                case ast::TypeKind::Int:
                case ast::TypeKind::UInt:
                case ast::TypeKind::Float:
                    elem_size = 4;
                    break;
                case ast::TypeKind::Long:
                case ast::TypeKind::ULong:
                case ast::TypeKind::Double:
                case ast::TypeKind::Pointer:
                    elem_size = 8;
                    break;
                default:
                    elem_size = 8;
                    break;
            }
        }
        auto elem_size_lit = std::make_unique<HirLiteral>();
        elem_size_lit->value = elem_size;
        hir->args.push_back(std::make_unique<HirExpr>(std::move(elem_size_lit), ast::make_int()));

        int64_t arr_len = obj_type->array_size.value_or(0);
        auto arr_len_lit = std::make_unique<HirLiteral>();
        arr_len_lit->value = arr_len;
        hir->args.push_back(std::make_unique<HirExpr>(std::move(arr_len_lit), ast::make_int()));

        if (slice.start) {
            hir->args.push_back(lower_expr(*slice.start));
        } else {
            auto zero = std::make_unique<HirLiteral>();
            zero->value = int64_t{0};
            hir->args.push_back(std::make_unique<HirExpr>(std::move(zero), ast::make_int()));
        }

        if (slice.end) {
            hir->args.push_back(lower_expr(*slice.end));
        } else {
            // endが省略された場合は配列の長さを使用
            auto arr_len_end = std::make_unique<HirLiteral>();
            arr_len_end->value = arr_len;
            hir->args.push_back(std::make_unique<HirExpr>(std::move(arr_len_end), ast::make_int()));
        }

        if (slice.step) {
            debug::hir::log(debug::hir::Id::Warning, "Array slice step not yet supported",
                            debug::Level::Warn);
        }

        return std::make_unique<HirExpr>(std::move(hir), type);
    }

    debug::hir::log(debug::hir::Id::Warning, "Slice on unsupported type", debug::Level::Warn);

    auto lit = std::make_unique<HirLiteral>();
    return std::make_unique<HirExpr>(std::move(lit), type);
}

}  // namespace cm::hir
