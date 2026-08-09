// ============================================================
// TypeChecker 実装 - コンパイル時定数評価と配列サイズのsize_param_name解決
// ============================================================

#include "internal/base/i18n.hpp"
#include "internal/types/type_checker.hpp"

#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace cm {

// ============================================================
// コンパイル時定数評価（const強化）
// ============================================================
std::optional<int64_t> TypeChecker::evaluate_const_expr(ast::Expr& expr) {
    // リテラル: 整数値を直接返す
    if (auto* lit = expr.as<ast::LiteralExpr>()) {
        if (std::holds_alternative<int64_t>(lit->value)) {
            return std::get<int64_t>(lit->value);
        }
        // bool値もint64_tに変換可能
        if (std::holds_alternative<bool>(lit->value)) {
            return std::get<bool>(lit->value) ? 1 : 0;
        }
        // double/charは整数として評価不可
        return std::nullopt;
    }

    // 識別子: const変数を検索
    if (auto* ident = expr.as<ast::IdentExpr>()) {
        auto sym = scopes_.current().lookup(ident->name);
        if (sym && sym->is_const && sym->const_int_value.has_value()) {
            return sym->const_int_value;
        }
        return std::nullopt;
    }

    // 単項演算子
    if (auto* unary = expr.as<ast::UnaryExpr>()) {
        auto val = evaluate_const_expr(*unary->operand);
        if (!val)
            return std::nullopt;

        switch (unary->op) {
            case ast::UnaryOp::Neg:
                return -(*val);
            case ast::UnaryOp::Not:
                return (*val) ? 0 : 1;
            case ast::UnaryOp::BitNot:
                return ~(*val);
            default:
                return std::nullopt;
        }
    }

    // 二項演算子
    if (auto* binary = expr.as<ast::BinaryExpr>()) {
        auto left_val = evaluate_const_expr(*binary->left);
        auto right_val = evaluate_const_expr(*binary->right);
        if (!left_val || !right_val)
            return std::nullopt;

        switch (binary->op) {
            case ast::BinaryOp::Add:
                return *left_val + *right_val;
            case ast::BinaryOp::Sub:
                return *left_val - *right_val;
            case ast::BinaryOp::Mul:
                return *left_val * *right_val;
            case ast::BinaryOp::Div:
                if (*right_val == 0)
                    return std::nullopt;  // ゼロ除算
                // int64で負に見える値はunsigned定数の可能性があり、符号付き除算だと
                // 結果が変わるため畳み込みを断念する（実行時は型に従い正しく計算される）
                if (*left_val < 0 || *right_val < 0)
                    return std::nullopt;
                return *left_val / *right_val;
            case ast::BinaryOp::Mod:
                if (*right_val == 0)
                    return std::nullopt;
                if (*left_val < 0 || *right_val < 0)
                    return std::nullopt;
                return *left_val % *right_val;
            case ast::BinaryOp::BitAnd:
                return *left_val & *right_val;
            case ast::BinaryOp::BitOr:
                return *left_val | *right_val;
            case ast::BinaryOp::BitXor:
                return *left_val ^ *right_val;
            case ast::BinaryOp::Shl:
                return *left_val << *right_val;
            case ast::BinaryOp::Shr:
                // 最上位ビットが立つ値はunsignedの論理シフトと符号付きの算術シフトで結果が分かれるため畳み込みを断念する（MIR側は型情報を持ち正しく畳む）
                if (*left_val < 0)
                    return std::nullopt;
                return *left_val >> *right_val;
            case ast::BinaryOp::Lt:
                return (*left_val < *right_val) ? 1 : 0;
            case ast::BinaryOp::Le:
                return (*left_val <= *right_val) ? 1 : 0;
            case ast::BinaryOp::Gt:
                return (*left_val > *right_val) ? 1 : 0;
            case ast::BinaryOp::Ge:
                return (*left_val >= *right_val) ? 1 : 0;
            case ast::BinaryOp::Eq:
                return (*left_val == *right_val) ? 1 : 0;
            case ast::BinaryOp::Ne:
                return (*left_val != *right_val) ? 1 : 0;
            case ast::BinaryOp::And:
                return (*left_val && *right_val) ? 1 : 0;
            case ast::BinaryOp::Or:
                return (*left_val || *right_val) ? 1 : 0;
            default:
                return std::nullopt;
        }
    }

    // 三項演算子
    if (auto* ternary = expr.as<ast::TernaryExpr>()) {
        auto cond = evaluate_const_expr(*ternary->condition);
        if (!cond)
            return std::nullopt;
        return *cond ? evaluate_const_expr(*ternary->then_expr)
                     : evaluate_const_expr(*ternary->else_expr);
    }

    // その他の式はコンパイル時評価不可
    return std::nullopt;
}

// ============================================================
// 配列サイズのsize_param_name解決（const強化）
// ============================================================
void TypeChecker::resolve_array_size(ast::TypePtr& type, bool best_effort) {
    if (!type)
        return;

    // 配列型でsize_param_nameが設定されている場合
    if (type->kind == ast::TypeKind::Array && !type->size_param_name.empty()) {
        // const変数を検索
        auto sym = scopes_.current().lookup(type->size_param_name);
        if (sym && sym->is_const && sym->const_int_value.has_value()) {
            int64_t size = *sym->const_int_value;
            if (size > 0 && size <= INT32_MAX) {
                type->array_size = static_cast<uint32_t>(size);
                // 注: size_param_name は解決後も保持する。
                // SVバックエンドが #[sv::parameter] の幅を記号のまま（[WIDTH-1:0]）出力するために使用する（v0.16.0 設計01）

                debug::tc::log(debug::tc::Id::TypeInfer,
                               "Resolved array size: " + sym->name + " = " + std::to_string(size),
                               debug::Level::Debug);
            } else if (!best_effort) {
                error(current_span_, i18n::msgf(i18n::MsgId::TcArraySizeMustPositiveInteger,
                                                std::to_string(size), type->size_param_name));
            }
        } else if (best_effort) {
            // ベストエフォート解決（構造体フィールド）: constとして解決できない名前は診断せず記号名のまま残す。
            // bit[WIDTH]のように同struct内の#[sv::param]フィールドやimport越しの未束縛パラメータを幅に使うため
        } else if (sym && !sym->is_const) {
            error(current_span_,
                  i18n::msgf(i18n::MsgId::TcArraySizeMustConstVariable, type->size_param_name));
        } else if (!sym) {
            error(current_span_,
                  i18n::msgf(i18n::MsgId::TcUndefinedVariableUsedArraySize, type->size_param_name));
        } else {
            error(current_span_,
                  i18n::msgf(i18n::MsgId::TcConstVariableDoesNotHave, type->size_param_name));
        }
    }

    // 要素型も再帰的に解決
    if (type->element_type) {
        resolve_array_size(type->element_type, best_effort);
    }
}

}  // namespace cm
