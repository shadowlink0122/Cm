#pragma once

#include "../debug.hpp"

#include <string>

namespace cm::debug::hir {

/// HIR メッセージID
enum class Id {
    // 基本フロー
    LowerStart,
    LowerEnd,
    OptimizeStart,
    OptimizeEnd,
    ProgramLower,
    DeclarationCount,

    // ノード作成
    NodeCreate,
    FunctionNode,
    FunctionName,
    FunctionParams,
    FunctionReturn,
    FunctionBody,
    StructNode,
    StructField,
    EnumNode,
    EnumVariant,
    InterfaceNode,
    InterfaceMethod,
    ImplNode,
    ImplTarget,
    ImplMethod,
    ImportNode,
    ImportPath,

    // 式変換（基本）
    ExprLower,
    ExprType,
    BinaryExprLower,
    BinaryOp,
    BinaryLhs,
    BinaryRhs,
    UnaryExprLower,
    UnaryOp,
    UnaryOperand,
    CallExprLower,
    CallTarget,
    CallArgs,
    CallArgEval,

    // 式変換（詳細）
    FieldAccessLower,
    FieldName,
    CastExprLower,
    CastFrom,
    CastTo,
    LiteralLower,
    IntLiteral,
    FloatLiteral,
    StringLiteral,
    BoolLiteral,
    CharLiteral,
    NullLiteral,
    IdentifierLower,
    IdentifierRef,
    IndexLower,
    IndexBase,
    IndexValue,
    AssignLower,
    AssignTarget,
    AssignValue,
    NewExprLower,
    NewType,
    NewArgs,
    LambdaLower,
    LambdaParams,
    LambdaBody,

    // 文変換（基本）
    StmtLower,
    StmtType,
    BlockLower,
    BlockSize,
    BlockStmt,
    LetLower,
    LetName,
    LetType,
    LetInit,
    LetConst,

    // 制御フロー
    IfLower,
    IfCond,
    IfThen,
    IfElse,
    WhileLower,
    WhileCond,
    WhileBody,
    ForLower,
    ForInit,
    ForCond,
    ForUpdate,
    ForBody,
    SwitchLower,
    SwitchExpr,
    SwitchCase,
    CasePattern,
    CaseBody,
    MatchLower,
    MatchExpr,
    MatchArm,
    LoopLower,
    BreakLower,
    ContinueLower,
    ReturnLower,
    ReturnValue,
    ReturnVoid,

    // 型処理
    TypeResolve,
    TypeInfer,
    TypeCheck,
    TypeMismatch,
    TypeCast,
    TypeCoercion,
    GenericResolve,
    GenericInstantiate,
    GenericParam,
    GenericArg,
    TraitResolve,
    TraitImpl,
    TraitMethod,
    TraitBound,

    // パターンマッチング
    PatternLower,
    PatternValue,
    PatternRange,
    PatternOr,
    PatternStruct,
    PatternTuple,
    PatternWildcard,
    PatternBind,

    // 最適化
    Optimize,
    DesugarPass,
    InlinePass,
    InlineCandidate,
    InlineExpand,
    SimplifyPass,
    ConstFold,
    DeadCodeElim,
    CommonSubExpr,

    // シンボル解決
    SymbolResolve,
    SymbolFound,
    SymbolNotFound,
    SymbolAmbiguous,
    NameBind,
    NameShadow,
    ScopeEnter,
    ScopeExit,
    ScopeDepth,
    LocalVar,
    GlobalVar,
    FunctionRef,
    TypeRef,

    // 型システム詳細
    StructInit,
    FieldInit,
    ArrayInit,
    ArraySize,
    TupleInit,
    PointerDeref,
    Reference,
    Move,
    Copy,
    Borrow,

