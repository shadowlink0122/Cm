#pragma once

#include "nodes.hpp"

#include <unordered_map>
#include <variant>

namespace cm::ast {

// ============================================================
// リテラル値
// ============================================================
using LiteralValue = std::variant<std::monostate,  // null
                                  bool,            // true/false
                                  int64_t,         // 整数
                                  double,          // 浮動小数点
                                  char,            // 文字
                                  std::string      // 文字列
                                  >;

struct LiteralExpr {
    LiteralValue value;

    LiteralExpr() = default;
    explicit LiteralExpr(bool v) : value(v) {}
    explicit LiteralExpr(int64_t v) : value(v) {}
    explicit LiteralExpr(double v) : value(v) {}
    explicit LiteralExpr(char v) : value(v) {}
    explicit LiteralExpr(std::string v) : value(std::move(v)) {}

    static LiteralExpr null_value() { return LiteralExpr{}; }

    bool is_null() const { return std::holds_alternative<std::monostate>(value); }
    bool is_bool() const { return std::holds_alternative<bool>(value); }
    bool is_int() const { return std::holds_alternative<int64_t>(value); }
    bool is_float() const { return std::holds_alternative<double>(value); }
    bool is_char() const { return std::holds_alternative<char>(value); }
    bool is_string() const { return std::holds_alternative<std::string>(value); }
};

// ============================================================
// 識別子
// ============================================================
struct IdentExpr {
    std::string name;

    explicit IdentExpr(std::string n) : name(std::move(n)) {}
};

// ============================================================
// 二項演算子
// ============================================================
enum class BinaryOp {
    // 算術
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    // ビット
    BitAnd,
    BitOr,
    BitXor,
    Shl,
    Shr,
    // 論理
    And,
    Or,
    // 比較
    Eq,
    Ne,
    Lt,
    Gt,
    Le,
    Ge,
    // 代入
    Assign,
    AddAssign,
    SubAssign,
    MulAssign,
    DivAssign,
    ModAssign,
    BitAndAssign,
    BitOrAssign,
    BitXorAssign,
    ShlAssign,
    ShrAssign,
};

inline const char* binary_op_str(BinaryOp op) {
    switch (op) {
        case BinaryOp::Add:
            return "+";
        case BinaryOp::Sub:
            return "-";
        case BinaryOp::Mul:
            return "*";
        case BinaryOp::Div:
            return "/";
        case BinaryOp::Mod:
            return "%";
        case BinaryOp::BitAnd:
            return "&";
        case BinaryOp::BitOr:
            return "|";
        case BinaryOp::BitXor:
            return "^";
        case BinaryOp::Shl:
            return "<<";
        case BinaryOp::Shr:
            return ">>";
        case BinaryOp::And:
            return "&&";
        case BinaryOp::Or:
            return "||";
        case BinaryOp::Eq:
            return "==";
        case BinaryOp::Ne:
            return "!=";
        case BinaryOp::Lt:
            return "<";
        case BinaryOp::Gt:
            return ">";
        case BinaryOp::Le:
            return "<=";
        case BinaryOp::Ge:
            return ">=";
        case BinaryOp::Assign:
            return "=";
        case BinaryOp::AddAssign:
            return "+=";
        case BinaryOp::SubAssign:
            return "-=";
        case BinaryOp::MulAssign:
            return "*=";
        case BinaryOp::DivAssign:
            return "/=";
        case BinaryOp::ModAssign:
            return "%=";
        case BinaryOp::BitAndAssign:
            return "&=";
        case BinaryOp::BitOrAssign:
            return "|=";
        case BinaryOp::BitXorAssign:
            return "^=";
        case BinaryOp::ShlAssign:
            return "<<=";
        case BinaryOp::ShrAssign:
            return ">>=";
    }
    return "?";
}

struct BinaryExpr {
    BinaryOp op;
    ExprPtr left;
    ExprPtr right;

