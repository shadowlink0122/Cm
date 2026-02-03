// lowering_expr.cpp - 式のlowering
#include "fwd.hpp"

#include <algorithm>  // std::reverse用

namespace cm::hir {

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
        if (sizeof_expr->target_type) {
            size = calculate_type_size(sizeof_expr->target_type);
            type_name = ast::type_to_string(*sizeof_expr->target_type);
        } else if (sizeof_expr->target_expr && sizeof_expr->target_expr->type) {
            size = calculate_type_size(sizeof_expr->target_expr->type);
            type_name = ast::type_to_string(*sizeof_expr->target_expr->type);
        }
        debug::hir::log(debug::hir::Id::LiteralLower,
                        "sizeof(" + type_name + ") = " + std::to_string(size), debug::Level::Debug);
        auto lit = std::make_unique<HirLiteral>();
        lit->value = size;
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
        return std::make_unique<HirExpr>(std::move(hir_cast), cast_expr->target_type);
    } else if (auto* move_expr = expr.as<ast::MoveExpr>()) {
        // move式: move x は x そのものを返す（所有権移動、ゼロコスト）
        debug::hir::log(debug::hir::Id::ExprLower, "Lowering move expression", debug::Level::Debug);
        // moveは単にオペランドを返す - 所有権追跡は型チェッカーで行われている
        return lower_expr(*move_expr->operand);
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
        if (auto* rhs_ident = binary.right->as<ast::IdentExpr>()) {
            auto enum_it = enum_values_.find(rhs_ident->name);
            if (enum_it != enum_values_.end()) {
                rhs_is_enum_tag = true;
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
                    // タグ抽出: lhs.0 == rhs_tag_value
                    auto member = std::make_unique<HirMember>();
                    member->object = lower_expr(*binary.left);
                    member->member = "__tag";  // Tagged Unionのタグフィールド

                    auto hir = std::make_unique<HirBinary>();
                    hir->op = (binary.op == ast::BinaryOp::Eq) ? HirBinaryOp::Eq : HirBinaryOp::Ne;
                    hir->lhs = std::make_unique<HirExpr>(std::move(member), ast::make_int());
                    hir->rhs = lower_expr(*binary.right);  // enumタグ値（int）
                    return std::make_unique<HirExpr>(std::move(hir), type);
                }
            }
        }

        // 逆順: 左辺がenum参照、右辺がenum変数
        std::string lhs_enum_name;
        bool lhs_is_enum_tag = false;
        if (auto* lhs_ident = binary.left->as<ast::IdentExpr>()) {
            auto enum_it = enum_values_.find(lhs_ident->name);
            if (enum_it != enum_values_.end()) {
                lhs_is_enum_tag = true;
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

                    auto hir = std::make_unique<HirBinary>();
                    hir->op = (binary.op == ast::BinaryOp::Eq) ? HirBinaryOp::Eq : HirBinaryOp::Ne;
                    hir->lhs = lower_expr(*binary.left);  // enumタグ値（int）
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

    // enum variantコンストラクタ呼び出しのチェック（例：OptVal::HasVal(42)）
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

        hir->func_name = func_name;
        debug::hir::log(debug::hir::Id::CallTarget, "function: " + func_name, debug::Level::Trace);

        static const std::set<std::string> builtin_funcs = {"printf",  "__println__", "__print__",
                                                            "sprintf", "exit",        "panic"};

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
    return std::make_unique<HirExpr>(std::move(hir), type);
}

// スライス式
HirExprPtr HirLowering::lower_slice(ast::SliceExpr& slice, TypePtr type) {
    debug::hir::log(debug::hir::Id::IndexLower, "Slice expression", debug::Level::Debug);

    auto obj_hir = lower_expr(*slice.object);
    TypePtr obj_type = obj_hir->type;

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
            hir->args.push_back(std::make_unique<HirExpr>(std::move(zero), ast::make_int()));
        }

        if (slice.end) {
            hir->args.push_back(lower_expr(*slice.end));
        } else {
            auto neg_one = std::make_unique<HirLiteral>();
            neg_one->value = int64_t{-1};
            hir->args.push_back(std::make_unique<HirExpr>(std::move(neg_one), ast::make_int()));
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

// メンバアクセス / メソッド呼び出し
HirExprPtr HirLowering::lower_member(ast::MemberExpr& mem, TypePtr type) {
    // メソッド呼び出しの場合
    if (mem.is_method_call) {
        debug::hir::log(
            debug::hir::Id::MethodCallLower,
            "method: " + mem.member + " with " + std::to_string(mem.args.size()) + " args",
            debug::Level::Debug);

        auto obj_hir = lower_expr(*mem.object);
        std::string type_name;

        TypePtr obj_type = nullptr;
        if (obj_hir->type) {
            obj_type = obj_hir->type;
            // type_to_stringでジェネリック型引数を含む形式で取得（Vector<int>）
            // 後でマングリング形式（Vector__int）に変換される
            type_name = ast::type_to_string(*obj_hir->type);
            debug::hir::log(debug::hir::Id::MethodCallLower, "obj_hir->type = " + type_name,
                            debug::Level::Info);
        } else if (mem.object->type) {
            obj_type = mem.object->type;
            type_name = ast::type_to_string(*mem.object->type);
            debug::hir::log(debug::hir::Id::MethodCallLower, "mem.object->type = " + type_name,
                            debug::Level::Info);
        } else {
            debug::hir::log(
                debug::hir::Id::MethodCallLower,
                "WARNING: Both obj_hir->type and mem.object->type are null for method: " +
                    mem.member,
                debug::Level::Warn);
        }

        // obj_typeがnullの場合はデバッグログのみ
        // （フォールバックは使用せず、型チェッカーの設定に依存）

        // 配列のビルトインメソッド処理
        if (obj_type && obj_type->kind == ast::TypeKind::Array) {
            // dim() - 配列の次元数を返す
            if (mem.member == "dim") {
                // 次元数を計算
                int dim = 1;
                TypePtr t = obj_type->element_type;
                while (t && t->kind == ast::TypeKind::Array) {
                    dim++;
                    t = t->element_type;
                }
                auto lit = std::make_unique<HirLiteral>();
                lit->value = static_cast<int64_t>(dim);
                debug::hir::log(debug::hir::Id::MethodCallLower,
                                "Array builtin dim() = " + std::to_string(dim),
                                debug::Level::Debug);
                return std::make_unique<HirExpr>(std::move(lit), ast::make_int());
            }

            // 動的配列（スライス）の場合はlenを関数呼び出しで処理
            if (!obj_type->array_size.has_value()) {
                if (mem.member == "size" || mem.member == "len" || mem.member == "length") {
                    auto hir = std::make_unique<HirCall>();
                    hir->func_name = "__builtin_slice_len";
                    hir->args.push_back(std::move(obj_hir));
                    debug::hir::log(debug::hir::Id::MethodCallLower, "Slice builtin len()",
                                    debug::Level::Debug);
                    return std::make_unique<HirExpr>(std::move(hir), ast::make_usize());
                }
            }
            // 静的配列の場合はサイズを定数リテラルとして返す
            else if (mem.member == "size" || mem.member == "len" || mem.member == "length") {
                auto lit = std::make_unique<HirLiteral>();
                lit->value = static_cast<int64_t>(obj_type->array_size.value());
                debug::hir::log(
                    debug::hir::Id::MethodCallLower,
                    "Array builtin size() = " + std::to_string(obj_type->array_size.value_or(0)),
                    debug::Level::Debug);
                return std::make_unique<HirExpr>(std::move(lit), ast::make_uint());
            }

            if (mem.member == "forEach") {
                auto hir = std::make_unique<HirCall>();
                hir->func_name = "__builtin_array_forEach";
                hir->args.push_back(std::move(obj_hir));
                auto size_lit = std::make_unique<HirLiteral>();
                size_lit->value = static_cast<int64_t>(obj_type->array_size.value_or(0));
                hir->args.push_back(
                    std::make_unique<HirExpr>(std::move(size_lit), ast::make_int()));
                for (auto& arg : mem.args) {
                    hir->args.push_back(lower_expr(*arg));
                }
                debug::hir::log(debug::hir::Id::MethodCallLower, "Array builtin forEach()",
                                debug::Level::Debug);
                return std::make_unique<HirExpr>(std::move(hir), ast::make_void());
            }

            if (mem.member == "reduce") {
                auto hir = std::make_unique<HirCall>();
                // 要素型に応じてサフィックスを決定
                std::string suffix = "_i32";  // デフォルト
                if (obj_type->element_type &&
                    (obj_type->element_type->kind == ast::TypeKind::Long ||
                     obj_type->element_type->kind == ast::TypeKind::ULong)) {
                    suffix = "_i64";
                }
                hir->func_name = "__builtin_array_reduce" + suffix;
                // 配列のアドレス
                auto addr_op = std::make_unique<HirUnary>();
                addr_op->op = HirUnaryOp::AddrOf;
                addr_op->operand = std::move(obj_hir);
                auto ptr_type = ast::make_pointer(obj_type->element_type);
                hir->args.push_back(std::make_unique<HirExpr>(std::move(addr_op), ptr_type));
                // 配列サイズ
                auto size_lit = std::make_unique<HirLiteral>();
                size_lit->value = static_cast<int64_t>(obj_type->array_size.value_or(0));
                hir->args.push_back(
                    std::make_unique<HirExpr>(std::move(size_lit), ast::make_int()));
                // コールバック関数と初期値
                for (auto& arg : mem.args) {
                    hir->args.push_back(lower_expr(*arg));
                }
                debug::hir::log(debug::hir::Id::MethodCallLower, "Array builtin reduce()",
                                debug::Level::Debug);
                return std::make_unique<HirExpr>(std::move(hir), obj_type->element_type);
            }

            if (mem.member == "some") {
                auto hir = std::make_unique<HirCall>();
                hir->func_name = "__builtin_array_some_i32";
                auto addr_op = std::make_unique<HirUnary>();
                addr_op->op = HirUnaryOp::AddrOf;
                addr_op->operand = std::move(obj_hir);
                auto ptr_type = ast::make_pointer(obj_type->element_type);
                hir->args.push_back(std::make_unique<HirExpr>(std::move(addr_op), ptr_type));
                auto size_lit = std::make_unique<HirLiteral>();
                size_lit->value = static_cast<int64_t>(obj_type->array_size.value_or(0));
                hir->args.push_back(
                    std::make_unique<HirExpr>(std::move(size_lit), ast::make_int()));
                for (auto& arg : mem.args) {
                    hir->args.push_back(lower_expr(*arg));
                }
                debug::hir::log(debug::hir::Id::MethodCallLower, "Array builtin some()",
                                debug::Level::Debug);
                return std::make_unique<HirExpr>(std::move(hir), ast::make_bool());
            }

            if (mem.member == "every") {
                auto hir = std::make_unique<HirCall>();
                hir->func_name = "__builtin_array_every_i32";
                auto addr_op = std::make_unique<HirUnary>();
                addr_op->op = HirUnaryOp::AddrOf;
                addr_op->operand = std::move(obj_hir);
                auto ptr_type = ast::make_pointer(obj_type->element_type);
                hir->args.push_back(std::make_unique<HirExpr>(std::move(addr_op), ptr_type));
                auto size_lit = std::make_unique<HirLiteral>();
                size_lit->value = static_cast<int64_t>(obj_type->array_size.value_or(0));
                hir->args.push_back(
                    std::make_unique<HirExpr>(std::move(size_lit), ast::make_int()));
                for (auto& arg : mem.args) {
                    hir->args.push_back(lower_expr(*arg));
                }
                debug::hir::log(debug::hir::Id::MethodCallLower, "Array builtin every()",
                                debug::Level::Debug);
                return std::make_unique<HirExpr>(std::move(hir), ast::make_bool());
            }

            if (mem.member == "findIndex") {
                auto hir = std::make_unique<HirCall>();
                hir->func_name = "__builtin_array_findIndex_i32";
                auto addr_op = std::make_unique<HirUnary>();
                addr_op->op = HirUnaryOp::AddrOf;
                addr_op->operand = std::move(obj_hir);
                auto ptr_type = ast::make_pointer(obj_type->element_type);
                hir->args.push_back(std::make_unique<HirExpr>(std::move(addr_op), ptr_type));
                auto size_lit = std::make_unique<HirLiteral>();
                size_lit->value = static_cast<int64_t>(obj_type->array_size.value_or(0));
                hir->args.push_back(
                    std::make_unique<HirExpr>(std::move(size_lit), ast::make_int()));
                for (auto& arg : mem.args) {
                    hir->args.push_back(lower_expr(*arg));
                }
                debug::hir::log(debug::hir::Id::MethodCallLower, "Array builtin findIndex()",
                                debug::Level::Debug);
                return std::make_unique<HirExpr>(std::move(hir), ast::make_int());
            }

            if (mem.member == "indexOf") {
                auto hir = std::make_unique<HirCall>();
                hir->func_name = "__builtin_array_indexOf_i32";
                auto addr_op = std::make_unique<HirUnary>();
                addr_op->op = HirUnaryOp::AddrOf;
                addr_op->operand = std::move(obj_hir);
                auto ptr_type = ast::make_pointer(obj_type->element_type);
                hir->args.push_back(std::make_unique<HirExpr>(std::move(addr_op), ptr_type));
                auto size_lit = std::make_unique<HirLiteral>();
                size_lit->value = static_cast<int64_t>(obj_type->array_size.value_or(0));
                hir->args.push_back(
                    std::make_unique<HirExpr>(std::move(size_lit), ast::make_int()));
                for (auto& arg : mem.args) {
                    hir->args.push_back(lower_expr(*arg));
                }
                debug::hir::log(debug::hir::Id::MethodCallLower, "Array builtin indexOf()",
                                debug::Level::Debug);
                return std::make_unique<HirExpr>(std::move(hir), ast::make_int());
            }

            if (mem.member == "includes" || mem.member == "contains") {
                auto hir = std::make_unique<HirCall>();
                hir->func_name = "__builtin_array_includes_i32";
                auto addr_op = std::make_unique<HirUnary>();
                addr_op->op = HirUnaryOp::AddrOf;
                addr_op->operand = std::move(obj_hir);
                auto ptr_type = ast::make_pointer(obj_type->element_type);
                hir->args.push_back(std::make_unique<HirExpr>(std::move(addr_op), ptr_type));
                auto size_lit = std::make_unique<HirLiteral>();
                size_lit->value = static_cast<int64_t>(obj_type->array_size.value_or(0));
                hir->args.push_back(
                    std::make_unique<HirExpr>(std::move(size_lit), ast::make_int()));
                for (auto& arg : mem.args) {
                    hir->args.push_back(lower_expr(*arg));
                }
                debug::hir::log(debug::hir::Id::MethodCallLower, "Array builtin includes()",
                                debug::Level::Debug);
                return std::make_unique<HirExpr>(std::move(hir), ast::make_bool());
            }

            if (mem.member == "map") {
                auto hir = std::make_unique<HirCall>();
                hir->func_name = "__builtin_array_map";
                // 配列のアドレス
                auto addr_op = std::make_unique<HirUnary>();
                addr_op->op = HirUnaryOp::AddrOf;
                addr_op->operand = std::move(obj_hir);
                auto ptr_type = ast::make_pointer(obj_type->element_type);
                hir->args.push_back(std::make_unique<HirExpr>(std::move(addr_op), ptr_type));
                // 配列サイズ
                auto size_lit = std::make_unique<HirLiteral>();
                size_lit->value = static_cast<int64_t>(obj_type->array_size.value_or(0));
                hir->args.push_back(
                    std::make_unique<HirExpr>(std::move(size_lit), ast::make_int()));
                // コールバック関数
                for (auto& arg : mem.args) {
                    hir->args.push_back(lower_expr(*arg));
                }
                debug::hir::log(debug::hir::Id::MethodCallLower, "Array builtin map()",
                                debug::Level::Debug);
                // 結果の型は常に動的配列/スライス
                TypePtr result_type = ast::make_array(obj_type->element_type, std::nullopt);
                return std::make_unique<HirExpr>(std::move(hir), result_type);
            }

            if (mem.member == "filter") {
                auto hir = std::make_unique<HirCall>();
                hir->func_name = "__builtin_array_filter";
                // 配列のアドレス
                auto addr_op = std::make_unique<HirUnary>();
                addr_op->op = HirUnaryOp::AddrOf;
                addr_op->operand = std::move(obj_hir);
                auto ptr_type = ast::make_pointer(obj_type->element_type);
                hir->args.push_back(std::make_unique<HirExpr>(std::move(addr_op), ptr_type));
                // 配列サイズ
                auto size_lit = std::make_unique<HirLiteral>();
                size_lit->value = static_cast<int64_t>(obj_type->array_size.value_or(0));
                hir->args.push_back(
                    std::make_unique<HirExpr>(std::move(size_lit), ast::make_int()));
                // コールバック関数
                for (auto& arg : mem.args) {
                    hir->args.push_back(lower_expr(*arg));
                }
                debug::hir::log(debug::hir::Id::MethodCallLower, "Array builtin filter()",
                                debug::Level::Debug);
                // 結果の型（動的配列/スライス）
                TypePtr result_type = ast::make_array(obj_type->element_type, std::nullopt);
                return std::make_unique<HirExpr>(std::move(hir), result_type);
            }

            if (mem.member == "reverse" && obj_type->array_size.has_value()) {
                auto hir = std::make_unique<HirCall>();
                hir->func_name = "__builtin_array_reverse";
                // 配列のアドレス
                auto addr_op = std::make_unique<HirUnary>();
                addr_op->op = HirUnaryOp::AddrOf;
                addr_op->operand = std::move(obj_hir);
                auto ptr_type = ast::make_pointer(obj_type->element_type);
                hir->args.push_back(std::make_unique<HirExpr>(std::move(addr_op), ptr_type));
                // 配列サイズ
                auto size_lit = std::make_unique<HirLiteral>();
                size_lit->value = static_cast<int64_t>(obj_type->array_size.value_or(0));
                hir->args.push_back(
                    std::make_unique<HirExpr>(std::move(size_lit), ast::make_int()));
                debug::hir::log(debug::hir::Id::MethodCallLower, "Array builtin reverse()",
                                debug::Level::Debug);
                // 動的配列（スライス）を返す
                auto return_type = ast::make_array(obj_type->element_type, std::nullopt);
                return std::make_unique<HirExpr>(std::move(hir), return_type);
            }

            if (mem.member == "sort" && obj_type->array_size.has_value()) {
                auto hir = std::make_unique<HirCall>();
                hir->func_name = "__builtin_array_sort";
                // 配列のアドレス
                auto addr_op = std::make_unique<HirUnary>();
                addr_op->op = HirUnaryOp::AddrOf;
                addr_op->operand = std::move(obj_hir);
                auto ptr_type = ast::make_pointer(obj_type->element_type);
                hir->args.push_back(std::make_unique<HirExpr>(std::move(addr_op), ptr_type));
                // 配列サイズ
                auto size_lit = std::make_unique<HirLiteral>();
                size_lit->value = static_cast<int64_t>(obj_type->array_size.value_or(0));
                hir->args.push_back(
                    std::make_unique<HirExpr>(std::move(size_lit), ast::make_int()));
                debug::hir::log(debug::hir::Id::MethodCallLower, "Array builtin sort()",
                                debug::Level::Debug);
                // 動的配列（スライス）を返す
                auto return_type = ast::make_array(obj_type->element_type, std::nullopt);
                return std::make_unique<HirExpr>(std::move(hir), return_type);
            }

            if (mem.member == "sortBy" && obj_type->array_size.has_value()) {
                auto hir = std::make_unique<HirCall>();
                hir->func_name = "__builtin_array_sortBy";
                // 配列のアドレス
                auto addr_op = std::make_unique<HirUnary>();
                addr_op->op = HirUnaryOp::AddrOf;
                addr_op->operand = std::move(obj_hir);
                auto ptr_type = ast::make_pointer(obj_type->element_type);
                hir->args.push_back(std::make_unique<HirExpr>(std::move(addr_op), ptr_type));
                // 配列サイズ
                auto size_lit = std::make_unique<HirLiteral>();
                size_lit->value = static_cast<int64_t>(obj_type->array_size.value_or(0));
                hir->args.push_back(
                    std::make_unique<HirExpr>(std::move(size_lit), ast::make_int()));
                // コンパレータ関数
                for (auto& arg : mem.args) {
                    hir->args.push_back(lower_expr(*arg));
                }
                debug::hir::log(debug::hir::Id::MethodCallLower, "Array builtin sortBy()",
                                debug::Level::Debug);
                // 動的配列（スライス）を返す
                auto return_type = ast::make_array(obj_type->element_type, std::nullopt);
                return std::make_unique<HirExpr>(std::move(hir), return_type);
            }

            // 固定サイズ配列のfirst（動的配列は後で処理）
            if (mem.member == "first" && obj_type->array_size.has_value()) {
                // 要素型が配列（多次元配列）の場合は、インデックスアクセスに変換
                if (obj_type->element_type &&
                    obj_type->element_type->kind == ast::TypeKind::Array) {
                    // arr.first() -> arr[0]
                    auto idx_lit = std::make_unique<HirLiteral>();
                    idx_lit->value = int64_t{0};
                    auto idx_expr = std::make_unique<HirExpr>(std::move(idx_lit), ast::make_int());
                    auto index_op = std::make_unique<HirIndex>();
                    index_op->object = std::move(obj_hir);
                    index_op->index = std::move(idx_expr);
                    debug::hir::log(debug::hir::Id::MethodCallLower,
                                    "Array builtin first() - multidim", debug::Level::Debug);
                    return std::make_unique<HirExpr>(std::move(index_op), obj_type->element_type);
                }

                auto hir = std::make_unique<HirCall>();
                // 要素型に応じてサフィックスを決定
                std::string suffix = "_i32";  // デフォルト
                if (obj_type->element_type &&
                    (obj_type->element_type->kind == ast::TypeKind::Long ||
                     obj_type->element_type->kind == ast::TypeKind::ULong)) {
                    suffix = "_i64";
                }
                hir->func_name = "__builtin_array_first" + suffix;
                // 配列のアドレス
                auto addr_op = std::make_unique<HirUnary>();
                addr_op->op = HirUnaryOp::AddrOf;
                addr_op->operand = std::move(obj_hir);
                auto ptr_type = ast::make_pointer(obj_type->element_type);
                hir->args.push_back(std::make_unique<HirExpr>(std::move(addr_op), ptr_type));
                // 配列サイズ
                auto size_lit = std::make_unique<HirLiteral>();
                size_lit->value = static_cast<int64_t>(obj_type->array_size.value_or(0));
                hir->args.push_back(
                    std::make_unique<HirExpr>(std::move(size_lit), ast::make_int()));
                debug::hir::log(debug::hir::Id::MethodCallLower, "Array builtin first()",
                                debug::Level::Debug);
                return std::make_unique<HirExpr>(std::move(hir), obj_type->element_type);
            }

            // 固定サイズ配列のlast（動的配列は後で処理）
            if (mem.member == "last" && obj_type->array_size.has_value()) {
                // 要素型が配列（多次元配列）の場合は、インデックスアクセスに変換
                if (obj_type->element_type &&
                    obj_type->element_type->kind == ast::TypeKind::Array) {
                    // arr.last() -> arr[size-1]
                    auto idx_lit = std::make_unique<HirLiteral>();
                    idx_lit->value = static_cast<int64_t>(obj_type->array_size.value_or(1) - 1);
                    auto idx_expr = std::make_unique<HirExpr>(std::move(idx_lit), ast::make_int());
                    auto index_op = std::make_unique<HirIndex>();
                    index_op->object = std::move(obj_hir);
                    index_op->index = std::move(idx_expr);
                    debug::hir::log(debug::hir::Id::MethodCallLower,
                                    "Array builtin last() - multidim", debug::Level::Debug);
                    return std::make_unique<HirExpr>(std::move(index_op), obj_type->element_type);
                }

                auto hir = std::make_unique<HirCall>();
                // 要素型に応じてサフィックスを決定
                std::string suffix = "_i32";  // デフォルト
                if (obj_type->element_type &&
                    (obj_type->element_type->kind == ast::TypeKind::Long ||
                     obj_type->element_type->kind == ast::TypeKind::ULong)) {
                    suffix = "_i64";
                }
                hir->func_name = "__builtin_array_last" + suffix;
                // 配列のアドレス
                auto addr_op = std::make_unique<HirUnary>();
                addr_op->op = HirUnaryOp::AddrOf;
                addr_op->operand = std::move(obj_hir);
                auto ptr_type = ast::make_pointer(obj_type->element_type);
                hir->args.push_back(std::make_unique<HirExpr>(std::move(addr_op), ptr_type));
                // 配列サイズ
                auto size_lit = std::make_unique<HirLiteral>();
                size_lit->value = static_cast<int64_t>(obj_type->array_size.value_or(0));
                hir->args.push_back(
                    std::make_unique<HirExpr>(std::move(size_lit), ast::make_int()));
                debug::hir::log(debug::hir::Id::MethodCallLower, "Array builtin last()",
                                debug::Level::Debug);
                return std::make_unique<HirExpr>(std::move(hir), obj_type->element_type);
            }

            if (mem.member == "find") {
                auto hir = std::make_unique<HirCall>();
                // 要素型に応じてサフィックスを決定
                std::string suffix = "_i32";  // デフォルト
                if (obj_type->element_type &&
                    (obj_type->element_type->kind == ast::TypeKind::Long ||
                     obj_type->element_type->kind == ast::TypeKind::ULong)) {
                    suffix = "_i64";
                }
                hir->func_name = "__builtin_array_find" + suffix;
                // 配列のアドレス
                auto addr_op = std::make_unique<HirUnary>();
                addr_op->op = HirUnaryOp::AddrOf;
                addr_op->operand = std::move(obj_hir);
                auto ptr_type = ast::make_pointer(obj_type->element_type);
                hir->args.push_back(std::make_unique<HirExpr>(std::move(addr_op), ptr_type));
                // 配列サイズ
                auto size_lit = std::make_unique<HirLiteral>();
                size_lit->value = static_cast<int64_t>(obj_type->array_size.value_or(0));
                hir->args.push_back(
                    std::make_unique<HirExpr>(std::move(size_lit), ast::make_int()));
                // コールバック関数
                for (auto& arg : mem.args) {
                    hir->args.push_back(lower_expr(*arg));
                }
                debug::hir::log(debug::hir::Id::MethodCallLower, "Array builtin find()",
                                debug::Level::Debug);
                return std::make_unique<HirExpr>(std::move(hir), obj_type->element_type);
            }
        }

        // 動的配列（スライス）のビルトインメソッド処理
        // 動的配列は array_size が設定されていない
        if (obj_type && obj_type->kind == ast::TypeKind::Array &&
            !obj_type->array_size.has_value()) {
            if (mem.member == "len" || mem.member == "size" || mem.member == "length") {
                auto hir = std::make_unique<HirCall>();
                hir->func_name = "__builtin_slice_len";
                hir->args.push_back(std::move(obj_hir));
                debug::hir::log(debug::hir::Id::MethodCallLower, "Slice builtin len()",
                                debug::Level::Debug);
                return std::make_unique<HirExpr>(std::move(hir), ast::make_usize());
            }

            if (mem.member == "cap" || mem.member == "capacity") {
                auto hir = std::make_unique<HirCall>();
                hir->func_name = "__builtin_slice_cap";
                hir->args.push_back(std::move(obj_hir));
                return std::make_unique<HirExpr>(std::move(hir), ast::make_usize());
            }

            if (mem.member == "push") {
                auto hir = std::make_unique<HirCall>();
                hir->func_name = "__builtin_slice_push";
                hir->args.push_back(std::move(obj_hir));
                for (auto& arg : mem.args) {
                    hir->args.push_back(lower_expr(*arg));
                }
                debug::hir::log(debug::hir::Id::MethodCallLower, "Slice builtin push()",
                                debug::Level::Debug);
                return std::make_unique<HirExpr>(std::move(hir), ast::make_void());
            }

            if (mem.member == "pop") {
                auto hir = std::make_unique<HirCall>();
                hir->func_name = "__builtin_slice_pop";
                hir->args.push_back(std::move(obj_hir));
                debug::hir::log(debug::hir::Id::MethodCallLower, "Slice builtin pop()",
                                debug::Level::Debug);
                TypePtr elem_type =
                    obj_type->element_type ? obj_type->element_type : ast::make_int();
                return std::make_unique<HirExpr>(std::move(hir), elem_type);
            }

            if (mem.member == "remove" || mem.member == "delete") {
                auto hir = std::make_unique<HirCall>();
                hir->func_name = "__builtin_slice_delete";
                hir->args.push_back(std::move(obj_hir));
                for (auto& arg : mem.args) {
                    hir->args.push_back(lower_expr(*arg));
                }
                debug::hir::log(debug::hir::Id::MethodCallLower, "Slice builtin remove()",
                                debug::Level::Debug);
                return std::make_unique<HirExpr>(std::move(hir), ast::make_void());
            }

            if (mem.member == "clear") {
                auto hir = std::make_unique<HirCall>();
                hir->func_name = "__builtin_slice_clear";
                hir->args.push_back(std::move(obj_hir));
                debug::hir::log(debug::hir::Id::MethodCallLower, "Slice builtin clear()",
                                debug::Level::Debug);
                return std::make_unique<HirExpr>(std::move(hir), ast::make_void());
            }

            if (mem.member == "first") {
                // 要素型が配列（多次元スライス）の場合は、インデックスアクセスに変換
                if (obj_type->element_type &&
                    obj_type->element_type->kind == ast::TypeKind::Array) {
                    // slice.first() -> slice[0]
                    auto idx_lit = std::make_unique<HirLiteral>();
                    idx_lit->value = int64_t{0};
                    auto idx_expr = std::make_unique<HirExpr>(std::move(idx_lit), ast::make_int());
                    auto index_op = std::make_unique<HirIndex>();
                    index_op->object = std::move(obj_hir);
                    index_op->index = std::move(idx_expr);
                    debug::hir::log(debug::hir::Id::MethodCallLower,
                                    "Slice builtin first() - multidim", debug::Level::Debug);
                    return std::make_unique<HirExpr>(std::move(index_op), obj_type->element_type);
                }

                auto hir = std::make_unique<HirCall>();
                // スライスの場合はcm_slice_first_*を使用
                std::string suffix = "_i32";  // デフォルト
                if (obj_type->element_type &&
                    (obj_type->element_type->kind == ast::TypeKind::Long ||
                     obj_type->element_type->kind == ast::TypeKind::ULong)) {
                    suffix = "_i64";
                }
                hir->func_name = "cm_slice_first" + suffix;
                hir->args.push_back(std::move(obj_hir));
                debug::hir::log(debug::hir::Id::MethodCallLower, "Slice builtin first()",
                                debug::Level::Debug);
                return std::make_unique<HirExpr>(std::move(hir), obj_type->element_type);
            }

            if (mem.member == "last") {
                // 要素型が配列（多次元スライス）の場合はcm_slice_last_ptrを使用
                if (obj_type->element_type &&
                    obj_type->element_type->kind == ast::TypeKind::Array) {
                    auto hir = std::make_unique<HirCall>();
                    hir->func_name = "cm_slice_last_ptr";
                    hir->args.push_back(std::move(obj_hir));
                    debug::hir::log(debug::hir::Id::MethodCallLower,
                                    "Slice builtin last() - multidim", debug::Level::Debug);
                    return std::make_unique<HirExpr>(std::move(hir), obj_type->element_type);
                }

                auto hir = std::make_unique<HirCall>();
                // スライスの場合はcm_slice_last_*を使用
                std::string suffix = "_i32";  // デフォルト
                if (obj_type->element_type &&
                    (obj_type->element_type->kind == ast::TypeKind::Long ||
                     obj_type->element_type->kind == ast::TypeKind::ULong)) {
                    suffix = "_i64";
                }
                hir->func_name = "cm_slice_last" + suffix;
                hir->args.push_back(std::move(obj_hir));
                debug::hir::log(debug::hir::Id::MethodCallLower, "Slice builtin last()",
                                debug::Level::Debug);
                return std::make_unique<HirExpr>(std::move(hir), obj_type->element_type);
            }

            if (mem.member == "reverse") {
                auto hir = std::make_unique<HirCall>();
                hir->func_name = "cm_slice_reverse";
                hir->args.push_back(std::move(obj_hir));
                debug::hir::log(debug::hir::Id::MethodCallLower, "Slice builtin reverse()",
                                debug::Level::Debug);
                return std::make_unique<HirExpr>(std::move(hir), type);
            }

            if (mem.member == "sort") {
                auto hir = std::make_unique<HirCall>();
                hir->func_name = "cm_slice_sort";
                hir->args.push_back(std::move(obj_hir));
                debug::hir::log(debug::hir::Id::MethodCallLower, "Slice builtin sort()",
                                debug::Level::Debug);
                return std::make_unique<HirExpr>(std::move(hir), type);
            }
        }

        // 文字列のビルトインメソッド処理
        if (obj_type && obj_type->kind == ast::TypeKind::String) {
            if (mem.member == "len" || mem.member == "size" || mem.member == "length") {
                auto hir = std::make_unique<HirCall>();
                hir->func_name = "__builtin_string_len";
                hir->args.push_back(std::move(obj_hir));
                debug::hir::log(debug::hir::Id::MethodCallLower, "String builtin len()",
                                debug::Level::Debug);
                return std::make_unique<HirExpr>(std::move(hir), ast::make_uint());
            }
            if (mem.member == "charAt" || mem.member == "at") {
                auto hir = std::make_unique<HirCall>();
                hir->func_name = "__builtin_string_charAt";
                hir->args.push_back(std::move(obj_hir));
                for (auto& arg : mem.args) {
                    hir->args.push_back(lower_expr(*arg));
                }
                debug::hir::log(debug::hir::Id::MethodCallLower, "String builtin charAt()",
                                debug::Level::Debug);
                return std::make_unique<HirExpr>(std::move(hir), ast::make_char());
            }
            if (mem.member == "substring" || mem.member == "slice") {
                auto hir = std::make_unique<HirCall>();
                hir->func_name = "__builtin_string_substring";
                hir->args.push_back(std::move(obj_hir));
                for (auto& arg : mem.args) {
                    hir->args.push_back(lower_expr(*arg));
                }
                debug::hir::log(debug::hir::Id::MethodCallLower, "String builtin substring()",
                                debug::Level::Debug);
                return std::make_unique<HirExpr>(std::move(hir), ast::make_string());
            }
            if (mem.member == "indexOf") {
                auto hir = std::make_unique<HirCall>();
                hir->func_name = "__builtin_string_indexOf";
                hir->args.push_back(std::move(obj_hir));
                for (auto& arg : mem.args) {
                    hir->args.push_back(lower_expr(*arg));
                }
                debug::hir::log(debug::hir::Id::MethodCallLower, "String builtin indexOf()",
                                debug::Level::Debug);
                return std::make_unique<HirExpr>(std::move(hir), ast::make_int());
            }
            if (mem.member == "toUpperCase") {
                auto hir = std::make_unique<HirCall>();
                hir->func_name = "__builtin_string_toUpperCase";
                hir->args.push_back(std::move(obj_hir));
                debug::hir::log(debug::hir::Id::MethodCallLower, "String builtin toUpperCase()",
                                debug::Level::Debug);
                return std::make_unique<HirExpr>(std::move(hir), ast::make_string());
            }
            if (mem.member == "toLowerCase") {
                auto hir = std::make_unique<HirCall>();
                hir->func_name = "__builtin_string_toLowerCase";
                hir->args.push_back(std::move(obj_hir));
                debug::hir::log(debug::hir::Id::MethodCallLower, "String builtin toLowerCase()",
                                debug::Level::Debug);
                return std::make_unique<HirExpr>(std::move(hir), ast::make_string());
            }
            if (mem.member == "trim") {
                auto hir = std::make_unique<HirCall>();
                hir->func_name = "__builtin_string_trim";
                hir->args.push_back(std::move(obj_hir));
                debug::hir::log(debug::hir::Id::MethodCallLower, "String builtin trim()",
                                debug::Level::Debug);
                return std::make_unique<HirExpr>(std::move(hir), ast::make_string());
            }
            if (mem.member == "startsWith") {
                auto hir = std::make_unique<HirCall>();
                hir->func_name = "__builtin_string_startsWith";
                hir->args.push_back(std::move(obj_hir));
                for (auto& arg : mem.args) {
                    hir->args.push_back(lower_expr(*arg));
                }
                debug::hir::log(debug::hir::Id::MethodCallLower, "String builtin startsWith()",
                                debug::Level::Debug);
                return std::make_unique<HirExpr>(std::move(hir), ast::make_bool());
            }
            if (mem.member == "endsWith") {
                auto hir = std::make_unique<HirCall>();
                hir->func_name = "__builtin_string_endsWith";
                hir->args.push_back(std::move(obj_hir));
                for (auto& arg : mem.args) {
                    hir->args.push_back(lower_expr(*arg));
                }
                debug::hir::log(debug::hir::Id::MethodCallLower, "String builtin endsWith()",
                                debug::Level::Debug);
                return std::make_unique<HirExpr>(std::move(hir), ast::make_bool());
            }
            if (mem.member == "includes" || mem.member == "contains") {
                auto hir = std::make_unique<HirCall>();
                hir->func_name = "__builtin_string_includes";
                hir->args.push_back(std::move(obj_hir));
                for (auto& arg : mem.args) {
                    hir->args.push_back(lower_expr(*arg));
                }
                debug::hir::log(debug::hir::Id::MethodCallLower, "String builtin includes()",
                                debug::Level::Debug);
                return std::make_unique<HirExpr>(std::move(hir), ast::make_bool());
            }
            if (mem.member == "repeat") {
                auto hir = std::make_unique<HirCall>();
                hir->func_name = "__builtin_string_repeat";
                hir->args.push_back(std::move(obj_hir));
                for (auto& arg : mem.args) {
                    hir->args.push_back(lower_expr(*arg));
                }
                debug::hir::log(debug::hir::Id::MethodCallLower, "String builtin repeat()",
                                debug::Level::Debug);
                return std::make_unique<HirExpr>(std::move(hir), ast::make_string());
            }
            if (mem.member == "replace") {
                auto hir = std::make_unique<HirCall>();
                hir->func_name = "__builtin_string_replace";
                hir->args.push_back(std::move(obj_hir));
                for (auto& arg : mem.args) {
                    hir->args.push_back(lower_expr(*arg));
                }
                debug::hir::log(debug::hir::Id::MethodCallLower, "String builtin replace()",
                                debug::Level::Debug);
                return std::make_unique<HirExpr>(std::move(hir), ast::make_string());
            }
            if (mem.member == "first") {
                auto hir = std::make_unique<HirCall>();
                hir->func_name = "__builtin_string_first";
                hir->args.push_back(std::move(obj_hir));
                debug::hir::log(debug::hir::Id::MethodCallLower, "String builtin first()",
                                debug::Level::Debug);
                return std::make_unique<HirExpr>(std::move(hir), ast::make_char());
            }
            if (mem.member == "last") {
                auto hir = std::make_unique<HirCall>();
                hir->func_name = "__builtin_string_last";
                hir->args.push_back(std::move(obj_hir));
                debug::hir::log(debug::hir::Id::MethodCallLower, "String builtin last()",
                                debug::Level::Debug);
                return std::make_unique<HirExpr>(std::move(hir), ast::make_char());
            }
        }

        // 名前空間を除去した型名を取得
        std::string method_type_name = type_name;
        size_t last_colon = type_name.rfind("::");
        if (last_colon != std::string::npos) {
            method_type_name = type_name.substr(last_colon + 2);
        }

        // ジェネリック型名（例：Vector<int>）をマングリング形式（例：Vector__int）に変換
        // Struct<T1, T2, ...> -> Struct__T1__T2__...
        size_t angle_pos = method_type_name.find('<');
        if (angle_pos != std::string::npos) {
            size_t close_pos = method_type_name.rfind('>');
            if (close_pos != std::string::npos && close_pos > angle_pos) {
                std::string base_name = method_type_name.substr(0, angle_pos);
                std::string type_args_str =
                    method_type_name.substr(angle_pos + 1, close_pos - angle_pos - 1);

                // 型引数を抽出（カンマ区切り、ネストを考慮）
                std::vector<std::string> type_args;
                int depth = 0;
                std::string current_arg;
                for (char c : type_args_str) {
                    if (c == '<') {
                        depth++;
                        current_arg += c;
                    } else if (c == '>') {
                        depth--;
                        current_arg += c;
                    } else if (c == ',' && depth == 0) {
                        // 空白を削除
                        while (!current_arg.empty() && current_arg.front() == ' ') {
                            current_arg = current_arg.substr(1);
                        }
                        while (!current_arg.empty() && current_arg.back() == ' ') {
                            current_arg.pop_back();
                        }
                        type_args.push_back(current_arg);
                        current_arg.clear();
                    } else {
                        current_arg += c;
                    }
                }
                // 最後の引数を追加
                while (!current_arg.empty() && current_arg.front() == ' ') {
                    current_arg = current_arg.substr(1);
                }
                while (!current_arg.empty() && current_arg.back() == ' ') {
                    current_arg.pop_back();
                }
                if (!current_arg.empty()) {
                    type_args.push_back(current_arg);
                }

                // マングリング名を生成：BaseType__Arg1__Arg2__...
                method_type_name = base_name;
                for (const auto& arg : type_args) {
                    method_type_name += "__" + arg;
                }

                debug::hir::log(
                    debug::hir::Id::MethodCallLower,
                    "Generic type name mangled: " + type_name + " -> " + method_type_name,
                    debug::Level::Debug);
            }
        }

        // 固定長配列（T[N]）の場合、スライス型名（T[]）にマッピング
        // impl int[] for Interface のメソッドを int[5], int[10] 等からも呼び出し可能にする
        bool needs_array_to_slice = false;
        if (obj_type && obj_type->kind == ast::TypeKind::Array &&
            obj_type->array_size.has_value()) {
            if (obj_type->element_type) {
                method_type_name = ast::type_to_string(*obj_type->element_type) + "[]";
                needs_array_to_slice = true;
                debug::hir::log(
                    debug::hir::Id::MethodCallLower,
                    "Fixed-size array -> slice impl: " + type_name + " -> " + method_type_name,
                    debug::Level::Debug);
            }
        }

        auto hir = std::make_unique<HirCall>();
        hir->func_name = method_type_name + "__" + mem.member;

        // 固定長配列→スライス変換が必要な場合、cm_array_to_sliceで変換
        if (needs_array_to_slice && obj_type->element_type) {
            // cm_array_to_slice(ptr, len, elem_size) を呼び出してスライスを作成
            auto convert_call = std::make_unique<HirCall>();
            convert_call->func_name = "cm_array_to_slice";

            // 配列のアドレスを取得
            auto addr_op = std::make_unique<HirUnary>();
            addr_op->op = HirUnaryOp::AddrOf;
            addr_op->operand = std::move(obj_hir);
            auto ptr_type = ast::make_pointer(obj_type->element_type);
            convert_call->args.push_back(std::make_unique<HirExpr>(std::move(addr_op), ptr_type));

            // 配列サイズ（コンパイル時定数）
            auto size_lit = std::make_unique<HirLiteral>();
            size_lit->value = static_cast<int64_t>(obj_type->array_size.value_or(0));
            convert_call->args.push_back(
                std::make_unique<HirExpr>(std::move(size_lit), ast::make_long()));

            // 要素サイズを計算
            int64_t elem_size = 4;  // デフォルトはint
            auto elem_kind = obj_type->element_type->kind;
            if (elem_kind == ast::TypeKind::Char || elem_kind == ast::TypeKind::Bool) {
                elem_size = 1;
            } else if (elem_kind == ast::TypeKind::Long || elem_kind == ast::TypeKind::ULong ||
                       elem_kind == ast::TypeKind::Double) {
                elem_size = 8;
            } else if (elem_kind == ast::TypeKind::Pointer || elem_kind == ast::TypeKind::String) {
                elem_size = 8;
            }
            auto elem_size_lit = std::make_unique<HirLiteral>();
            elem_size_lit->value = elem_size;
            convert_call->args.push_back(
                std::make_unique<HirExpr>(std::move(elem_size_lit), ast::make_long()));

            // 変換結果をself引数として渡す
            auto slice_type = ast::make_array(obj_type->element_type, std::nullopt);
            hir->args.push_back(std::make_unique<HirExpr>(std::move(convert_call), slice_type));
        } else {
            hir->args.push_back(std::move(obj_hir));
        }

        for (auto& arg : mem.args) {
            hir->args.push_back(lower_expr(*arg));
        }

        return std::make_unique<HirExpr>(std::move(hir), type);
    }

    // 通常のフィールドアクセス
    debug::hir::log(debug::hir::Id::FieldAccessLower, "", debug::Level::Debug);
    auto hir = std::make_unique<HirMember>();
    hir->object = lower_expr(*mem.object);
    hir->member = mem.member;
    debug::hir::log(debug::hir::Id::FieldName, "field: " + mem.member, debug::Level::Trace);
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

// match式のlowering
// v0.13.0: 両方の形式をサポート:
//   - 式形式: pattern => expr (三項演算子チェーンに変換)
//   - ブロック形式: 文として処理されるのでここでは呼ばれない
HirExprPtr HirLowering::lower_match(ast::MatchExpr& match, TypePtr type) {
    // 全てのarmが式形式かチェック
    bool all_expr_form = true;
    for (auto& arm : match.arms) {
        if (arm.is_block_form) {
            all_expr_form = false;
            break;
        }
    }

    // ブロック形式が混在している場合は警告してダミー値を返す
    if (!all_expr_form) {
        debug::hir::log(debug::hir::Id::Warning,
                        "match with block arms should be used as statement", debug::Level::Warn);
        auto lit = std::make_unique<HirLiteral>();
        lit->value = int64_t{0};
        return std::make_unique<HirExpr>(std::move(lit), ast::make_void());
    }

    // 式形式: 三項演算子のチェーンに変換
    auto scrutinee = lower_expr(*match.scrutinee);
    auto scrutinee_type = match.scrutinee->type;

    // scrutinee_type->nameが空の場合、パターンのenum_variantからenum名を抽出
    std::string original_enum_name;
    if (scrutinee_type && !scrutinee_type->name.empty()) {
        original_enum_name = scrutinee_type->name;
    } else {
        for (const auto& arm : match.arms) {
            if (arm.pattern &&
                (arm.pattern->kind == ast::MatchPatternKind::EnumVariant ||
                 arm.pattern->kind == ast::MatchPatternKind::EnumVariantWithBinding)) {
                const std::string& variant_name = arm.pattern->enum_variant;
                auto sep = variant_name.rfind("::");
                if (sep != std::string::npos) {
                    original_enum_name = variant_name.substr(0, sep);
                    break;
                }
            }
        }
    }

    // armsを逆順でネストされた三項演算子に変換
    HirExprPtr result = nullptr;

    // デフォルト値（ガードなしワイルドカード/Variablearmがあればそれ、なければ0）
    for (auto& arm : match.arms) {
        // ガード条件がある場合はデフォルトとして扱わない
        if ((arm.pattern->kind == ast::MatchPatternKind::Wildcard ||
             arm.pattern->kind == ast::MatchPatternKind::Variable) &&
            !arm.guard) {
            if (arm.expr_body) {
                result = lower_expr(*arm.expr_body);
            }
            break;
        }
    }

    if (!result) {
        result = make_default_value(type);
    }

    // 非ワイルドカードarmを逆順に処理してネスト
    for (auto it = match.arms.rbegin(); it != match.arms.rend(); ++it) {
        auto& arm = *it;

        // ワイルドカードはスキップ（デフォルト値として既に処理済み）
        // ただし、ガード条件がある場合はスキップしない
        if ((arm.pattern->kind == ast::MatchPatternKind::Wildcard ||
             arm.pattern->kind == ast::MatchPatternKind::Variable) &&
            !arm.guard) {
            continue;
        }

        // 条件を構築
        HirExprPtr cond;

        if (arm.pattern->kind == ast::MatchPatternKind::Variable && arm.guard) {
            // 変数バインディング+ガードの場合：ガード条件のみを評価
            // 変数をscrutineeで置換
            std::string var_name = arm.pattern->var_name;
            cond = lower_guard_with_binding(*arm.guard, var_name, scrutinee, scrutinee_type);
        } else {
            cond = build_match_condition(scrutinee, scrutinee_type, arm);

            // ガード条件がある場合は論理AND
            if (arm.guard) {
                // パターンがenum variant + bindingの場合、ガード内の変数を置換
                HirExprPtr guard;
                if (arm.pattern->kind == ast::MatchPatternKind::EnumVariantWithBinding &&
                    !arm.pattern->binding_name.empty()) {
                    // バインディング変数をペイロード値で置換
                    // ペイロード型を取得
                    TypePtr payload_type = scrutinee_type;
                    std::string variant_name = arm.pattern->enum_variant;
                    if (!original_enum_name.empty()) {
                        auto enum_it = enum_defs_.find(original_enum_name);
                        if (enum_it != enum_defs_.end() && enum_it->second) {
                            auto sep = variant_name.rfind("::");
                            std::string short_variant = (sep != std::string::npos)
                                                            ? variant_name.substr(sep + 2)
                                                            : variant_name;
                            for (const auto& member : enum_it->second->members) {
                                if (member.name == short_variant && !member.fields.empty()) {
                                    payload_type = member.fields[0].second;
                                    break;
                                }
                            }
                        }
                    }
                    // ペイロード抽出式を生成
                    auto payload_extract = std::make_unique<HirEnumPayload>();
                    payload_extract->scrutinee = clone_hir_expr(scrutinee);
                    payload_extract->variant_name = variant_name;
                    payload_extract->payload_type = payload_type;
                    auto payload_expr =
                        std::make_unique<HirExpr>(std::move(payload_extract), payload_type);
                    // ガード内のバインディング変数をペイロード式で置換
                    guard = lower_guard_with_binding(*arm.guard, arm.pattern->binding_name,
                                                     payload_expr, payload_type);
                } else {
                    guard = lower_expr(*arm.guard);
                }
                auto and_cond = std::make_unique<HirBinary>();
                and_cond->op = HirBinaryOp::And;
                and_cond->lhs = std::move(cond);
                and_cond->rhs = std::move(guard);
                cond = std::make_unique<HirExpr>(std::move(and_cond),
                                                 std::make_shared<ast::Type>(ast::TypeKind::Bool));
            }
        }

        // このarmの値
        HirExprPtr arm_value;
        if (arm.expr_body) {
            // EnumVariantWithBindingパターンの場合、バインディング変数をペイロード式で置換
            if (arm.pattern->kind == ast::MatchPatternKind::EnumVariantWithBinding &&
                !arm.pattern->binding_name.empty()) {
                // ペイロード型を取得
                TypePtr payload_type = scrutinee_type;
                std::string variant_name = arm.pattern->enum_variant;
                if (!original_enum_name.empty()) {
                    auto enum_it = enum_defs_.find(original_enum_name);
                    if (enum_it != enum_defs_.end() && enum_it->second) {
                        auto sep = variant_name.rfind("::");
                        std::string short_variant = (sep != std::string::npos)
                                                        ? variant_name.substr(sep + 2)
                                                        : variant_name;
                        for (const auto& member : enum_it->second->members) {
                            if (member.name == short_variant && !member.fields.empty()) {
                                payload_type = member.fields[0].second;
                                break;
                            }
                        }
                    }
                }
                // ペイロード抽出式を生成
                auto payload_extract = std::make_unique<HirEnumPayload>();
                payload_extract->scrutinee = clone_hir_expr(scrutinee);
                payload_extract->variant_name = variant_name;
                payload_extract->payload_type = payload_type;
                auto payload_expr =
                    std::make_unique<HirExpr>(std::move(payload_extract), payload_type);

                // arm.expr_body内のバインディング変数をペイロード式で置換
                arm_value = lower_guard_with_binding(*arm.expr_body, arm.pattern->binding_name,
                                                     payload_expr, payload_type);
            } else {
                arm_value = lower_expr(*arm.expr_body);
            }
        } else {
            arm_value = make_default_value(type);
        }

        // 三項演算子を構築
        auto ternary = std::make_unique<HirTernary>();
        ternary->condition = std::move(cond);
        ternary->then_expr = std::move(arm_value);
        ternary->else_expr = std::move(result);
        result = std::make_unique<HirExpr>(std::move(ternary), type);
    }

    return result;
}

// 型に応じたデフォルト値を生成
HirExprPtr HirLowering::make_default_value(TypePtr type) {
    auto lit = std::make_unique<HirLiteral>();

    if (type && type->kind == ast::TypeKind::String) {
        lit->value = std::string("");
    } else if (type && type->kind == ast::TypeKind::Bool) {
        lit->value = false;
    } else if (type &&
               (type->kind == ast::TypeKind::Float || type->kind == ast::TypeKind::Double)) {
        lit->value = 0.0;
    } else if (type && type->kind == ast::TypeKind::Char) {
        lit->value = '\0';
    } else {
        lit->value = int64_t{0};
    }

    return std::make_unique<HirExpr>(std::move(lit), type);
}

// matchパターンの条件式を構築（単一パターン用ヘルパー）
HirExprPtr HirLowering::build_single_pattern_condition(const HirExprPtr& scrutinee,
                                                       const ast::MatchPattern& pattern) {
    HirExprPtr scrutinee_copy = clone_hir_expr(scrutinee);

    switch (pattern.kind) {
        case ast::MatchPatternKind::Literal: {
            auto pattern_value = lower_expr(*pattern.value);
            auto cond = std::make_unique<HirBinary>();
            cond->op = HirBinaryOp::Eq;
            cond->lhs = std::move(scrutinee_copy);
            cond->rhs = std::move(pattern_value);
            return std::make_unique<HirExpr>(std::move(cond),
                                             std::make_shared<ast::Type>(ast::TypeKind::Bool));
        }

        case ast::MatchPatternKind::EnumVariant: {
            // scrutineeがint型（単純enum）の場合は直接値比較
            // scrutineeがTagged Union型の場合は__tagアクセス
            auto pattern_value = lower_expr(*pattern.value);
            HirExprPtr lhs_expr;

            // パターン値からenum名を抽出してassociated dataを持つかチェック
            bool is_tagged_union = false;
            if (pattern.value) {
                if (auto* member = pattern.value->as<ast::MemberExpr>()) {
                    // MemberExpr形式: Enum.Variant
                    if (auto* obj = member->object->as<ast::IdentExpr>()) {
                        std::string enum_name = obj->name;
                        auto it = enum_defs_.find(enum_name);
                        if (it != enum_defs_.end() && it->second) {
                            // enumがassociated dataを持つかチェック
                            for (const auto& m : it->second->members) {
                                if (!m.fields.empty()) {
                                    is_tagged_union = true;
                                    break;
                                }
                            }
                        }
                    }
                } else if (auto* ident = pattern.value->as<ast::IdentExpr>()) {
                    // IdentExpr形式: EnumName::Variant（::を含む場合）
                    std::string full_name = ident->name;
                    size_t pos = full_name.find("::");
                    if (pos != std::string::npos) {
                        std::string enum_name = full_name.substr(0, pos);
                        auto it = enum_defs_.find(enum_name);
                        if (it != enum_defs_.end() && it->second) {
                            // enumがassociated dataを持つかチェック
                            for (const auto& m : it->second->members) {
                                if (!m.fields.empty()) {
                                    is_tagged_union = true;
                                    break;
                                }
                            }
                        }
                    }
                }
            }

            if (is_tagged_union) {
                // Tagged Union: scrutinee.tag (field[0]) を抽出して比較
                auto tag_access = std::make_unique<HirMember>();
                tag_access->object = std::move(scrutinee_copy);
                tag_access->member = "__tag";  // tagフィールド
                lhs_expr = std::make_unique<HirExpr>(std::move(tag_access), make_int());
            } else {
                // 単純なenum（int型）: 直接比較
                lhs_expr = std::move(scrutinee_copy);
            }

            auto cond = std::make_unique<HirBinary>();
            cond->op = HirBinaryOp::Eq;
            cond->lhs = std::move(lhs_expr);
            cond->rhs = std::move(pattern_value);
            return std::make_unique<HirExpr>(std::move(cond),
                                             std::make_shared<ast::Type>(ast::TypeKind::Bool));
        }

        case ast::MatchPatternKind::EnumVariantWithBinding: {
            // Tagged Union: scrutinee.tag (field[0]) を抽出して比較
            auto tag_access = std::make_unique<HirMember>();
            tag_access->object = std::move(scrutinee_copy);
            tag_access->member = "__tag";  // tagフィールド
            auto tag_expr = std::make_unique<HirExpr>(std::move(tag_access), make_int());

            auto enum_variant_ident = ast::make_ident(pattern.enum_variant, {});
            auto pattern_value = lower_expr(*enum_variant_ident);
            auto cond = std::make_unique<HirBinary>();
            cond->op = HirBinaryOp::Eq;
            cond->lhs = std::move(tag_expr);
            cond->rhs = std::move(pattern_value);
            return std::make_unique<HirExpr>(std::move(cond),
                                             std::make_shared<ast::Type>(ast::TypeKind::Bool));
        }

        case ast::MatchPatternKind::Variable:
        case ast::MatchPatternKind::Wildcard: {
            auto lit = std::make_unique<HirLiteral>();
            lit->value = true;
            return std::make_unique<HirExpr>(std::move(lit),
                                             std::make_shared<ast::Type>(ast::TypeKind::Bool));
        }

        case ast::MatchPatternKind::Range: {
            // 範囲パターン: start <= scrutinee && scrutinee <= end
            auto start_val = lower_expr(*pattern.range_start);
            auto end_val = lower_expr(*pattern.range_end);
            auto scrutinee_copy2 = clone_hir_expr(scrutinee);

            // scrutinee >= start
            auto ge_cond = std::make_unique<HirBinary>();
            ge_cond->op = HirBinaryOp::Ge;
            ge_cond->lhs = std::move(scrutinee_copy);
            ge_cond->rhs = std::move(start_val);
            auto ge_expr = std::make_unique<HirExpr>(
                std::move(ge_cond), std::make_shared<ast::Type>(ast::TypeKind::Bool));

            // scrutinee <= end
            auto le_cond = std::make_unique<HirBinary>();
            le_cond->op = HirBinaryOp::Le;
            le_cond->lhs = std::move(scrutinee_copy2);
            le_cond->rhs = std::move(end_val);
            auto le_expr = std::make_unique<HirExpr>(
                std::move(le_cond), std::make_shared<ast::Type>(ast::TypeKind::Bool));

            // ge && le
            auto and_cond = std::make_unique<HirBinary>();
            and_cond->op = HirBinaryOp::And;
            and_cond->lhs = std::move(ge_expr);
            and_cond->rhs = std::move(le_expr);
            return std::make_unique<HirExpr>(std::move(and_cond),
                                             std::make_shared<ast::Type>(ast::TypeKind::Bool));
        }

        case ast::MatchPatternKind::Or: {
            // 再帰的にORパターンを処理
            if (pattern.or_patterns.empty()) {
                auto lit = std::make_unique<HirLiteral>();
                lit->value = false;
                return std::make_unique<HirExpr>(std::move(lit),
                                                 std::make_shared<ast::Type>(ast::TypeKind::Bool));
            }

            // 最初のパターンの条件を構築
            HirExprPtr result = build_single_pattern_condition(scrutinee, *pattern.or_patterns[0]);

            // 残りのパターンをOR結合
            for (size_t i = 1; i < pattern.or_patterns.size(); ++i) {
                auto next_cond = build_single_pattern_condition(scrutinee, *pattern.or_patterns[i]);

                auto or_cond = std::make_unique<HirBinary>();
                or_cond->op = HirBinaryOp::Or;
                or_cond->lhs = std::move(result);
                or_cond->rhs = std::move(next_cond);
                result = std::make_unique<HirExpr>(
                    std::move(or_cond), std::make_shared<ast::Type>(ast::TypeKind::Bool));
            }

            return result;
        }
    }

    auto lit = std::make_unique<HirLiteral>();
    lit->value = false;
    return std::make_unique<HirExpr>(std::move(lit),
                                     std::make_shared<ast::Type>(ast::TypeKind::Bool));
}

// matchパターンの条件式を構築
HirExprPtr HirLowering::build_match_condition(const HirExprPtr& scrutinee,
                                              TypePtr /*scrutinee_type*/,
                                              const ast::MatchArm& arm) {
    return build_single_pattern_condition(scrutinee, *arm.pattern);
}

// HIR式の簡易クローン
HirExprPtr HirLowering::clone_hir_expr(const HirExprPtr& expr) {
    if (!expr)
        return nullptr;

    if (auto* var = std::get_if<std::unique_ptr<HirVarRef>>(&expr->kind)) {
        auto clone = std::make_unique<HirVarRef>();
        clone->name = (*var)->name;
        return std::make_unique<HirExpr>(std::move(clone), expr->type, expr->span);
    }

    if (auto* lit = std::get_if<std::unique_ptr<HirLiteral>>(&expr->kind)) {
        auto clone = std::make_unique<HirLiteral>();
        clone->value = (*lit)->value;
        return std::make_unique<HirExpr>(std::move(clone), expr->type, expr->span);
    }

    if (auto* member = std::get_if<std::unique_ptr<HirMember>>(&expr->kind)) {
        auto clone = std::make_unique<HirMember>();
        clone->object = clone_hir_expr((*member)->object);
        clone->member = (*member)->member;
        return std::make_unique<HirExpr>(std::move(clone), expr->type, expr->span);
    }

    if (auto* binary = std::get_if<std::unique_ptr<HirBinary>>(&expr->kind)) {
        auto clone = std::make_unique<HirBinary>();
        clone->op = (*binary)->op;
        clone->lhs = clone_hir_expr((*binary)->lhs);
        clone->rhs = clone_hir_expr((*binary)->rhs);
        return std::make_unique<HirExpr>(std::move(clone), expr->type, expr->span);
    }

    if (auto* unary = std::get_if<std::unique_ptr<HirUnary>>(&expr->kind)) {
        auto clone = std::make_unique<HirUnary>();
        clone->op = (*unary)->op;
        clone->operand = clone_hir_expr((*unary)->operand);
        return std::make_unique<HirExpr>(std::move(clone), expr->type, expr->span);
    }

    if (auto* index = std::get_if<std::unique_ptr<HirIndex>>(&expr->kind)) {
        auto clone = std::make_unique<HirIndex>();
        clone->object = clone_hir_expr((*index)->object);
        clone->index = clone_hir_expr((*index)->index);
        return std::make_unique<HirExpr>(std::move(clone), expr->type, expr->span);
    }

    // HirEnumPayloadのクローン
    if (auto* payload = std::get_if<std::unique_ptr<HirEnumPayload>>(&expr->kind)) {
        auto clone = std::make_unique<HirEnumPayload>();
        clone->scrutinee = clone_hir_expr((*payload)->scrutinee);
        clone->variant_name = (*payload)->variant_name;
        clone->payload_type = (*payload)->payload_type;
        return std::make_unique<HirExpr>(std::move(clone), expr->type, expr->span);
    }

    debug::hir::log(debug::hir::Id::Warning, "Complex expression cloning not fully supported",
                    debug::Level::Warn);

    auto clone = std::make_unique<HirLiteral>();
    clone->value = int64_t{0};
    return std::make_unique<HirExpr>(std::move(clone), expr->type, expr->span);
}

// ガード式内の変数束縛をscrutineeで置換してlower
HirExprPtr HirLowering::lower_guard_with_binding(ast::Expr& guard, const std::string& var_name,
                                                 const HirExprPtr& scrutinee,
                                                 TypePtr scrutinee_type) {
    if (auto* ident = guard.as<ast::IdentExpr>()) {
        if (ident->name == var_name) {
            return clone_hir_expr(scrutinee);
        }
    }

    if (auto* binary = guard.as<ast::BinaryExpr>()) {
        auto left = lower_guard_with_binding(*binary->left, var_name, scrutinee, scrutinee_type);
        auto right = lower_guard_with_binding(*binary->right, var_name, scrutinee, scrutinee_type);

        auto hir = std::make_unique<HirBinary>();
        hir->op = convert_binary_op(binary->op);
        hir->lhs = std::move(left);
        hir->rhs = std::move(right);

        TypePtr result_type;
        if (is_comparison_op(binary->op)) {
            result_type = std::make_shared<ast::Type>(ast::TypeKind::Bool);
        } else {
            result_type = left ? left->type : scrutinee_type;
        }

        return std::make_unique<HirExpr>(std::move(hir), result_type);
    }

    if (auto* unary = guard.as<ast::UnaryExpr>()) {
        auto operand =
            lower_guard_with_binding(*unary->operand, var_name, scrutinee, scrutinee_type);

        auto hir = std::make_unique<HirUnary>();
        hir->op = convert_unary_op(unary->op);
        hir->operand = std::move(operand);

        TypePtr result_type;
        if (unary->op == ast::UnaryOp::Not) {
            result_type = std::make_shared<ast::Type>(ast::TypeKind::Bool);
        } else {
            result_type = hir->operand ? hir->operand->type : scrutinee_type;
        }

        return std::make_unique<HirExpr>(std::move(hir), result_type);
    }

    return lower_expr(guard);
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
