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
// 型制約の種類（インターフェース境界）
// ============================================================
enum class ConstraintKind {
    None,    // 制約なし
    Single,  // T: Eq (単一インターフェースを実装している)
    And,     // T: Eq + Ord (複数インターフェースをすべて実装している)
    Or,      // T: Eq | Ord (複数インターフェースのいずれかを実装している)
};

// ============================================================
// インターフェース境界（型制約）
// すべての制約はインターフェースを対象とする
// 「型T はインターフェースI を実装している」ことを要求
// ============================================================
struct TypeConstraint {
    ConstraintKind kind = ConstraintKind::None;
    std::vector<std::string> interfaces;  // インターフェース名のリスト

    TypeConstraint() = default;

    // 単一インターフェース制約
    explicit TypeConstraint(std::string single_interface) : kind(ConstraintKind::Single) {
        interfaces.push_back(std::move(single_interface));
    }

    // 複合インターフェース制約
    TypeConstraint(ConstraintKind k, std::vector<std::string> ifaces)
        : kind(k), interfaces(std::move(ifaces)) {}

    // 後方互換性のためのアクセサ
    const std::vector<std::string>& types() const { return interfaces; }

    bool is_empty() const { return kind == ConstraintKind::None || interfaces.empty(); }
    bool is_and() const { return kind == ConstraintKind::And; }
    bool is_or() const { return kind == ConstraintKind::Or; }
};

// ============================================================
// ジェネリックパラメータの種類
// ============================================================
enum class GenericParamKind {
    Type,   // 型パラメータ（T, U等）
    Const,  // 定数パラメータ（const N: int等）
};

// ============================================================
// ジェネリックパラメータ（インターフェース境界付き、定数パラメータ対応）
// ============================================================
struct GenericParam {
    GenericParamKind kind = GenericParamKind::Type;  // パラメータの種類
    std::string name;                                // パラメータ名（T, N等）
    std::vector<std::string> constraints;            // 後方互換性用
    TypeConstraint type_constraint;  // インターフェース境界（型パラメータ用）
    TypePtr const_type;              // 定数パラメータの型（int, bool等）

    GenericParam() = default;
    explicit GenericParam(std::string n) : kind(GenericParamKind::Type), name(std::move(n)) {}

    // 後方互換性: constraints リストから構築（型パラメータ）
    GenericParam(std::string n, std::vector<std::string> c)
        : kind(GenericParamKind::Type), name(std::move(n)), constraints(std::move(c)) {
        if (!constraints.empty()) {
            if (constraints.size() == 1) {
                type_constraint = TypeConstraint(constraints[0]);
            } else {
                // 複数の制約は「+」で結合されたAND制約
                type_constraint = TypeConstraint(ConstraintKind::And, constraints);
            }
        }
    }

    // 新しい構築方法: TypeConstraint から（型パラメータ）
    GenericParam(std::string n, TypeConstraint tc)
        : kind(GenericParamKind::Type), name(std::move(n)), type_constraint(std::move(tc)) {
        // 後方互換性: type_constraintからconstraintsを設定
        constraints = type_constraint.interfaces;
    }

    // 定数パラメータ用コンストラクタ
    GenericParam(std::string n, TypePtr ct)
        : kind(GenericParamKind::Const), name(std::move(n)), const_type(std::move(ct)) {}

    bool has_constraint() const { return !type_constraint.is_empty(); }
    bool is_const() const { return kind == GenericParamKind::Const; }
    bool is_type() const { return kind == GenericParamKind::Type; }
};

// ============================================================
// 関数定義
// ============================================================
struct FunctionDecl {
    std::string name;
    Span name_span;  // 名前の位置（Lintエラー表示用）
    std::vector<Param> params;
    TypePtr return_type;
    std::vector<StmtPtr> body;

    // 修飾子
    Visibility visibility = Visibility::Private;
    bool is_static = false;
    bool is_inline = false;

    // コンストラクタ/デストラクタ
    bool is_constructor = false;  // self() コンストラクタ
    bool is_destructor = false;   // ~self() デストラクタ
    bool is_overload = false;     // overload修飾子
    bool is_extern = false;       // extern "C" 関数

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
    Span name_span;  // 名前の位置（Lintエラー表示用）
    std::vector<Field> fields;
    Visibility visibility = Visibility::Private;
    std::vector<AttributeNode> attributes;

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
    std::vector<AttributeNode> attributes;
    std::vector<std::string> generic_params;      // 後方互換性のため維持
    std::vector<GenericParam> generic_params_v2;  // 型制約付き

    InterfaceDecl(std::string n, std::vector<MethodSig> m)
        : name(std::move(n)), methods(std::move(m)) {}
};

// ============================================================
// impl定義
// ============================================================
// where句（関数やimplのインターフェース境界を記述）
// where T: Interface, U: I + J, V: I | J
// ============================================================

struct WhereClause {
    std::string type_param;     // 型パラメータ名 (T, U, V等)
    TypeConstraint constraint;  // インターフェース境界