    BinaryExpr(BinaryOp o, ExprPtr l, ExprPtr r) : op(o), left(std::move(l)), right(std::move(r)) {}
};

// ============================================================
// 単項演算子
// ============================================================
enum class UnaryOp {
    Neg,      // -
    Not,      // !
    BitNot,   // ~
    Deref,    // *
    AddrOf,   // &
    PreInc,   // ++x
    PreDec,   // --x
    PostInc,  // x++
    PostDec,  // x--
};

inline const char* unary_op_str(UnaryOp op) {
    switch (op) {
        case UnaryOp::Neg:
            return "-";
        case UnaryOp::Not:
            return "!";
        case UnaryOp::BitNot:
            return "~";
        case UnaryOp::Deref:
            return "*";
        case UnaryOp::AddrOf:
            return "&";
        case UnaryOp::PreInc:
            return "++";
        case UnaryOp::PreDec:
            return "--";
        case UnaryOp::PostInc:
            return "++";
        case UnaryOp::PostDec:
            return "--";
    }
    return "?";
}

struct UnaryExpr {
    UnaryOp op;
    ExprPtr operand;

    UnaryExpr(UnaryOp o, ExprPtr e) : op(o), operand(std::move(e)) {}
};

// ============================================================
// 関数呼び出し
// ============================================================
struct CallExpr {
    ExprPtr callee;
    std::vector<ExprPtr> args;

    // ジェネリック型引数（型推論で推論されたもの）
    std::unordered_map<std::string, TypePtr> inferred_type_args;

    // 順序付き型引数（HIR lowering用）
    std::vector<TypePtr> ordered_type_args;

    CallExpr(ExprPtr c, std::vector<ExprPtr> a) : callee(std::move(c)), args(std::move(a)) {}
};

// ============================================================
// 配列アクセス
// ============================================================
struct IndexExpr {
    ExprPtr object;
    ExprPtr index;

    IndexExpr(ExprPtr o, ExprPtr i) : object(std::move(o)), index(std::move(i)) {}
};

// ============================================================
// スライス式（Python風: arr[start:end:step]）
// ============================================================
struct SliceExpr {
    ExprPtr object;
    ExprPtr start;  // nullなら最初から
    ExprPtr end;    // nullなら最後まで
    ExprPtr step;   // nullならstep=1

    SliceExpr(ExprPtr o, ExprPtr s, ExprPtr e, ExprPtr st = nullptr)
        : object(std::move(o)), start(std::move(s)), end(std::move(e)), step(std::move(st)) {}
};

// ============================================================
// メンバアクセス
// ============================================================
struct MemberExpr {
    ExprPtr object;
    std::string member;
    bool is_method_call = false;
    std::vector<ExprPtr> args;  // メソッド呼び出しの場合

    MemberExpr(ExprPtr o, std::string m) : object(std::move(o)), member(std::move(m)) {}
};

// ============================================================
// 三項演算子
// ============================================================
struct TernaryExpr {
    ExprPtr condition;
    ExprPtr then_expr;
    ExprPtr else_expr;

    TernaryExpr(ExprPtr c, ExprPtr t, ExprPtr e)
        : condition(std::move(c)), then_expr(std::move(t)), else_expr(std::move(e)) {}
};

// ============================================================
// new式
// ============================================================
struct NewExpr {
    TypePtr type;
    std::vector<ExprPtr> args;

    NewExpr(TypePtr t, std::vector<ExprPtr> a) : type(std::move(t)), args(std::move(a)) {}
};

// ============================================================
// sizeof式 - コンパイル時に型/式のサイズを取得
// ============================================================
struct SizeofExpr {
    TypePtr target_type;  // sizeof(型) の場合
    ExprPtr target_expr;  // sizeof(式) の場合

    explicit SizeofExpr(TypePtr t) : target_type(std::move(t)) {}
    explicit SizeofExpr(ExprPtr e) : target_expr(std::move(e)) {}
};

// ============================================================
// typeof式 - 式の型を取得（型コンテキストで使用）
// ============================================================
struct TypeofExpr {
    ExprPtr target_expr;

