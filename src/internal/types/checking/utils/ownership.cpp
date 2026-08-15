// dtor持ち構造体の暗黙コピー診断（二重解放ハザードの言語側対策）。
// Cmの構造体代入は浅いコピーで、デストラクタ持ち構造体を値としてコピーすると両方のインスタンスでdtorが走り二重解放になる
// （HashSetコンストラクタで実際に発生: docs/archive/v0.17.2/bugfix-hashset-double-free.md）。
// 受理サイト（let初期化・代入・return・構造体リテラルのフィールド初期化）で場所式のコピーを検出し、moveによる所有権移動を提案する。
// Z5（数値縮小変換）と同じ段階導入方式: 通常は警告、--strict（check/lint）ではエラーへ昇格する

#include "internal/base/i18n.hpp"
#include "internal/types/type_checker.hpp"

namespace cm {

namespace {

// 場所式（変数参照・フィールド参照・要素参照）か。
// 一時値（コンストラクタ呼び出し・メソッド戻り値等）は所有が一意でコピー元が残らないため診断対象にしない
bool is_place_expr(const ast::Expr& expr) {
    if (expr.as<ast::IdentExpr>()) {
        return true;
    }
    if (const auto* member = expr.as<ast::MemberExpr>()) {
        return !member->is_method_call;
    }
    if (expr.as<ast::IndexExpr>()) {
        return true;
    }
    return false;
}

}  // namespace

bool TypeChecker::type_has_destructor(const ast::TypePtr& type) {
    if (!type) {
        return false;
    }
    auto t = resolve_typedef(type);
    if (!t || t->kind != ast::TypeKind::Struct) {
        return false;
    }
    if (types_with_destructor_.count(t->name)) {
        return true;
    }
    // 特殊化サフィックス（Name<...>・Name__k）と名前空間修飾を剥がして再照合する
    const std::string base = strip_spec_suffix(t->name);
    if (types_with_destructor_.count(base)) {
        return true;
    }
    const size_t pos = base.rfind("::");
    if (pos != std::string::npos && types_with_destructor_.count(base.substr(pos + 2))) {
        return true;
    }
    return false;
}

void TypeChecker::check_dtor_copy_policy(const ast::TypePtr& source_type,
                                         const ast::Expr* value_expr, Span span) {
    if (!value_expr) {
        return;
    }
    // move式は所有権移動（移動元のdtor登録がMIRで解除される）なので対象外
    if (value_expr->as<ast::MoveExpr>()) {
        return;
    }
    if (!is_place_expr(*value_expr)) {
        return;
    }
    if (!type_has_destructor(source_type)) {
        return;
    }
    auto t = resolve_typedef(source_type);
    const std::string msg = i18n::msgf(i18n::MsgId::TcImplicitDtorCopy, ast::type_to_string(*t));
    if (enable_naming_check_) {
        error(span, msg);
    } else {
        warning(span, msg);
    }
}

}  // namespace cm
