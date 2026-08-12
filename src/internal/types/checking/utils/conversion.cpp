// 数値変換の意味論分類と受理サイトの診断（Z5: 暗黙変換と明示キャストの設計整理）。
// 「拡大は暗黙・縮小/符号解釈変化は要as」の設計意図を1つの分類表として定義し、受理判定と診断を同じ表から導く。
// 変換命令の挿入はMIR loweringのcoerce_numeric_context（浮動小数が絡む文脈）と既存のコード生成幅合わせ（整数同士）が担う

#include "internal/base/i18n.hpp"
#include "internal/types/type_checker.hpp"

#include <cstdint>

namespace cm {

namespace {

// 整数リテラル値が宛先整数型の範囲に適合するか（適合するリテラルは縮小/符号変化の診断対象外）。
// negated=単項マイナス付きリテラル（vは符号反転済みの値）
bool int_literal_fits(int64_t v, bool negated, ast::TypeKind target) {
    switch (target) {
        case ast::TypeKind::Tiny:
            return v >= -128 && v <= 127;
        case ast::TypeKind::UTiny:
            return v >= 0 && v <= 255;
        case ast::TypeKind::Short:
            return v >= -32768 && v <= 32767;
        case ast::TypeKind::UShort:
            return v >= 0 && v <= 65535;
        case ast::TypeKind::Int:
            return v >= INT32_MIN && v <= INT32_MAX;
        case ast::TypeKind::UInt:
            return v >= 0 && v <= static_cast<int64_t>(UINT32_MAX);
        case ast::TypeKind::Long:
        case ast::TypeKind::ISize:
            return true;  // int64のあらゆる値はlongに適合する
        case ast::TypeKind::ULong:
        case ast::TypeKind::USize:
            // 素のリテラルは書かれた64bitビットパターンをそのまま保持する（2^63以上の10進・hexは
            // int64格納上は負値になるがulongの意図が明確）。明示的な負値リテラル（-1等）のみ診断する
            return !negated || v >= 0;
        default:
            return true;  // 浮動小数宛先への整数リテラルは適合（丸めのみ）
    }
}

// 診断対象外にするリテラル初期化子か（単項マイナスを1段だけ剥がして判定する）
bool literal_fits_target(const ast::Expr* expr, const ast::Type& target) {
    if (!expr) {
        return false;
    }
    bool negated = false;
    if (const auto* unary = expr->as<ast::UnaryExpr>()) {
        if (unary->op == ast::UnaryOp::Neg && unary->operand) {
            negated = true;
            expr = unary->operand.get();
        }
    }
    const auto* lit = expr->as<ast::LiteralExpr>();
    if (!lit) {
        return false;
    }
    if (lit->is_int()) {
        int64_t v = std::get<int64_t>(lit->value);
        if (negated) {
            v = -v;
        }
        return int_literal_fits(v, negated, target.kind);
    }
    if (lit->is_float()) {
        // 浮動小数リテラルの縮小（double→float）は丸めのみで値の意図が明確なため適合扱い。
        // 整数宛先への浮動小数リテラルは切り捨てが起きるため適合にしない
        return target.is_floating();
    }
    return false;
}

// R11: 符号なし浮動小数型（ufloat/udouble）への負リテラル初期化/代入か（単項マイナスを1段剥がして判定する）
bool is_negative_literal(const ast::Expr* expr) {
    if (!expr) {
        return false;
    }
    bool negated = false;
    if (const auto* unary = expr->as<ast::UnaryExpr>()) {
        if (unary->op == ast::UnaryOp::Neg && unary->operand) {
            negated = true;
            expr = unary->operand.get();
        }
    }
    const auto* lit = expr->as<ast::LiteralExpr>();
    if (!lit) {
        return false;
    }
    if (lit->is_int()) {
        int64_t v = std::get<int64_t>(lit->value);
        return negated ? v > 0 : v < 0;
    }
    if (lit->is_float()) {
        double v = std::get<double>(lit->value);
        return negated ? v > 0 : v < 0;
    }
    return false;
}

}  // namespace

TypeChecker::NumericConversion TypeChecker::classify_numeric_conversion(
    const ast::TypePtr& target, const ast::TypePtr& source) {
    if (!target || !source) {
        return NumericConversion::NotNumeric;
    }
    auto t = resolve_typedef(target);
    auto s = resolve_typedef(source);
    if (!t || !s || !t->is_numeric() || !s->is_numeric()) {
        return NumericConversion::NotNumeric;
    }
    if (t->kind == s->kind) {
        return NumericConversion::Identity;
    }

    // f32/f64の実表現幅（UFloat/Floatのような符号制約のみの違いは同幅）
    auto float_width = [](ast::TypeKind k) {
        return (k == ast::TypeKind::Float || k == ast::TypeKind::UFloat) ? 32u : 64u;
    };

    if (t->is_floating() && s->is_floating()) {
        if (float_width(t->kind) > float_width(s->kind)) {
            return NumericConversion::Widening;
        }
        if (float_width(t->kind) < float_width(s->kind)) {
            return NumericConversion::Narrowing;  // double→float
        }
        return NumericConversion::Identity;  // float↔ufloat等の符号制約のみの違い
    }
    if (t->is_floating()) {
        return NumericConversion::Widening;  // 整数→浮動小数（int→float/doubleは設計上の拡大）
    }
    if (s->is_floating()) {
        return NumericConversion::Narrowing;  // 浮動小数→整数（fptosiの切り捨て）
    }

    // 整数同士
    const uint32_t t_size = t->info().size;
    const uint32_t s_size = s->info().size;
    if (t_size > s_size) {
        // 符号付き→広い符号なし（int→ulong等）は負値の意味が変わるため要as。それ以外の拡大は値を保存する
        if (s->is_signed() && !t->is_signed()) {
            return NumericConversion::SignChange;
        }
        return NumericConversion::Widening;
    }
    if (t_size == s_size) {
        return NumericConversion::SignChange;  // int↔uint等（同幅・符号解釈のみ変化）
    }
    return NumericConversion::Narrowing;  // long→int・int→short等
}

void TypeChecker::check_numeric_conversion_policy(const ast::TypePtr& target,
                                                  const ast::TypePtr& source,
                                                  const ast::Expr* value_expr, Span span) {
    // R11: ufloat/udoubleへの負リテラルを診断する（非負制約がどこにも強制されず事実上float/doubleの別名になっていた。実行時に負になる演算までは検査しない）
    {
        auto ut = resolve_typedef(target);
        if (ut && ut->is_unsigned_float() && is_negative_literal(value_expr)) {
            const std::string msg =
                i18n::msgf(i18n::MsgId::TcNegativeValueUnsignedFloat, ast::type_to_string(*ut));
            if (enable_naming_check_) {
                error(span, msg);
            } else {
                warning(span, msg);
            }
            return;
        }
    }
    const auto kind = classify_numeric_conversion(target, source);
    if (kind != NumericConversion::Narrowing && kind != NumericConversion::SignChange) {
        return;
    }
    auto t = resolve_typedef(target);
    auto s_pre = resolve_typedef(source);
    // 段階導入の現段階では符号なし整数→符号付き整数は診断しない（分類上は縮小/符号変化）。
    // len()/cap()/sizeof等がuint/usizeを返し「int n = arr.len()」が言語全体の既存イディオムであるため、
    // このパターンへの一斉警告は churn が大きすぎる。2^31超の実害は稀であり、将来--strictでの再検討課題として仕様に記録する
    if (t && s_pre && t->is_integer() && s_pre->is_integer() && !s_pre->is_signed() &&
        t->is_signed()) {
        return;
    }
    if (t && literal_fits_target(value_expr, *t)) {
        return;
    }
    const auto msg_id = (kind == NumericConversion::Narrowing)
                            ? i18n::MsgId::TcImplicitNarrowingConversion
                            : i18n::MsgId::TcImplicitSignConversion;
    const std::string msg =
        i18n::msgf(msg_id, ast::type_to_string(*s_pre), ast::type_to_string(*t));
    // 段階導入（Z5）: 通常は警告、--strict（check/lint --strict）ではエラーへ昇格する（H6と同じ運用）
    if (enable_naming_check_) {
        error(span, msg);
    } else {
        warning(span, msg);
    }
}

}  // namespace cm
