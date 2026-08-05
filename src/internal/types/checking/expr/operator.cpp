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

namespace {

// オペランドを昇格先型へ包む明示Castノードを挿入する（Y4）。
// 型検査を変換判断の唯一の点とし、HIR/MIR/コード生成は「二項演算のオペランドは同型」を前提にできる
void wrap_operand_cast(ast::ExprPtr& operand, const ast::TypePtr& target) {
    Span span = operand->span;
    auto cast = std::make_unique<ast::CastExpr>(std::move(operand), target);
    operand = std::make_unique<ast::Expr>(std::move(cast), span);
    operand->type = target;
}

}  // namespace

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
                error(binary.left->span,
                      i18n::msgf(i18n::MsgId::TcCannotAssignMovedVariableVariable, ident->name));
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
                // M3: const集約のフィールド・要素への代入は現行仕様では浅いconstとして素通りする。
                // 既存コードが依存しているため破壊的変更を避け、check/lint時の警告として段階導入する（将来エラー化）
                if (base != binary.left.get() && enable_lint_warnings_) {
                    auto base_sym = scopes_.current().lookup(base_ident->name);
                    if (base_sym && base_sym->is_const) {
                        // MemberExpr/IndexExprのspanは未設定のことがあるため基底式のspanを使う
                        warning(base->span, i18n::msgf(i18n::MsgId::TypeAssignToConstAggregate,
                                                       base_ident->name));
                    }
                }
                mark_variable_initialized(base_ident->name);
                mark_variable_modified(base_ident->name);
            }
        }
    }

    auto ltype = infer_type(*binary.left);
    // 単純代入は左辺型を期待型として右辺へ渡す（無名リテラル代入の型決定を一元化）
    auto rtype = (binary.op == ast::BinaryOp::Assign) ? infer_type_expecting(*binary.right, ltype)
                                                      : infer_type(*binary.right);

    if (!ltype || !rtype)
        return ast::make_error();

    // typedef型を基底型に解決（算術演算のis_numeric()チェック用）
    ltype = resolve_typedef(ltype);
    rtype = resolve_typedef(rtype);

    // 代入系演算の縮小/符号変化診断用に、Y4の昇格Cast挿入前の右辺型と式を退避する（Z5）
    const auto rtype_before_promotion = rtype;
    const ast::Expr* rhs_expr_before_promotion = binary.right.get();

    // floatオペランド×浮動小数リテラルは、doubleへの共通昇格でなくリテラル側をfloat文脈へ適合させる（Z5）。
    // 「return v / 2.0;」（vはfloat）が双方doubleへ昇格されると戻り値でdouble→floatの縮小警告になる誤検出を防ぎ、
    // 演算自体もユーザーの意図どおりfloat幅で行う
    {
        auto is_float32 = [](const ast::TypePtr& t) {
            return t->kind == ast::TypeKind::Float || t->kind == ast::TypeKind::UFloat;
        };
        auto is_plain_float_literal = [](const ast::ExprPtr& e) {
            const ast::Expr* p = e.get();
            if (const auto* unary = p->as<ast::UnaryExpr>()) {
                if (unary->op == ast::UnaryOp::Neg && unary->operand) {
                    p = unary->operand.get();
                }
            }
            const auto* lit = p->as<ast::LiteralExpr>();
            return lit && lit->is_float();
        };
        if (is_float32(ltype) && rtype->kind == ast::TypeKind::Double &&
            is_plain_float_literal(binary.right)) {
            wrap_operand_cast(binary.right, ltype);
            rtype = ltype;
        } else if (is_float32(rtype) && ltype->kind == ast::TypeKind::Double &&
                   is_plain_float_literal(binary.left)) {
            wrap_operand_cast(binary.left, rtype);
            ltype = rtype;
        }
    }

    // 混合数値オペランドの暗黙昇格（Y4）。
    // 従来は型検査が混合を受理したままオペランド昇格を挿入せず、fadd i32, double等の不正IRや無出力SIGBUSになっていた。
    // 浮動小数が絡む混合のみ共通型へ揃える（整数同士の幅混在は既存のコード生成幅合わせが機能しているため挙動を変えない）
    {
        const bool is_arith_or_cmp =
            binary.op == ast::BinaryOp::Add || binary.op == ast::BinaryOp::Sub ||
            binary.op == ast::BinaryOp::Mul || binary.op == ast::BinaryOp::Div ||
            binary.op == ast::BinaryOp::Mod || binary.op == ast::BinaryOp::Eq ||
            binary.op == ast::BinaryOp::Ne || binary.op == ast::BinaryOp::Lt ||
            binary.op == ast::BinaryOp::Gt || binary.op == ast::BinaryOp::Le ||
            binary.op == ast::BinaryOp::Ge;
        const bool is_arith_compound =
            binary.op == ast::BinaryOp::AddAssign || binary.op == ast::BinaryOp::SubAssign ||
            binary.op == ast::BinaryOp::MulAssign || binary.op == ast::BinaryOp::DivAssign ||
            binary.op == ast::BinaryOp::ModAssign;
        const bool mixed_with_float = ltype->is_numeric() && rtype->is_numeric() &&
                                      ltype->kind != rtype->kind &&
                                      (ltype->is_floating() || rtype->is_floating());
        if (mixed_with_float && is_arith_or_cmp) {
            auto target = common_type(ltype, rtype);
            if (target) {
                if (ltype->kind != target->kind) {
                    wrap_operand_cast(binary.left, target);
                    ltype = target;
                }
                if (rtype->kind != target->kind) {
                    wrap_operand_cast(binary.right, target);
                    rtype = target;
                }
            }
        } else if (mixed_with_float && is_arith_compound) {
            // 複合代入は宛先型（左辺）へ右辺を揃える。double += int はsitofp、int += double はfptosi（切り詰め）
            wrap_operand_cast(binary.right, ltype);
            rtype = ltype;
        }
    }

    // 代入・複合代入の縮小/符号変化の暗黙変換を診断（Z5。適合リテラルは対象外、--strictではエラー昇格）
    if (is_assignment) {
        check_numeric_conversion_policy(
            ltype, rtype_before_promotion, rhs_expr_before_promotion,
            binary.right->span.start != 0 ? binary.right->span : current_span_);
    }

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
                error(current_span_, i18n::msg(i18n::MsgId::TcLogicalOperatorsRequireBoolOperands));
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
                          i18n::msgf(i18n::MsgId::TcCannotAssignConstVariable, ident->name));
                    return ast::make_error();
                }
                // 借用チェック: 借用中の変数への代入を禁止（DRY原則）
                if (scopes_.current().is_borrowed(ident->name)) {
                    error(binary.left->span,
                          i18n::msgf(i18n::MsgId::TcCannotAssignWhileItBorrowed, ident->name));
                    return ast::make_error();
                }
                // 変数が変更されたことをマーク（const推奨警告用）
                mark_variable_modified(ident->name);
                // 代入は初期化とみなす（宣言のみ→代入のパターンを未初期化と誤検出しない）
                mark_variable_initialized(ident->name);

                // M3段階3: 非constポインタへのconst基点&式の代入を警告（const int*は対象外）
                if (binary.op == ast::BinaryOp::Assign) {
                    warn_addr_of_const_into_mutable_ptr(ltype, binary.right.get());
                }

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
                                          i18n::msgf(i18n::MsgId::TcCannotStoreReferenceMayDropped,
                                                     rhs_ident->name, ident->name, rhs_ident->name,
                                                     ident->name));
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
                            error(binary.left->span,
                                  i18n::msg(i18n::MsgId::TcCannotAssignThroughConstPointer));
                            return ast::make_error();
                        }
                        // 要素型がconstの場合も禁止（const修飾された要素への代入）
                        if (ptr_type->element_type && ptr_type->element_type->qualifiers.is_const) {
                            error(binary.left->span,
                                  i18n::msg(i18n::MsgId::TcCannotAssignThroughPointerConst));
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
                    error(binary.left->span, i18n::msgf(i18n::MsgId::TcTypeDoesNotImplementOperator,
                                                        type_name, iface_name));
                    return ast::make_error();
                }
            }
            if (!types_compatible(ltype, rtype)) {
                error(binary.left->span, i18n::msg(i18n::MsgId::TcAssignmentTypeMismatch));
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
            error(current_span_, i18n::msg(i18n::MsgId::TcAddOperatorRequiresNumericOperands));
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
            error(current_span_, i18n::msg(i18n::MsgId::TcSubOperatorRequiresNumericOperands));
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
                error(current_span_,
                      i18n::msg(i18n::MsgId::TcArithmeticOperatorsRequireNumericOperands));
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
                error(current_span_, i18n::msg(i18n::MsgId::TcNegationRequiresNumericOperand));
            }
            return otype;
        case ast::UnaryOp::Not:
            if (otype->kind != ast::TypeKind::Bool) {
                error(current_span_, i18n::msg(i18n::MsgId::TcLogicalNotRequiresBoolOperand));
            }
            return std::make_shared<ast::Type>(ast::TypeKind::Bool);
        case ast::UnaryOp::BitNot:
            if (!otype->is_integer()) {
                error(current_span_, i18n::msg(i18n::MsgId::TcBitwiseNotRequiresIntegerOperand));
            }
            return otype;
        case ast::UnaryOp::Deref:
            if (otype->kind != ast::TypeKind::Pointer) {
                error(current_span_, i18n::msg(i18n::MsgId::TcCannotDereferenceNonPointer));
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
                          i18n::msgf(i18n::MsgId::TcCannotModifyConstVariable, ident->name));
                    return ast::make_error();
                }
                // 借用チェック: 借用中の変数への変更を禁止（DRY原則）
                if (scopes_.current().is_borrowed(ident->name)) {
                    error(unary.operand->span,
                          i18n::msgf(i18n::MsgId::TcCannotModifyWhileItBorrowed, ident->name));
                    return ast::make_error();
                }
                // 変数が変更されたことをマーク（const推奨警告用）
                mark_variable_modified(ident->name);
            }
            if (!otype->is_numeric()) {
                error(current_span_,
                      i18n::msg(i18n::MsgId::TcIncrementDecrementRequiresNumericOperand));
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
        error(current_span_, i18n::msg(i18n::MsgId::TcTernaryConditionMustBoolInt));
    }

    auto then_type = infer_type(*ternary.then_expr);
    auto else_type = infer_type(*ternary.else_expr);

    if (!types_compatible(then_type, else_type)) {
        error(current_span_, i18n::msg(i18n::MsgId::TcTernaryBranchesHaveIncompatibleTypes));
    }

    // 数値同士の腕は昇格型（幅の広い方、同幅は符号なし優先）を返す。
    // then側固定だと `false ? 0 : uint値` がint扱いになり、4000000000が-294967296と表示される
    if (then_type && else_type && then_type->is_integer() && else_type->is_integer() &&
        then_type->kind != else_type->kind) {
        auto int_width = [](ast::TypeKind k) -> int {
            switch (k) {
                case ast::TypeKind::Tiny:
                case ast::TypeKind::UTiny:
                    return 8;
                case ast::TypeKind::Short:
                case ast::TypeKind::UShort:
                    return 16;
                case ast::TypeKind::Int:
                case ast::TypeKind::UInt:
                    return 32;
                default:
                    return 64;
            }
        };
        auto is_uns = [](ast::TypeKind k) {
            return k == ast::TypeKind::UTiny || k == ast::TypeKind::UShort ||
                   k == ast::TypeKind::UInt || k == ast::TypeKind::ULong ||
                   k == ast::TypeKind::USize;
        };
        const int tw = int_width(then_type->kind);
        const int ew = int_width(else_type->kind);
        if (tw != ew) {
            return tw > ew ? then_type : else_type;
        }
        return is_uns(then_type->kind) ? then_type : else_type;
    }
    // float/doubleの混在はdouble優先（thenがfloat固定だと精度が落ちる）
    if (then_type && else_type && then_type->is_floating() && else_type->is_floating() &&
        then_type->kind != else_type->kind) {
        const bool then_double =
            then_type->kind == ast::TypeKind::Double || then_type->kind == ast::TypeKind::UDouble;
        return then_double ? then_type : else_type;
    }

    return then_type;
}

ast::TypePtr TypeChecker::infer_index(ast::IndexExpr& idx) {
    auto obj_type = infer_type(*idx.object);
    auto index_type = infer_type(*idx.index);
    if (!index_type || !index_type->is_integer()) {
        error(current_span_, i18n::msg(i18n::MsgId::TcArrayIndexMustIntegerType));
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

    error(current_span_, i18n::msg(i18n::MsgId::TcIndexAccessNonArrayType));
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
            error(current_span_, i18n::msg(i18n::MsgId::TcSliceStartIndexMustInteger));
        }
    }
    if (slice.end) {
        auto end_type = infer_type(*slice.end);
        if (!end_type || !end_type->is_integer()) {
            error(current_span_, i18n::msg(i18n::MsgId::TcSliceEndIndexMustInteger));
        }
    }
    if (slice.step) {
        auto step_type = infer_type(*slice.step);
        if (!step_type || !step_type->is_integer()) {
            error(current_span_, i18n::msg(i18n::MsgId::TcSliceStepMustIntegerType));
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

    error(current_span_, i18n::msg(i18n::MsgId::TcSliceAccessNonArrayString));
    return ast::make_error();
}

}  // namespace cm
