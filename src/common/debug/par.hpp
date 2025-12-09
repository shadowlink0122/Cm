#pragma once

#include "../debug.hpp"

#include <string>

namespace cm::debug::par {

/// Parser メッセージID
enum class Id {
    // 基本フロー
    Start,
    End,
    TokenConsume,
    TokenPeek,
    Backtrack,

    // 定義
    FuncDef,
    StructDef,
    EnumDef,
    InterfaceDef,
    ImplDef,
    TypeDef,
    ConstDef,
    ConstDecl,  // 追加: const変数宣言
    MacroDef,
    ModuleDef,

    // 式関連（基本）
    Expr,
    ExprStart,
    ExprEnd,
    BinaryExpr,
    UnaryExpr,
    CallExpr,
    IndexExpr,
    FieldAccess,
    LiteralExpr,
    IdentExpr,
    LambdaExpr,
    PrimaryExpr,

    // 式関連（詳細）
    AssignmentCheck,
    AssignmentOp,
    AssignmentCreate,
    CompoundAssignment,
    NoAssignment,
    PostfixStart,
    PostfixEnd,
    FunctionCall,
    CallArg,
    CallCreate,
    ArrayAccess,
    IndexCreate,
    MemberAccess,
    MemberCreate,
    MethodCall,
    MethodCreate,
    PostIncrement,
    PostDecrement,

    // リテラル詳細
    IntLiteral,
    FloatLiteral,
    StringLiteral,
    CharLiteral,
    BoolLiteral,
    NullLiteral,

    // 識別子と参照
    IdentifierRef,
    VariableDetected,
    ParenExpr,
    ParenClose,
    ExprError,

    // New式
    NewExpr,
    NewArgs,
    NewCreate,

    // 文
    Stmt,
    VarDecl,
    VarName,
    VarInit,
    VarInitComplete,
    VarNoInit,
    VarDeclComplete,
    Assignment,
    IfStmt,
    WhileStmt,
    ForStmt,
    MatchStmt,
    ReturnStmt,
    BreakStmt,
    ContinueStmt,

    // ブロックと制御構造
    Block,
    Scope,
    Pattern,
    MatchArm,
    CatchBlock,

    // 型
    TypeParse,
    GenericType,
    FunctionType,
    ArrayType,
    PointerType,

