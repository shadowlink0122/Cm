// 暗黙変換の種別分類の実装（仕様は convkind.hpp を参照）

#include "convkind.hpp"

namespace cm::ast::convkind {

Kind classify(const TypePtr& dest, const TypePtr& src, const Env& env) {
    if (!dest || !src) {
        return Kind::None;
    }
    TypePtr d = dest;
    TypePtr s = src;
    if (env.resolve) {
        if (auto rd = env.resolve(dest)) {
            d = rd;
        }
        if (auto rs = env.resolve(src)) {
            s = rs;
        }
    }
    if (!d || !s) {
        return Kind::None;
    }

    // T -> Union（宛先がユニオンで値が非ユニオン。変種の互換・事前正規化は担当側の規則）
    if (d->kind == TypeKind::Union && s->kind != TypeKind::Union) {
        return Kind::UnionWrap;
    }

    // 具象構造体 -> interface値
    if (d->kind == TypeKind::Struct && env.is_interface && env.is_interface(d->name) &&
        s->kind == TypeKind::Struct && !s->name.empty() && !env.is_interface(s->name)) {
        return Kind::IfaceValueUpcast;
    }

    // 具象構造体ポインタ -> interfaceポインタ
    if (d->kind == TypeKind::Pointer && d->element_type && s->kind == TypeKind::Pointer &&
        s->element_type && env.is_interface) {
        TypePtr de = d->element_type;
        TypePtr se = s->element_type;
        if (env.resolve) {
            if (auto r = env.resolve(de)) {
                de = r;
            }
            if (auto r = env.resolve(se)) {
                se = r;
            }
        }
        if (de && de->kind == TypeKind::Struct && env.is_interface(de->name) && se &&
            se->kind == TypeKind::Struct && !se->name.empty() && !env.is_interface(se->name)) {
            return Kind::IfacePtrUpcast;
        }
    }

    // 固定長配列 -> スライス
    if (d->kind == TypeKind::Array && !d->array_size.has_value() && s->kind == TypeKind::Array &&
        s->array_size.has_value()) {
        return Kind::ArrayToSlice;
    }

    // 数値型間の暗黙変換（同kindの数値は変換不要のためNone）
    if (d->is_numeric() && s->is_numeric() && d->kind != s->kind) {
        return Kind::NumericImplicit;
    }

    return Kind::None;
}

}  // namespace cm::ast::convkind