    // 後方互換性のため
    std::string constraint_type;  // 単一の型名（非推奨）

    WhereClause() = default;

    WhereClause(std::string param, std::string single_interface)
        : type_param(std::move(param)), constraint_type(single_interface) {
        constraint = TypeConstraint(single_interface);
    }

    WhereClause(std::string param, TypeConstraint c)
        : type_param(std::move(param)), constraint(std::move(c)) {
        if (!constraint.interfaces.empty()) {
            constraint_type = constraint.interfaces[0];  // 後方互換性
        }
    }
};

struct ImplDecl {
    std::string interface_name;  // forなしの場合は空文字列
    TypePtr target_type;
    std::vector<std::unique_ptr<FunctionDecl>> methods;
    std::vector<std::unique_ptr<OperatorImpl>> operators;  // 演算子実装
    std::vector<AttributeNode> attributes;
    std::vector<std::string> generic_params;      // 後方互換性のため維持
    std::vector<GenericParam> generic_params_v2;  // 型制約付き
    std::vector<TypePtr>
        interface_type_args;  // インターフェースの型引数（例: ValueHolder<T> の T）
    std::vector<WhereClause> where_clauses;  // where句

    // コンストラクタ/デストラクタ専用impl（forなし）
    bool is_ctor_impl = false;
    std::vector<std::unique_ptr<FunctionDecl>> constructors;  // self()
    std::unique_ptr<FunctionDecl> destructor;                 // ~self()

    // v4 module system: export impl blocks
    bool is_export = false;

    ImplDecl(std::string iface, TypePtr target)
        : interface_name(std::move(iface)), target_type(std::move(target)) {}

    // コンストラクタ専用implのコンストラクタ
    explicit ImplDecl(TypePtr target)
        : interface_name(""), target_type(std::move(target)), is_ctor_impl(true) {}
};

// ============================================================
// Enumメンバ定義（Tagged Union対応）
// 設計: 各バリアントは0個または1個のフィールドのみ持つ
// 複数値が必要な場合は構造体で包む（例: Move(Point)）
// ============================================================
struct EnumMember {
    std::string name;
    std::optional<int64_t> value;  // 明示的な値（なければオートインクリメント）

    // Associated data（タグ付きユニオン用）
    // 設計: 1つのフィールドのみ許可（例: Some(int) -> fields = [{_, int}]）
    // 複数値が必要な場合は構造体で包む（例: Move(Point) -> fields = [{_, Point}]）
    std::vector<std::pair<std::string, TypePtr>> fields;

    // シンプルなenumメンバ
    EnumMember(std::string n, std::optional<int64_t> v = std::nullopt)
        : name(std::move(n)), value(v) {}

    // Associated data付きenumメンバ（1フィールドのみ推奨）
    EnumMember(std::string n, std::vector<std::pair<std::string, TypePtr>> f)
        : name(std::move(n)), value(std::nullopt), fields(std::move(f)) {}

    // Associated dataを持つかどうか
    bool has_data() const { return !fields.empty(); }

    // フィールドが1つのみかどうか（推奨設計）
    bool has_single_field() const { return fields.size() == 1; }
};

// ============================================================
// Enum定義（ジェネリック対応）
// ============================================================
struct EnumDecl {
    std::string name;
    std::vector<EnumMember> members;
    Visibility visibility = Visibility::Private;
    std::vector<AttributeNode> attributes;

    // ジェネリックパラメータ（例: Result<T, E>）
    std::vector<std::string> generic_params;      // 後方互換性
    std::vector<GenericParam> generic_params_v2;  // 型制約付き

    EnumDecl(std::string n, std::vector<EnumMember> m)
        : name(std::move(n)), members(std::move(m)) {}

    // ジェネリックenumかどうか
    bool is_generic() const { return !generic_params.empty() || !generic_params_v2.empty(); }

    // Tagged Union（Associated dataを持つメンバがある）かどうか
    bool is_tagged_union() const {
        return std::any_of(members.begin(), members.end(),
                           [](const EnumMember& m) { return m.has_data(); });
    }
};

// ============================================================
// グローバル変数/定数宣言 (v4: const/global変数サポート)
// ============================================================
struct GlobalVarDecl {
    std::string name;
    TypePtr type;
    ExprPtr init_expr;
    bool is_const = false;
    Visibility visibility = Visibility::Private;
    std::vector<AttributeNode> attributes;

    GlobalVarDecl(std::string n, TypePtr t, ExprPtr init, bool c = false)
        : name(std::move(n)), type(std::move(t)), init_expr(std::move(init)), is_const(c) {}
};

// ============================================================
// Extern "C" ブロック宣言
// ============================================================
struct ExternBlockDecl {
    std::string language;  // "C" など
    std::vector<std::unique_ptr<FunctionDecl>> declarations;
    std::vector<AttributeNode> attributes;

    explicit ExternBlockDecl(std::string lang) : language(std::move(lang)) {}
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
