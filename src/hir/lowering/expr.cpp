// lowering_expr.cpp - 式のlowering
#include "fwd.hpp"

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
                        "alignof(" + type_name + ") = " + std::to_string(alignment), debug::Level::Debug);
        auto lit = std::make_unique<HirLiteral>();
        lit->value = alignment;
        return std::make_unique<HirExpr>(std::move(lit), ast::make_uint());
    } else if (auto* typename_expr = expr.as<ast::TypenameOfExpr>()) {
        // typename(T) - 型の名前を文字列として返す
        std::string type_name;
        if (typename_expr->target_type) {
            type_name = ast::type_to_string(*typename_expr->target_type);
        }
        debug::hir::log(debug::hir::Id::LiteralLower,
                        "typename = \"" + type_name + "\"", debug::Level::Debug);
        auto lit = std::make_unique<HirLiteral>();
        lit->value = type_name;
        return std::make_unique<HirExpr>(std::move(lit), ast::make_string());
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

        static const std::set<std::string> builtin_funcs = {"printf",
                                                            "__println__", "__print__",
                                                            "sprintf", "exit",  "panic"};

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

    auto obj_hir = lower_expr(*idx.object);
    TypePtr obj_type = obj_hir->type;

    // 文字列インデックスの場合
    if (obj_type && obj_type->kind == ast::TypeKind::String) {
        debug::hir::log(debug::hir::Id::IndexLower, "String index access", debug::Level::Debug);
        auto hir = std::make_unique<HirCall>();
        hir->func_name = "__builtin_string_charAt";
        hir->args.push_back(std::move(obj_hir));
        hir->args.push_back(lower_expr(*idx.index));
        return std::make_unique<HirExpr>(std::move(hir), ast::make_char());
    }

    // 通常の配列/ポインタインデックス
    auto hir = std::make_unique<HirIndex>();
    debug::hir::log(debug::hir::Id::IndexBase, "Evaluating base", debug::Level::Trace);
    hir->object = std::move(obj_hir);
    debug::hir::log(debug::hir::Id::IndexValue, "Evaluating index", debug::Level::Trace);
    hir->index = lower_expr(*idx.index);
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
            auto neg_one = std::make_unique<HirLiteral>();
            neg_one->value = int64_t{-1};
            hir->args.push_back(std::make_unique<HirExpr>(std::move(neg_one), ast::make_int()));
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
            type_name = ast::type_to_string(*obj_hir->type);
        } else if (mem.object->type) {
            obj_type = mem.object->type;
            type_name = ast::type_to_string(*mem.object->type);
        }

        // 配列のビルトインメソッド処理
        if (obj_type && obj_type->kind == ast::TypeKind::Array) {
            if (mem.member == "size" || mem.member == "len" || mem.member == "length") {
                auto lit = std::make_unique<HirLiteral>();
                if (obj_type->array_size.has_value()) {
                    lit->value = static_cast<int64_t>(obj_type->array_size.value());
                } else {
                    lit->value = int64_t{0};
                }
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
                hir->func_name = "__builtin_array_reduce";
                hir->args.push_back(std::move(obj_hir));
                auto size_lit = std::make_unique<HirLiteral>();
                size_lit->value = static_cast<int64_t>(obj_type->array_size.value_or(0));
                hir->args.push_back(
                    std::make_unique<HirExpr>(std::move(size_lit), ast::make_int()));
                for (auto& arg : mem.args) {
                    hir->args.push_back(lower_expr(*arg));
                }
                debug::hir::log(debug::hir::Id::MethodCallLower, "Array builtin reduce()",
                                debug::Level::Debug);
                return std::make_unique<HirExpr>(std::move(hir), ast::make_int());
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
        }

        // 名前空間を除去した型名を取得
        std::string method_type_name = type_name;
        size_t last_colon = type_name.rfind("::");
        if (last_colon != std::string::npos) {
            method_type_name = type_name.substr(last_colon + 2);
        }

        auto hir = std::make_unique<HirCall>();
        hir->func_name = method_type_name + "__" + mem.member;

        hir->args.push_back(std::move(obj_hir));

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

// match式
HirExprPtr HirLowering::lower_match(ast::MatchExpr& match, TypePtr type) {
    debug::hir::log(debug::hir::Id::LiteralLower, "Lowering match expression", debug::Level::Debug);

    auto scrutinee = lower_expr(*match.scrutinee);
    auto scrutinee_type = scrutinee->type;

    if (match.arms.empty()) {
        auto lit = std::make_unique<HirLiteral>();
        lit->value = int64_t{0};
        return std::make_unique<HirExpr>(std::move(lit), type);
    }

    HirExprPtr result = nullptr;
    for (int i = static_cast<int>(match.arms.size()) - 1; i >= 0; i--) {
        auto& arm = match.arms[i];
        auto body = lower_expr(*arm.body);

        if (arm.pattern->kind == ast::MatchPatternKind::Wildcard) {
            if (result == nullptr) {
                result = std::move(body);
            } else {
                debug::hir::log(debug::hir::Id::Warning, "Wildcard pattern should be last",
                                debug::Level::Warn);
            }
        } else {
            auto cond = build_match_condition(scrutinee, scrutinee_type, arm);

            if (arm.guard) {
                HirExprPtr guard_cond;

                if (arm.pattern->kind == ast::MatchPatternKind::Variable &&
                    !arm.pattern->var_name.empty()) {
                    guard_cond = lower_guard_with_binding(*arm.guard, arm.pattern->var_name,
                                                          scrutinee, scrutinee_type);
                } else {
                    guard_cond = lower_expr(*arm.guard);
                }

                auto combined = std::make_unique<HirBinary>();
                combined->op = HirBinaryOp::And;
                combined->lhs = std::move(cond);
                combined->rhs = std::move(guard_cond);
                cond = std::make_unique<HirExpr>(std::move(combined),
                                                 std::make_shared<ast::Type>(ast::TypeKind::Bool));
            }

            auto ternary = std::make_unique<HirTernary>();
            ternary->condition = std::move(cond);
            ternary->then_expr = std::move(body);

            if (result) {
                ternary->else_expr = std::move(result);
            } else {
                ternary->else_expr = make_default_value(type);
            }

            result = std::make_unique<HirExpr>(std::move(ternary), type);
        }
    }

    if (!result) {
        result = make_default_value(type);
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

// matchパターンの条件式を構築
HirExprPtr HirLowering::build_match_condition(const HirExprPtr& scrutinee,
                                              TypePtr /*scrutinee_type*/,
                                              const ast::MatchArm& arm) {
    HirExprPtr scrutinee_copy = clone_hir_expr(scrutinee);

    switch (arm.pattern->kind) {
        case ast::MatchPatternKind::Literal:
        case ast::MatchPatternKind::EnumVariant: {
            auto pattern_value = lower_expr(*arm.pattern->value);
            auto cond = std::make_unique<HirBinary>();
            cond->op = HirBinaryOp::Eq;
            cond->lhs = std::move(scrutinee_copy);
            cond->rhs = std::move(pattern_value);
            return std::make_unique<HirExpr>(std::move(cond),
                                             std::make_shared<ast::Type>(ast::TypeKind::Bool));
        }

        case ast::MatchPatternKind::Variable: {
            auto lit = std::make_unique<HirLiteral>();
            lit->value = true;
            return std::make_unique<HirExpr>(std::move(lit),
                                             std::make_shared<ast::Type>(ast::TypeKind::Bool));
        }

        case ast::MatchPatternKind::Wildcard: {
            auto lit = std::make_unique<HirLiteral>();
            lit->value = true;
            return std::make_unique<HirExpr>(std::move(lit),
                                             std::make_shared<ast::Type>(ast::TypeKind::Bool));
        }
    }

    auto lit = std::make_unique<HirLiteral>();
    lit->value = false;
    return std::make_unique<HirExpr>(std::move(lit),
                                     std::make_shared<ast::Type>(ast::TypeKind::Bool));
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

}  // namespace cm::hir