    // エラー処理
    Error,
    Warning,
    Recover,
    ExpectedToken,
    UnexpectedToken,
    SyntaxError
};

/// メッセージテーブル [en, ja]
inline const char* messages[][2] = {
    // 基本フロー
    {"Starting parsing", "構文解析を開始"},
    {"Completed parsing", "構文解析を完了"},
    {"Consuming token", "トークンを消費"},
    {"Peeking token", "トークンを先読み"},
    {"Backtracking", "バックトラック"},

    // 定義
    {"Parsing function definition", "関数定義を解析"},
    {"Parsing struct definition", "構造体定義を解析"},
    {"Parsing enum definition", "列挙型定義を解析"},
    {"Parsing interface definition", "インターフェース定義を解析"},
    {"Parsing impl definition", "impl定義を解析"},
    {"Parsing type definition", "型定義を解析"},
    {"Parsing const definition", "定数定義を解析"},
    {"Parsing const declaration", "const宣言を解析"},
    {"Parsing macro definition", "マクロ定義を解析"},
    {"Parsing module definition", "モジュール定義を解析"},

    // 式関連（基本）
    {"Parsing expression", "式を解析"},
    {"Starting expression", "式開始"},
    {"Expression complete", "式完了"},
    {"Parsing binary expression", "二項式を解析"},
    {"Parsing unary expression", "単項式を解析"},
    {"Parsing call expression", "関数呼び出しを解析"},
    {"Parsing index expression", "インデックスを解析"},
    {"Parsing field access", "フィールドアクセスを解析"},
    {"Parsing literal", "リテラルを解析"},
    {"Parsing identifier", "識別子を解析"},
    {"Parsing lambda expression", "ラムダ式を解析"},
    {"Parsing primary expression", "プライマリ式を解析"},

    // 式関連（詳細）
    {"Checking assignment", "代入をチェック"},
    {"Assignment operator", "代入演算子"},
    {"Creating assignment", "代入を作成"},
    {"Compound assignment", "複合代入"},
    {"No assignment", "代入なし"},
    {"Starting postfix", "後置開始"},
    {"Postfix complete", "後置完了"},
    {"Function call detected", "関数呼び出し検出"},
    {"Call argument", "呼び出し引数"},
    {"Creating call", "呼び出し作成"},
    {"Array access detected", "配列アクセス検出"},
    {"Creating index", "インデックス作成"},
    {"Member access detected", "メンバアクセス検出"},
    {"Creating member access", "メンバアクセス作成"},
    {"Method call detected", "メソッド呼び出し検出"},
    {"Creating method call", "メソッド呼び出し作成"},
    {"Post-increment", "後置インクリメント"},
    {"Post-decrement", "後置デクリメント"},

    // リテラル詳細
    {"Integer literal", "整数リテラル"},
    {"Float literal", "浮動小数点リテラル"},
    {"String literal", "文字列リテラル"},
    {"Character literal", "文字リテラル"},
    {"Boolean literal", "真偽値リテラル"},
    {"Null literal", "nullリテラル"},

    // 識別子と参照
    {"Identifier reference", "識別子参照"},
    {"Variable detected", "変数検出"},
    {"Parenthesized expression", "括弧付き式"},
    {"Closing parenthesis", "括弧閉じる"},
    {"Expression error", "式エラー"},

    // New式
    {"New expression", "new式"},
    {"New arguments", "new引数"},
    {"Creating new", "new作成"},

    // 文
    {"Parsing statement", "文を解析"},
    {"Parsing variable declaration", "変数宣言を解析"},
    {"Variable name", "変数名"},
    {"Variable initializer", "変数初期化"},
    {"Variable init complete", "変数初期化完了"},
    {"No initializer", "初期化子なし"},
    {"Variable declaration complete", "変数宣言完了"},
    {"Parsing assignment", "代入文を解析"},
    {"Parsing if statement", "if文を解析"},
    {"Parsing while statement", "while文を解析"},
    {"Parsing for statement", "for文を解析"},
    {"Parsing match statement", "match文を解析"},
    {"Parsing return statement", "return文を解析"},
    {"Parsing break statement", "break文を解析"},
    {"Parsing continue statement", "continue文を解析"},

    // ブロックと制御構造
    {"Parsing block", "ブロックを解析"},
    {"Entering scope", "スコープに入る"},
    {"Parsing pattern", "パターンを解析"},
    {"Parsing match arm", "マッチアームを解析"},
    {"Parsing catch block", "catchブロックを解析"},

    // 型
    {"Parsing type", "型を解析"},
    {"Parsing generic type", "ジェネリック型を解析"},
    {"Parsing function type", "関数型を解析"},
    {"Parsing array type", "配列型を解析"},
    {"Parsing pointer type", "ポインタ型を解析"},

    // エラー処理
    {"Parse error", "構文解析エラー"},
    {"Parse warning", "構文解析警告"},
    {"Recovering from error", "エラーから回復"},
    {"Expected token", "トークンが必要"},
    {"Unexpected token", "予期しないトークン"},
    {"Syntax error", "構文エラー"}};

inline const char* get(Id id) {
    return messages[static_cast<int>(id)][::cm::debug::g_lang];
}

inline void log(Id id, ::cm::debug::Level level = ::cm::debug::Level::Debug) {
    if (!::cm::debug::g_debug_mode || level < ::cm::debug::g_debug_level)
        return;
    ::cm::debug::log(::cm::debug::Stage::Parser, level, get(id));
}

inline void log(Id id, const std::string& detail,
                ::cm::debug::Level level = ::cm::debug::Level::Debug) {
    if (!::cm::debug::g_debug_mode || level < ::cm::debug::g_debug_level)
        return;
    ::cm::debug::log(::cm::debug::Stage::Parser, level, std::string(get(id)) + ": " + detail);
}

/// ノード情報をダンプ（Traceレベル）
inline void dump_node(const std::string& node_type, const std::string& info) {
    if (!::cm::debug::g_debug_mode || ::cm::debug::Level::Trace < ::cm::debug::g_debug_level)
        return;
    ::cm::debug::log(::cm::debug::Stage::Parser, ::cm::debug::Level::Trace,
                     "Node[" + node_type + "]: " + info);
}

/// トークン期待値をダンプ（Traceレベル）
inline void dump_expectation(const std::string& expected, const std::string& got) {
    if (!::cm::debug::g_debug_mode || ::cm::debug::Level::Trace < ::cm::debug::g_debug_level)
        return;
    ::cm::debug::log(::cm::debug::Stage::Parser, ::cm::debug::Level::Trace,
                     "Expected: " + expected + ", Got: " + got);
}

/// スコープ情報をダンプ（Traceレベル）
inline void dump_scope(int depth, const std::string& context = "") {
    if (!::cm::debug::g_debug_mode || ::cm::debug::Level::Trace < ::cm::debug::g_debug_level)
        return;
    std::string msg = "Scope depth: " + std::to_string(depth);
    if (!context.empty()) {
        msg += " (" + context + ")";
    }
    ::cm::debug::log(::cm::debug::Stage::Parser, ::cm::debug::Level::Trace, msg);
}

}  // namespace cm::debug::par