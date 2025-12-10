#pragma once

#include "expr.hpp"
#include "module.hpp"  // for AttributeNode
#include "nodes.hpp"
#include "stmt.hpp"

namespace cm::ast {

// ============================================================
// 可視性
// ============================================================
enum class Visibility {
    Private,  // デフォルト（ファイル内）
    Export,   // 他ファイルからアクセス可能
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

    // ディレクティブ/アトリビュート（#test, #bench, #deprecated等）
    std::vector<AttributeNode> attributes;

    // ジェネリクス（将来用）
    std::vector<std::string> generic_params;

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

    // ジェネリクス（将来用）
    std::vector<std::string> generic_params;

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
// インターフェース定義
// ============================================================
struct InterfaceDecl {
    std::string name;
    std::vector<MethodSig> methods;
    Visibility visibility = Visibility::Private;
    std::vector<std::string> generic_params;

    InterfaceDecl(std::string n, std::vector<MethodSig> m)
        : name(std::move(n)), methods(std::move(m)) {}
};

// ============================================================
// impl定義
// ============================================================
struct ImplDecl {
    std::string interface_name;
    TypePtr target_type;
    std::vector<std::unique_ptr<FunctionDecl>> methods;

    ImplDecl(std::string iface, TypePtr target)
        : interface_name(std::move(iface)), target_type(std::move(target)) {}
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