    explicit TypeofExpr(ExprPtr e) : target_expr(std::move(e)) {}
};

// ============================================================
// alignof式 - 型のアラインメントを取得
// ============================================================
struct AlignofExpr {
    TypePtr target_type;

    explicit AlignofExpr(TypePtr t) : target_type(std::move(t)) {}
};

// ============================================================
// typename_of式 - 型の名前を文字列で取得
// __typename__(型) または __typename__(式) の両方に対応
// ============================================================
struct TypenameOfExpr {
    TypePtr target_type;  // 型指定の場合
    ExprPtr target_expr;  // 式指定の場合

    explicit TypenameOfExpr(TypePtr t) : target_type(std::move(t)) {}
    explicit TypenameOfExpr(ExprPtr e) : target_expr(std::move(e)) {}
};

// ============================================================
// 構造体リテラル (StructName{field1: val1, field2: val2})
// 名前付き初期化のみ対応
// ============================================================
struct StructLiteralField {
    std::string name;  // フィールド名（必須）
    ExprPtr value;

    StructLiteralField(std::string n, ExprPtr v) : name(std::move(n)), value(std::move(v)) {}
};

struct StructLiteralExpr {
    std::string type_name;
    std::vector<StructLiteralField> fields;

    StructLiteralExpr(std::string name, std::vector<StructLiteralField> f)
        : type_name(std::move(name)), fields(std::move(f)) {}
};

// ============================================================
// 配列リテラル [val1, val2, val3]
// ============================================================
struct ArrayLiteralExpr {
    std::vector<ExprPtr> elements;

    ArrayLiteralExpr(std::vector<ExprPtr> elems) : elements(std::move(elems)) {}
};

// ============================================================
// ラムダ式
// ============================================================
struct Param {
    std::string name;
    TypePtr type;
    TypeQualifiers qualifiers;
    ExprPtr default_value;  // デフォルト引数（nullなら必須引数）
};

struct LambdaExpr {
    std::vector<Param> params;
    TypePtr return_type;  // nullならauto
    std::variant<ExprPtr, std::vector<StmtPtr>> body;

    // キャプチャされる変数（TypeChecker が解析後に設定）
    struct Capture {
        std::string name;
        TypePtr type;
        bool by_ref;  // 参照キャプチャか値キャプチャか
    };
    std::vector<Capture> captures;

    bool is_expr_body() const { return std::holds_alternative<ExprPtr>(body); }
};

// ============================================================
// matchパターン（式用）
// v0.13.0: 範囲パターン(l...r)とORパターン(l | r)をサポート
// ============================================================
enum class MatchPatternKind {
    Literal,                 // リテラル値 (42, "hello", true)
    Variable,                // 変数束縛 (x)
    Wildcard,                // ワイルドカード (_)
    EnumVariant,             // enum値 (Option::Some, Color::Red)
    EnumVariantWithBinding,  // enum値 + バインディング (Option::Some(value))
    Range,                   // 範囲パターン (1...10)
    Or                       // ORパターン (1 | 2 | 3)
};

struct MatchPattern {
    MatchPatternKind kind;
    ExprPtr value;             // Literal/EnumVariant用
    std::string var_name;      // Variable用（束縛変数名）
    std::string enum_variant;  // EnumVariantWithBinding用（バリアント名）
    std::string binding_name;  // EnumVariantWithBinding用（束縛変数名）
    ExprPtr range_start;       // Range用（開始値）
    ExprPtr range_end;         // Range用（終了値）
    std::vector<std::unique_ptr<MatchPattern>> or_patterns;  // Or用

    static std::unique_ptr<MatchPattern> make_literal(ExprPtr val) {
        auto p = std::make_unique<MatchPattern>();
        p->kind = MatchPatternKind::Literal;
        p->value = std::move(val);
        return p;
    }

