#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cm {
namespace cpp_mir {

// CPP-MIRの基本型
enum class Type {
    VOID,
    BOOL,
    INT,
    DOUBLE,
    STRING,
    CHAR_PTR  // const char* for string literals
};

// CPP-MIR式
struct Expression {
    enum Kind {
        LITERAL,       // リテラル値
        VARIABLE,      // 変数参照
        BINARY_OP,     // 二項演算
        UNARY_OP,      // 単項演算
        CALL,          // 関数呼び出し
        CAST,          // 型変換
        STRING_FORMAT  // フォーマット済み文字列
    };

    Kind kind = LITERAL;
    Type type = Type::INT;

    // データを文字列として保持（シンプル化）
    std::string value;  // LITERAL, VARIABLE, BINARY_OP, UNARY_OP, CAST用

    // CALL用のデータ
    std::string func_name;
    std::vector<Expression> args;

    // デフォルトコンストラクタ
    Expression() = default;

    // 便利なファクトリメソッド
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

    static Expression Call(const std::string& func, const std::vector<Expression>& call_args) {
        Expression e;
        e.kind = CALL;
        e.type = Type::VOID;
        e.func_name = func;
        e.args = call_args;
        return e;
    }

    static Expression BinaryOp(const std::string& op_str, Type t) {
        Expression e;
        e.kind = BINARY_OP;
        e.type = t;
        e.value = op_str;
        return e;
    }
};

// CPP-MIR文の種類
enum class StatementKind {
    DECLARATION,  // 変数宣言
    ASSIGNMENT,   // 代入
    PRINTF,       // printf呼び出し（最適化済み）
    EXPRESSION,   // 式文
    IF_ELSE,      // if-else文
    WHILE,        // whileループ
    FOR,          // forループ
    RETURN,       // return文
    BREAK,        // break文
    CONTINUE      // continue文
};

// 前方宣言
struct Statement;
using StatementPtr = std::shared_ptr<Statement>;

// 変数宣言
struct Declaration {
    Type type = Type::INT;
    std::string name;
    std::optional<Expression> init;
};

// 代入
struct Assignment {
    std::string target;
    Expression value;
};

// Printf
struct Printf {
    std::string format;
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

// For
struct For {
    StatementPtr init;  // nullptr可
    std::optional<Expression> condition;
    StatementPtr update;  // nullptr可
    std::vector<StatementPtr> body;
};

// CPP-MIR文
struct Statement {
    StatementKind kind = StatementKind::EXPRESSION;

    // 各種データ（union風に使用）
    Declaration decl_data;
    Assignment assign_data;
    Printf printf_data;
    Expression expr_data;
    std::shared_ptr<IfElse> if_data;
    std::shared_ptr<While> while_data;
    std::shared_ptr<For> for_data;
    Return return_data;

    // デフォルトコンストラクタ
    Statement() = default;

    // ファクトリメソッド
    static Statement Declare(Type type, const std::string& name,
                             std::optional<Expression> init = std::nullopt) {
        Statement stmt;
        stmt.kind = StatementKind::DECLARATION;
        stmt.decl_data = Declaration{type, name, init};
        return stmt;
    }

    static Statement Assign(const std::string& target, const Expression& value) {
        Statement stmt;
        stmt.kind = StatementKind::ASSIGNMENT;
        stmt.assign_data = Assignment{target, value};
        return stmt;
    }

    static Statement PrintF(const std::string& format, const std::vector<Expression>& args) {
        Statement stmt;
        stmt.kind = StatementKind::PRINTF;
        stmt.printf_data = Printf{format, args};
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

// CPP-MIR関数
struct Function {
    std::string name;
    Type return_type = Type::VOID;
    std::vector<std::pair<Type, std::string>> parameters;
    std::vector<Statement> body;

    // メタデータ（最適化ヒント）
    bool is_linear = true;     // 線形フローか
    bool uses_printf = false;  // printfを使用するか
    bool uses_string = false;  // std::stringを使用するか
    bool uses_format = false;  // フォーマット文字列を使用するか
};

// CPP-MIRプログラム
struct Program {
    std::vector<std::string> includes;  // 必要なヘッダファイル
    std::vector<Function> functions;

    // ヘルパー関数が必要か
    bool needs_format_helpers = false;
    bool needs_string_helpers = false;
};

// 文字列補間の解析結果
struct FormatSpec {
    enum SpecType {
        STRING,      // %s
        INTEGER,     // %d
        HEX,         // %x
        OCTAL,       // %o
        BINARY,      // カスタム（ヘルパー関数使用）
        FLOAT,       // %f
        SCIENTIFIC,  // %e
        BOOL         // カスタム（"true"/"false"）
    };

    SpecType spec_type = INTEGER;
    std::string flags;  // "-", "+", " ", "#", "0"
    std::optional<int> width;
    std::optional<int> precision;

    std::string toPrintfSpec() const {
        std::string spec = "%";
        spec += flags;
        if (width)
            spec += std::to_string(*width);
        if (precision)
            spec += "." + std::to_string(*precision);

        switch (spec_type) {
            case STRING:
                return spec + "s";
            case INTEGER:
                return spec + "d";
            case HEX:
                return spec + "x";
            case OCTAL:
                return spec + "o";
            case FLOAT:
                return spec + "f";
            case SCIENTIFIC:
                return spec + "e";
            case BINARY:
                return "%s";  // ヘルパー関数で処理
            case BOOL:
                return "%s";  // "true"/"false"
        }
        return spec + "d";
    }
};

// 文字列補間パーサー
class StringInterpolationParser {
   public:
    struct InterpolationPart {
        bool is_literal = true;
        std::string content;
        std::optional<FormatSpec> format;
    };

    static std::vector<InterpolationPart> parse(const std::string& str);
    static std::pair<std::string, std::vector<Expression>> buildPrintfCall(
        const std::string& interpolated, const std::vector<Expression>& args);
};

}  // namespace cpp_mir
}  // namespace cm