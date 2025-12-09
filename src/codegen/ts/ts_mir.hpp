#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cm {
namespace ts_mir {

// ============================================================
// TS-MIRの基本型
// ============================================================
enum class Type {
    VOID,     // void
    BOOLEAN,  // boolean
    NUMBER,   // number
    STRING,   // string
    ANY,      // any
    UNKNOWN   // unknown
};

// ============================================================
// TS-MIR式
// ============================================================
struct Expression {
    enum Kind {
        LITERAL,       // リテラル値
        VARIABLE,      // 変数参照
        BINARY_OP,     // 二項演算
        UNARY_OP,      // 単項演算
        CALL,          // 関数呼び出し
        METHOD_CALL,   // メソッド呼び出し
        TEMPLATE_LIT,  // テンプレートリテラル
        ARROW_FUNC,    // アロー関数
        TERNARY        // 三項演算子
    };

    Kind kind = LITERAL;
    Type type = Type::NUMBER;
    std::string value;  // 生成済みのTSコード

    // CALL/METHOD_CALL用
    std::string func_name;
    std::vector<Expression> args;

    // METHOD_CALL用
    std::string method_name;
    std::unique_ptr<Expression> receiver;

    // デフォルトコンストラクタ
    Expression() = default;
    Expression(const Expression& other)
        : kind(other.kind),
          type(other.type),
          value(other.value),
          func_name(other.func_name),
          args(other.args),
          method_name(other.method_name) {
        if (other.receiver) {
            receiver = std::make_unique<Expression>(*other.receiver);
        }
    }
    Expression& operator=(const Expression& other) {
        if (this != &other) {
            kind = other.kind;
            type = other.type;
            value = other.value;
            func_name = other.func_name;
            args = other.args;
            method_name = other.method_name;
            if (other.receiver) {
                receiver = std::make_unique<Expression>(*other.receiver);
            } else {
                receiver.reset();
            }
        }
        return *this;
    }
    Expression(Expression&&) = default;
    Expression& operator=(Expression&&) = default;

    // ファクトリメソッド
    static Expression Literal(const std::string& val, Type t) {
        Expression e;
        e.kind = LITERAL;
        e.type = t;
        e.value = val;
        return e;
    }

    static Expression Variable(const std::string& name, Type t) {
        Expression e;
        e.kind = VARIABLE;
        e.type = t;
        e.value = name;
        return e;
    }

    static Expression BinaryOp(const std::string& expr_str, Type t) {
        Expression e;
        e.kind = BINARY_OP;
        e.type = t;
        e.value = expr_str;
        return e;
    }

    static Expression Call(const std::string& func, const std::vector<Expression>& call_args,
                           Type ret_type = Type::VOID) {
        Expression e;
        e.kind = CALL;
        e.type = ret_type;
        e.func_name = func;
        e.args = call_args;
        return e;
    }

    static Expression TemplateLiteral(const std::string& template_str) {
        Expression e;
        e.kind = TEMPLATE_LIT;
        e.type = Type::STRING;
        e.value = template_str;
        return e;
    }
};

// ============================================================
// TS-MIR文の種類
// ============================================================
enum class StatementKind {
    CONST,        // const宣言
    LET,          // let宣言
    ASSIGNMENT,   // 代入
    EXPRESSION,   // 式文
    CONSOLE_LOG,  // console.log（最適化済み）
    IF_ELSE,      // if-else文
    WHILE,        // whileループ
    FOR,          // forループ
    FOR_OF,       // for...of
    RETURN,       // return
    BREAK,        // break
    CONTINUE      // continue
};

// 前方宣言
struct Statement;
using StatementPtr = std::shared_ptr<Statement>;

// 変数宣言
struct VarDecl {
    Type type = Type::NUMBER;
    std::string name;
    bool is_const = true;
    std::optional<Expression> init;
};

// 代入
struct Assignment {
    std::string target;
    Expression value;
};

// Console.log
struct ConsoleLog {
    std::vector<Expression> args;
};

// Return
struct Return {
    std::optional<Expression> value;
};

// If-Else
struct IfElse {
    Expression condition;
    std::vector<StatementPtr> then_body;
    std::vector<StatementPtr> else_body;
};

// While
struct While {
    Expression condition;
    std::vector<StatementPtr> body;
};

// For (C-style)
struct For {
    StatementPtr init;  // nullptr可
    std::optional<Expression> condition;
    StatementPtr update;  // nullptr可
    std::vector<StatementPtr> body;
};

// For...of
struct ForOf {
    std::string var_name;
    Expression iterable;
    std::vector<StatementPtr> body;
};

// ============================================================
// TS-MIR文
// ============================================================
struct Statement {
    StatementKind kind = StatementKind::EXPRESSION;

