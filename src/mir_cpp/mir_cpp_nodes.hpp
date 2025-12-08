#pragma once

#include <memory>
#include <string>
#include <variant>
#include <vector>
#include "../hir/hir_nodes.hpp"

namespace cm::mir_cpp {

// 前方宣言
struct Expression;
struct Statement;
struct Block;
using ExprPtr = std::shared_ptr<Expression>;
using StmtPtr = std::shared_ptr<Statement>;
using BlockPtr = std::shared_ptr<Block>;

// 式の種類
enum class ExprKind {
    Literal,
    Variable,
    Binary,
    Unary,
    Call,
    Cast,
    StringInterpolation
};

// リテラル
struct Literal {
    std::variant<int64_t, double, bool, char, std::string> value;
    hir::TypePtr type;
};

// 変数参照
struct Variable {
    std::string name;
    hir::TypePtr type;
};

// 二項演算
struct BinaryOp {
    enum Op {
        Add, Sub, Mul, Div, Mod,
        Eq, Ne, Lt, Le, Gt, Ge,
        And, Or,
        BitAnd, BitOr, BitXor, Shl, Shr
    };
    Op op;
    ExprPtr left;
    ExprPtr right;
};

// 単項演算
struct UnaryOp {
    enum Op { Neg, Not, BitNot };
    Op op;
    ExprPtr operand;
};

// 関数呼び出し
struct CallExpr {
    std::string func_name;
    std::vector<ExprPtr> args;
    hir::TypePtr return_type;
};

// キャスト
struct CastExpr {
    ExprPtr expr;
    hir::TypePtr target_type;
};

// 文字列補間（フォーマット）
struct StringInterpolation {
    struct Part {
        std::string text;
        ExprPtr expr;           // nullptr if text only
        std::string format_spec; // e.g., "x", ".2", "<10"
    };
    std::vector<Part> parts;
};

// 式
struct Expression {
    ExprKind kind;
    std::variant<
        Literal,
        Variable,
        BinaryOp,
        UnaryOp,
        CallExpr,
        CastExpr,
        StringInterpolation
    > data;
    hir::TypePtr type;
};

// 文の種類
enum class StmtKind {
    VarDecl,
    Assignment,
    Expression,
    If,
    While,
    For,
    Return,
    Break,
    Continue,
    Block
};

// 変数宣言
struct VarDecl {
    std::string name;
    hir::TypePtr type;
    ExprPtr init;  // nullptrも可
    bool is_const = false;
};

// 代入
struct Assignment {
    std::string target;
    ExprPtr value;
};

// if文
struct IfStmt {
    ExprPtr condition;
    BlockPtr then_block;
    BlockPtr else_block;  // nullptrも可
};

// while文
struct WhileStmt {
    ExprPtr condition;
    BlockPtr body;
};

// for文
struct ForStmt {
    StmtPtr init;      // 初期化（変数宣言など）
    ExprPtr condition;
    StmtPtr update;    // 更新式
    BlockPtr body;
};

// return文
struct ReturnStmt {
    ExprPtr value;  // nullptrも可（void関数）
};

// 文
struct Statement {
    StmtKind kind;
    std::variant<
        VarDecl,
        Assignment,
        ExprPtr,      // 式文
        IfStmt,
        WhileStmt,
        ForStmt,
        ReturnStmt,
        std::monostate,  // Break, Continue
        BlockPtr      // ブロック文
    > data;
};

// ブロック
struct Block {
    std::vector<StmtPtr> statements;
};

// 関数
struct Function {
    std::string name;
    std::vector<std::pair<std::string, hir::TypePtr>> params;
    hir::TypePtr return_type;
    BlockPtr body;
    bool is_main = false;
};

// プログラム全体
struct Program {
    std::vector<Function> functions;
    std::vector<std::string> imports;  // 必要なヘッダーファイル
};

// ヘルパー関数
inline ExprPtr make_literal(const Literal& lit) {
    auto expr = std::make_shared<Expression>();
    expr->kind = ExprKind::Literal;
    expr->data = lit;
    expr->type = lit.type;
    return expr;
}

inline ExprPtr make_var(const std::string& name, hir::TypePtr type) {
    auto expr = std::make_shared<Expression>();
    expr->kind = ExprKind::Variable;
    expr->data = Variable{name, type};
    expr->type = type;
    return expr;
}

inline ExprPtr make_binary(BinaryOp::Op op, ExprPtr left, ExprPtr right) {
    auto expr = std::make_shared<Expression>();
    expr->kind = ExprKind::Binary;
    expr->data = BinaryOp{op, left, right};
    // 型は左右のオペランドから推論（簡略化）
    expr->type = left->type;
    return expr;
}

inline StmtPtr make_var_decl(const std::string& name, hir::TypePtr type, ExprPtr init = nullptr) {
    auto stmt = std::make_shared<Statement>();
    stmt->kind = StmtKind::VarDecl;
    stmt->data = VarDecl{name, type, init, false};
    return stmt;
}

inline StmtPtr make_assignment(const std::string& target, ExprPtr value) {
    auto stmt = std::make_shared<Statement>();
    stmt->kind = StmtKind::Assignment;
    stmt->data = Assignment{target, value};
    return stmt;
}

inline StmtPtr make_if(ExprPtr cond, BlockPtr then_block, BlockPtr else_block = nullptr) {
    auto stmt = std::make_shared<Statement>();
    stmt->kind = StmtKind::If;
    stmt->data = IfStmt{cond, then_block, else_block};
    return stmt;
}

inline StmtPtr make_return(ExprPtr value = nullptr) {
    auto stmt = std::make_shared<Statement>();
    stmt->kind = StmtKind::Return;
    stmt->data = ReturnStmt{value};
    return stmt;
}

inline BlockPtr make_block(std::vector<StmtPtr> stmts = {}) {
    return std::make_shared<Block>(Block{std::move(stmts)});
}

}  // namespace cm::mir_cpp