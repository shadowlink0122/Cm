// lowering_expr.cpp - 式のlowering
#include "expr_internal.hpp"
#include "fwd.hpp"

#include <algorithm>   // std::reverse用
#include <functional>  // sizeof複合ジェネリック判定の再帰ラムダ用
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace cm::hir {

namespace {

// 整数リテラル値の取り出し（checkerで検証済みの前提）
std::optional<int64_t> slice_lit(const ast::ExprPtr& e) {
    if (!e) {
        return std::nullopt;
    }
    if (auto* lit = e->as<ast::LiteralExpr>()) {
        if (auto* iv = std::get_if<int64_t>(&lit->value)) {
            return *iv;
        }
    }
    return std::nullopt;
}

}  // namespace

// 式の変換
HirExprPtr HirLowering::lower_expr(ast::Expr& expr) {
    debug::hir::log(debug::hir::Id::ExprLower, "", debug::Level::Trace);
    TypePtr type = expr.type ? expr.type : make_error();

    if (type && type->kind != ast::TypeKind::Error) {
        debug::hir::log(debug::hir::Id::ExprType, type_to_string(*type), debug::Level::Trace);
    }

    if (auto* lit = expr.as<ast::LiteralExpr>()) {
        return lower_literal(*lit, type);
    } else if (auto* ident = expr.as<ast::IdentExpr>()) {
        debug::hir::log(debug::hir::Id::IdentifierLower, ident->name, debug::Level::Debug);

        // enum値アクセスかチェック
        auto it = enum_values_.find(ident->name);
        if (it != enum_values_.end()) {
            debug::hir::log(debug::hir::Id::IdentifierRef,
                            "enum value: " + ident->name + " = " + std::to_string(it->second),
                            debug::Level::Debug);

            // Tagged Union enumの場合はHirEnumConstructを生成
            // これにより、Option::Noneのようなペイロードなしバリアントも__TaggedUnion_型で正しく初期化される（tag + payload）
            std::string enum_name;
            std::string variant_name;
            auto sep = ident->name.find("::");
            if (sep != std::string::npos) {
                enum_name = ident->name.substr(0, sep);
                variant_name = ident->name.substr(sep + 2);
            }

            bool is_tagged = false;
            if (!enum_name.empty()) {
                auto def_it = enum_defs_.find(enum_name);
                if (def_it != enum_defs_.end() && def_it->second) {
                    is_tagged = def_it->second->is_tagged_union();
                }
            }

            if (is_tagged) {
                // Tagged Union: HirEnumConstructを生成（ペイロードなし）
                auto enum_construct = std::make_unique<HirEnumConstruct>();
                enum_construct->enum_name = enum_name;
                enum_construct->variant_name = variant_name;
                enum_construct->tag_value = it->second;
                // ペイロードなし（payload = nullptr）

                auto tagged_union_type = std::make_shared<ast::Type>(ast::TypeKind::Struct);
                tagged_union_type->name = "__TaggedUnion_" + enum_name;

                return std::make_unique<HirExpr>(std::move(enum_construct), tagged_union_type);
            }

            // 通常のenum（Tagged Unionでない）はintリテラルとして処理
            auto lit = std::make_unique<HirLiteral>();
            lit->value = it->second;
            return std::make_unique<HirExpr>(std::move(lit), ast::make_int());
        }

        // v0.13.0: int型マクロ定数アクセスかチェック
        auto macro_it = macro_values_.find(ident->name);
        if (macro_it != macro_values_.end()) {
            debug::hir::log(debug::hir::Id::IdentifierRef,
                            "macro int: " + ident->name + " = " + std::to_string(macro_it->second),
                            debug::Level::Debug);
            auto lit = std::make_unique<HirLiteral>();
            lit->value = macro_it->second;
            return std::make_unique<HirExpr>(std::move(lit), ast::make_int());
        }

        // v0.13.0: string型マクロ定数アクセスかチェック
        auto macro_str_it = macro_string_values_.find(ident->name);
        if (macro_str_it != macro_string_values_.end()) {
            debug::hir::log(debug::hir::Id::IdentifierRef,
                            "macro string: " + ident->name + " = \"" + macro_str_it->second + "\"",
                            debug::Level::Debug);
            auto lit = std::make_unique<HirLiteral>();
            lit->value = macro_str_it->second;
            return std::make_unique<HirExpr>(std::move(lit), ast::make_string());
        }

        // v0.13.0: bool型マクロ定数アクセスかチェック
        auto macro_bool_it = macro_bool_values_.find(ident->name);
        if (macro_bool_it != macro_bool_values_.end()) {
            debug::hir::log(
                debug::hir::Id::IdentifierRef,
                "macro bool: " + ident->name + " = " + (macro_bool_it->second ? "true" : "false"),
                debug::Level::Debug);
            auto lit = std::make_unique<HirLiteral>();
            lit->value = macro_bool_it->second;
            return std::make_unique<HirExpr>(std::move(lit), ast::make_bool());
        }

        debug::hir::log(debug::hir::Id::IdentifierRef, "variable: " + ident->name,
                        debug::Level::Trace);
        auto var_ref = std::make_unique<HirVarRef>();
        var_ref->name = ident->name;
        if (func_defs_.find(ident->name) != func_defs_.end()) {
            var_ref->is_function_ref = true;
            debug::hir::log(debug::hir::Id::IdentifierRef, "function reference: " + ident->name,
                            debug::Level::Debug);
        }
        return std::make_unique<HirExpr>(std::move(var_ref), type);
    } else if (auto* binary = expr.as<ast::BinaryExpr>()) {
        return lower_binary(*binary, type);
    } else if (auto* unary = expr.as<ast::UnaryExpr>()) {
        return lower_unary(*unary, type);
    } else if (auto* call = expr.as<ast::CallExpr>()) {
        return lower_call(*call, type);
    } else if (auto* idx = expr.as<ast::IndexExpr>()) {
        return lower_index(*idx, type);
    } else if (auto* slice = expr.as<ast::SliceExpr>()) {
        return lower_slice(*slice, type);
    } else if (auto* mem = expr.as<ast::MemberExpr>()) {
        return lower_member(*mem, type);
    } else if (auto* tern = expr.as<ast::TernaryExpr>()) {
        return lower_ternary(*tern, type);
    } else if (auto* match_expr = expr.as<ast::MatchExpr>()) {
        return lower_match(*match_expr, type);
    } else if (auto* struct_lit = expr.as<ast::StructLiteralExpr>()) {
        return lower_struct_literal(*struct_lit, type);
    } else if (auto* array_lit = expr.as<ast::ArrayLiteralExpr>()) {
        return lower_array_literal(*array_lit, type);
    } else if (auto* lambda_expr = expr.as<ast::LambdaExpr>()) {
        return lower_lambda(*lambda_expr, type);
    } else if (auto* sizeof_expr = expr.as<ast::SizeofExpr>()) {
        // sizeof(T) または sizeof(expr) をコンパイル時定数として評価
        int64_t size = 0;
        std::string type_name;
        ast::TypePtr target_type_ptr;
        if (sizeof_expr->target_type) {
            size = calculate_type_size(sizeof_expr->target_type);
            type_name = ast::type_to_string(*sizeof_expr->target_type);
            target_type_ptr = sizeof_expr->target_type;
        } else if (sizeof_expr->target_expr && sizeof_expr->target_expr->type) {
            size = calculate_type_size(sizeof_expr->target_expr->type);
            type_name = ast::type_to_string(*sizeof_expr->target_expr->type);
            target_type_ptr = sizeof_expr->target_expr->type;
        }
        debug::hir::log(debug::hir::Id::LiteralLower,
                        "sizeof(" + type_name + ") = " + std::to_string(size), debug::Level::Debug);
        auto lit = std::make_unique<HirLiteral>();
        lit->value = size;

        // ジェネリック型パラメータ（T, U, V, K, E等）を検出
        // 型名が単一の大文字、または大文字＋数字（T1, T2等）の場合
        // マーカー型情報を持つuintとして返す（モノモーフィゼーション時に再計算）
        bool is_generic_param = false;
        if (target_type_ptr) {
            const std::string& tname = target_type_ptr->name;
            // TypeKind::Generic または TypeKind::Struct でも
            // 型名が単一大文字または大文字+数字の場合はジェネリックパラメータとみなす
            if (target_type_ptr->kind == ast::TypeKind::Generic ||
                target_type_ptr->kind == ast::TypeKind::Struct) {
                if (tname.length() == 1 && std::isupper(tname[0])) {
                    is_generic_param = true;
                } else if (tname.length() == 2 && std::isupper(tname[0]) &&
                           std::isdigit(tname[1])) {
                    is_generic_param = true;
                }
            }
        }

        if (is_generic_param) {
            // ジェネリック型パラメータのsizeofの場合
            // 特別なマーカー型を作成（sizeof_for_T形式）
            // モノモーフィゼーション時にこの型を検出して正しいサイズを計算
            auto marker_type = std::make_shared<ast::Type>(ast::TypeKind::Generic);
            marker_type->name = "sizeof_for_" + target_type_ptr->name;
            debug::hir::log(debug::hir::Id::LiteralLower,
                            "sizeof for generic param: " + target_type_ptr->name +
                                " (marker: " + marker_type->name + ")",
                            debug::Level::Debug);
            return std::make_unique<HirExpr>(std::move(lit), marker_type);
        }

        // 型引数にジェネリックパラメータを含む複合型（QueueNode<T> 等）もマーカー化してモノモーフィゼーション時に実サイズへ置換する。
        // HIR時点の推定サイズはT=longや構造体Tで実レイアウトを下回り、mallocの下回り確保による隣接ヒープ破壊の原因になっていた
        if (target_type_ptr && !target_type_ptr->type_args.empty()) {
            std::function<bool(const ast::TypePtr&)> has_generic_param =
                [&](const ast::TypePtr& t) -> bool {
                if (!t) {
                    return false;
                }
                const std::string& n = t->name;
                bool param_like =
                    (n.length() == 1 && std::isupper(static_cast<unsigned char>(n[0]))) ||
                    (n.length() == 2 && std::isupper(static_cast<unsigned char>(n[0])) &&
                     std::isdigit(static_cast<unsigned char>(n[1])));
                if ((t->kind == ast::TypeKind::Generic || t->kind == ast::TypeKind::Struct) &&
                    param_like) {
                    return true;
                }
                for (const auto& a : t->type_args) {
                    if (has_generic_param(a)) {
                        return true;
                    }
                }
                if (t->element_type) {
                    return has_generic_param(t->element_type);
                }
                return false;
            };
            if (has_generic_param(target_type_ptr)) {
                auto marker_type = std::make_shared<ast::Type>(ast::TypeKind::Generic);
                std::string composite = target_type_ptr->name + "<";
                for (size_t i = 0; i < target_type_ptr->type_args.size(); ++i) {
                    if (i > 0) {
                        composite += ", ";
                    }
                    const auto& a = target_type_ptr->type_args[i];
                    composite +=
                        (a && !a->name.empty()) ? a->name : (a ? ast::type_to_string(*a) : "?");
                }
                composite += ">";
                marker_type->name = "sizeof_for_" + composite;
                debug::hir::log(debug::hir::Id::LiteralLower,
                                "sizeof for generic composite: " + composite +
                                    " (marker: " + marker_type->name + ")",
                                debug::Level::Debug);
                return std::make_unique<HirExpr>(std::move(lit), marker_type);
            }
        }

        return std::make_unique<HirExpr>(std::move(lit), ast::make_uint());
    } else if (auto* typeof_expr = expr.as<ast::TypeofExpr>()) {
        // typeof(expr) - 式の型を返すが、値としては0を返す（型コンテキストで使用）
        // 式として評価される場合はエラーとして扱う
        (void)typeof_expr;  // 未使用警告を抑制
        debug::hir::log(debug::hir::Id::Warning, "typeof expression used in value context",
                        debug::Level::Warn);
        auto lit = std::make_unique<HirLiteral>();
        lit->value = static_cast<int64_t>(0);
        return std::make_unique<HirExpr>(std::move(lit), ast::make_error());
    } else if (auto* alignof_expr = expr.as<ast::AlignofExpr>()) {
        // alignof(T) - 型のアラインメントをコンパイル時定数として評価
        int64_t alignment = 0;
        std::string type_name;
        if (alignof_expr->target_type) {
            alignment = calculate_type_align(alignof_expr->target_type);
            type_name = ast::type_to_string(*alignof_expr->target_type);
        }
        debug::hir::log(debug::hir::Id::LiteralLower,
                        "alignof(" + type_name + ") = " + std::to_string(alignment),
                        debug::Level::Debug);
        auto lit = std::make_unique<HirLiteral>();
        lit->value = alignment;
        return std::make_unique<HirExpr>(std::move(lit), ast::make_uint());
    } else if (auto* typename_expr = expr.as<ast::TypenameOfExpr>()) {
        // typename(T) または typename(expr) - 型の名前を文字列として返す
        std::string type_name;
        if (typename_expr->target_type) {
            // 型が直接指定された場合
            type_name = ast::type_to_string(*typename_expr->target_type);
        } else if (typename_expr->target_expr) {
            // 式が指定された場合、式の型を取得
            auto lowered = lower_expr(*typename_expr->target_expr);
            if (lowered && lowered->type) {
                type_name = ast::type_to_string(*lowered->type);
            } else {
                type_name = "<unknown>";
            }
        }
        debug::hir::log(debug::hir::Id::LiteralLower, "typename = \"" + type_name + "\"",
                        debug::Level::Debug);
        auto lit = std::make_unique<HirLiteral>();
        lit->value = type_name;
        return std::make_unique<HirExpr>(std::move(lit), ast::make_string());
    } else if (auto* cast_expr = expr.as<ast::CastExpr>()) {
        // キャスト式: expr as Type
        debug::hir::log(debug::hir::Id::CastExprLower, "Lowering cast expression",
                        debug::Level::Debug);
        auto operand = lower_expr(*cast_expr->operand);
        auto hir_cast = std::make_unique<HirCast>();
        hir_cast->operand = std::move(operand);
        hir_cast->target_type = cast_expr->target_type;
        // is（型判別）はboolを返す
        hir_cast->check_only = cast_expr->type_check;
        auto result_type = cast_expr->type_check ? ast::make_bool() : cast_expr->target_type;
        return std::make_unique<HirExpr>(std::move(hir_cast), result_type);
    } else if (auto* move_expr = expr.as<ast::MoveExpr>()) {
        // move式: move x は x そのものを返す（所有権移動、ゼロコスト）
        debug::hir::log(debug::hir::Id::ExprLower, "Lowering move expression", debug::Level::Debug);
        // moveは単にオペランドを返す - 所有権追跡は型チェッカーで行われている
        auto moved = lower_expr(*move_expr->operand);
        // 変数参照のmoveはMIRへ伝搬し、moved-out変数のデストラクタ登録を解除させる
        // （これが無いと move で所有権を渡した変数がスコープ終了時にも解放され二重解放になる）
        if (moved) {
            if (auto* var_ref = std::get_if<std::unique_ptr<HirVarRef>>(&moved->kind)) {
                if (*var_ref)
                    (*var_ref)->is_moved_from = true;
            }
        }
        return moved;
    } else if (auto* await_expr = expr.as<ast::AwaitExpr>()) {
        // await式: オペランドを評価し、is_awaitedフラグを設定
        debug::hir::log(debug::hir::Id::ExprLower, "Lowering await expression",
                        debug::Level::Debug);
        auto hir_operand = lower_expr(*await_expr->operand);

        // オペランドがHirCallの場合、is_awaitedをtrueに設定
        if (auto* hir_call = std::get_if<std::unique_ptr<HirCall>>(&hir_operand->kind)) {
            (*hir_call)->is_awaited = true;
        }

        return hir_operand;
    }

    debug::hir::log(debug::hir::Id::Warning, "Unknown expression type, using null literal",
                    debug::Level::Warn);
    auto lit = std::make_unique<HirLiteral>();
    return std::make_unique<HirExpr>(std::move(lit), type);
}

// リテラル
HirExprPtr HirLowering::lower_literal(ast::LiteralExpr& lit, TypePtr type) {
    debug::hir::log(debug::hir::Id::LiteralLower, "", debug::Level::Trace);

    std::visit(
        [](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, int64_t>) {
                debug::hir::log(debug::hir::Id::IntLiteral, std::to_string(arg),
                                debug::Level::Trace);
            } else if constexpr (std::is_same_v<T, double>) {
                debug::hir::log(debug::hir::Id::FloatLiteral, std::to_string(arg),
                                debug::Level::Trace);
            } else if constexpr (std::is_same_v<T, std::string>) {
                debug::hir::log(debug::hir::Id::StringLiteral, "\"" + arg + "\"",
                                debug::Level::Trace);
            } else if constexpr (std::is_same_v<T, bool>) {
                debug::hir::log(debug::hir::Id::BoolLiteral, arg ? "true" : "false",
                                debug::Level::Trace);
            } else if constexpr (std::is_same_v<T, char>) {
                debug::hir::log(debug::hir::Id::CharLiteral, std::string(1, arg),
                                debug::Level::Trace);
            } else if constexpr (std::is_same_v<T, std::monostate>) {
                debug::hir::log(debug::hir::Id::NullLiteral, "null", debug::Level::Trace);
            }
        },
        lit.value);

    auto hir_lit = std::make_unique<HirLiteral>();
    hir_lit->value = lit.value;
    hir_lit->bit_info = lit.bit_info;  // SV幅付きリテラル伝搬
    // 型検査で脱糖済みの補間部分式をHIRへ下ろす（第4段b）。MIRはテキスト再パースせずこの式を消費する
    for (auto& [content, part_expr] : lit.interp_parts) {
        if (part_expr) {
            hir_lit->interp_parts.emplace_back(content,
                                               std::shared_ptr<HirExpr>(lower_expr(*part_expr)));
        }
    }
    return std::make_unique<HirExpr>(std::move(hir_lit), type);
}

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
        if (auto* sl = binary.left->as<ast::SliceExpr>()) {
            ast::TypePtr sobj_type = sl->object ? sl->object->type : nullptr;
            if (is_bits_type(sobj_type) &&
                (sl->is_part_select || (sl->start && sl->end && !sl->step))) {
                int64_t width = 0;
                HirExprPtr shift1;  // mask<<shift 用
                HirExprPtr shift2;  // (v&mask)<<shift 用
                if (sl->is_part_select) {
                    auto w = slice_lit(sl->end);
                    if (w) {
                        width = *w;
                        shift1 = lower_expr(*sl->start);
                        shift2 = lower_expr(*sl->start);
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

// 関数呼び出し
HirExprPtr HirLowering::lower_call(ast::CallExpr& call, TypePtr type) {
    debug::hir::log(debug::hir::Id::CallExprLower, "", debug::Level::Debug);

    // デバッグ: calleeの種類を確認
    if (call.callee) {
        if (auto* ident = call.callee->as<ast::IdentExpr>()) {
            debug::hir::log(debug::hir::Id::CallTarget, "callee is IdentExpr: " + ident->name,
                            debug::Level::Debug);
        } else if (auto* member = call.callee->as<ast::MemberExpr>()) {
            if (auto* obj_ident = member->object->as<ast::IdentExpr>()) {
                debug::hir::log(debug::hir::Id::CallTarget,
                                "callee is MemberExpr: " + obj_ident->name + "::" + member->member,
                                debug::Level::Debug);
            } else {
                debug::hir::log(debug::hir::Id::CallTarget,
                                "callee is MemberExpr with non-IdentExpr object: " + member->member,
                                debug::Level::Debug);
            }
        } else {
            debug::hir::log(debug::hir::Id::CallTarget, "callee is unknown type",
                            debug::Level::Debug);
        }
    }

    // enum variantコンストラクタ呼び出しのチェック
    // パターン1: IdentExpr (例：OptVal::HasVal(42) - パーサーが::を含む名前として解析)
    if (auto* ident = call.callee->as<ast::IdentExpr>()) {
        auto enum_it = enum_values_.find(ident->name);
        if (enum_it != enum_values_.end()) {
            // Tagged Union: enum variantコンストラクタ呼び出し
            debug::hir::log(debug::hir::Id::CallTarget,
                            "enum variant constructor: " + ident->name + " = " +
                                std::to_string(enum_it->second),
                            debug::Level::Debug);

            // HirEnumConstructノードを生成（タグ+ペイロード）
            auto enum_construct = std::make_unique<HirEnumConstruct>();

            // enum名とバリアント名を分解（"EnumName::VariantName" 形式）
            std::string full_name = ident->name;
            auto sep = full_name.find("::");
            if (sep != std::string::npos) {
                enum_construct->enum_name = full_name.substr(0, sep);
                enum_construct->variant_name = full_name.substr(sep + 2);
            } else {
                enum_construct->enum_name = full_name;
                enum_construct->variant_name = full_name;
            }
            enum_construct->tag_value = enum_it->second;

            // 引数があればペイロードとして保存
            if (!call.args.empty()) {
                enum_construct->payload = lower_expr(*call.args[0]);
            }

            // Tagged Union型を作成
            // 結果型は__TaggedUnion_{enum_name}構造体
            auto tagged_union_type = std::make_shared<ast::Type>(ast::TypeKind::Struct);
            tagged_union_type->name = "__TaggedUnion_" + enum_construct->enum_name;

            return std::make_unique<HirExpr>(std::move(enum_construct), tagged_union_type);
        }
    }

    // パターン2: MemberExpr (例：Result::Err - パーサーがResultをIdentでErrをメンバとして解析)
    if (auto* member = call.callee->as<ast::MemberExpr>()) {
        if (auto* obj_ident = member->object->as<ast::IdentExpr>()) {
            // EnumName::VariantName形式を構築
            std::string full_name = obj_ident->name + "::" + member->member;
            auto enum_it = enum_values_.find(full_name);
            if (enum_it != enum_values_.end()) {
                // Tagged Union: enum variantコンストラクタ呼び出し
                debug::hir::log(debug::hir::Id::CallTarget,
                                "enum variant constructor (MemberExpr): " + full_name + " = " +
                                    std::to_string(enum_it->second),
                                debug::Level::Debug);

                auto enum_construct = std::make_unique<HirEnumConstruct>();
                enum_construct->enum_name = obj_ident->name;
                enum_construct->variant_name = member->member;
                enum_construct->tag_value = enum_it->second;

                if (!call.args.empty()) {
                    enum_construct->payload = lower_expr(*call.args[0]);
                }

                auto tagged_union_type = std::make_shared<ast::Type>(ast::TypeKind::Struct);
                tagged_union_type->name = "__TaggedUnion_" + enum_construct->enum_name;

                return std::make_unique<HirExpr>(std::move(enum_construct), tagged_union_type);
            }
        }
    }

    auto hir = std::make_unique<HirCall>();

    std::string func_name;
    if (auto* ident = call.callee->as<ast::IdentExpr>()) {
        func_name = ident->name;

        // インポートエイリアスをチェック
        auto alias_it = import_aliases_.find(func_name);
        if (alias_it != import_aliases_.end()) {
            func_name = alias_it->second;
            debug::hir::log(debug::hir::Id::CallTarget,
                            "resolved import alias: " + ident->name + " -> " + func_name,
                            debug::Level::Trace);
        } else if (func_name == "println") {
            // フォールバック: printlnは常に__println__にマップ
            func_name = "__println__";
        } else if (func_name == "print") {
            // フォールバック: printは常に__print__にマップ
            func_name = "__print__";
        }

        // 静的メソッド呼び出し(Type::method)をType__method形式に変換
        // モジュールパス(std::io::println)は変換しない
        // 判定: ::が1つのみで、左側が既知の構造体/enum名の場合のみ変換（大文字始まりだけで判定すると大文字の名前空間エイリアス
        // `import ./mod as M; M::f()` が誤変換されシンボル不一致になる）
        size_t first_colon = func_name.find("::");
        if (first_colon != std::string::npos) {
            size_t second_colon = func_name.find("::", first_colon + 2);
            // ::が1つだけ存在する場合
            if (second_colon == std::string::npos) {
                std::string type_part = func_name.substr(0, first_colon);
                if (!type_part.empty() && std::isupper(static_cast<unsigned char>(type_part[0])) &&
                    (struct_defs_.count(type_part) > 0 || enum_defs_.count(type_part) > 0)) {
                    func_name.replace(first_colon, 2, "__");
                }
            }
        }

        hir->func_name = func_name;
        debug::hir::log(debug::hir::Id::CallTarget, "function: " + func_name, debug::Level::Trace);

        static const std::set<std::string> builtin_funcs = {
            "printf", "__println__",      "__print__",          "sprintf", "exit", "panic",
            "assert", "__builtin_concat", "__builtin_replicate"};

        bool is_builtin = builtin_funcs.find(func_name) != builtin_funcs.end();
        bool is_defined = func_defs_.find(func_name) != func_defs_.end();
        bool is_namespaced = func_name.find("::") != std::string::npos;

        if (!is_builtin && !is_defined && !is_namespaced) {
            hir->is_indirect = true;
            debug::hir::log(debug::hir::Id::CallTarget, "indirect call via variable: " + func_name,
                            debug::Level::Debug);
        }
    } else {
        hir->func_name = "<indirect>";
        hir->is_indirect = true;
        debug::hir::log(debug::hir::Id::CallTarget, "indirect call", debug::Level::Trace);
    }

    debug::hir::log(debug::hir::Id::CallArgs, "count=" + std::to_string(call.args.size()),
                    debug::Level::Trace);
    for (size_t i = 0; i < call.args.size(); i++) {
        debug::hir::log(debug::hir::Id::CallArgEval, "arg[" + std::to_string(i) + "]",
                        debug::Level::Trace);
        hir->args.push_back(lower_expr(*call.args[i]));
    }

    // デフォルト引数を適用
    if (!func_name.empty() && !hir->is_indirect) {
        auto func_it = func_defs_.find(func_name);
        if (func_it != func_defs_.end()) {
            const auto* func_def = func_it->second;
            for (size_t i = call.args.size(); i < func_def->params.size(); ++i) {
                const auto& param = func_def->params[i];
                if (param.default_value) {
                    debug::hir::log(debug::hir::Id::CallArgEval,
                                    "default arg[" + std::to_string(i) + "] for " + param.name,
                                    debug::Level::Trace);
                    hir->args.push_back(lower_expr(*param.default_value));
                }
            }
        }
    }

    return std::make_unique<HirExpr>(std::move(hir), type);
}

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
    // x[hi:lo] → (x >> lo) & ((1<<w)-1) / x[base +: w] → (x >> base) & ((1<<w)-1)
    // 全バックエンド共通のシフト+マスク脱糖（SVへの[hi:lo]直接出力は将来最適化）
    if (is_bits_type(obj_type) &&
        (slice.is_part_select || (slice.start && slice.end && !slice.step))) {
        int64_t width = 0;
        HirExprPtr shift_amount;
        if (slice.is_part_select) {
            auto w = slice_lit(slice.end);
            if (w) {
                width = *w;
                shift_amount = lower_expr(*slice.start);
            }
        } else {
            auto hi = slice_lit(slice.start);
            auto lo = slice_lit(slice.end);
            if (hi && lo && *hi >= *lo) {
                width = *hi - *lo + 1;
                shift_amount = make_int_lit(*lo, ast::make_int());
            }
        }
        if (width > 0 && shift_amount) {
            int64_t mask = (width >= 64) ? -1 : ((int64_t{1} << width) - 1);
            ast::TypePtr result_type =
                ast::make_array(ast::make_bit(), static_cast<uint32_t>(width));
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

// 三項演算子
HirExprPtr HirLowering::lower_ternary(ast::TernaryExpr& tern, TypePtr type) {
    auto hir = std::make_unique<HirTernary>();
    hir->condition = lower_expr(*tern.condition);
    hir->then_expr = lower_expr(*tern.then_expr);
    hir->else_expr = lower_expr(*tern.else_expr);
    return std::make_unique<HirExpr>(std::move(hir), type);
}

// 構造体リテラル
HirExprPtr HirLowering::lower_struct_literal(ast::StructLiteralExpr& lit, TypePtr expected_type) {
    std::string type_name = lit.type_name;

    if (type_name.empty() && expected_type) {
        if (expected_type->kind == ast::TypeKind::Struct && !expected_type->name.empty()) {
            type_name = expected_type->name;
            debug::hir::log(debug::hir::Id::LiteralLower,
                            "Inferred struct type from context: " + type_name, debug::Level::Debug);
        }
    }

    debug::hir::log(debug::hir::Id::LiteralLower, "Lowering struct literal: " + type_name,
                    debug::Level::Debug);

    auto hir_lit = std::make_unique<HirStructLiteral>();
    hir_lit->type_name = type_name;

    TypePtr struct_type = std::make_shared<ast::Type>(ast::TypeKind::Struct);
    struct_type->name = type_name;

    // ジェネリック特殊化別名（typedef IntPair = Pair<int,int>;）のリテラルは型検査が型引数付きの基底型を返すため、型引数を保持したままMIRへ伝播する（B8）
    if (expected_type && expected_type->kind == ast::TypeKind::Struct &&
        expected_type->name == type_name && !expected_type->type_args.empty()) {
        struct_type = expected_type;
    }

    const ast::StructDecl* struct_def = nullptr;
    if (!type_name.empty()) {
        auto struct_it = struct_defs_.find(type_name);
        if (struct_it != struct_defs_.end()) {
            struct_def = struct_it->second;
        }
    }

    for (auto& field : lit.fields) {
        HirStructLiteralField hir_field;
        hir_field.name = field.name;

        if (struct_def) {
            for (auto& def_field : struct_def->fields) {
                if (def_field.name == field.name) {
                    if (auto* nested_lit = field.value->as<ast::StructLiteralExpr>()) {
                        if (nested_lit->type_name.empty() && def_field.type &&
                            def_field.type->kind == ast::TypeKind::Struct) {
                            nested_lit->type_name = def_field.type->name;
                            debug::hir::log(
                                debug::hir::Id::LiteralLower,
                                "Propagated type to nested struct: " + def_field.type->name,
                                debug::Level::Debug);
                        }
                    }
                    break;
                }
            }
        }

        hir_field.value = lower_expr(*field.value);
        hir_lit->fields.push_back(std::move(hir_field));
    }

    return std::make_unique<HirExpr>(std::move(hir_lit), struct_type);
}

// 配列リテラル
HirExprPtr HirLowering::lower_array_literal(ast::ArrayLiteralExpr& lit, TypePtr expected_type) {
    debug::hir::log(
        debug::hir::Id::LiteralLower,
        "Lowering array literal with " + std::to_string(lit.elements.size()) + " elements",
        debug::Level::Debug);

    auto hir_lit = std::make_unique<HirArrayLiteral>();

    TypePtr expected_elem_type = nullptr;
    if (expected_type && expected_type->kind == ast::TypeKind::Array &&
        expected_type->element_type) {
        expected_elem_type = expected_type->element_type;
        debug::hir::log(debug::hir::Id::LiteralLower,
                        "Using expected element type: " + expected_elem_type->name,
                        debug::Level::Debug);
    }

    TypePtr elem_type = expected_elem_type;
    for (auto& elem : lit.elements) {
        if (expected_elem_type && expected_elem_type->kind == ast::TypeKind::Struct) {
            if (auto* nested_lit = elem->as<ast::StructLiteralExpr>()) {
                if (nested_lit->type_name.empty()) {
                    nested_lit->type_name = expected_elem_type->name;
                    debug::hir::log(
                        debug::hir::Id::LiteralLower,
                        "Propagated type to array element struct: " + expected_elem_type->name,
                        debug::Level::Debug);
                }
            }
        }

        auto lowered_elem = lower_expr(*elem);
        if (!elem_type) {
            elem_type = lowered_elem->type;
        }
        hir_lit->elements.push_back(std::move(lowered_elem));
    }

    if (!elem_type) {
        elem_type = hir::make_int();
    }

    TypePtr array_type = hir::make_array(elem_type, lit.elements.size());

    return std::make_unique<HirExpr>(std::move(hir_lit), array_type);
}

// ラムダ式
HirExprPtr HirLowering::lower_lambda(ast::LambdaExpr& lambda, TypePtr expected_type) {
    debug::hir::log(debug::hir::Id::ExprLower,
                    "Lowering lambda with " + std::to_string(lambda.params.size()) + " params" +
                        ", captures: " + std::to_string(lambda.captures.size()),
                    debug::Level::Debug);

    // パラメータを変換
    // 型が指定されていない場合、expected_typeから推論
    TypePtr return_type = nullptr;
    std::vector<TypePtr> param_types;

    if (expected_type && expected_type->kind == ast::TypeKind::Function) {
        return_type = expected_type->return_type;
        param_types = expected_type->param_types;
    }

    // 一意な名前を生成
    static int lambda_counter = 0;
    std::string lambda_name = "__lambda_" + std::to_string(lambda_counter++);

    // ラムダを関数として生成
    auto hir_func = std::make_unique<HirFunction>();
    hir_func->name = lambda_name;

    // キャプチャされた変数を最初のパラメータとして追加
    for (const auto& cap : lambda.captures) {
        HirParam cap_param;
        cap_param.name = cap.name;
        cap_param.type = cap.type;
        hir_func->params.push_back(std::move(cap_param));

        debug::hir::log(debug::hir::Id::ExprLower, "Lambda capture param: " + cap.name,
                        debug::Level::Debug);
    }

    for (size_t i = 0; i < lambda.params.size(); ++i) {
        HirParam param;
        param.name = lambda.params[i].name;

        // パラメータの型を決定
        if (lambda.params[i].type) {
            param.type = lambda.params[i].type;
        } else if (i < param_types.size()) {
            param.type = param_types[i];
        } else {
            param.type = hir::make_int();  // デフォルトはint
        }

        hir_func->params.push_back(std::move(param));
    }

    // 戻り値型
    hir_func->return_type = lambda.return_type ? lambda.return_type : return_type;
    if (!hir_func->return_type) {
        hir_func->return_type = hir::make_int();  // デフォルトはint
    }

    // ボディを変換
    if (lambda.is_expr_body()) {
        // 式本体の場合、returnに変換
        auto& body_expr = std::get<ast::ExprPtr>(lambda.body);
        auto hir_expr = lower_expr(*body_expr);

        auto ret = std::make_unique<HirReturn>();
        ret->value = std::move(hir_expr);
        auto ret_stmt = std::make_unique<HirStmt>(std::move(ret));
        hir_func->body.push_back(std::move(ret_stmt));
    } else {
        // ブロック本体の場合
        auto& body_stmts = std::get<std::vector<ast::StmtPtr>>(lambda.body);
        for (auto& stmt : body_stmts) {
            auto hir_stmt = lower_stmt(*stmt);
            if (hir_stmt) {
                hir_func->body.push_back(std::move(hir_stmt));
            }
        }
    }

    // ラムダ関数をリストに追加（後でプログラムに追加される）
    lambda_functions_.push_back(std::move(hir_func));

    // 関数ポインタ型を作成（キャプチャを含まない元の型）
    std::vector<TypePtr> hir_param_types;
    for (size_t i = lambda.captures.size(); i < lambda_functions_.back()->params.size(); ++i) {
        hir_param_types.push_back(lambda_functions_.back()->params[i].type);
    }
    TypePtr lambda_type =
        hir::make_function_ptr(lambda_functions_.back()->return_type, hir_param_types);

    debug::hir::log(debug::hir::Id::ExprLower, "Lambda lowered as function: " + lambda_name,
                    debug::Level::Debug);

    // キャプチャがある場合はクロージャ情報を持つ関数参照を生成
    if (!lambda.captures.empty()) {
        // クロージャ呼び出し用の特殊な参照を生成
        auto var_ref = std::make_unique<HirVarRef>();
        var_ref->name = lambda_name;
        var_ref->is_function_ref = true;
        var_ref->is_closure = true;

        // キャプチャ変数をコピー
        for (const auto& cap : lambda.captures) {
            HirVarRef::CapturedVar cv;
            cv.name = cap.name;
            cv.type = cap.type;
            var_ref->captured_vars.push_back(cv);
        }

        return std::make_unique<HirExpr>(std::move(var_ref), lambda_type);
    }

    // 関数参照を返す
    auto var_ref = std::make_unique<HirVarRef>();
    var_ref->name = lambda_name;
    var_ref->is_function_ref = true;

    return std::make_unique<HirExpr>(std::move(var_ref), lambda_type);
}

}  // namespace cm::hir
