#pragma once

#include "../common/span.hpp"
#include "types.hpp"

#include <memory>
#include <optional>
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
    bool is_function_ref = false;  // 関数名への参照（関数ポインタ用）
    bool is_closure = false;       // クロージャ（キャプチャあり）か

    // クロージャ用：キャプチャされた変数の情報
    struct CapturedVar {
        std::string name;
        TypePtr type;
    };
    std::vector<CapturedVar> captured_vars;
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
    std::string func_name;    // 完全修飾名（関数ポインタの場合は変数名）
    std::string callee_name;  // クロージャ用：実際の関数名
    std::vector<HirExprPtr> args;
    std::vector<HirExprPtr> captured_args;  // キャプチャされた変数の値
    bool is_indirect = false;               // 関数ポインタ経由の呼び出し
    bool is_closure = false;                // クロージャ呼び出しか
};

// 配列アクセス
struct HirIndex {
    HirExprPtr object;
    HirExprPtr index;                 // 単一インデックス（後方互換性）
    std::vector<HirExprPtr> indices;  // 多次元配列用の複数インデックス
    // indices が空でない場合、index は無視される
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

// 構造体リテラル
struct HirStructLiteralField {
    std::string name;  // フィールド名（必須）
    HirExprPtr value;
};

struct HirStructLiteral {
    std::string type_name;
    std::vector<HirStructLiteralField> fields;
};

// 配列リテラル
struct HirArrayLiteral {
    std::vector<HirExprPtr> elements;
};

// ラムダ式のパラメータ
struct HirLambdaParam {
    std::string name;
    TypePtr type;
};

// ラムダ式
struct HirLambda {
    std::vector<HirLambdaParam> params;
    TypePtr return_type;  // nullptrなら推論
    std::vector<HirStmtPtr> body;
    std::string generated_name;  // 生成されたクロージャ名
};

// キャスト式
struct HirCast {
    HirExprPtr operand;
    TypePtr target_type;
};

// 式の種類
using HirExprKind =
    std::variant<std::unique_ptr<HirLiteral>, std::unique_ptr<HirVarRef>,
                 std::unique_ptr<HirBinary>, std::unique_ptr<HirUnary>, std::unique_ptr<HirCall>,
                 std::unique_ptr<HirIndex>, std::unique_ptr<HirMember>, std::unique_ptr<HirTernary>,
                 std::unique_ptr<HirStructLiteral>, std::unique_ptr<HirArrayLiteral>,
                 std::unique_ptr<HirLambda>, std::unique_ptr<HirCast>>;

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
    bool is_static = false;  // static変数フラグ
    bool is_move = false;    // move初期化フラグ（真のゼロコストmove用）
    HirExprPtr ctor_call;    // コンストラクタ呼び出し（オプション）
};

