// ============================================================
// TypeChecker 実装 - 二項・単項・三項演算子とインデックス・スライス式の型推論
// ============================================================

#include "internal/base/i18n.hpp"
#include "internal/base/text_utils.hpp"
#include "internal/types/type_checker.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cm {

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
        // 代入先は書き込み位置なので、左辺の基底変数を初期化済みとして先にマークする（x = 1 / arr[i] = v / p.field = v を「使用前の未初期化」と誤検出しない）
        ast::Expr* base = binary.left.get();
        while (base) {
            if (auto* idx = base->as<ast::IndexExpr>()) {
                base = idx->object.get();
            } else if (auto* mem = base->as<ast::MemberExpr>()) {
                base = mem->object.get();
            } else if (auto* slc = base->as<ast::SliceExpr>()) {
                // ビットスライス代入 word[11:4] = v も基底変数の変更として扱う
                base = slc->object.get();
            } else {
                break;
            }
        }
        if (base) {
            if (auto* base_ident = base->as<ast::IdentExpr>()) {
                // 要素・フィールドへの書き込みも基底変数の初期化・変更として扱う（名前空間内の非修飾参照は先に修飾名へ解決してからマークする）
                lookup_var_ident(*base_ident);
                mark_variable_initialized(base_ident->name);
                mark_variable_modified(base_ident->name);
            }
        }
    }

    auto ltype = infer_type(*binary.left);
    auto rtype = infer_type(*binary.right);

    if (!ltype || !rtype)
        return ast::make_error();

    // typedef型を基底型に解決（算術演算のis_numeric()チェック用）
    ltype = resolve_typedef(ltype);
    rtype = resolve_typedef(rtype);

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
                // 代入は初期化とみなす（宣言のみ→代入のパターンを未初期化と誤検出しない）
                mark_variable_initialized(ident->name);

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

    // typedef型を基底型に解決（単項演算のis_numeric()チェック用）
    otype = resolve_typedef(otype);

    switch (unary.op) {
        case ast::UnaryOp::Try: {
            // ?演算子: Result<T,E>/Option<T> のエラー伝播。
            // OkならT、Err/Noneなら現在の関数からそのまま早期returnする
            std::string base = otype->name;
            auto lt = base.find('<');
            if (lt != std::string::npos) {
                base = base.substr(0, lt);
            }
            bool is_result_like =
                (otype->kind == ast::TypeKind::Struct && (base == "Result" || base == "Option"));
            if (!is_result_like) {
                error(current_span_,
                      i18n::msgf(i18n::MsgId::TypeCanOnlyBeUsedOn, ast::type_to_string(*otype)));
                return ast::make_error();
            }
            // 現在の関数の戻り値型も同じ種別（Result?はResult返却関数、Option?はOption返却関数）
            std::string ret_base;
            if (current_return_type_) {
                ret_base = current_return_type_->name;
                auto rlt = ret_base.find('<');
                if (rlt != std::string::npos) {
                    ret_base = ret_base.substr(0, rlt);
                }
            }
            if (ret_base != base) {
                error(current_span_,
                      i18n::msgf(i18n::MsgId::TypeCanOnlyBeUsedInside, base,
                                 (current_return_type_ ? ast::type_to_string(*current_return_type_)
                                                       : i18n::msg(i18n::MsgId::TypeLabelNone))));
            }
            // Ok/Someのペイロード型を返す
            if (!otype->type_args.empty() && otype->type_args[0]) {
                return otype->type_args[0];
            }
            return ast::make_int();
        }
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
            // &x / &arr[i] / &s.field はポインタ経由の書き込みがあり得るため、基底変数を保守的に変更あり・初期化済みとして扱う（const推奨・未初期化警告の誤検出防止）
            {
                ast::Expr* addr_base = unary.operand.get();
                while (addr_base) {
                    if (auto* idx = addr_base->as<ast::IndexExpr>()) {
                        addr_base = idx->object.get();
                    } else if (auto* mem = addr_base->as<ast::MemberExpr>()) {
                        addr_base = mem->object.get();
                    } else {
                        break;
                    }
                }
                if (addr_base) {
                    if (auto* base_ident = addr_base->as<ast::IdentExpr>()) {
                        mark_variable_modified(base_ident->name);
                        mark_variable_initialized(base_ident->name);
                    }
                }
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

    // ビットスライス（v0.16.0）: オブジェクトが bit[N] または整数型のとき、x[hi:lo]（定数範囲・SVと同じ降順・両端含む）と x[base +: width] をビット選択として解釈する。結果型は bit[w]
    {
        bool obj_is_bits =
            obj_type && ((obj_type->kind == ast::TypeKind::Array && obj_type->element_type &&
                          obj_type->element_type->kind == ast::TypeKind::Bit) ||
                         obj_type->is_integer() || obj_type->kind == ast::TypeKind::Bit);
        auto lit_value = [](const ast::ExprPtr& e) -> std::optional<int64_t> {
            if (!e) {
                return std::nullopt;
            }
            if (auto* lit = e->as<ast::LiteralExpr>()) {
                if (auto* iv = std::get_if<int64_t>(&lit->value)) {
                    return *iv;
                }
            }
            return std::nullopt;
        };
        if (obj_is_bits && slice.is_part_select) {
            // base は任意の整数式、width は正の整数リテラル
            auto base_type = infer_type(*slice.start);
            if (!base_type || !base_type->is_integer()) {
                error(current_span_, i18n::msg(i18n::MsgId::TypeTheBaseOfAPart));
            }
            auto w = lit_value(slice.end);
            if (!w || *w <= 0 || *w > 64) {
                error(current_span_, i18n::msg(i18n::MsgId::TypePartSelectWidthMustBe));
                return ast::make_error();
            }
            // スカラーbit（幅1）に幅2以上のパートセレクトは不可
            if (obj_type->kind == ast::TypeKind::Bit && *w != 1) {
                error(current_span_, i18n::msg(i18n::MsgId::TypePartSelectWidthOnA));
                return ast::make_error();
            }
            return ast::make_array(ast::make_bit(), static_cast<uint32_t>(*w));
        }
        if (obj_is_bits && slice.start && slice.end && !slice.step) {
            auto hi = lit_value(slice.start);
            auto lo = lit_value(slice.end);
            if (!hi || !lo) {
                error(current_span_, i18n::msg(i18n::MsgId::TypeBitSliceRangesMustBe));
                return ast::make_error();
            }
            if (*lo < 0 || *hi < *lo || *hi - *lo + 1 > 64) {
                error(current_span_, i18n::msg(i18n::MsgId::TypeInvalidBitSliceRangeHi));
                return ast::make_error();
            }
            if (obj_type->kind == ast::TypeKind::Array && obj_type->array_size &&
                *hi >= static_cast<int64_t>(*obj_type->array_size)) {
                error(current_span_, i18n::msg(i18n::MsgId::TypeTheUpperBitOfThe));
                return ast::make_error();
            }
            // スカラーbitは幅1として扱い、[0:0] 以外の範囲はエラー
            if (obj_type->kind == ast::TypeKind::Bit && (*hi != 0 || *lo != 0)) {
                error(current_span_, i18n::msg(i18n::MsgId::TypeBitSlicesOnAScalar));
                return ast::make_error();
            }
            return ast::make_array(ast::make_bit(), static_cast<uint32_t>(*hi - *lo + 1));
        }
    }

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

}  // namespace cm
