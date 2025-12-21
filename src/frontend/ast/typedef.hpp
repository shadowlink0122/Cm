#pragma once

#include "types.hpp"
#include "decl.hpp"

#include <string>
#include <variant>
#include <vector>

namespace cm::ast {

// ============================================================
// リテラル型
// ============================================================
struct LiteralType {
    std::variant<std::string,  // 文字列リテラル
                 int64_t,      // 整数リテラル
                 double        // 浮動小数点リテラル
                 >
        value;

    LiteralType(std::string v) : value(std::move(v)) {}
    LiteralType(int64_t v) : value(v) {}
    LiteralType(double v) : value(v) {}
};

// ============================================================
// ユニオン型のバリアント
// ============================================================
struct UnionVariant {
    std::string tag;                       // タグ名（"ok", "err" など）
    std::vector<TypePtr> fields;           // フィールドの型
    std::vector<std::string> field_names;  // フィールド名（オプション）

    UnionVariant(std::string t) : tag(std::move(t)) {}
    UnionVariant(std::string t, std::vector<TypePtr> f) : tag(std::move(t)), fields(std::move(f)) {}
};

// ============================================================
// ユニオン型
// ============================================================
struct UnionType : Type {
    std::vector<UnionVariant> variants;

    UnionType() : Type(TypeKind::Union) {}

    void add_variant(UnionVariant v) { variants.push_back(std::move(v)); }
};

// ============================================================
// リテラルユニオン型（"a" | "b" | "c"）
// ============================================================
struct LiteralUnionType : Type {
    std::vector<LiteralType> literals;

    LiteralUnionType() : Type(TypeKind::LiteralUnion) {}

    void add_literal(LiteralType lit) { literals.push_back(std::move(lit)); }
};

// ============================================================
// typedef宣言
// ============================================================
struct TypedefDecl {
    std::string name;                             // エイリアス名
    TypePtr type;                                 // 実際の型
    std::vector<std::string> type_params;         // ジェネリックパラメータ
    Visibility visibility = Visibility::Private;  // v4 module system

    TypedefDecl(std::string n, TypePtr t) : name(std::move(n)), type(std::move(t)) {}

    TypedefDecl(std::string n, std::vector<std::string> params, TypePtr t)
        : name(std::move(n)), type(std::move(t)), type_params(std::move(params)) {}
};

// ============================================================
// ヘルパー関数
// ============================================================

// リテラルユニオン型を作成
inline TypePtr make_literal_union(std::vector<LiteralType> literals) {
    auto type = std::make_unique<LiteralUnionType>();
    for (auto& lit : literals) {
        type->add_literal(std::move(lit));
    }
    return type;
}

// ユニオン型を作成
inline TypePtr make_union(std::vector<UnionVariant> variants) {
    auto type = std::make_unique<UnionType>();
    for (auto& v : variants) {
        type->add_variant(std::move(v));
    }
    return type;
}

// Result<T, E> 型を作成
inline TypePtr make_result_type(TypePtr ok_type, TypePtr err_type) {
    std::vector<UnionVariant> variants;

    UnionVariant ok("ok");
    ok.fields.push_back(std::move(ok_type));
    ok.field_names.push_back("value");

    UnionVariant err("err");
    err.fields.push_back(std::move(err_type));
    err.field_names.push_back("error");

    variants.push_back(std::move(ok));
    variants.push_back(std::move(err));

    return make_union(std::move(variants));
}

// Option<T> 型を作成
inline TypePtr make_option_type(TypePtr some_type) {
    std::vector<UnionVariant> variants;

    UnionVariant some("some");
    some.fields.push_back(std::move(some_type));
    some.field_names.push_back("value");

    UnionVariant none("none");

    variants.push_back(std::move(some));
    variants.push_back(std::move(none));

    return make_union(std::move(variants));
}

}  // namespace cm::ast