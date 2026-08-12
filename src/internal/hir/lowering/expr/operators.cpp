// ============================================================
// HIR lowering - 演算子式（二項・単項・三項）
// ============================================================

#include "internal/hir/lowering/expr/internal.hpp"
#include "internal/hir/lowering/fwd.hpp"

#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace cm::hir {

// 二項演算
HirExprPtr HirLowering::lower_binary(ast::BinaryExpr& binary, TypePtr type) {
    debug::hir::log(debug::hir::Id::BinaryExprLower, "", debug::Level::Debug);

    // 複合代入演算子を脱糖
    if (is_compound_assign(binary.op)) {
        debug::hir::log(debug::hir::Id::DesugarPass, "Compound assignment", debug::Level::Trace);
        auto base_op = get_base_op(binary.op);

        auto inner = std::make_unique<HirBinary>();
        inner->op = base_op;
        debug::hir::log(debug::hir::Id::BinaryLhs, "Evaluating left for inner op",
                        debug::Level::Trace);
        inner->lhs = lower_expr(*binary.left);
        debug::hir::log(debug::hir::Id::BinaryRhs, "Evaluating right for inner op",
                        debug::Level::Trace);
        inner->rhs = lower_expr(*binary.right);

        auto outer = std::make_unique<HirBinary>();
        outer->op = HirBinaryOp::Assign;
        debug::hir::log(debug::hir::Id::BinaryLhs, "Re-evaluating left for assignment",
                        debug::Level::Trace);
        outer->lhs = lower_expr(*binary.left);
        outer->rhs = std::make_unique<HirExpr>(std::move(inner), type);

        return std::make_unique<HirExpr>(std::move(outer), type);
    }

    // 代入演算子の場合
    if (binary.op == ast::BinaryOp::Assign) {
        debug::hir::log(debug::hir::Id::AssignLower, "Assignment detected", debug::Level::Debug);

        // ビットスライスへの代入（v0.16.0）:
        // x[hi:lo] = v → x = (x & ~(mask<<lo)) | ((v & mask) << lo)
        // （read-modify-write脱糖。対象式は副作用のない左辺値であること）
        // SVターゲットはnative部分代入（x[hi:lo] = v / x[base +: w] = v）出力用のビルトイン呼び出し文へ落とす（SV-N1）
        if (auto* sl = binary.left->as<ast::SliceExpr>()) {
            ast::TypePtr sobj_type = sl->object ? sl->object->type : nullptr;
            if (is_bits_type(sobj_type) &&
                (sl->is_part_select || (sl->start && sl->end && !sl->step))) {
                if (sv_target_ && !hir_retained_context_) {
                    std::optional<int64_t> whi;
                    std::optional<int64_t> wlo;
                    std::optional<int64_t> wwidth;
                    if (sl->is_part_select) {
                        wwidth = slice_lit(sl->end);
                    } else {
                        whi = slice_lit(sl->start);
                        wlo = slice_lit(sl->end);
                    }
                    if ((whi && wlo && *whi >= *wlo) || (wwidth && *wwidth > 0)) {
                        auto hir = std::make_unique<HirCall>();
                        hir->args.push_back(lower_expr(*sl->object));
                        if (wwidth) {
                            hir->func_name = sl->part_select_down ? "__builtin_sv_part_assign_down"
                                                                  : "__builtin_sv_part_assign";
                            hir->args.push_back(lower_expr(*sl->start));
                            hir->args.push_back(make_int_lit(*wwidth, ast::make_int()));
                        } else {
                            hir->func_name = "__builtin_sv_range_assign";
                            hir->args.push_back(make_int_lit(*whi, ast::make_int()));
                            hir->args.push_back(make_int_lit(*wlo, ast::make_int()));
                        }
                        hir->args.push_back(lower_expr(*binary.right));
                        // 値を返さない代入文（SVコード生成が x[...] = v; へ写像する）
                        return std::make_unique<HirExpr>(std::move(hir), ast::make_void());
                    }
                }
                int64_t width = 0;
                HirExprPtr shift1;  // mask<<shift 用
                HirExprPtr shift2;  // (v&mask)<<shift 用
                if (sl->is_part_select) {
                    auto w = slice_lit(sl->end);
                    if (w) {
                        width = *w;
                        if (sl->part_select_down) {
                            // 下降方向: シフト量は base-(w-1)
                            auto mk_sub = [&]() {
                                auto sub = std::make_unique<HirBinary>();
                                sub->op = HirBinaryOp::Sub;
                                sub->lhs = lower_expr(*sl->start);
                                sub->rhs = make_int_lit(width - 1, ast::make_int());
                                return std::make_unique<HirExpr>(std::move(sub), ast::make_int());
                            };
                            shift1 = mk_sub();
                            shift2 = mk_sub();
                        } else {
                            shift1 = lower_expr(*sl->start);
                            shift2 = lower_expr(*sl->start);
                        }
                    }
                } else {
                    auto hi = slice_lit(sl->start);
                    auto lo = slice_lit(sl->end);
                    if (hi && lo && *hi >= *lo) {
                        width = *hi - *lo + 1;
                        shift1 = make_int_lit(*lo, ast::make_int());
                        shift2 = make_int_lit(*lo, ast::make_int());
                    }
                }
                if (width > 0 && shift1 && shift2) {
                    int64_t mask = (width >= 64) ? -1 : ((int64_t{1} << width) - 1);
                    auto mk_bin = [&](HirBinaryOp op, HirExprPtr l, HirExprPtr r, ast::TypePtr t) {
                        auto b = std::make_unique<HirBinary>();
                        b->op = op;
                        b->lhs = std::move(l);
                        b->rhs = std::move(r);
                        return std::make_unique<HirExpr>(std::move(b), std::move(t));
                    };
                    // ~(mask << lo)
                    auto mask_shifted = mk_bin(HirBinaryOp::Shl, make_int_lit(mask, sobj_type),
                                               std::move(shift1), sobj_type);
                    auto un = std::make_unique<HirUnary>();
                    un->op = HirUnaryOp::BitNot;
                    un->operand = std::move(mask_shifted);
                    auto inv_mask = std::make_unique<HirExpr>(std::move(un), sobj_type);
                    // x & ~(...)
                    auto cleared = mk_bin(HirBinaryOp::BitAnd, lower_expr(*sl->object),
                                          std::move(inv_mask), sobj_type);
                    // (v & mask) << lo
                    auto v_masked = mk_bin(HirBinaryOp::BitAnd, lower_expr(*binary.right),
                                           make_int_lit(mask, sobj_type), sobj_type);
                    auto v_shifted =
                        mk_bin(HirBinaryOp::Shl, std::move(v_masked), std::move(shift2), sobj_type);
                    // 合成して代入
                    auto merged = mk_bin(HirBinaryOp::BitOr, std::move(cleared),
                                         std::move(v_shifted), sobj_type);
                    auto assign = std::make_unique<HirBinary>();
                    assign->op = HirBinaryOp::Assign;
                    assign->lhs = lower_expr(*sl->object);
                    assign->rhs = std::move(merged);
                    return std::make_unique<HirExpr>(std::move(assign), sobj_type);
                }
            }
        }

        auto lhs_type = binary.left->type;
        auto rhs_type = binary.right->type;

        // 暗黙的構造体リテラルに左辺の型を伝播
        if (lhs_type && lhs_type->kind == ast::TypeKind::Struct) {
            if (auto* struct_lit = binary.right->as<ast::StructLiteralExpr>()) {
                if (struct_lit->type_name.empty()) {
                    struct_lit->type_name = lhs_type->name;
                    debug::hir::log(debug::hir::Id::AssignLower,
                                    "Propagated type to implicit struct literal in assignment: " +
                                        lhs_type->name,
                                    debug::Level::Debug);
                }
            }
        }

        // 配列リテラルへの型伝播
        if (lhs_type && lhs_type->kind == ast::TypeKind::Array && lhs_type->element_type) {
            if (auto* array_lit = binary.right->as<ast::ArrayLiteralExpr>()) {
                if (lhs_type->element_type->kind == ast::TypeKind::Struct) {
                    for (auto& elem : array_lit->elements) {
                        if (auto* struct_lit = elem->as<ast::StructLiteralExpr>()) {
                            if (struct_lit->type_name.empty()) {
                                struct_lit->type_name = lhs_type->element_type->name;
                            }
                        }
                    }
                }
            }
        }

        // defaultメンバへの暗黙的な代入をチェック
        if (lhs_type && lhs_type->kind == ast::TypeKind::Struct && rhs_type &&
            rhs_type->kind != ast::TypeKind::Struct) {
            std::string default_member = get_default_member_name(lhs_type->name);
            if (!default_member.empty()) {
                debug::hir::log(debug::hir::Id::AssignLower,
                                "Converting to default member assignment: " + default_member,
                                debug::Level::Debug);
                auto hir = std::make_unique<HirBinary>();
                hir->op = HirBinaryOp::Assign;

                auto member = std::make_unique<HirMember>();
                member->object = lower_expr(*binary.left);
                member->member = default_member;
                hir->lhs = std::make_unique<HirExpr>(std::move(member), rhs_type);

                hir->rhs = lower_expr(*binary.right);
                return std::make_unique<HirExpr>(std::move(hir), type);
            }
        }
    }

    // 配列/スライスの比較演算
    if (binary.op == ast::BinaryOp::Eq || binary.op == ast::BinaryOp::Ne) {
        auto lhs_type = binary.left->type;
        auto rhs_type = binary.right->type;

        bool lhs_is_array = lhs_type && lhs_type->kind == ast::TypeKind::Array;
        bool rhs_is_array = rhs_type && rhs_type->kind == ast::TypeKind::Array;

        if (lhs_is_array && rhs_is_array) {
            debug::hir::log(debug::hir::Id::BinaryExprLower, "Array/slice comparison",
                            debug::Level::Debug);

            // cm_array_equal(lhs, rhs, lhs_len, rhs_len, elem_size)を呼び出す
            auto hir = std::make_unique<HirCall>();

            bool lhs_dynamic = !lhs_type->array_size.has_value();
            bool rhs_dynamic = !rhs_type->array_size.has_value();

            // 動的スライス同士の比較
            if (lhs_dynamic && rhs_dynamic) {
                hir->func_name = "cm_slice_equal";
                hir->args.push_back(lower_expr(*binary.left));
                hir->args.push_back(lower_expr(*binary.right));
            } else {
                // 固定配列を含む比較
                hir->func_name = "cm_array_equal";
                hir->args.push_back(lower_expr(*binary.left));
                hir->args.push_back(lower_expr(*binary.right));

                // 配列の長さ
                int64_t lhs_len = lhs_type->array_size.value_or(0);
                int64_t rhs_len = rhs_type->array_size.value_or(0);

                auto lhs_len_lit = std::make_unique<HirLiteral>();
                lhs_len_lit->value = lhs_len;
                hir->args.push_back(
                    std::make_unique<HirExpr>(std::move(lhs_len_lit), ast::make_long()));

                auto rhs_len_lit = std::make_unique<HirLiteral>();
                rhs_len_lit->value = rhs_len;
                hir->args.push_back(
                    std::make_unique<HirExpr>(std::move(rhs_len_lit), ast::make_long()));

                // 要素サイズ
                int64_t elem_size = 8;  // デフォルト
                if (lhs_type->element_type) {
                    switch (lhs_type->element_type->kind) {
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
                        default:
                            elem_size = 8;
                            break;
                    }
                }

                auto elem_size_lit = std::make_unique<HirLiteral>();
                elem_size_lit->value = elem_size;
                hir->args.push_back(
                    std::make_unique<HirExpr>(std::move(elem_size_lit), ast::make_long()));
            }

            // != の場合は結果を反転
            if (binary.op == ast::BinaryOp::Ne) {
                auto call_expr = std::make_unique<HirExpr>(std::move(hir), ast::make_bool());
                auto not_op = std::make_unique<HirUnary>();
                not_op->op = HirUnaryOp::Not;
                not_op->operand = std::move(call_expr);
                return std::make_unique<HirExpr>(std::move(not_op), type);
            }

            return std::make_unique<HirExpr>(std::move(hir), type);
        }
    }

    // enum比較：enum変数とenumバリアント参照の比較を検出
    // s == Option::Some のような比較で、sのタグ値を抽出して比較
    if (binary.op == ast::BinaryOp::Eq || binary.op == ast::BinaryOp::Ne) {
        // 右辺がenum参照（例：Option::Some）かチェック
        std::string rhs_enum_name;
        bool rhs_is_enum_tag = false;
        int64_t rhs_tag_value = 0;
        if (auto* rhs_ident = binary.right->as<ast::IdentExpr>()) {
            auto enum_it = enum_values_.find(rhs_ident->name);
            if (enum_it != enum_values_.end()) {
                rhs_is_enum_tag = true;
                rhs_tag_value = enum_it->second;
                // enum名を抽出（例：Option::Some -> Option）
                auto sep = rhs_ident->name.find("::");
                if (sep != std::string::npos) {
                    rhs_enum_name = rhs_ident->name.substr(0, sep);
                }
            }
        }

        // 左辺がenum変数参照（IdentExpr）で、右辺がそのenumのバリアントの場合
        // -> 左辺のタグ（field[0]）を抽出して比較
        if (rhs_is_enum_tag && !rhs_enum_name.empty()) {
            if (auto* lhs_ident = binary.left->as<ast::IdentExpr>()) {
                // 左辺がenum_values_に登録されたenum参照ではなく、通常の変数であることを確認
                if (enum_values_.find(lhs_ident->name) == enum_values_.end()) {
                    debug::hir::log(debug::hir::Id::BinaryExprLower,
                                    "Enum comparison: extracting tag from variable",
                                    debug::Level::Debug);
                    // タグ抽出: lhs.__tag == rhs_tag_value
                    auto member = std::make_unique<HirMember>();
                    member->object = lower_expr(*binary.left);
                    member->member = "__tag";  // Tagged Unionのタグフィールド

                    // タグ値を直接intリテラルとして生成
                    // lower_expr(binary.right)を使うとTagged Union型のHirEnumConstructが返されるため、int比較に使用不可
                    auto tag_lit = std::make_unique<HirLiteral>();
                    tag_lit->value = rhs_tag_value;

                    auto hir = std::make_unique<HirBinary>();
                    hir->op = (binary.op == ast::BinaryOp::Eq) ? HirBinaryOp::Eq : HirBinaryOp::Ne;
                    hir->lhs = std::make_unique<HirExpr>(std::move(member), ast::make_int());
                    hir->rhs = std::make_unique<HirExpr>(std::move(tag_lit), ast::make_int());
                    return std::make_unique<HirExpr>(std::move(hir), type);
                }
            }
        }

        // 逆順: 左辺がenum参照、右辺がenum変数
        std::string lhs_enum_name;
        bool lhs_is_enum_tag = false;
        int64_t lhs_tag_value = 0;
        if (auto* lhs_ident = binary.left->as<ast::IdentExpr>()) {
            auto enum_it = enum_values_.find(lhs_ident->name);
            if (enum_it != enum_values_.end()) {
                lhs_is_enum_tag = true;
                lhs_tag_value = enum_it->second;
                auto sep = lhs_ident->name.find("::");
                if (sep != std::string::npos) {
                    lhs_enum_name = lhs_ident->name.substr(0, sep);
                }
            }
        }

        if (lhs_is_enum_tag && !lhs_enum_name.empty()) {
            if (auto* rhs_ident = binary.right->as<ast::IdentExpr>()) {
                if (enum_values_.find(rhs_ident->name) == enum_values_.end()) {
                    debug::hir::log(debug::hir::Id::BinaryExprLower,
                                    "Enum comparison (reversed): extracting tag from variable",
                                    debug::Level::Debug);
                    auto member = std::make_unique<HirMember>();
                    member->object = lower_expr(*binary.right);
                    member->member = "__tag";  // Tagged Unionのタグフィールド

                    // タグ値を直接intリテラルとして生成
                    auto tag_lit = std::make_unique<HirLiteral>();
                    tag_lit->value = lhs_tag_value;

                    auto hir = std::make_unique<HirBinary>();
                    hir->op = (binary.op == ast::BinaryOp::Eq) ? HirBinaryOp::Eq : HirBinaryOp::Ne;
                    hir->lhs = std::make_unique<HirExpr>(std::move(tag_lit), ast::make_int());
                    hir->rhs = std::make_unique<HirExpr>(std::move(member), ast::make_int());
                    return std::make_unique<HirExpr>(std::move(hir), type);
                }
            }
        }
    }

    // 通常の二項演算子処理
    auto hir = std::make_unique<HirBinary>();
    hir->op = convert_binary_op(binary.op);
    debug::hir::log(debug::hir::Id::BinaryOp, hir_binary_op_to_string(hir->op),
                    debug::Level::Trace);

    debug::hir::log(debug::hir::Id::BinaryLhs, "Evaluating left operand", debug::Level::Trace);
    hir->lhs = lower_expr(*binary.left);
    debug::hir::log(debug::hir::Id::BinaryRhs, "Evaluating right operand", debug::Level::Trace);
    hir->rhs = lower_expr(*binary.right);
    return std::make_unique<HirExpr>(std::move(hir), type);
}

// 単項演算
HirExprPtr HirLowering::lower_unary(ast::UnaryExpr& unary, TypePtr type) {
    debug::hir::log(debug::hir::Id::UnaryExprLower, "", debug::Level::Debug);
    auto hir = std::make_unique<HirUnary>();
    hir->op = convert_unary_op(unary.op);
    debug::hir::log(debug::hir::Id::UnaryOp, hir_unary_op_to_string(hir->op), debug::Level::Trace);

    debug::hir::log(debug::hir::Id::UnaryOperand, "Evaluating operand", debug::Level::Trace);
    hir->operand = lower_expr(*unary.operand);
    return std::make_unique<HirExpr>(std::move(hir), type);
}

// 三項演算子
HirExprPtr HirLowering::lower_ternary(ast::TernaryExpr& tern, TypePtr type) {
    auto hir = std::make_unique<HirTernary>();
    hir->condition = lower_expr(*tern.condition);
    hir->then_expr = lower_expr(*tern.then_expr);
    hir->else_expr = lower_expr(*tern.else_expr);
    return std::make_unique<HirExpr>(std::move(hir), type);
}

}  // namespace cm::hir
