#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cm {
namespace rust_mir {

// ============================================================
// Rust-MIRの基本型
// ============================================================
enum class Type {
    VOID,      // ()
    BOOL,      // bool
    CHAR,      // char
    I8,        // i8 (tiny)
    I16,       // i16 (short)
    I32,       // i32 (int)
    I64,       // i64 (long)
    U8,        // u8 (utiny)
    U16,       // u16 (ushort)
    U32,       // u32 (uint)
    U64,       // u64 (ulong)
    F32,       // f32 (float)
    F64,       // f64 (double)
    STRING,    // String
    STR_REF,   // &str
    STR_SLICE  // &'static str
};

// ============================================================
// Rust-MIR式
// ============================================================
struct Expression {
    enum Kind {
        LITERAL,      // リテラル値
        VARIABLE,     // 変数参照
        BINARY_OP,    // 二項演算
        UNARY_OP,     // 単項演算
        CALL,         // 関数呼び出し
        MACRO_CALL,   // マクロ呼び出し (println!, format! 等)
        METHOD_CALL,  // メソッド呼び出し
        REFERENCE,    // 参照 (&x, &mut x)
        DEREF,        // 逆参照 (*x)
        FORMAT_ARGS   // format_args! 用
    };

    Kind kind = LITERAL;
    Type type = Type::I32;
    std::string value;  // 生成済みのRustコード

    // CALL/MACRO_CALL用
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

    static Expression MacroCall(const std::string& macro_name,
                                const std::vector<Expression>& call_args) {
        Expression e;
        e.kind = MACRO_CALL;
        e.type = Type::VOID;
        e.func_name = macro_name;
        e.args = call_args;
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
};

// ============================================================
// Rust-MIR文の種類
// ============================================================
enum class StatementKind {
    LET,         // let文 (immutable)
    LET_MUT,     // let mut文 (mutable)
    ASSIGNMENT,  // 代入
    EXPRESSION,  // 式文
    PRINTLN,     // println!マクロ（最適化済み）
    IF_ELSE,     // if-else式
    WHILE,       // whileループ
    FOR,         // forループ (range)
    LOOP,        // 無限ループ
    RETURN,      // return
    BREAK,       // break
    CONTINUE     // continue
};

// 前方宣言
struct Statement;
using StatementPtr = std::shared_ptr<Statement>;

// Let宣言
struct LetDecl {
    Type type = Type::I32;
    std::string name;
    bool is_mutable = false;
    std::optional<Expression> init;
};

// 代入
struct Assignment {
    std::string target;
    Expression value;
};

// Println (最適化済み)
struct Println {
    std::string format;            // フォーマット文字列
    std::vector<Expression> args;  // 引数
    bool with_newline = true;      // println! vs print!
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

// For (Rust風: for i in 0..n)
struct For {
    std::string var_name;
    Expression range_start;
    Expression range_end;
    bool inclusive = false;  // ..= の場合true
    std::vector<StatementPtr> body;
};

// Loop (無限ループ)
struct Loop {
    std::vector<StatementPtr> body;
};

// ============================================================
// Rust-MIR文
// ============================================================
struct Statement {
    StatementKind kind = StatementKind::EXPRESSION;

    // 各種データ
    LetDecl let_data;
    Assignment assign_data;
    Expression expr_data;
    Println println_data;
    std::shared_ptr<IfElse> if_data;
    std::shared_ptr<While> while_data;
    std::shared_ptr<For> for_data;
    std::shared_ptr<Loop> loop_data;
    Return return_data;

    // デフォルトコンストラクタ
    Statement() = default;

    // ファクトリメソッド
    static Statement Let(Type type, const std::string& name, bool is_mut = false,
                         std::optional<Expression> init = std::nullopt) {
        Statement stmt;
        stmt.kind = is_mut ? StatementKind::LET_MUT : StatementKind::LET;
        stmt.let_data = LetDecl{type, name, is_mut, init};
        return stmt;
    }

    static Statement Assign(const std::string& target, const Expression& value) {
        Statement stmt;
        stmt.kind = StatementKind::ASSIGNMENT;
        stmt.assign_data = Assignment{target, value};
        return stmt;
    }

    static Statement PrintLn(const std::string& format, const std::vector<Expression>& args,
                             bool newline = true) {
        Statement stmt;
        stmt.kind = StatementKind::PRINTLN;
        stmt.println_data = Println{format, args, newline};
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

    static Statement ForRange(const std::string& var, const Expression& start,
                              const Expression& end, bool inclusive,
                              std::vector<StatementPtr> body) {
        Statement stmt;
        stmt.kind = StatementKind::FOR;
        stmt.for_data = std::make_shared<For>();
        stmt.for_data->var_name = var;
        stmt.for_data->range_start = start;
        stmt.for_data->range_end = end;
        stmt.for_data->inclusive = inclusive;
        stmt.for_data->body = std::move(body);
        return stmt;
    }

    static Statement InfiniteLoop(std::vector<StatementPtr> body) {
        Statement stmt;
        stmt.kind = StatementKind::LOOP;
        stmt.loop_data = std::make_shared<Loop>();
        stmt.loop_data->body = std::move(body);
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
// Rust-MIR関数
// ============================================================
struct Function {
    std::string name;
    Type return_type = Type::VOID;
    std::vector<std::pair<Type, std::string>> parameters;
    std::vector<Statement> body;

    // メタデータ
    bool is_main = false;
    bool uses_format = false;  // format!マクロを使用するか
};

// ============================================================
// Rust-MIRプログラム
// ============================================================
struct Program {
    std::vector<Function> functions;
    std::string crate_name = "cm_output";
};

}  // namespace rust_mir
}  // namespace cm
