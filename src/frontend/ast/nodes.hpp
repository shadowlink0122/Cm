#pragma once

#include "../../common/span.hpp"
#include "types.hpp"

#include <memory>
#include <variant>
#include <vector>

namespace cm::ast {

// ============================================================
// 前方宣言
// ============================================================
struct Expr;
struct Stmt;
struct Decl;

using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;
using DeclPtr = std::unique_ptr<Decl>;

// ============================================================
// ASTノード基底
// ============================================================
struct Node {
    Span span;  // ソース位置

    Node() = default;
    explicit Node(Span s) : span(s) {}
    virtual ~Node() = default;
};

// ============================================================
// 式（Expression）
// ============================================================

// リテラル値
struct LiteralExpr;
struct IdentExpr;
struct BinaryExpr;
struct UnaryExpr;
struct CallExpr;
struct IndexExpr;
struct SliceExpr;
struct MemberExpr;
struct TernaryExpr;
struct NewExpr;
struct SizeofExpr;
struct TypeofExpr;
struct AlignofExpr;
struct TypenameOfExpr;
struct StructLiteralExpr;
struct ArrayLiteralExpr;
struct LambdaExpr;
struct MatchExpr;
struct CastExpr;
struct MoveExpr;
struct AwaitExpr;  // v0.13.0: 非同期await式

// 式の種類
using ExprKind =
    std::variant<std::unique_ptr<LiteralExpr>, std::unique_ptr<IdentExpr>,
                 std::unique_ptr<BinaryExpr>, std::unique_ptr<UnaryExpr>, std::unique_ptr<CallExpr>,
                 std::unique_ptr<IndexExpr>, std::unique_ptr<SliceExpr>,
                 std::unique_ptr<MemberExpr>, std::unique_ptr<TernaryExpr>,
                 std::unique_ptr<NewExpr>, std::unique_ptr<SizeofExpr>, std::unique_ptr<TypeofExpr>,
                 std::unique_ptr<AlignofExpr>, std::unique_ptr<TypenameOfExpr>,
                 std::unique_ptr<StructLiteralExpr>, std::unique_ptr<ArrayLiteralExpr>,
                 std::unique_ptr<LambdaExpr>, std::unique_ptr<MatchExpr>, std::unique_ptr<CastExpr>,
                 std::unique_ptr<MoveExpr>, std::unique_ptr<AwaitExpr>>;

struct Expr : Node {
    ExprKind kind;
    TypePtr type;  // 推論された型（型チェック後に設定）

    template <typename T>
    Expr(std::unique_ptr<T> k, Span s = Span{}) : Node(s), kind(std::move(k)) {}

    template <typename T>
    T* as() {
        if (auto* p = std::get_if<std::unique_ptr<T>>(&kind)) {
            return p->get();
        }
        return nullptr;
    }

    template <typename T>
    const T* as() const {
        if (auto* p = std::get_if<std::unique_ptr<T>>(&kind)) {
            return p->get();
        }
        return nullptr;
    }
};

// ============================================================
// 文（Statement）
// ============================================================

struct LetStmt;
struct ExprStmt;
struct ReturnStmt;
struct IfStmt;
struct ForStmt;
struct ForInStmt;
struct WhileStmt;
struct BlockStmt;
struct SwitchStmt;
struct BreakStmt;
struct ContinueStmt;
struct DeferStmt;

using StmtKind =
    std::variant<std::unique_ptr<LetStmt>, std::unique_ptr<ExprStmt>, std::unique_ptr<ReturnStmt>,
                 std::unique_ptr<IfStmt>, std::unique_ptr<ForStmt>, std::unique_ptr<ForInStmt>,
                 std::unique_ptr<WhileStmt>, std::unique_ptr<BlockStmt>,
                 std::unique_ptr<SwitchStmt>, std::unique_ptr<BreakStmt>,
                 std::unique_ptr<ContinueStmt>, std::unique_ptr<DeferStmt>>;

struct Stmt : Node {
    StmtKind kind;

    template <typename T>
    Stmt(std::unique_ptr<T> k, Span s = Span{}) : Node(s), kind(std::move(k)) {}

    template <typename T>
    T* as() {
        if (auto* p = std::get_if<std::unique_ptr<T>>(&kind)) {
            return p->get();
        }
        return nullptr;
    }
};

// ============================================================
// 宣言（Declaration）
// ============================================================

struct FunctionDecl;
struct StructDecl;
struct InterfaceDecl;
struct ImplDecl;
struct ImportDecl;
struct ExportDecl;
struct ModuleDecl;
struct MacroDecl;
struct UseDecl;
struct EnumDecl;
struct TypedefDecl;
struct GlobalVarDecl;
struct ExternBlockDecl;

using DeclKind =
    std::variant<std::unique_ptr<FunctionDecl>, std::unique_ptr<StructDecl>,
                 std::unique_ptr<InterfaceDecl>, std::unique_ptr<ImplDecl>,
                 std::unique_ptr<ImportDecl>, std::unique_ptr<ExportDecl>,
                 std::unique_ptr<ModuleDecl>, std::unique_ptr<MacroDecl>, std::unique_ptr<UseDecl>,
                 std::unique_ptr<EnumDecl>, std::unique_ptr<TypedefDecl>,
                 std::unique_ptr<GlobalVarDecl>, std::unique_ptr<ExternBlockDecl>>;

struct Decl : Node {
    DeclKind kind;

    template <typename T>
    Decl(std::unique_ptr<T> k, Span s = Span{}) : Node(s), kind(std::move(k)) {}

    template <typename T>
    T* as() {
        if (auto* p = std::get_if<std::unique_ptr<T>>(&kind)) {
            return p->get();
        }
        return nullptr;
    }
};

// ============================================================
// プログラム（ルートノード）
// ============================================================
struct Program : Node {
    std::vector<DeclPtr> declarations;
    std::string filename;

    Program() = default;
    explicit Program(std::string file) : filename(std::move(file)) {}
};

}  // namespace cm::ast

// モジュール関連の定義をインクルード
#include "module.hpp"