    // エラー処理
    Error,
    Warning,
    RecoverError,
    InvalidNode,
    UnsupportedFeature
};

/// メッセージテーブル [en, ja]
inline const char* messages[][2] = {
    // 基本フロー
    {"Starting HIR lowering", "HIR変換を開始"},
    {"Completed HIR lowering", "HIR変換を完了"},
    {"Starting HIR optimization", "HIR最適化を開始"},
    {"Completed HIR optimization", "HIR最適化を完了"},
    {"Lowering program", "プログラムを変換"},
    {"Declaration count", "宣言数"},

    // ノード作成
    {"Creating HIR node", "HIRノードを作成"},
    {"Creating function node", "関数ノードを作成"},
    {"Function name", "関数名"},
    {"Function parameters", "関数パラメータ"},
    {"Function return type", "関数戻り値型"},
    {"Function body", "関数本体"},
    {"Creating struct node", "構造体ノードを作成"},
    {"Struct field", "構造体フィールド"},
    {"Creating enum node", "列挙型ノードを作成"},
    {"Enum variant", "列挙型バリアント"},
    {"Creating interface node", "インターフェースノードを作成"},
    {"Interface method", "インターフェースメソッド"},
    {"Creating impl node", "implノードを作成"},
    {"Impl target", "impl対象"},
    {"Impl method", "implメソッド"},
    {"Creating import node", "importノードを作成"},
    {"Import path", "importパス"},

    // 式変換（基本）
    {"Lowering expression", "式を変換"},
    {"Expression type", "式の型"},
    {"Lowering binary expression", "二項式を変換"},
    {"Binary operator", "二項演算子"},
    {"Binary left operand", "左オペランド"},
    {"Binary right operand", "右オペランド"},
    {"Lowering unary expression", "単項式を変換"},
    {"Unary operator", "単項演算子"},
    {"Unary operand", "単項オペランド"},
    {"Lowering call expression", "関数呼び出しを変換"},
    {"Call target", "呼び出し対象"},
    {"Call arguments", "呼び出し引数"},
    {"Evaluating call argument", "呼び出し引数を評価"},

    // 式変換（詳細）
    {"Lowering field access", "フィールドアクセスを変換"},
    {"Field name", "フィールド名"},
    {"Lowering cast expression", "キャスト式を変換"},
    {"Cast from type", "キャスト元型"},
    {"Cast to type", "キャスト先型"},
    {"Lowering literal", "リテラルを変換"},
    {"Integer literal", "整数リテラル"},
    {"Float literal", "浮動小数点リテラル"},
    {"String literal", "文字列リテラル"},
    {"Boolean literal", "真偽値リテラル"},
    {"Character literal", "文字リテラル"},
    {"Null literal", "nullリテラル"},
    {"Lowering identifier", "識別子を変換"},
    {"Identifier reference", "識別子参照"},
    {"Lowering index", "インデックスを変換"},
    {"Index base", "インデックスベース"},
    {"Index value", "インデックス値"},
    {"Lowering assignment", "代入を変換"},
    {"Assignment target", "代入先"},
    {"Assignment value", "代入値"},
    {"Lowering new expression", "new式を変換"},
    {"New type", "new型"},
    {"New arguments", "new引数"},
    {"Lowering lambda", "ラムダを変換"},
    {"Lambda parameters", "ラムダパラメータ"},
    {"Lambda body", "ラムダ本体"},

    // 文変換（基本）
    {"Lowering statement", "文を変換"},
    {"Statement type", "文の型"},
    {"Lowering block", "ブロックを変換"},
    {"Block size", "ブロックサイズ"},
    {"Block statement", "ブロック文"},
    {"Lowering let", "let文を変換"},
    {"Let variable name", "let変数名"},
    {"Let type", "let型"},
    {"Let initializer", "let初期化子"},
    {"Let const", "let定数"},

    // 制御フロー
    {"Lowering if statement", "if文を変換"},
    {"If condition", "if条件"},
    {"If then block", "thenブロック"},
    {"If else block", "elseブロック"},
    {"Lowering while", "while文を変換"},
    {"While condition", "while条件"},
    {"While body", "while本体"},
    {"Lowering for", "for文を変換"},
    {"For init", "for初期化"},
    {"For condition", "for条件"},
    {"For update", "for更新"},
    {"For body", "for本体"},
    {"Lowering switch", "switch文を変換"},
    {"Switch expression", "switch式"},
    {"Switch case", "switchケース"},
    {"Case pattern", "ケースパターン"},
    {"Case body", "ケース本体"},
    {"Lowering match statement", "match文を変換"},
    {"Match expression", "match式"},
    {"Match arm", "matchアーム"},
    {"Lowering loop", "ループを変換"},
    {"Lowering break", "break文を変換"},
    {"Lowering continue", "continue文を変換"},
    {"Lowering return", "return文を変換"},
    {"Return value", "戻り値"},
    {"Return void", "void戻り"},

    // 型処理
    {"Resolving type", "型を解決"},
    {"Inferring type", "型を推論"},
    {"Checking type", "型をチェック"},
    {"Type mismatch", "型の不一致"},
    {"Type cast", "型キャスト"},
    {"Type coercion", "型強制"},
    {"Resolving generic", "ジェネリックを解決"},
    {"Generic instantiation", "ジェネリック実体化"},
    {"Generic parameter", "ジェネリックパラメータ"},
    {"Generic argument", "ジェネリック引数"},
    {"Resolving trait", "トレイトを解決"},
    {"Trait implementation", "トレイト実装"},
    {"Trait method", "トレイトメソッド"},
    {"Trait bound", "トレイト境界"},

    // パターンマッチング
    {"Lowering pattern", "パターンを変換"},
    {"Pattern value", "パターン値"},
    {"Pattern range", "パターン範囲"},
    {"Pattern or", "パターンOR"},
    {"Pattern struct", "パターン構造体"},
    {"Pattern tuple", "パターンタプル"},
    {"Pattern wildcard", "パターンワイルドカード"},
    {"Pattern bind", "パターンバインド"},

    // 最適化
    {"Optimizing HIR", "HIRを最適化"},
    {"Desugaring pass", "脱糖パス"},
    {"Inlining pass", "インライン化パス"},
    {"Inline candidate", "インライン候補"},
    {"Inline expansion", "インライン展開"},
    {"Simplification pass", "簡略化パス"},
    {"Constant folding", "定数畳み込み"},
    {"Dead code elimination", "デッドコード除去"},
    {"Common subexpression", "共通部分式"},

    // シンボル解決
    {"Resolving symbol", "シンボルを解決"},
    {"Symbol found", "シンボル発見"},
    {"Symbol not found", "シンボルなし"},
    {"Symbol ambiguous", "シンボル曖昧"},
    {"Binding name", "名前をバインド"},
    {"Name shadowing", "名前シャドウイング"},
    {"Entering scope", "スコープに入る"},
    {"Exiting scope", "スコープを出る"},
    {"Scope depth", "スコープ深度"},
    {"Local variable", "ローカル変数"},
    {"Global variable", "グローバル変数"},
    {"Function reference", "関数参照"},
    {"Type reference", "型参照"},

    // 型システム詳細
    {"Struct initialization", "構造体初期化"},
    {"Field initialization", "フィールド初期化"},
    {"Array initialization", "配列初期化"},
    {"Array size", "配列サイズ"},
    {"Tuple initialization", "タプル初期化"},
    {"Pointer dereference", "ポインタ参照外し"},
    {"Reference", "参照"},
    {"Move", "ムーブ"},
    {"Copy", "コピー"},
    {"Borrow", "借用"},

    // エラー処理
    {"HIR error", "HIRエラー"},
    {"HIR warning", "HIR警告"},
    {"Recovery from error", "エラーから回復"},
    {"Invalid node", "無効なノード"},
    {"Unsupported feature", "未サポート機能"}};

inline const char* get(Id id) {
    return messages[static_cast<int>(id)][::cm::debug::g_lang];
}

inline void log(Id id, ::cm::debug::Level level = ::cm::debug::Level::Debug) {
    if (!::cm::debug::g_debug_mode || level < ::cm::debug::g_debug_level)
        return;
    ::cm::debug::log(::cm::debug::Stage::Hir, level, get(id));
}

inline void log(Id id, const std::string& detail,
                ::cm::debug::Level level = ::cm::debug::Level::Debug) {
    if (!::cm::debug::g_debug_mode || level < ::cm::debug::g_debug_level)
        return;
    ::cm::debug::log(::cm::debug::Stage::Hir, level, std::string(get(id)) + ": " + detail);
}

/// HIRノード情報をダンプ（Traceレベル）
inline void dump_node(const std::string& node_type, const std::string& info) {
    if (!::cm::debug::g_debug_mode || ::cm::debug::Level::Trace < ::cm::debug::g_debug_level)
        return;
    ::cm::debug::log(::cm::debug::Stage::Hir, ::cm::debug::Level::Trace,
                     "HIR Node[" + node_type + "]: " + info);
}

/// 型情報をダンプ（Traceレベル）
inline void dump_type(const std::string& var_name, const std::string& type_info) {
    if (!::cm::debug::g_debug_mode || ::cm::debug::Level::Trace < ::cm::debug::g_debug_level)
        return;
    ::cm::debug::log(::cm::debug::Stage::Hir, ::cm::debug::Level::Trace,
                     "Type[" + var_name + "] = " + type_info);
}

/// シンボル情報をダンプ（Traceレベル）
inline void dump_symbol(const std::string& name, const std::string& scope,
                        const std::string& type = "") {
    if (!::cm::debug::g_debug_mode || ::cm::debug::Level::Trace < ::cm::debug::g_debug_level)
        return;
    std::string msg = "Symbol[" + name + "] @ " + scope;
    if (!type.empty()) {
        msg += " : " + type;
    }
    ::cm::debug::log(::cm::debug::Stage::Hir, ::cm::debug::Level::Trace, msg);
}

/// 式情報をダンプ（Traceレベル）
inline void dump_expr(const std::string& expr_type, const std::string& detail) {
    if (!::cm::debug::g_debug_mode || ::cm::debug::Level::Trace < ::cm::debug::g_debug_level)
        return;
    ::cm::debug::log(::cm::debug::Stage::Hir, ::cm::debug::Level::Trace,
                     "Expr[" + expr_type + "]: " + detail);
}

/// 文情報をダンプ（Traceレベル）
inline void dump_stmt(const std::string& stmt_type, const std::string& detail) {
    if (!::cm::debug::g_debug_mode || ::cm::debug::Level::Trace < ::cm::debug::g_debug_level)
        return;
    ::cm::debug::log(::cm::debug::Stage::Hir, ::cm::debug::Level::Trace,
                     "Stmt[" + stmt_type + "]: " + detail);
}

/// 最適化情報をダンプ（Traceレベル）
inline void dump_optimization(const std::string& pass_name, const std::string& action,
                              int count = -1) {
    if (!::cm::debug::g_debug_mode || ::cm::debug::Level::Trace < ::cm::debug::g_debug_level)
        return;
    std::string msg = "Optimization[" + pass_name + "]: " + action;
    if (count >= 0) {
        msg += " (count=" + std::to_string(count) + ")";
    }
    ::cm::debug::log(::cm::debug::Stage::Hir, ::cm::debug::Level::Trace, msg);
}

/// スコープ情報をダンプ（Traceレベル）
inline void dump_scope(int depth, const std::string& context = "") {
    if (!::cm::debug::g_debug_mode || ::cm::debug::Level::Trace < ::cm::debug::g_debug_level)
        return;
    std::string msg = "Scope depth: " + std::to_string(depth);
    if (!context.empty()) {
        msg += " (" + context + ")";
    }
    ::cm::debug::log(::cm::debug::Stage::Hir, ::cm::debug::Level::Trace, msg);
}

}  // namespace cm::debug::hir