    // 各種データ
    VarDecl var_data;
    Assignment assign_data;
    Expression expr_data;
    ConsoleLog console_data;
    std::shared_ptr<IfElse> if_data;
    std::shared_ptr<While> while_data;
    std::shared_ptr<For> for_data;
    std::shared_ptr<ForOf> for_of_data;
    Return return_data;

    // デフォルトコンストラクタ
    Statement() = default;

    // ファクトリメソッド
    static Statement Const(Type type, const std::string& name,
                           std::optional<Expression> init = std::nullopt) {
        Statement stmt;
        stmt.kind = StatementKind::CONST;
        stmt.var_data = VarDecl{type, name, true, init};
        return stmt;
    }

    static Statement Let(Type type, const std::string& name, bool is_const = false,
                         std::optional<Expression> init = std::nullopt) {
        Statement stmt;
        stmt.kind = is_const ? StatementKind::CONST : StatementKind::LET;
        stmt.var_data = VarDecl{type, name, is_const, init};
        return stmt;
    }

    static Statement Assign(const std::string& target, const Expression& value) {
        Statement stmt;
        stmt.kind = StatementKind::ASSIGNMENT;
        stmt.assign_data = Assignment{target, value};
        return stmt;
    }

    static Statement Log(const std::vector<Expression>& args) {
        Statement stmt;
        stmt.kind = StatementKind::CONSOLE_LOG;
        stmt.console_data = ConsoleLog{args};
        return stmt;
    }

    static Statement Expr(const Expression& expr) {
        Statement stmt;
        stmt.kind = StatementKind::EXPRESSION;
        stmt.expr_data = expr;
        return stmt;
    }

    static Statement If(const Expression& cond, std::vector<StatementPtr> then_body,
                        std::vector<StatementPtr> else_body = {}) {
        Statement stmt;
        stmt.kind = StatementKind::IF_ELSE;
        stmt.if_data = std::make_shared<IfElse>();
        stmt.if_data->condition = cond;
        stmt.if_data->then_body = std::move(then_body);
        stmt.if_data->else_body = std::move(else_body);
        return stmt;
    }

    static Statement WhileLoop(const Expression& cond, std::vector<StatementPtr> body) {
        Statement stmt;
        stmt.kind = StatementKind::WHILE;
        stmt.while_data = std::make_shared<While>();
        stmt.while_data->condition = cond;
        stmt.while_data->body = std::move(body);
        return stmt;
    }

    static Statement ForLoop(StatementPtr init, std::optional<Expression> cond, StatementPtr update,
                             std::vector<StatementPtr> body) {
        Statement stmt;
        stmt.kind = StatementKind::FOR;
        stmt.for_data = std::make_shared<For>();
        stmt.for_data->init = std::move(init);
        stmt.for_data->condition = cond;
        stmt.for_data->update = std::move(update);
        stmt.for_data->body = std::move(body);
        return stmt;
    }

    static Statement ForOfLoop(const std::string& var, const Expression& iterable,
                               std::vector<StatementPtr> body) {
        Statement stmt;
        stmt.kind = StatementKind::FOR_OF;
        stmt.for_of_data = std::make_shared<ForOf>();
        stmt.for_of_data->var_name = var;
        stmt.for_of_data->iterable = iterable;
        stmt.for_of_data->body = std::move(body);
        return stmt;
    }

    static Statement ReturnVoid() {
        Statement stmt;
        stmt.kind = StatementKind::RETURN;
        stmt.return_data = Return{std::nullopt};
        return stmt;
    }

    static Statement ReturnValue(const Expression& value) {
        Statement stmt;
        stmt.kind = StatementKind::RETURN;
        stmt.return_data = Return{value};
        return stmt;
    }

    static Statement Break() {
        Statement stmt;
        stmt.kind = StatementKind::BREAK;
        return stmt;
    }

    static Statement Continue() {
        Statement stmt;
        stmt.kind = StatementKind::CONTINUE;
        return stmt;
    }
};

// ============================================================
// TS-MIR関数
// ============================================================
struct Function {
    std::string name;
    Type return_type = Type::VOID;
    std::vector<std::pair<Type, std::string>> parameters;
    std::vector<Statement> body;

    // メタデータ
    bool is_main = false;
    bool is_async = false;
    bool is_exported = false;
};

// ============================================================
// TS-MIRプログラム
// ============================================================
struct Program {
    std::vector<Function> functions;
    std::vector<std::string> imports;
};

}  // namespace ts_mir
}  // namespace cm
