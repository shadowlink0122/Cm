#pragma once

#include "../common/span.hpp"
#include "hir_types.hpp"

#include <memory>
#include <variant>
#include <vector>

namespace cm::hir {

// ============================================================
// 前方宣言
// ============================================================
struct HirExpr;
struct HirStmt;
struct HirDecl;

using HirExprPtr = std::unique_ptr<HirExpr>;
using HirStmtPtr = std::unique_ptr<HirStmt>;
using HirDeclPtr = std::unique_ptr<HirDecl>;

// ============================================================
// HIR式ノード
// ============================================================

// リテラル
struct HirLiteral {
    std::variant<std::monostate, bool, int64_t, double, char, std::string> value;
};

// 変数参照
struct HirVarRef {
    std::string name;
};

// 二項演算
enum class HirBinaryOp {
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    BitAnd,
    BitOr,
    BitXor,
    Shl,
    Shr,
    And,
    Or,
    Eq,
    Ne,
    Lt,
    Gt,
    Le,
    Ge,
    Assign,
};

struct HirBinary {
    HirBinaryOp op;
    HirExprPtr lhs;
    HirExprPtr rhs;
};

// 単項演算
enum class HirUnaryOp {
    Neg,
    Not,
    BitNot,
    Deref,
    AddrOf,
    PreInc,   // ++x
    PreDec,   // --x
    PostInc,  // x++
    PostDec,  // x--
};

struct HirUnary {
    HirUnaryOp op;
    HirExprPtr operand;
};

// 関数呼び出し
struct HirCall {
    std::string func_name;  // 完全修飾名
    std::vector<HirExprPtr> args;
};

// 配列アクセス
struct HirIndex {
    HirExprPtr object;
    HirExprPtr index;
};

// メンバアクセス
struct HirMember {
    HirExprPtr object;
    std::string member;
};

// 三項演算子
struct HirTernary {
    HirExprPtr condition;
    HirExprPtr then_expr;
    HirExprPtr else_expr;
};

// 式の種類
using HirExprKind = std::variant<std::unique_ptr<HirLiteral>, std::unique_ptr<HirVarRef>,
                                 std::unique_ptr<HirBinary>, std::unique_ptr<HirUnary>,
                                 std::unique_ptr<HirCall>, std::unique_ptr<HirIndex>,
                                 std::unique_ptr<HirMember>, std::unique_ptr<HirTernary>>;

struct HirExpr {
    HirExprKind kind;
    TypePtr type;  // 型情報（必須）
    Span span;

    template <typename T>
    HirExpr(std::unique_ptr<T> k, TypePtr t, Span s = {})
        : kind(std::move(k)), type(std::move(t)), span(s) {}
};

// ============================================================
// HIR文ノード
// ============================================================

// 変数宣言
struct HirLet {
    std::string name;
    TypePtr type;
    HirExprPtr init;
    bool is_const;
    HirExprPtr ctor_call;  // コンストラクタ呼び出し（オプション）
};

// 代入
struct HirAssign {
    std::string target;
    HirExprPtr value;
};

// return
struct HirReturn {
    HirExprPtr value;
};

// if
struct HirIf {
    HirExprPtr cond;
    std::vector<HirStmtPtr> then_block;
    std::vector<HirStmtPtr> else_block;
};

// loop (無限ループ)
struct HirLoop {
    std::vector<HirStmtPtr> body;
};

// while文
struct HirWhile {
    HirExprPtr cond;
    std::vector<HirStmtPtr> body;
};

// for文
struct HirFor {
    HirStmtPtr init;    // 初期化（nullptrの場合あり）
    HirExprPtr cond;    // 条件（nullptrの場合は無限ループ）
    HirExprPtr update;  // 更新式（nullptrの場合あり）
    std::vector<HirStmtPtr> body;
};

// break
struct HirBreak {};

// continue
struct HirContinue {};

// defer
struct HirDefer {
    HirStmtPtr body;
};

// 式文
struct HirExprStmt {
    HirExprPtr expr;
};

// ブロック文
struct HirBlock {
    std::vector<HirStmtPtr> stmts;
};

// switch文のパターン条件
struct HirSwitchPattern {
    enum Kind {
        SingleValue,  // 単一値
        Range,        // 範囲
        Or            // ORで結合された複数パターン
    } kind;

