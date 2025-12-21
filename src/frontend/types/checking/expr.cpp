// ============================================================
// TypeChecker 実装 - 式の型推論
// ============================================================

#include "../type_checker.hpp"

namespace cm {

ast::TypePtr TypeChecker::infer_type(ast::Expr& expr) {
    debug::tc::log(debug::tc::Id::CheckExpr, "", debug::Level::Trace);

    ast::TypePtr inferred_type;

    if (auto* lit = expr.as<ast::LiteralExpr>()) {
        inferred_type = infer_literal(*lit);
    } else if (auto* ident = expr.as<ast::IdentExpr>()) {
        inferred_type = infer_ident(*ident);
    } else if (auto* binary = expr.as<ast::BinaryExpr>()) {
        inferred_type = infer_binary(*binary);
    } else if (auto* unary = expr.as<ast::UnaryExpr>()) {
        inferred_type = infer_unary(*unary);
    } else if (auto* call = expr.as<ast::CallExpr>()) {
        inferred_type = infer_call(*call);
    } else if (auto* member = expr.as<ast::MemberExpr>()) {
        inferred_type = infer_member(*member);
    } else if (auto* ternary = expr.as<ast::TernaryExpr>()) {
        inferred_type = infer_ternary(*ternary);
    } else if (auto* idx = expr.as<ast::IndexExpr>()) {
        inferred_type = infer_index(*idx);
    } else if (auto* slice = expr.as<ast::SliceExpr>()) {
        inferred_type = infer_slice(*slice);
    } else if (auto* match_expr = expr.as<ast::MatchExpr>()) {
        inferred_type = infer_match(*match_expr);
    } else if (auto* array_lit = expr.as<ast::ArrayLiteralExpr>()) {
        inferred_type = infer_array_literal(*array_lit);
    } else if (auto* struct_lit = expr.as<ast::StructLiteralExpr>()) {
        inferred_type = infer_struct_literal(*struct_lit);
    } else {
        inferred_type = ast::make_error();
    }

    if (inferred_type && !expr.type) {
        expr.type = inferred_type;
    } else if (!expr.type) {
        expr.type = ast::make_error();
    }

    return expr.type;
}

ast::TypePtr TypeChecker::infer_literal(ast::LiteralExpr& lit) {
    if (lit.is_null())
        return ast::make_void();
    if (lit.is_bool())
        return std::make_shared<ast::Type>(ast::TypeKind::Bool);
    if (lit.is_int())
        return ast::make_int();
    if (lit.is_float())
        return ast::make_double();
    if (lit.is_char())
        return ast::make_char();
    if (lit.is_string())
        return ast::make_string();
    return ast::make_error();
}

ast::TypePtr TypeChecker::infer_array_literal(ast::ArrayLiteralExpr& lit) {
    if (lit.elements.empty()) {
        return ast::make_array(ast::make_int(), 0);
    }

    auto first_type = infer_type(*lit.elements[0]);

    for (size_t i = 1; i < lit.elements.size(); ++i) {
        auto elem_type = infer_type(*lit.elements[i]);
    }

    return ast::make_array(first_type, lit.elements.size());
}

ast::TypePtr TypeChecker::infer_struct_literal(ast::StructLiteralExpr& lit) {
    if (lit.type_name.empty()) {
        return ast::make_error();
    }

    auto struct_it = struct_defs_.find(lit.type_name);
    if (struct_it == struct_defs_.end()) {
        error(Span{}, "Unknown struct type: " + lit.type_name);
        return ast::make_error();
    }

    for (auto& field : lit.fields) {
        infer_type(*field.value);
    }

    auto type = std::make_shared<ast::Type>(ast::TypeKind::Struct);
    type->name = lit.type_name;
    return type;
}

ast::TypePtr TypeChecker::infer_ident(ast::IdentExpr& ident) {
    auto sym = scopes_.current().lookup(ident.name);
    if (!sym) {
        if (!current_impl_target_type_.empty()) {
            auto struct_it = struct_defs_.find(current_impl_target_type_);
            if (struct_it != struct_defs_.end()) {
                for (const auto& field : struct_it->second->fields) {
                    if (field.name == ident.name) {
                        debug::tc::log(debug::tc::Id::Resolved,
                                       "Implicit this field: " + ident.name, debug::Level::Debug);
                        return field.type;
                    }
                }
            }
        }
        error(Span{}, "Undefined variable '" + ident.name + "'");
        return ast::make_error();
    }
    debug::tc::log(debug::tc::Id::Resolved, ident.name + " : " + ast::type_to_string(*sym->type),
                   debug::Level::Trace);
    return sym->type;
}

ast::TypePtr TypeChecker::infer_binary(ast::BinaryExpr& binary) {
    auto ltype = infer_type(*binary.left);
    auto rtype = infer_type(*binary.right);

    if (!ltype || !rtype)
        return ast::make_error();

    switch (binary.op) {
        case ast::BinaryOp::Eq:
        case ast::BinaryOp::Ne:
        case ast::BinaryOp::Lt:
        case ast::BinaryOp::Gt:
        case ast::BinaryOp::Le:
        case ast::BinaryOp::Ge:
            return std::make_shared<ast::Type>(ast::TypeKind::Bool);

        case ast::BinaryOp::And:
        case ast::BinaryOp::Or:
            if (ltype->kind != ast::TypeKind::Bool || rtype->kind != ast::TypeKind::Bool) {
                error(Span{}, "Logical operators require bool operands");
            }
            return std::make_shared<ast::Type>(ast::TypeKind::Bool);

        case ast::BinaryOp::Assign:
        case ast::BinaryOp::AddAssign:
        case ast::BinaryOp::SubAssign:
        case ast::BinaryOp::MulAssign:
        case ast::BinaryOp::DivAssign: {
            if (auto* ident = binary.left->as<ast::IdentExpr>()) {
                auto sym = scopes_.current().lookup(ident->name);
                if (sym && sym->is_const) {
                    error(binary.left->span,
                          "Cannot assign to const variable '" + ident->name + "'");
                    return ast::make_error();
                }
            }
            if (!types_compatible(ltype, rtype)) {
                error(binary.left->span, "Assignment type mismatch");
            }
            return ltype;
        }

        case ast::BinaryOp::Add:
            if (ltype->kind == ast::TypeKind::String || rtype->kind == ast::TypeKind::String) {
                return ast::make_string();
            }
            if (ltype->is_numeric() && rtype->is_numeric()) {
                return common_type(ltype, rtype);
            }
            error(Span{}, "Add operator requires numeric operands or string concatenation");
            return ast::make_error();

        default:
            if (!ltype->is_numeric() || !rtype->is_numeric()) {
                error(Span{}, "Arithmetic operators require numeric operands");
                return ast::make_error();
            }
            return common_type(ltype, rtype);
    }
}

ast::TypePtr TypeChecker::infer_unary(ast::UnaryExpr& unary) {
    auto otype = infer_type(*unary.operand);
    if (!otype)
        return ast::make_error();

    switch (unary.op) {
        case ast::UnaryOp::Neg:
            if (!otype->is_numeric()) {
                error(Span{}, "Negation requires numeric operand");
            }
            return otype;
        case ast::UnaryOp::Not:
            if (otype->kind != ast::TypeKind::Bool) {
                error(Span{}, "Logical not requires bool operand");
            }
            return std::make_shared<ast::Type>(ast::TypeKind::Bool);
        case ast::UnaryOp::BitNot:
            if (!otype->is_integer()) {
                error(Span{}, "Bitwise not requires integer operand");
            }
            return otype;
        case ast::UnaryOp::Deref:
            if (otype->kind != ast::TypeKind::Pointer) {
                error(Span{}, "Cannot dereference non-pointer");
                return ast::make_error();
            }
            return otype->element_type;
        case ast::UnaryOp::AddrOf:
            if (otype->kind == ast::TypeKind::Function) {
                return otype;
            }
            return ast::make_pointer(otype);
        case ast::UnaryOp::PreInc:
        case ast::UnaryOp::PreDec:
        case ast::UnaryOp::PostInc:
        case ast::UnaryOp::PostDec:
            if (!otype->is_numeric()) {
                error(Span{}, "Increment/decrement requires numeric operand");
            }
            return otype;
    }
    return ast::make_error();
}

ast::TypePtr TypeChecker::infer_ternary(ast::TernaryExpr& ternary) {
    auto cond_type = infer_type(*ternary.condition);
    if (!cond_type ||
        (cond_type->kind != ast::TypeKind::Bool && cond_type->kind != ast::TypeKind::Int)) {
        error(Span{}, "Ternary condition must be bool or int");
    }

    auto then_type = infer_type(*ternary.then_expr);
    auto else_type = infer_type(*ternary.else_expr);

    if (!types_compatible(then_type, else_type)) {
        error(Span{}, "Ternary branches have incompatible types");
    }

    return then_type;
}

ast::TypePtr TypeChecker::infer_index(ast::IndexExpr& idx) {
    auto obj_type = infer_type(*idx.object);
    auto index_type = infer_type(*idx.index);
    if (!index_type || !index_type->is_integer()) {
        error(Span{}, "Array index must be an integer type");
    }

    if (!obj_type) {
        return ast::make_error();
    }

    if (obj_type->kind == ast::TypeKind::Array) {
        return obj_type->element_type;
    }

    if (obj_type->kind == ast::TypeKind::Pointer) {
        return obj_type->element_type;
    }

    if (obj_type->kind == ast::TypeKind::String) {
        return ast::make_char();
    }

    error(Span{}, "Index access on non-array type");
    return ast::make_error();
}

ast::TypePtr TypeChecker::infer_slice(ast::SliceExpr& slice) {
    auto obj_type = infer_type(*slice.object);

    if (slice.start) {
        auto start_type = infer_type(*slice.start);
        if (!start_type || !start_type->is_integer()) {
            error(Span{}, "Slice start index must be an integer type");
        }
    }
    if (slice.end) {
        auto end_type = infer_type(*slice.end);
        if (!end_type || !end_type->is_integer()) {
            error(Span{}, "Slice end index must be an integer type");
        }
    }
    if (slice.step) {
        auto step_type = infer_type(*slice.step);
        if (!step_type || !step_type->is_integer()) {
            error(Span{}, "Slice step must be an integer type");
        }
    }

    if (!obj_type) {
        return ast::make_error();
    }

    if (obj_type->kind == ast::TypeKind::Array) {
        return ast::make_array(obj_type->element_type, std::nullopt);
    }

    if (obj_type->kind == ast::TypeKind::String) {
        return ast::make_string();
    }

    error(Span{}, "Slice access on non-array/string type");
    return ast::make_error();
}

ast::TypePtr TypeChecker::infer_match(ast::MatchExpr& match) {
    auto scrutinee_type = infer_type(*match.scrutinee);
    if (!scrutinee_type) {
        error(Span{}, "Cannot infer type of match scrutinee");
        return ast::make_error();
    }

    ast::TypePtr result_type = nullptr;
    for (auto& arm : match.arms) {
        check_match_pattern(arm.pattern.get(), scrutinee_type);

        if (arm.guard) {
            auto guard_type = infer_type(*arm.guard);
            if (!guard_type || guard_type->kind != ast::TypeKind::Bool) {
                error(Span{}, "Match guard must be a boolean expression");
            }
        }

        auto body_type = infer_type(*arm.body);

        if (!result_type) {
            result_type = body_type;
        } else if (!types_compatible(result_type, body_type)) {
            error(Span{}, "Match arms have incompatible types");
        }
    }

    if (!result_type) {
        error(Span{}, "Match expression has no arms");
        return ast::make_error();
    }

    check_match_exhaustiveness(match, scrutinee_type);

    return result_type;
}

void TypeChecker::check_match_exhaustiveness(ast::MatchExpr& match, ast::TypePtr scrutinee_type) {
    if (!scrutinee_type)
        return;

    bool has_wildcard = false;
    bool has_variable_binding = false;
    std::set<std::string> covered_values;
    std::string detected_enum_name;

    for (const auto& arm : match.arms) {
        if (!arm.pattern)
            continue;

        switch (arm.pattern->kind) {
            case ast::MatchPatternKind::Wildcard:
                has_wildcard = true;
                break;
            case ast::MatchPatternKind::Variable:
                if (!arm.guard) {
                    has_variable_binding = true;
                }
                break;
            case ast::MatchPatternKind::Literal:
                if (arm.pattern->value) {
                    if (auto* lit = arm.pattern->value->as<ast::LiteralExpr>()) {
                        if (lit->is_int()) {
                            covered_values.insert(std::to_string(std::get<int64_t>(lit->value)));
                        } else if (lit->is_bool()) {
                            covered_values.insert(std::get<bool>(lit->value) ? "true" : "false");
                        }
                    }
                }
                break;
            case ast::MatchPatternKind::EnumVariant:
                if (arm.pattern->value) {
                    if (auto* ident = arm.pattern->value->as<ast::IdentExpr>()) {
                        covered_values.insert(ident->name);
                        auto pos = ident->name.find("::");
                        if (pos != std::string::npos) {
                            std::string enum_name = ident->name.substr(0, pos);
                            if (enum_names_.count(enum_name)) {
                                detected_enum_name = enum_name;
                            }
                        }
                    }
                }
                break;
        }
    }

    if (has_wildcard || has_variable_binding) {
        return;
    }

    if (scrutinee_type->kind == ast::TypeKind::Bool) {
        if (!covered_values.count("true") || !covered_values.count("false")) {
            error(Span{},
                  "Non-exhaustive match: missing 'true' or 'false' pattern (or add '_' wildcard)");
        }
        return;
    }

    if (!detected_enum_name.empty()) {
        std::set<std::string> all_variants;
        for (const auto& [key, value] : enum_values_) {
            if (key.find(detected_enum_name + "::") == 0) {
                all_variants.insert(key);
            }
        }

        for (const auto& variant : all_variants) {
            if (!covered_values.count(variant)) {
                error(Span{}, "Non-exhaustive match: missing pattern for '" + variant +
                                  "' (or add '_' wildcard)");
                return;
            }
        }
        return;
    }

    std::string type_name = ast::type_to_string(*scrutinee_type);
    if (enum_names_.count(type_name)) {
        std::set<std::string> all_variants;
        for (const auto& [key, value] : enum_values_) {
            if (key.find(type_name + "::") == 0) {
                all_variants.insert(key);
            }
        }

        for (const auto& variant : all_variants) {
            if (!covered_values.count(variant)) {
                error(Span{}, "Non-exhaustive match: missing pattern for '" + variant +
                                  "' (or add '_' wildcard)");
                return;
            }
        }
        return;
    }

    if (scrutinee_type->is_integer()) {
        error(Span{}, "Non-exhaustive match: integer patterns require a '_' wildcard pattern");
    }
}

void TypeChecker::check_match_pattern(ast::MatchPattern* pattern, ast::TypePtr expected_type) {
    if (!pattern)
        return;

    switch (pattern->kind) {
        case ast::MatchPatternKind::Literal:
            if (pattern->value) {
                auto lit_type = infer_type(*pattern->value);
                if (!types_compatible(lit_type, expected_type)) {
                    error(Span{}, "Pattern type does not match scrutinee type");
                }
            }
            break;

        case ast::MatchPatternKind::Variable:
            if (!pattern->var_name.empty()) {
                scopes_.current().define(pattern->var_name, expected_type);
            }
            break;

        case ast::MatchPatternKind::EnumVariant:
            if (pattern->value) {
                auto enum_type = infer_type(*pattern->value);
                if (!types_compatible(enum_type, expected_type)) {
                    error(Span{}, "Enum pattern type does not match scrutinee type");
                }
            }
            break;

        case ast::MatchPatternKind::Wildcard:
            break;
    }
}

}  // namespace cm
