#pragma once

#include "expr.hpp"
#include "module.hpp"  // for AttributeNode
#include "nodes.hpp"
#include "stmt.hpp"

#include <optional>

namespace cm::ast {

// ============================================================
// 可視性
// ============================================================
enum class Visibility {
    Private,  // デフォルト（ファイル内）
    Export,   // 他ファイルからアクセス可能
};

// ============================================================
// 型制約の種類
// ============================================================
enum class ConstraintKind {
    Interface,  // T: Eq (インターフェースを実装している)
    Type,       // T: Polygon (具体的な型)
    Union,      // T: int | string (複数の型のいずれか)
};

// ============================================================
// 型制約
// ============================================================
struct TypeConstraint {
    ConstraintKind kind = ConstraintKind::Type;
    std::vector<std::string>
        types;  // Union: ["int", "string"], Interface: ["Eq"], Type: ["Polygon"]

    TypeConstraint() = default;
    explicit TypeConstraint(std::string single_type) : kind(ConstraintKind::Type) {
        types.push_back(std::move(single_type));
    }
    TypeConstraint(ConstraintKind k, std::vector<std::string> t) : kind(k), types(std::move(t)) {}
};

// ============================================================
// ジェネリックパラメータ（型制約付き）
// ============================================================
struct GenericParam {
    std::string name;                      // 型パラメータ名（T, U等）
    std::vector<std::string> constraints;  // 型制約（Ord, Clone等） - 後方互換性
    TypeConstraint type_constraint;        // 新しい型制約（ユニオン型対応）
    bool is_union_constraint = false;      // T: int | string のようなユニオン制約か

    GenericParam() = default;
    explicit GenericParam(std::string n) : name(std::move(n)) {}
    GenericParam(std::string n, std::vector<std::string> c)
        : name(std::move(n)), constraints(std::move(c)) {
        // 後方互換性: constraintsをtype_constraintに変換
        if (!c.empty()) {
            if (c.size() == 1) {
                type_constraint = TypeConstraint(c[0]);
            } else {
                // 複数の制約は「+」で結合されたインターフェース制約
                type_constraint = TypeConstraint(ConstraintKind::Interface, c);
            }
        }
    }
    GenericParam(std::string n, TypeConstraint tc, bool is_union = false)
        : name(std::move(n)), type_constraint(std::move(tc)), is_union_constraint(is_union) {
        // 後方互換性: type_constraintからconstraintsを設定
        constraints = type_constraint.types;
    }
};

// ============================================================
// 関数定義
// ============================================================
struct FunctionDecl {
    std::string name;
    std::vector<Param> params;
    TypePtr return_type;
    std::vector<StmtPtr> body;

    // 修飾子
    Visibility visibility = Visibility::Private;
    bool is_static = false;
    bool is_inline = false;
    bool is_async = false;

    // コンストラクタ/デストラクタ
    bool is_constructor = false;  // self() コンストラクタ
    bool is_destructor = false;   // ~self() デストラクタ
    bool is_overload = false;     // overload修飾子

    // ディレクティブ/アトリビュート（#test, #bench, #deprecated等）
    std::vector<AttributeNode> attributes;

    // ジェネリクス
    std::vector<std::string> generic_params;      // 後方互換性のため維持
    std::vector<GenericParam> generic_params_v2;  // 型制約付き

    FunctionDecl(std::string n, std::vector<Param> p, TypePtr r, std::vector<StmtPtr> b)
        : name(std::move(n)), params(std::move(p)), return_type(std::move(r)), body(std::move(b)) {}
};

// ============================================================
// フィールド定義
// ============================================================
struct Field {
    std::string name;
    TypePtr type;
    Visibility visibility = Visibility::Private;
    TypeQualifiers qualifiers;
    ExprPtr default_value;    // オプション
    bool is_default = false;  // デフォルトメンバ（構造体に1つだけ）
};

// ============================================================
// 構造体定義
// ============================================================
struct StructDecl {
    std::string name;
    std::vector<Field> fields;
    Visibility visibility = Visibility::Private;

    // with キーワードで自動実装するinterface
    std::vector<std::string> auto_impls;

    // ジェネリクス
    std::vector<std::string> generic_params;      // 後方互換性のため維持
    std::vector<GenericParam> generic_params_v2;  // 型制約付き

