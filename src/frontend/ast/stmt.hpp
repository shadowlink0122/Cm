#pragma once

#include "expr.hpp"
#include "nodes.hpp"

namespace cm::ast {

// ============================================================
// 変数宣言
// ============================================================
struct LetStmt {
    std::string name;
    TypePtr type;  // nullならauto推論
    ExprPtr init;  // 初期化式（省略可）
    bool is_const = false;

    LetStmt(std::string n, TypePtr t, ExprPtr i, bool c = false)
        : name(std::move(n)), type(std::move(t)), init(std::move(i)), is_const(c) {}
};

// ============================================================
// 式文
// ============================================================
struct ExprStmt {
    ExprPtr expr;

    explicit ExprStmt(ExprPtr e) : expr(std::move(e)) {}
};

// ============================================================
// return文
// ============================================================
struct ReturnStmt {
    ExprPtr value;  // nullならreturn;

    ReturnStmt() = default;
    explicit ReturnStmt(ExprPtr v) : value(std::move(v)) {}
};

// ============================================================
// if文
// ============================================================
struct IfStmt {
    ExprPtr condition;
    std::vector<StmtPtr> then_block;
    std::vector<StmtPtr> else_block;  // else ifの場合は単一のIfStmt

    IfStmt(ExprPtr c, std::vector<StmtPtr> t, std::vector<StmtPtr> e = {})
        : condition(std::move(c)), then_block(std::move(t)), else_block(std::move(e)) {}
};

// ============================================================
// for文
// ============================================================
struct ForStmt {
    StmtPtr init;       // nullまたはLetStmt/ExprStmt
    ExprPtr condition;  // nullなら無限ループ
    ExprPtr update;     // nullなら更新なし
    std::vector<StmtPtr> body;

    ForStmt(StmtPtr i, ExprPtr c, ExprPtr u, std::vector<StmtPtr> b)
        : init(std::move(i)), condition(std::move(c)), update(std::move(u)), body(std::move(b)) {}
};

// ============================================================
// while文
// ============================================================
struct WhileStmt {
    ExprPtr condition;
    std::vector<StmtPtr> body;

    WhileStmt(ExprPtr c, std::vector<StmtPtr> b) : condition(std::move(c)), body(std::move(b)) {}
};

// ============================================================
// ブロック文
// ============================================================
struct BlockStmt {
    std::vector<StmtPtr> stmts;

    BlockStmt() = default;
    explicit BlockStmt(std::vector<StmtPtr> s) : stmts(std::move(s)) {}
};

// ============================================================
// switch文のパターン
// ============================================================
enum class PatternKind {
    Value,  // 単一値
    Range,  // 範囲 (val...val2)
    Or      // OR結合されたパターン
};

struct Pattern {
    PatternKind kind;
    ExprPtr value;                                      // Value pattern
    ExprPtr range_start;                                // Range pattern start
    ExprPtr range_end;                                  // Range pattern end
    std::vector<std::unique_ptr<Pattern>> or_patterns;  // OR patterns

    // 単一値パターン
    static std::unique_ptr<Pattern> make_value(ExprPtr val) {
        auto p = std::make_unique<Pattern>();
        p->kind = PatternKind::Value;
        p->value = std::move(val);
        return p;
    }

    // 範囲パターン
    static std::unique_ptr<Pattern> make_range(ExprPtr start, ExprPtr end) {
        auto p = std::make_unique<Pattern>();
        p->kind = PatternKind::Range;
        p->range_start = std::move(start);
        p->range_end = std::move(end);
        return p;
    }

    // ORパターン
    static std::unique_ptr<Pattern> make_or(std::vector<std::unique_ptr<Pattern>> patterns) {
        auto p = std::make_unique<Pattern>();
        p->kind = PatternKind::Or;
        p->or_patterns = std::move(patterns);
        return p;
    }
};

// ============================================================
// switch文
// ============================================================
struct SwitchCase {
    std::unique_ptr<Pattern> pattern;  // nullptr for else case
    std::vector<StmtPtr> stmts;

    SwitchCase() = default;  // else case
    explicit SwitchCase(std::unique_ptr<Pattern> p, std::vector<StmtPtr> s)
        : pattern(std::move(p)), stmts(std::move(s)) {}
};

struct SwitchStmt {
    ExprPtr expr;
    std::vector<SwitchCase> cases;

    SwitchStmt(ExprPtr e, std::vector<SwitchCase> c) : expr(std::move(e)), cases(std::move(c)) {}
};

// ============================================================
// break / continue
// ============================================================
struct BreakStmt {};
struct ContinueStmt {};

// ============================================================
// 文作成ヘルパー
// ============================================================
inline StmtPtr make_let(std::string name, TypePtr type, ExprPtr init, bool is_const = false,
                        Span s = {}) {
    return std::make_unique<Stmt>(
        std::make_unique<LetStmt>(std::move(name), std::move(type), std::move(init), is_const), s);
}

inline StmtPtr make_expr_stmt(ExprPtr expr, Span s = {}) {
    return std::make_unique<Stmt>(std::make_unique<ExprStmt>(std::move(expr)), s);
}

inline StmtPtr make_return(ExprPtr value = nullptr, Span s = {}) {
    return std::make_unique<Stmt>(std::make_unique<ReturnStmt>(std::move(value)), s);
}

inline StmtPtr make_if(ExprPtr cond, std::vector<StmtPtr> then_block,
                       std::vector<StmtPtr> else_block = {}, Span s = {}) {
    return std::make_unique<Stmt>(
        std::make_unique<IfStmt>(std::move(cond), std::move(then_block), std::move(else_block)), s);
}

inline StmtPtr make_while(ExprPtr cond, std::vector<StmtPtr> body, Span s = {}) {
    return std::make_unique<Stmt>(std::make_unique<WhileStmt>(std::move(cond), std::move(body)), s);
}

inline StmtPtr make_block(std::vector<StmtPtr> stmts, Span s = {}) {
    return std::make_unique<Stmt>(std::make_unique<BlockStmt>(std::move(stmts)), s);
}

inline StmtPtr make_switch(ExprPtr expr, std::vector<SwitchCase> cases, Span s = {}) {
    return std::make_unique<Stmt>(std::make_unique<SwitchStmt>(std::move(expr), std::move(cases)),
                                  s);
}

inline StmtPtr make_break(Span s = {}) {
    return std::make_unique<Stmt>(std::make_unique<BreakStmt>(), s);
}

inline StmtPtr make_continue(Span s = {}) {
    return std::make_unique<Stmt>(std::make_unique<ContinueStmt>(), s);
}

}  // namespace cm::ast
