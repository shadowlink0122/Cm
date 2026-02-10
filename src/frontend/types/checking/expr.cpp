// ============================================================
// TypeChecker 実装 - 式の型推論
// ============================================================

#include "../type_checker.hpp"

#include <functional>
#include <memory>
#include <unordered_set>

namespace cm {

ast::TypePtr TypeChecker::infer_type(ast::Expr& expr) {
    debug::tc::log(debug::tc::Id::CheckExpr, "", debug::Level::Trace);

    // エラー表示用に現在の式のSpanを保存
    current_span_ = expr.span;

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
    } else if (auto* lambda_expr = expr.as<ast::LambdaExpr>()) {
        inferred_type = infer_lambda(*lambda_expr);
    } else if (auto* sizeof_expr = expr.as<ast::SizeofExpr>()) {
        // sizeof(型)の場合、型が有効かチェック
        // 無効な場合は変数として解釈を試みる
        if (sizeof_expr->target_type) {
            auto& target_type = sizeof_expr->target_type;
            // 構造体型として解析されたが、実際には変数かもしれない
            if (target_type->kind == ast::TypeKind::Struct) {
                std::string name = target_type->name;
                // typedefや構造体として定義されているかチェック
                bool is_valid_type = false;
                if (typedef_defs_.count(name) > 0) {
                    is_valid_type = true;
                } else if (struct_defs_.count(name) > 0) {
                    is_valid_type = true;
                }

                if (!is_valid_type) {
                    // 変数として解決を試みる
                    auto sym = scopes_.current().lookup(name);
                    if (sym && sym->type && sym->type->kind != ast::TypeKind::Error) {
                        // 変数として有効 - target_exprを設定
                        sizeof_expr->target_expr = ast::make_ident(name, {});
                        sizeof_expr->target_expr->type = sym->type;
                        sizeof_expr->target_type = nullptr;
                    }
                }
            }
        }
        // sizeof(式) の場合は式の型チェックを行う
        if (sizeof_expr->target_expr) {
            infer_type(*sizeof_expr->target_expr);
        }
        // sizeof は常に uint (符号なし整数) を返す
        inferred_type = ast::make_uint();
    } else if (auto* typeof_expr = expr.as<ast::TypeofExpr>()) {
        // typeof(式) - 式の型を推論
        if (typeof_expr->target_expr) {
            infer_type(*typeof_expr->target_expr);
            // typeof自体は型を返すが、ここでは式としてerrorを返す
            // (typeofは通常、型コンテキストで使用される)
        }
        inferred_type = ast::make_error();
    } else if (auto* typename_expr = expr.as<ast::TypenameOfExpr>()) {
        // __typename__(型) または __typename__(式) - 文字列を返す
        if (typename_expr->target_expr) {
            infer_type(*typename_expr->target_expr);
        }
        inferred_type = ast::make_string();
    } else if (auto* cast_expr = expr.as<ast::CastExpr>()) {
        // キャスト式: expr as Type
        // オペランドの型を推論
        if (cast_expr->operand) {
            infer_type(*cast_expr->operand);
        }
        // ターゲット型を返す
        inferred_type = cast_expr->target_type;
    } else if (auto* move_expr = expr.as<ast::MoveExpr>()) {
        // move式: オペランドの型を推論し、変数をmoved状態にマーク
        if (move_expr->operand) {
            inferred_type = infer_type(*move_expr->operand);
            // オペランドが識別子の場合、その変数を移動済みとしてマーク
            if (auto* ident = move_expr->operand->as<ast::IdentExpr>()) {
                // 借用中の変数はmove禁止（借用安全性）
                if (scopes_.current().is_borrowed(ident->name)) {
                    error(current_span_, "Cannot move '" + ident->name + "' while it is borrowed");
                    return ast::make_error();
                }
                mark_variable_moved(ident->name);
                debug::tc::log(debug::tc::Id::CheckExpr,
                               "Marked variable '" + ident->name + "' as moved",
                               debug::Level::Debug);
            }
        } else {
            inferred_type = ast::make_error();
        }
    } else if (auto* await_expr = expr.as<ast::AwaitExpr>()) {
        // await式: Future<T>を待機してTを返す
        // 現時点では単にオペランドの型を返す（同期的実行）
        if (await_expr->operand) {
            inferred_type = infer_type(*await_expr->operand);
            debug::tc::log(debug::tc::Id::CheckExpr, "Inferred await expression type",
                           debug::Level::Debug);
        } else {
            inferred_type = ast::make_error();
        }
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
        error(current_span_, "Unknown struct type: " + lit.type_name);
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
        // 暗黙的selfは許可しない - 明示的にself.fieldを使用する必要がある
        error(current_span_, "Undefined variable '" + ident.name + "'");
        return ast::make_error();
    }