    HirExprPtr value;                                            // 単一値の場合
    HirExprPtr range_start;                                      // 範囲の開始値
    HirExprPtr range_end;                                        // 範囲の終了値
    std::vector<std::unique_ptr<HirSwitchPattern>> or_patterns;  // ORパターンのリスト
};

// switch文のケース
struct HirSwitchCase {
    std::unique_ptr<HirSwitchPattern> pattern;  // nullptr for else/default case
    std::vector<HirStmtPtr> stmts;              // ケース内の文（独立スコープ）

    // 旧互換性のためのヘルパー
    HirExprPtr value;  // 単一値パターンの場合のみ使用（後方互換性）
};

// switch文
struct HirSwitch {
    HirExprPtr expr;
    std::vector<HirSwitchCase> cases;
};

using HirStmtKind =
    std::variant<std::unique_ptr<HirLet>, std::unique_ptr<HirAssign>, std::unique_ptr<HirReturn>,
                 std::unique_ptr<HirIf>, std::unique_ptr<HirLoop>, std::unique_ptr<HirWhile>,
                 std::unique_ptr<HirFor>, std::unique_ptr<HirBreak>, std::unique_ptr<HirContinue>,
                 std::unique_ptr<HirDefer>, std::unique_ptr<HirExprStmt>, std::unique_ptr<HirBlock>,
                 std::unique_ptr<HirSwitch>>;

struct HirStmt {
    HirStmtKind kind;
    Span span;

    template <typename T>
    HirStmt(std::unique_ptr<T> k, Span s = {}) : kind(std::move(k)), span(s) {}
};

// ============================================================
// HIR宣言ノード
// ============================================================

// パラメータ
struct HirParam {
    std::string name;
    TypePtr type;
};

// 関数
struct HirFunction {
    std::string name;
    std::vector<HirParam> params;
    TypePtr return_type;
    std::vector<HirStmtPtr> body;
    bool is_export = false;
    bool is_constructor = false;
    bool is_destructor = false;
};

// 構造体フィールド
struct HirField {
    std::string name;
    TypePtr type;
};

// 構造体
struct HirStruct {
    std::string name;
    std::vector<HirField> fields;
    bool is_export = false;
};

// メソッドシグネチャ
struct HirMethodSig {
    std::string name;
    std::vector<HirParam> params;
    TypePtr return_type;
};

// インターフェース
struct HirInterface {
    std::string name;
    std::vector<HirMethodSig> methods;
    bool is_export = false;
};

// 実装ブロック
struct HirImpl {
    std::string interface_name;  // 空の場合は inherent impl
    std::string target_type;
    std::vector<std::unique_ptr<HirFunction>> methods;
    bool is_ctor_impl = false;  // コンストラクタ/デストラクタ専用impl
};

// インポート
struct HirImport {
    std::vector<std::string> path;  // e.g., {"std", "io"}
    std::string alias;
};

// Enumメンバ
struct HirEnumMember {
    std::string name;
    int64_t value;
};

// Enum定義
struct HirEnum {
    std::string name;
    std::vector<HirEnumMember> members;
    bool is_export = false;
};

// Typedef定義
struct HirTypedef {
    std::string name;
    TypePtr type;
    bool is_export = false;
};

using HirDeclKind = std::variant<std::unique_ptr<HirFunction>, std::unique_ptr<HirStruct>,
                                 std::unique_ptr<HirInterface>, std::unique_ptr<HirImpl>,
                                 std::unique_ptr<HirImport>, std::unique_ptr<HirEnum>,
                                 std::unique_ptr<HirTypedef>>;

struct HirDecl {
    HirDeclKind kind;
    Span span;

    template <typename T>
    HirDecl(std::unique_ptr<T> k, Span s = {}) : kind(std::move(k)), span(s) {}
};

// ============================================================
// HIRプログラム
// ============================================================
struct HirProgram {
    std::vector<HirDeclPtr> declarations;
    std::string filename;
};

}  // namespace cm::hir