    static std::unique_ptr<MatchPattern> make_variable(std::string name) {
        auto p = std::make_unique<MatchPattern>();
        p->kind = MatchPatternKind::Variable;
        p->var_name = std::move(name);
        return p;
    }

    static std::unique_ptr<MatchPattern> make_wildcard() {
        auto p = std::make_unique<MatchPattern>();
        p->kind = MatchPatternKind::Wildcard;
        return p;
    }

    static std::unique_ptr<MatchPattern> make_enum_variant(ExprPtr val) {
        auto p = std::make_unique<MatchPattern>();
        p->kind = MatchPatternKind::EnumVariant;
        p->value = std::move(val);
        return p;
    }

    // enum値 + バインディング (Option::Some(value))
    static std::unique_ptr<MatchPattern> make_enum_variant_with_binding(std::string variant,
                                                                        std::string binding) {
        auto p = std::make_unique<MatchPattern>();
        p->kind = MatchPatternKind::EnumVariantWithBinding;
        p->enum_variant = std::move(variant);
        p->binding_name = std::move(binding);
        return p;
    }

    // 範囲パターン (1...10)
    static std::unique_ptr<MatchPattern> make_range(ExprPtr start, ExprPtr end) {
        auto p = std::make_unique<MatchPattern>();
        p->kind = MatchPatternKind::Range;
        p->range_start = std::move(start);
        p->range_end = std::move(end);
        return p;
    }

    // ORパターン (1 | 2 | 3)
    static std::unique_ptr<MatchPattern> make_or(
        std::vector<std::unique_ptr<MatchPattern>> patterns) {
        auto p = std::make_unique<MatchPattern>();
        p->kind = MatchPatternKind::Or;
        p->or_patterns = std::move(patterns);
        return p;
    }
};

// ============================================================
// matchアーム
// v0.13.0: 両方の形式をサポート:
//   - 式形式: pattern => expr (暗黙的にreturn)
//   - ブロック形式: pattern => { stmts } (明示的なreturn or void)
// ============================================================
struct MatchArm {
    std::unique_ptr<MatchPattern> pattern;
    ExprPtr guard;                    // オプショナルなガード条件 (if condition)
    ExprPtr expr_body;                // 式形式の本体 (nullptrならブロック形式)
    std::vector<StmtPtr> block_body;  // ブロック形式の本体
    bool is_block_form = false;       // trueならブロック形式

    // 式形式コンストラクタ
    MatchArm(std::unique_ptr<MatchPattern> p, ExprPtr g, ExprPtr e)
        : pattern(std::move(p)),
          guard(std::move(g)),
          expr_body(std::move(e)),
          is_block_form(false) {}

    // ブロック形式コンストラクタ
    MatchArm(std::unique_ptr<MatchPattern> p, ExprPtr g, std::vector<StmtPtr> b)
        : pattern(std::move(p)),
          guard(std::move(g)),
          block_body(std::move(b)),
          is_block_form(true) {}
};

// ============================================================
// match文（式として残すが、値を返さない文として使用）
// v0.13.0: matchは if/switch と同様に制御構文として使用
// 各armはブロック本体を持ち、returnは関数スコープから戻る
// ============================================================
struct MatchExpr {
    ExprPtr scrutinee;           // マッチ対象の式
    std::vector<MatchArm> arms;  // マッチアームのリスト

    MatchExpr(ExprPtr s, std::vector<MatchArm> a) : scrutinee(std::move(s)), arms(std::move(a)) {}
};

// ============================================================
// キャスト式 - expr as Type
// ============================================================
struct CastExpr {
    ExprPtr operand;      // キャスト対象の式
    TypePtr target_type;  // キャスト先の型

    CastExpr(ExprPtr e, TypePtr t) : operand(std::move(e)), target_type(std::move(t)) {}
};

// ============================================================
// move式 - 所有権の移動を明示 (move expr)
// ============================================================
struct MoveExpr {
    ExprPtr operand;  // 移動対象の式