    // 変数使用をマーク（未使用変数検出用 W001）
    scopes_.current().mark_used(ident.name);

    // 初期化前使用のチェック
    check_uninitialized_use(ident.name, current_span_);

    // 移動後使用のチェック（Move Semantics）
    check_use_after_move(ident.name, current_span_);

    debug::tc::log(debug::tc::Id::Resolved, ident.name + " : " + ast::type_to_string(*sym->type),
                   debug::Level::Trace);
    return sym->type;
}

ast::TypePtr TypeChecker::infer_binary(ast::BinaryExpr& binary) {
    // 代入演算子の場合、左辺がmove済み変数ならエラー
    // move後の変数は完全に無効化され、再代入も禁止
    bool is_assignment =
        (binary.op == ast::BinaryOp::Assign || binary.op == ast::BinaryOp::AddAssign ||
         binary.op == ast::BinaryOp::SubAssign || binary.op == ast::BinaryOp::MulAssign ||
         binary.op == ast::BinaryOp::DivAssign || binary.op == ast::BinaryOp::ModAssign ||
         binary.op == ast::BinaryOp::BitAndAssign || binary.op == ast::BinaryOp::BitOrAssign ||
         binary.op == ast::BinaryOp::BitXorAssign || binary.op == ast::BinaryOp::ShlAssign ||
         binary.op == ast::BinaryOp::ShrAssign);
    if (is_assignment) {
        if (auto* ident = binary.left->as<ast::IdentExpr>()) {
            // move済み変数への代入は禁止
            if (scopes_.current().is_moved(ident->name)) {
                error(binary.left->span, "Cannot assign to moved variable '" + ident->name +
                                             "': variable no longer exists after move");
                return ast::make_error();
            }
        }
    }

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
                error(current_span_, "Logical operators require bool operands");
            }
            return std::make_shared<ast::Type>(ast::TypeKind::Bool);

        case ast::BinaryOp::Assign:
        case ast::BinaryOp::AddAssign:
        case ast::BinaryOp::SubAssign:
        case ast::BinaryOp::MulAssign:
        case ast::BinaryOp::DivAssign:
        case ast::BinaryOp::ModAssign:
        case ast::BinaryOp::BitAndAssign:
        case ast::BinaryOp::BitOrAssign:
        case ast::BinaryOp::BitXorAssign:
        case ast::BinaryOp::ShlAssign:
        case ast::BinaryOp::ShrAssign: {
            if (auto* ident = binary.left->as<ast::IdentExpr>()) {
                auto sym = scopes_.current().lookup(ident->name);
                if (sym && sym->is_const) {
                    error(binary.left->span,
                          "Cannot assign to const variable '" + ident->name + "'");
                    return ast::make_error();
                }
                // 借用チェック: 借用中の変数への代入を禁止（DRY原則）
                if (scopes_.current().is_borrowed(ident->name)) {
                    error(binary.left->span,
                          "Cannot assign to '" + ident->name + "' while it is borrowed");
                    return ast::make_error();
                }
                // 変数が変更されたことをマーク（const推奨警告用）
                mark_variable_modified(ident->name);

                // ライフタイムチェック: ポインタ代入時のスコープ比較
                // p = &x の場合、pのスコープレベル < xのスコープレベルなら危険
                if (binary.op == ast::BinaryOp::Assign && ltype &&
                    ltype->kind == ast::TypeKind::Pointer) {
                    if (auto* unary = binary.right->as<ast::UnaryExpr>()) {
                        if (unary->op == ast::UnaryOp::AddrOf) {
                            if (auto* rhs_ident = unary->operand->as<ast::IdentExpr>()) {
                                int lhs_level = scopes_.current().get_scope_level(ident->name);
                                int rhs_level = scopes_.current().get_scope_level(rhs_ident->name);
                                // 左辺が外側スコープ（寿命長い）で右辺が内側スコープ（寿命短い）→危険
                                if (lhs_level < rhs_level) {
                                    error(binary.left->span,
                                          "Cannot store reference to '" + rhs_ident->name +
                                              "' in '" + ident->name + "': '" + rhs_ident->name +
                                              "' may be dropped while '" + ident->name +
                                              "' is still alive");
                                    return ast::make_error();
                                }
                            }
                        }
                    }
                }
            }
            // デリファレンス経由の代入チェック（借用システム Phase 2）
            // *p = value の場合、pがconstポインタなら代入禁止
            else if (auto* unary = binary.left->as<ast::UnaryExpr>()) {
                if (unary->op == ast::UnaryOp::Deref) {
                    // デリファレンスされるポインタの型を取得
                    auto ptr_type = infer_type(*unary->operand);
                    if (ptr_type && ptr_type->kind == ast::TypeKind::Pointer) {
                        // ポインタ自体がconstの場合（const int* p）
                        if (ptr_type->qualifiers.is_const) {
                            error(binary.left->span, "Cannot assign through const pointer");
                            return ast::make_error();
                        }
                        // 要素型がconstの場合も禁止（const修飾された要素への代入）
                        if (ptr_type->element_type && ptr_type->element_type->qualifiers.is_const) {
                            error(binary.left->span, "Cannot assign through pointer to const");
                            return ast::make_error();
                        }
                    }
                }
            }
            // 複合代入演算子の場合、構造体のオペレーターオーバーロードをチェック
            if (binary.op != ast::BinaryOp::Assign && ltype->kind == ast::TypeKind::Struct) {
                std::string type_name = ltype->name;
                std::string iface_name;
                switch (binary.op) {
                    case ast::BinaryOp::AddAssign:
                        iface_name = "Add";
                        break;
                    case ast::BinaryOp::SubAssign:
                        iface_name = "Sub";
                        break;
                    case ast::BinaryOp::MulAssign:
                        iface_name = "Mul";
                        break;
                    case ast::BinaryOp::DivAssign:
                        iface_name = "Div";
                        break;
                    case ast::BinaryOp::ModAssign:
                        iface_name = "Mod";
                        break;
                    case ast::BinaryOp::BitAndAssign:
                        iface_name = "BitAnd";
                        break;
                    case ast::BinaryOp::BitOrAssign:
                        iface_name = "BitOr";
                        break;
                    case ast::BinaryOp::BitXorAssign:
                        iface_name = "BitXor";
                        break;
                    case ast::BinaryOp::ShlAssign:
                        iface_name = "Shl";
                        break;
                    case ast::BinaryOp::ShrAssign:
                        iface_name = "Shr";
                        break;
                    default:
                        break;
                }
                if (!iface_name.empty()) {
                    auto it = impl_interfaces_.find(type_name);
                    if (it != impl_interfaces_.end() && it->second.count(iface_name)) {
                        return ltype;  // オペレーターオーバーロード対応
                    }
                    error(binary.left->span, "Type '" + type_name + "' does not implement " +
                                                 iface_name + " operator for compound assignment");
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
            // ポインタ演算: pointer + int または int + pointer
            if (ltype->kind == ast::TypeKind::Pointer && rtype->is_integer()) {
                return ltype;  // pointer + int = pointer
            }
            if (ltype->is_integer() && rtype->kind == ast::TypeKind::Pointer) {
                return rtype;  // int + pointer = pointer
            }
            // 演算子オーバーロード: impl for Add
            if (ltype->kind == ast::TypeKind::Struct) {
                std::string type_name = ltype->name;
                auto it = impl_interfaces_.find(type_name);
                if (it != impl_interfaces_.end() && it->second.count("Add")) {
                    return ltype;
                }
            }
            error(current_span_, "Add operator requires numeric operands or string concatenation");
            return ast::make_error();

        case ast::BinaryOp::Sub:
            if (ltype->is_numeric() && rtype->is_numeric()) {
                return common_type(ltype, rtype);
            }
            // ポインタ演算: pointer - int
            if (ltype->kind == ast::TypeKind::Pointer && rtype->is_integer()) {
                return ltype;  // pointer - int = pointer
            }
            // ポインタ差分: pointer - pointer = int (要素数の差)
            if (ltype->kind == ast::TypeKind::Pointer && rtype->kind == ast::TypeKind::Pointer) {
                return ast::make_long();  // ポインタ差分はlong
            }
            // 演算子オーバーロード: impl for Sub
            if (ltype->kind == ast::TypeKind::Struct) {
                std::string type_name = ltype->name;
                auto it = impl_interfaces_.find(type_name);
                if (it != impl_interfaces_.end() && it->second.count("Sub")) {
                    return ltype;
                }
            }
            error(current_span_, "Sub operator requires numeric operands");
            return ast::make_error();

        default:
            if (!ltype->is_numeric() || !rtype->is_numeric()) {
                // 演算子オーバーロード: impl for Mul/Div/Mod
                if (ltype->kind == ast::TypeKind::Struct) {
                    std::string type_name = ltype->name;
                    std::string iface_name;
                    if (binary.op == ast::BinaryOp::Mul)
                        iface_name = "Mul";
                    else if (binary.op == ast::BinaryOp::Div)
                        iface_name = "Div";
                    else if (binary.op == ast::BinaryOp::Mod)
                        iface_name = "Mod";
                    else if (binary.op == ast::BinaryOp::BitAnd)
                        iface_name = "BitAnd";
                    else if (binary.op == ast::BinaryOp::BitOr)
                        iface_name = "BitOr";
                    else if (binary.op == ast::BinaryOp::BitXor)
                        iface_name = "BitXor";
                    else if (binary.op == ast::BinaryOp::Shl)
                        iface_name = "Shl";
                    else if (binary.op == ast::BinaryOp::Shr)
                        iface_name = "Shr";
                    if (!iface_name.empty()) {
                        auto it = impl_interfaces_.find(type_name);
                        if (it != impl_interfaces_.end() && it->second.count(iface_name)) {
                            return ltype;
                        }
                    }
                }
                error(current_span_, "Arithmetic operators require numeric operands");
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
                error(current_span_, "Negation requires numeric operand");
            }
            return otype;
        case ast::UnaryOp::Not:
            if (otype->kind != ast::TypeKind::Bool) {
                error(current_span_, "Logical not requires bool operand");
            }
            return std::make_shared<ast::Type>(ast::TypeKind::Bool);
        case ast::UnaryOp::BitNot:
            if (!otype->is_integer()) {
                error(current_span_, "Bitwise not requires integer operand");
            }
            return otype;
        case ast::UnaryOp::Deref:
            if (otype->kind != ast::TypeKind::Pointer) {
                error(current_span_, "Cannot dereference non-pointer");
                return ast::make_error();
            }
            return otype->element_type;
        case ast::UnaryOp::AddrOf:
            if (otype->kind == ast::TypeKind::Function) {
                return otype;
            }
            // 借用追跡: オペランドが識別子の場合、借用を登録
            if (auto* ident = unary.operand->as<ast::IdentExpr>()) {
                scopes_.current().add_borrow(ident->name);
                debug::tc::log(debug::tc::Id::CheckExpr, "Added borrow for '" + ident->name + "'",
                               debug::Level::Debug);
            }
            return ast::make_pointer(otype);
        case ast::UnaryOp::PreInc:
        case ast::UnaryOp::PreDec:
        case ast::UnaryOp::PostInc:
        case ast::UnaryOp::PostDec: {
            // const check: 代入と同様に、const変数の変更を禁止（DRY原則）
            if (auto* ident = unary.operand->as<ast::IdentExpr>()) {
                auto sym = scopes_.current().lookup(ident->name);
                if (sym && sym->is_const) {
                    error(unary.operand->span,
                          "Cannot modify const variable '" + ident->name + "'");
                    return ast::make_error();
                }
                // 借用チェック: 借用中の変数への変更を禁止（DRY原則）
                if (scopes_.current().is_borrowed(ident->name)) {
                    error(unary.operand->span,
                          "Cannot modify '" + ident->name + "' while it is borrowed");
                    return ast::make_error();
                }
                // 変数が変更されたことをマーク（const推奨警告用）
                mark_variable_modified(ident->name);
            }
            if (!otype->is_numeric()) {
                error(current_span_, "Increment/decrement requires numeric operand");
            }
            return otype;
        }
    }
    return ast::make_error();
}

ast::TypePtr TypeChecker::infer_ternary(ast::TernaryExpr& ternary) {
    auto cond_type = infer_type(*ternary.condition);
    if (!cond_type ||
        (cond_type->kind != ast::TypeKind::Bool && cond_type->kind != ast::TypeKind::Int)) {
        error(current_span_, "Ternary condition must be bool or int");
    }

    auto then_type = infer_type(*ternary.then_expr);
    auto else_type = infer_type(*ternary.else_expr);

    if (!types_compatible(then_type, else_type)) {
        error(current_span_, "Ternary branches have incompatible types");
    }

    return then_type;
}

ast::TypePtr TypeChecker::infer_index(ast::IndexExpr& idx) {
    auto obj_type = infer_type(*idx.object);
    auto index_type = infer_type(*idx.index);
    if (!index_type || !index_type->is_integer()) {
        error(current_span_, "Array index must be an integer type");
    }

    if (!obj_type) {
        return ast::make_error();
    }

    // typedefを解決
    obj_type = resolve_typedef(obj_type);

    if (obj_type->kind == ast::TypeKind::Array) {
        // 要素型もtypedefを解決
        return resolve_typedef(obj_type->element_type);
    }

    if (obj_type->kind == ast::TypeKind::Pointer) {
        // 要素型もtypedefを解決
        return resolve_typedef(obj_type->element_type);
    }

    if (obj_type->kind == ast::TypeKind::String) {
        return ast::make_char();
    }

    error(current_span_, "Index access on non-array type");
    return ast::make_error();
}

ast::TypePtr TypeChecker::infer_slice(ast::SliceExpr& slice) {
    auto obj_type = infer_type(*slice.object);

    if (slice.start) {
        auto start_type = infer_type(*slice.start);
        if (!start_type || !start_type->is_integer()) {
            error(current_span_, "Slice start index must be an integer type");
        }
    }
    if (slice.end) {
        auto end_type = infer_type(*slice.end);
        if (!end_type || !end_type->is_integer()) {
            error(current_span_, "Slice end index must be an integer type");
        }
    }
    if (slice.step) {
        auto step_type = infer_type(*slice.step);
        if (!step_type || !step_type->is_integer()) {
            error(current_span_, "Slice step must be an integer type");
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

    error(current_span_, "Slice access on non-array/string type");
    return ast::make_error();
}

// v0.13.0: matchは両方の形式をサポート:
//   - 式形式: pattern => expr (同じ型を返す)
//   - ブロック形式: pattern => { stmts } (void または return文の型)
ast::TypePtr TypeChecker::infer_match(ast::MatchExpr& match) {
    auto scrutinee_type = infer_type(*match.scrutinee);
    if (!scrutinee_type) {
        error(current_span_, "Cannot infer type of match scrutinee");
        return ast::make_error();
    }

    // 全armが式形式かブロック形式かを判定
    bool all_expr_form = true;
    bool all_block_form = true;
    for (auto& arm : match.arms) {
        if (arm.is_block_form) {
            all_expr_form = false;
        } else {
            all_block_form = false;
        }
    }

    // 混在時は警告を出す（式形式とブロック形式が混在）
    bool is_mixed = !all_expr_form && !all_block_form;
    if (is_mixed) {
        // 警告のみ、エラーではない
        // 混在時はvoid型を返す
    }

    ast::TypePtr result_type = nullptr;
    size_t arm_index = 0;

    for (auto& arm : match.arms) {
        check_match_pattern(arm.pattern.get(), scrutinee_type);

        if (arm.guard) {
            auto guard_type = infer_type(*arm.guard);
            if (!guard_type || guard_type->kind != ast::TypeKind::Bool) {
                error(current_span_, "Match guard must be a boolean expression");
            }
        }

        scopes_.push();

        // EnumVariantWithBindingの場合、バインディング変数をスコープに追加
        if (arm.pattern && arm.pattern->kind == ast::MatchPatternKind::EnumVariantWithBinding) {
            if (!arm.pattern->binding_name.empty()) {
                // ペイロードの実際の型を取得（enum定義からバリアントのフィールド型を取得）
                ast::TypePtr binding_type = scrutinee_type;  // フォールバック

                // scrutinee_typeの型名でenum定義を検索
                if (scrutinee_type && !scrutinee_type->name.empty()) {
                    auto enum_it = enum_defs_.find(scrutinee_type->name);
                    if (enum_it != enum_defs_.end() && enum_it->second) {
                        // バリアント名を取得（Type::Variant形式からVariantを抽出）
                        std::string variant_name = arm.pattern->enum_variant;
                        auto sep = variant_name.rfind("::");
                        if (sep != std::string::npos) {
                            variant_name = variant_name.substr(sep + 2);
                        }
                        // enum定義からフィールド型を取得
                        for (const auto& member : enum_it->second->members) {
                            if (member.name == variant_name && !member.fields.empty()) {
                                // 最初のフィールドの型を使用（設計: 1フィールド推奨）
                                binding_type = member.fields[0].second;
                                break;
                            }
                        }
                    }
                }
                scopes_.current().define(arm.pattern->binding_name, binding_type);
            }
        }

        if (arm.is_block_form) {
            // ブロック形式: 各文をチェック
            for (auto& stmt : arm.block_body) {
                check_statement(*stmt);
            }
            // ブロック形式はvoid扱い（return文があっても関数のreturn）
        } else {
            // 式形式: 式の型をチェック
            if (arm.expr_body) {
                auto arm_type = infer_type(*arm.expr_body);
                if (arm_type && arm_type->kind != ast::TypeKind::Error) {
                    if (!result_type) {
                        result_type = arm_type;
                    } else if (!types_compatible(result_type, arm_type)) {
                        error(current_span_, "Match arm " + std::to_string(arm_index + 1) +
                                                 " has incompatible type (expected '" +
                                                 ast::type_to_string(*result_type) + "', got '" +
                                                 ast::type_to_string(*arm_type) + "')");
                    }
                }
            }
        }

        scopes_.pop();
        arm_index++;
    }

    if (match.arms.empty()) {
        error(current_span_, "Match statement has no arms");
        return ast::make_error();
    }

    check_match_exhaustiveness(match, scrutinee_type);

    // 式形式でresult_typeがあればそれを返す
    if (all_expr_form && result_type) {
        return result_type;
    }

    // 混在またはブロック形式のみの場合はvoid
    return ast::make_void();
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
            case ast::MatchPatternKind::EnumVariantWithBinding:
                // EnumType::Variant(binding) パターン
                if (!arm.pattern->enum_variant.empty()) {
                    covered_values.insert(arm.pattern->enum_variant);
                    auto pos = arm.pattern->enum_variant.find("::");
                    if (pos != std::string::npos) {
                        std::string enum_name = arm.pattern->enum_variant.substr(0, pos);
                        if (enum_names_.count(enum_name)) {
                            detected_enum_name = enum_name;
                        }
                    }
                }
                break;
            case ast::MatchPatternKind::Range:
                // 範囲パターンは完全性チェックが複雑なため、現時点ではスキップ
                // （範囲内の値をカバーとみなす）
                break;
            case ast::MatchPatternKind::Or:
                // ORパターンは各サブパターンをカバーとみなす
                // TODO: 再帰的にサブパターンをチェック
                break;
        }
    }
    if (has_wildcard || has_variable_binding) {
        return;
    }

    if (scrutinee_type->kind == ast::TypeKind::Bool) {
        if (!covered_values.count("true") || !covered_values.count("false")) {
            error(current_span_,
                  "Non-exhaustive match: missing 'true' or 'false' pattern (or add '_' "
                  "wildcard)");
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
                error(current_span_, "Non-exhaustive match: missing pattern for '" + variant +
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
                error(current_span_, "Non-exhaustive match: missing pattern for '" + variant +
                                         "' (or add '_' wildcard)");
                return;
            }
        }
        return;
    }

    if (scrutinee_type->is_integer()) {
        error(current_span_,
              "Non-exhaustive match: integer patterns require a '_' wildcard pattern");
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
                    error(current_span_, "Pattern type does not match scrutinee type");
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
                // enum型のscrutineeに対するenumバリアントパターン
                // Option型に対してOption::Someパターンをチェック
                if (auto* ident = pattern->value->as<ast::IdentExpr>()) {
                    // パターン名からenum型を抽出（例：Option::Some -> Option）
                    auto pos = ident->name.find("::");
                    if (pos != std::string::npos) {
                        std::string pattern_enum_name = ident->name.substr(0, pos);
                        // パターンのenum型がenum_names_に登録されていれば許可
                        // (scrutineeはint型として解決されているため、直接比較できない)
                        if (enum_names_.count(pattern_enum_name)) {
                            // enumパターンがenum型として有効 - OK
                            // scrutineeは必ずint型に解決されるため、チェックをパス
                            break;
                        }
                    }
                }
                // フォールバック: 通常のtype互換性チェック
                auto enum_type = infer_type(*pattern->value);
                if (!types_compatible(enum_type, expected_type)) {
                    error(current_span_, "Enum pattern type does not match scrutinee type");
                }
            }
            break;

        case ast::MatchPatternKind::EnumVariantWithBinding: {
            // EnumType::Variant(binding) のパターン
            // バリアント名を検証し、バインディング変数をスコープに追加
            if (!pattern->enum_variant.empty()) {
                // パターン名からenum型を抽出（例：Option::Some -> Option）
                auto pos = pattern->enum_variant.find("::");
                bool type_matched = false;
                if (pos != std::string::npos) {
                    std::string pattern_enum_name = pattern->enum_variant.substr(0, pos);
                    // パターンのenum型がenum_names_に登録されていれば許可
                    // (scrutineeはint型として解決されているため、直接比較できない)
                    if (enum_names_.count(pattern_enum_name)) {
                        type_matched = true;
                    }
                }

                if (!type_matched) {
                    // フォールバック: 通常のtype互換性チェック
                    auto enum_ident = ast::make_ident(pattern->enum_variant, {});
                    auto enum_type = infer_type(*enum_ident);
                    if (!types_compatible(enum_type, expected_type)) {
                        error(current_span_, "Enum pattern type does not match scrutinee type");
                    }
                }

                // TODO: バインディング変数にAssociated Dataの実際の型を設定
                // 現時点ではexpected_typeをそのまま使用
                if (!pattern->binding_name.empty()) {
                    scopes_.current().define(pattern->binding_name, expected_type);
                }
            }
            break;
        }

        case ast::MatchPatternKind::Wildcard:
            break;

        case ast::MatchPatternKind::Range:
            // 範囲パターンのチェック
            if (pattern->range_start) {
                auto start_type = infer_type(*pattern->range_start);
                if (!types_compatible(start_type, expected_type)) {
                    error(current_span_, "Range start type does not match scrutinee type");
                }
            }
            if (pattern->range_end) {
                auto end_type = infer_type(*pattern->range_end);
                if (!types_compatible(end_type, expected_type)) {
                    error(current_span_, "Range end type does not match scrutinee type");
                }
            }
            break;

        case ast::MatchPatternKind::Or:
            // ORパターン内の各サブパターンをチェック
            for (const auto& sub_pattern : pattern->or_patterns) {
                check_match_pattern(sub_pattern.get(), expected_type);
            }
            break;
    }
}