    StructDecl(std::string n, std::vector<Field> f) : name(std::move(n)), fields(std::move(f)) {}
};

// ============================================================
// メソッドシグネチャ
// ============================================================
struct MethodSig {
    std::string name;
    std::vector<Param> params;
    TypePtr return_type;
};

// ============================================================
// 演算子の種類
// ============================================================
enum class OperatorKind {
    Eq,      // ==
    Ne,      // != (自動導出)
    Lt,      // <
    Gt,      // > (自動導出)
    Le,      // <= (自動導出)
    Ge,      // >= (自動導出)
    Add,     // +
    Sub,     // -
    Mul,     // *
    Div,     // /
    Mod,     // %
    BitAnd,  // &
    BitOr,   // |
    BitXor,  // ^
    Shl,     // <<
    Shr,     // >>
    Neg,     // - (unary)
    Not,     // ! (unary)
    BitNot,  // ~ (unary)
};

// ============================================================
// 演算子シグネチャ（interface内で使用）
// ============================================================
struct OperatorSig {
    OperatorKind op;
    std::vector<Param> params;  // 引数（selfは暗黙）
    TypePtr return_type;
};

// ============================================================
// 演算子実装（impl内で使用）
// ============================================================
struct OperatorImpl {
    OperatorKind op;
    std::vector<Param> params;
    TypePtr return_type;
    std::vector<StmtPtr> body;
};

// ============================================================
// インターフェース定義
// ============================================================
struct InterfaceDecl {
    std::string name;
    std::vector<MethodSig> methods;
    std::vector<OperatorSig> operators;  // 演算子シグネチャ
    Visibility visibility = Visibility::Private;
    std::vector<std::string> generic_params;      // 後方互換性のため維持
    std::vector<GenericParam> generic_params_v2;  // 型制約付き

    InterfaceDecl(std::string n, std::vector<MethodSig> m)
        : name(std::move(n)), methods(std::move(m)) {}
};

// ============================================================
// impl定義
// ============================================================

struct WhereClause {
    std::string type_param;     // T
    TypeConstraint constraint;  // 型制約

    // 後方互換性のため
    std::string constraint_type;  // 単一の型（非推奨、constraintを使用）

    WhereClause() = default;
    WhereClause(std::string param, std::string single_type)
        : type_param(std::move(param)), constraint_type(single_type) {
        constraint = TypeConstraint(single_type);
    }
    WhereClause(std::string param, TypeConstraint c)
        : type_param(std::move(param)), constraint(std::move(c)) {
        if (!constraint.types.empty()) {
            constraint_type = constraint.types[0];  // 後方互換性
        }
    }
};

struct ImplDecl {
    std::string interface_name;  // forなしの場合は空文字列
    TypePtr target_type;
    std::vector<std::unique_ptr<FunctionDecl>> methods;
    std::vector<std::unique_ptr<OperatorImpl>> operators;  // 演算子実装
    std::vector<std::string> generic_params;               // 後方互換性のため維持
    std::vector<GenericParam> generic_params_v2;           // 型制約付き
    std::vector<TypePtr>
        interface_type_args;  // インターフェースの型引数（例: ValueHolder<T> の T）
    std::vector<WhereClause> where_clauses;  // where句

    // コンストラクタ/デストラクタ専用impl（forなし）
    bool is_ctor_impl = false;
    std::vector<std::unique_ptr<FunctionDecl>> constructors;  // self()
    std::unique_ptr<FunctionDecl> destructor;                 // ~self()

    ImplDecl(std::string iface, TypePtr target)
        : interface_name(std::move(iface)), target_type(std::move(target)) {}

    // コンストラクタ専用implのコンストラクタ
    explicit ImplDecl(TypePtr target)
        : interface_name(""), target_type(std::move(target)), is_ctor_impl(true) {}
};

// ============================================================
// Enumメンバ定義
// ============================================================
struct EnumMember {
    std::string name;
    std::optional<int64_t> value;  // 明示的な値（なければオートインクリメント）

    EnumMember(std::string n, std::optional<int64_t> v = std::nullopt)
        : name(std::move(n)), value(v) {}
};

// ============================================================
// Enum定義
// ============================================================
struct EnumDecl {
    std::string name;
    std::vector<EnumMember> members;
    Visibility visibility = Visibility::Private;

    EnumDecl(std::string n, std::vector<EnumMember> m)
        : name(std::move(n)), members(std::move(m)) {}
};

// ImportDeclはmodule.hppに移動

// ============================================================
// 宣言作成ヘルパー
// ============================================================
inline DeclPtr make_function(std::string name, std::vector<Param> params, TypePtr return_type,
                             std::vector<StmtPtr> body, Span s = {}) {
    return std::make_unique<Decl>(
        std::make_unique<FunctionDecl>(std::move(name), std::move(params), std::move(return_type),
                                       std::move(body)),
        s);
}

inline DeclPtr make_struct(std::string name, std::vector<Field> fields, Span s = {}) {
    return std::make_unique<Decl>(std::make_unique<StructDecl>(std::move(name), std::move(fields)),
                                  s);
}

// TODO: パーサー更新時に修正
// inline DeclPtr make_import(std::vector<std::string> path, std::string alias = "", Span s = {}) {
//     return std::make_unique<Decl>(std::make_unique<ImportDecl>(std::move(path),
//     std::move(alias)),
//                                   s);
// }

}  // namespace cm::ast

// typedef関連
#include "typedef.hpp"