    explicit MoveExpr(ExprPtr e) : operand(std::move(e)) {}
};

// ============================================================
// 式作成ヘルパー
// ============================================================
inline ExprPtr make_int_literal(int64_t v, Span s = {}) {
    return std::make_unique<Expr>(std::make_unique<LiteralExpr>(v), s);
}

inline ExprPtr make_float_literal(double v, Span s = {}) {
    return std::make_unique<Expr>(std::make_unique<LiteralExpr>(v), s);
}

inline ExprPtr make_bool_literal(bool v, Span s = {}) {
    return std::make_unique<Expr>(std::make_unique<LiteralExpr>(v), s);
}

inline ExprPtr make_string_literal(std::string v, Span s = {}) {
    return std::make_unique<Expr>(std::make_unique<LiteralExpr>(std::move(v)), s);
}

inline ExprPtr make_null_literal(Span s = {}) {
    return std::make_unique<Expr>(std::make_unique<LiteralExpr>(LiteralExpr::null_value()), s);
}

inline ExprPtr make_ident(std::string name, Span s = {}) {
    return std::make_unique<Expr>(std::make_unique<IdentExpr>(std::move(name)), s);
}

inline ExprPtr make_binary(BinaryOp op, ExprPtr left, ExprPtr right, Span s = {}) {
    return std::make_unique<Expr>(
        std::make_unique<BinaryExpr>(op, std::move(left), std::move(right)), s);
}

inline ExprPtr make_unary(UnaryOp op, ExprPtr operand, Span s = {}) {
    return std::make_unique<Expr>(std::make_unique<UnaryExpr>(op, std::move(operand)), s);
}

inline ExprPtr make_call(ExprPtr callee, std::vector<ExprPtr> args, Span s = {}) {
    return std::make_unique<Expr>(std::make_unique<CallExpr>(std::move(callee), std::move(args)),
                                  s);
}

inline ExprPtr make_struct_literal(std::string type_name, std::vector<StructLiteralField> fields,
                                   Span s = {}) {
    return std::make_unique<Expr>(
        std::make_unique<StructLiteralExpr>(std::move(type_name), std::move(fields)), s);
}

inline ExprPtr make_array_literal(std::vector<ExprPtr> elements, Span s = {}) {
    return std::make_unique<Expr>(std::make_unique<ArrayLiteralExpr>(std::move(elements)), s);
}

inline ExprPtr make_sizeof(TypePtr type, Span s = {}) {
    return std::make_unique<Expr>(std::make_unique<SizeofExpr>(std::move(type)), s);
}

inline ExprPtr make_sizeof_expr(ExprPtr expr, Span s = {}) {
    return std::make_unique<Expr>(std::make_unique<SizeofExpr>(std::move(expr)), s);
}

inline ExprPtr make_typeof(ExprPtr expr, Span s = {}) {
    return std::make_unique<Expr>(std::make_unique<TypeofExpr>(std::move(expr)), s);
}

inline ExprPtr make_alignof(TypePtr type, Span s = {}) {
    return std::make_unique<Expr>(std::make_unique<AlignofExpr>(std::move(type)), s);
}

inline ExprPtr make_typename_of(TypePtr type, Span s = {}) {
    return std::make_unique<Expr>(std::make_unique<TypenameOfExpr>(std::move(type)), s);
}

inline ExprPtr make_typename_of_expr(ExprPtr expr, Span s = {}) {
    return std::make_unique<Expr>(std::make_unique<TypenameOfExpr>(std::move(expr)), s);
}

inline ExprPtr make_cast(ExprPtr expr, TypePtr type, Span s = {}) {
    return std::make_unique<Expr>(std::make_unique<CastExpr>(std::move(expr), std::move(type)), s);
}

inline ExprPtr make_move(ExprPtr expr, Span s = {}) {
    return std::make_unique<Expr>(std::make_unique<MoveExpr>(std::move(expr)), s);
}

}  // namespace cm::ast