ast::TypePtr TypeChecker::infer_lambda(ast::LambdaExpr& lambda) {
    // ラムダ式の型チェック
    // パラメータの型が明示されていない場合はエラー
    std::vector<ast::TypePtr> param_types;
    std::unordered_set<std::string> param_names;

    for (const auto& param : lambda.params) {
        if (!param.type || param.type->kind == ast::TypeKind::Error) {
            error(current_span_, "Lambda parameter '" + param.name +
                                     "' must have an explicit type. "
                                     "Use: (Type param_name) => { ... }");
            return ast::make_error();
        }
        param_types.push_back(param.type);
        param_names.insert(param.name);
    }

    // 新しいスコープを作成してパラメータを登録
    scopes_.push();
    for (const auto& param : lambda.params) {
        scopes_.current().define(param.name, param.type);
    }

    // ラムダ本体の型チェック
    // 現在の戻り値型を保存し、一時的にnullに
    auto saved_return_type = current_return_type_;
    current_return_type_ = nullptr;

    ast::TypePtr return_type = ast::make_void();

    // キャプチャ検出用：ラムダ内で使用される識別子を収集
    std::unordered_set<std::string> used_identifiers;
    std::unordered_set<std::string> local_vars;  // ラムダ内で定義された変数

    // 式からすべての識別子を収集するヘルパーラムダ
    std::function<void(ast::Expr&)> collect_identifiers = [&](ast::Expr& expr) {
        if (auto* ident = expr.as<ast::IdentExpr>()) {
            used_identifiers.insert(ident->name);
        } else if (auto* binary = expr.as<ast::BinaryExpr>()) {
            collect_identifiers(*binary->left);
            collect_identifiers(*binary->right);
        } else if (auto* unary = expr.as<ast::UnaryExpr>()) {
            collect_identifiers(*unary->operand);
        } else if (auto* call = expr.as<ast::CallExpr>()) {
            collect_identifiers(*call->callee);
            for (auto& arg : call->args) {
                collect_identifiers(*arg);
            }
        } else if (auto* member = expr.as<ast::MemberExpr>()) {
            collect_identifiers(*member->object);
        } else if (auto* index = expr.as<ast::IndexExpr>()) {
            collect_identifiers(*index->object);
            collect_identifiers(*index->index);
        } else if (auto* ternary = expr.as<ast::TernaryExpr>()) {
            collect_identifiers(*ternary->condition);
            collect_identifiers(*ternary->then_expr);
            collect_identifiers(*ternary->else_expr);
        }
        // その他の式タイプも必要に応じて追加
    };

    // 文から識別子を収集するヘルパーラムダ
    std::function<void(ast::Stmt&)> collect_from_stmt = [&](ast::Stmt& stmt) {
        if (auto* let = stmt.as<ast::LetStmt>()) {
            local_vars.insert(let->name);  // ローカル変数として記録
            if (let->init) {
                collect_identifiers(*let->init);
            }
        } else if (auto* ret = stmt.as<ast::ReturnStmt>()) {
            if (ret->value) {
                collect_identifiers(*ret->value);
            }
        } else if (auto* expr_stmt = stmt.as<ast::ExprStmt>()) {
            collect_identifiers(*expr_stmt->expr);
        } else if (auto* if_stmt = stmt.as<ast::IfStmt>()) {
            collect_identifiers(*if_stmt->condition);
            for (auto& s : if_stmt->then_block) {
                collect_from_stmt(*s);
            }
            for (auto& s : if_stmt->else_block) {
                collect_from_stmt(*s);
            }
        } else if (auto* while_stmt = stmt.as<ast::WhileStmt>()) {
            collect_identifiers(*while_stmt->condition);
            for (auto& s : while_stmt->body) {
                collect_from_stmt(*s);
            }
        } else if (auto* for_stmt = stmt.as<ast::ForStmt>()) {
            if (for_stmt->init)
                collect_from_stmt(*for_stmt->init);
            if (for_stmt->condition)
                collect_identifiers(*for_stmt->condition);
            if (for_stmt->update)
                collect_identifiers(*for_stmt->update);
            for (auto& s : for_stmt->body) {
                collect_from_stmt(*s);
            }
        }
        // その他の文タイプも必要に応じて追加
    };

    if (lambda.is_expr_body()) {
        // 式ボディ: (int x) => x * 2
        auto& expr = std::get<ast::ExprPtr>(lambda.body);
        collect_identifiers(*expr);
        return_type = infer_type(*expr);
    } else {
        // 文ボディ: (int x) => { return x * 2; }
        auto& stmts = std::get<std::vector<ast::StmtPtr>>(lambda.body);

        // 識別子を収集
        for (auto& stmt : stmts) {
            collect_from_stmt(*stmt);
        }

        // まず戻り値の型を先に推論
        for (auto& stmt : stmts) {
            if (auto* ret = stmt->as<ast::ReturnStmt>()) {
                if (ret->value) {
                    return_type = infer_type(*ret->value);
                    break;
                }
            }
        }

        // 戻り値型を設定してから文をチェック
        current_return_type_ = return_type;
        for (auto& stmt : stmts) {
            check_statement(*stmt);
        }
    }

    // 戻り値型を復元
    current_return_type_ = saved_return_type;

    scopes_.pop();

    // キャプチャされる変数を特定
    // used_identifiers から param_names と local_vars を除いたものが外部変数
    lambda.captures.clear();
    for (const auto& name : used_identifiers) {
        // パラメータまたはラムダ内ローカル変数は除外
        if (param_names.count(name) > 0 || local_vars.count(name) > 0) {
            continue;
        }

        // 外側のスコープで定義されているか確認
        auto sym = scopes_.current().lookup(name);
        if (sym && sym->type) {
            ast::LambdaExpr::Capture cap;
            cap.name = name;
            cap.type = sym->type;
            cap.by_ref = false;  // デフォルトは値キャプチャ
            lambda.captures.push_back(cap);

            debug::tc::log(debug::tc::Id::Resolved, "Lambda captures: " + name,
                           debug::Level::Debug);
        }
        // 見つからない場合は、グローバル変数や関数かもしれないので無視
    }

    // 関数ポインタ型を構築: ReturnType*(ParamTypes...)
    auto func_type = std::make_shared<ast::Type>(ast::TypeKind::Function);
    func_type->return_type = return_type;
    func_type->param_types = std::move(param_types);

    return func_type;
}

// ============================================================
// Move Semantics ヘルパー関数
// ============================================================

void TypeChecker::mark_variable_moved(const std::string& name) {
    // Scopeベースの移動状態管理
    scopes_.current().mark_moved(name);
}

void TypeChecker::check_use_after_move(const std::string& name, Span span) {
    // Symbolのis_movedフラグをチェック
    auto sym = scopes_.current().lookup(name);
    if (sym && sym->is_moved) {
        error(span, "Variable '" + name + "' used after move");
    }
}

}  // namespace cm