// 代入
struct HirAssign {
    HirExprPtr target;  // 左辺値（変数参照、メンバーアクセス、配列アクセス等）
    HirExprPtr value;  // 右辺値
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

// インラインアセンブリ
struct HirAsm {
    std::string code;                   // アセンブリコード
    bool is_must;                       // must修飾（最適化抑制）
    std::vector<std::string> clobbers;  // 破壊レジスタ
};

using HirStmtKind =
    std::variant<std::unique_ptr<HirLet>, std::unique_ptr<HirAssign>, std::unique_ptr<HirReturn>,
                 std::unique_ptr<HirIf>, std::unique_ptr<HirLoop>, std::unique_ptr<HirWhile>,
                 std::unique_ptr<HirFor>, std::unique_ptr<HirBreak>, std::unique_ptr<HirContinue>,
                 std::unique_ptr<HirDefer>, std::unique_ptr<HirExprStmt>, std::unique_ptr<HirBlock>,
                 std::unique_ptr<HirSwitch>, std::unique_ptr<HirAsm>>;

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

// ジェネリック型パラメータ
struct HirGenericParam {
    std::string name;
    std::vector<std::string> bounds;  // 型制約（例: Ord, Clone）
};

// メソッドのアクセス修飾子
enum class HirMethodAccess {
    Public,  // デフォルト（外部からアクセス可能）
    Private  // impl内からのみアクセス可能
};

// 関数
struct HirFunction {
    std::string name;
    std::vector<HirGenericParam> generic_params;  // ジェネリックパラメータ
    std::vector<HirParam> params;
    TypePtr return_type;
    std::vector<HirStmtPtr> body;
    bool is_export = false;
    bool is_extern = false;    // extern "C" 関数
    bool is_variadic = false;  // 可変長引数（FFI用）
    bool is_constructor = false;
    bool is_destructor = false;
    bool is_overload = false;                          // overloadキーワードの有無
    HirMethodAccess access = HirMethodAccess::Public;  // メソッドの場合のアクセス修飾子
};

// フィールドのアクセス修飾子
enum class HirFieldAccess {
    Public,  // デフォルト（外部からアクセス可能）
    Private,  // コンストラクタ/デストラクタ内のthisポインタからのみアクセス可能
    Default  // デフォルトメンバ（構造体に1つのみ）
};

// 構造体フィールド
struct HirField {
    std::string name;
    TypePtr type;
    HirFieldAccess access = HirFieldAccess::Public;  // デフォルトはpublic
};

// 構造体
struct HirStruct {
    std::string name;
    std::vector<HirGenericParam> generic_params;  // ジェネリックパラメータ
    std::vector<HirField> fields;
    std::vector<std::string> auto_impls;  // with キーワードで自動実装するinterface
    bool is_export = false;
    bool has_explicit_constructor = false;
    bool is_css = false;
};

// メソッドシグネチャ
struct HirMethodSig {
    std::string name;
    std::vector<HirParam> params;
    TypePtr return_type;
    HirMethodAccess access = HirMethodAccess::Public;  // デフォルトはpublic
};

// 演算子の種類（ASTからコピー）
enum class HirOperatorKind {
    Eq,      // ==
    Ne,      // != (自動導出)
    Lt,      // <
    Gt,      // > (自動導出)
    Le,      // <= (自動導出)
    Ge,      // >= (自動導出)
    Add,     // +
    Sub,     // -
    Mul,     // *
    Div,     // /
    Mod,     // %
    BitAnd,  // &
    BitOr,   // |
    BitXor,  // ^
    Shl,     // <<
    Shr,     // >>
    Neg,     // - (unary)
    Not,     // ! (unary)
    BitNot,  // ~ (unary)
};

// 演算子シグネチャ
struct HirOperatorSig {
    HirOperatorKind op;
    std::vector<HirParam> params;
    TypePtr return_type;
};

// 演算子実装
struct HirOperatorImpl {
    HirOperatorKind op;
    std::vector<HirParam> params;
    TypePtr return_type;
    std::vector<HirStmtPtr> body;
};

// インターフェース
struct HirInterface {
    std::string name;
    std::vector<HirGenericParam> generic_params;  // ジェネリックパラメータ
    std::vector<HirMethodSig> methods;
    std::vector<HirOperatorSig> operators;  // 演算子シグネチャ
    bool is_export = false;
};

// where句
struct HirWhereClause {
    std::string type_param;
    std::string constraint_type;
};

// 実装ブロック
struct HirImpl {
    std::string interface_name;  // 空の場合は inherent impl
    std::string target_type;
    std::vector<HirGenericParam> generic_params;  // ジェネリックパラメータ
    std::vector<std::unique_ptr<HirFunction>> methods;
    std::vector<std::unique_ptr<HirOperatorImpl>> operators;  // 演算子実装
    std::vector<HirWhereClause> where_clauses;                // where句
    bool is_ctor_impl = false;  // コンストラクタ/デストラクタ専用impl
};

// インポート
struct HirImport {
    std::vector<std::string> path;  // e.g., {"std", "io"}
    std::string package_name;       // パッケージ名 (e.g. "axios")
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

// グローバル変数/定数定義
struct HirGlobalVar {
    std::string name;
    TypePtr type;
    HirExprPtr init;
    bool is_const;
    bool is_export = false;
};

// extern "C" ブロック
struct HirExternBlock {
    std::string language;      // "C" など
    std::string package_name;  // パッケージ名 (FFI用)
    std::vector<std::unique_ptr<HirFunction>> functions;
};

using HirDeclKind =
    std::variant<std::unique_ptr<HirFunction>, std::unique_ptr<HirStruct>,
                 std::unique_ptr<HirInterface>, std::unique_ptr<HirImpl>,
                 std::unique_ptr<HirImport>, std::unique_ptr<HirEnum>, std::unique_ptr<HirTypedef>,
                 std::unique_ptr<HirGlobalVar>, std::unique_ptr<HirExternBlock>>;

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
