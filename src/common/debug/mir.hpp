#pragma once

#include "../debug.hpp"

#include <string>

namespace cm::debug::mir {

/// MIR メッセージID
enum class Id {
    // 基本フロー
    LowerStart,
    LowerEnd,
    OptimizeStart,
    OptimizeEnd,

    // 関数処理
    FunctionLower,
    FunctionAnalyze,
    BasicBlockCreate,
    BasicBlockConnect,
    BasicBlockSwitch,
    PhiNodeInsert,

    // 文のlowering
    StatementLower,
    LetStmtLower,
    AssignStmtLower,
    ReturnStmtLower,
    IfStmtLower,
    WhileStmtLower,
    ForStmtLower,
    SwitchStmtLower,
    BlockStmtLower,
    BreakStmtLower,
    ContinueStmtLower,

    // 式のlowering
    ExprLower,
    LiteralExpr,
    VarRef,
    BinaryExpr,
    UnaryExpr,
    CallExpr,
    TernaryExpr,
    MemberExpr,
    IndexExpr,
    FieldAccess,

    // ローカル変数
    LocalAlloc,
    LocalLoad,
    LocalStore,
    TempAlloc,
    ParamAlloc,
    StorageLive,
    StorageDead,
    InitExpr,

    // 命令生成
    InstAlloc,
    InstLoad,
    InstStore,
    InstBinary,
    InstUnary,
    InstCall,
    InstReturn,
    InstBranch,
    InstJump,
    InstPhi,
    InstSwitch,
    InstNop,

    // オペランド
    OperandCopy,
    OperandMove,
    OperandConst,
    PlaceField,
    PlaceDeref,
    PlaceIndex,

    // SSA形式
    SSAConstruct,
    SSAVariable,
    SSAUse,
    SSADef,
    DominanceCompute,
    DominanceFrontier,

    // 最適化パス
    OptDeadCodeElim,
    OptConstProp,
    OptConstFold,
    OptCommonSubexpr,
    OptInline,
    OptLoopAnalysis,
    OptSimplify,
    OptCopyElim,

    // 型とメモリ
    TypeLower,
    TypeCheck,
    MemoryLayout,
    RegisterAlloc,
    StackAlloc,

    // 制御フロー
    CFGBuild,
    CFGSimplify,
    CFGEdge,
    LoopDetect,
    LoopHeader,
    LoopExit,

    // エラーと警告
    Error,
    Warning,
    TypeMismatch,
    UnresolvedVar,

    // デバッグ情報
    ValueDump,
    BlockDump,
    GraphDump,
    InstDump,
    LocalDump
};

/// メッセージテーブル [en, ja]
inline const char* messages[][2] = {
    // 基本フロー
    {"Starting MIR lowering", "MIR変換を開始"},
    {"Completed MIR lowering", "MIR変換を完了"},
    {"Starting MIR optimization", "MIR最適化を開始"},
    {"Completed MIR optimization", "MIR最適化を完了"},

    // 関数処理
    {"Lowering function", "関数を変換中"},
    {"Analyzing function", "関数を解析中"},
    {"Creating basic block", "基本ブロックを作成"},
    {"Connecting basic blocks", "基本ブロックを接続"},
    {"Switching basic block", "基本ブロックを切り替え"},
    {"Inserting PHI node", "PHIノードを挿入"},

    // 文のlowering
    {"Lowering statement", "文を変換"},
    {"Lowering let statement", "let文を変換"},
    {"Lowering assignment", "代入文を変換"},
    {"Lowering return", "return文を変換"},
    {"Lowering if statement", "if文を変換"},
    {"Lowering while loop", "whileループを変換"},
    {"Lowering for loop", "forループを変換"},
    {"Lowering switch", "switch文を変換"},
    {"Lowering block", "ブロックを変換"},
    {"Lowering break", "break文を変換"},
    {"Lowering continue", "continue文を変換"},

    // 式のlowering
    {"Lowering expression", "式を変換"},
    {"Literal expression", "リテラル式"},
    {"Variable reference", "変数参照"},
    {"Binary expression", "二項式"},
    {"Unary expression", "単項式"},
    {"Call expression", "関数呼び出し"},
    {"Ternary expression", "三項式"},
    {"Member expression", "メンバー式"},
    {"Index expression", "インデックス式"},
    {"Field access", "フィールドアクセス"},

    // ローカル変数
    {"Allocating local", "ローカル変数を割り当て"},
    {"Loading local", "ローカル変数を読み込み"},
    {"Storing local", "ローカル変数に格納"},
    {"Allocating temp", "一時変数を割り当て"},
    {"Allocating param", "パラメータを割り当て"},
    {"Storage live", "ストレージ有効"},
    {"Storage dead", "ストレージ無効"},
    {"Initializer expression", "初期化式"},

    // 命令生成
    {"Generating alloca instruction", "alloca命令を生成"},
    {"Generating load instruction", "load命令を生成"},
    {"Generating store instruction", "store命令を生成"},
    {"Generating binary operation", "二項演算を生成"},
    {"Generating unary operation", "単項演算を生成"},
    {"Generating call instruction", "call命令を生成"},
    {"Generating return instruction", "return命令を生成"},
    {"Generating branch instruction", "branch命令を生成"},
    {"Generating jump instruction", "jump命令を生成"},
    {"Generating PHI instruction", "PHI命令を生成"},
    {"Generating switch instruction", "switch命令を生成"},
    {"Generating NOP", "NOP命令を生成"},

    // オペランド
    {"Copy operand", "コピーオペランド"},
    {"Move operand", "ムーブオペランド"},
    {"Constant operand", "定数オペランド"},
    {"Field place", "フィールドプレース"},
    {"Deref place", "参照外しプレース"},
    {"Index place", "インデックスプレース"},

    // SSA形式
    {"Constructing SSA form", "SSA形式を構築"},
    {"Creating SSA variable", "SSA変数を作成"},
    {"SSA use", "SSA使用"},
    {"SSA definition", "SSA定義"},
    {"Computing dominance tree", "支配木を計算"},
    {"Computing dominance frontier", "支配境界を計算"},

    // 最適化パス
    {"Dead code elimination", "デッドコード除去"},
    {"Constant propagation", "定数伝播"},
    {"Constant folding", "定数畳み込み"},
    {"Common subexpression elimination", "共通部分式除去"},
    {"Function inlining", "関数インライン化"},
    {"Loop analysis", "ループ解析"},
    {"Simplification", "簡略化"},
    {"Copy elimination", "コピー除去"},

    // 型とメモリ
    {"Lowering type", "型を変換"},
    {"Type checking", "型チェック"},
    {"Computing memory layout", "メモリレイアウトを計算"},
    {"Allocating registers", "レジスタ割り当て"},
    {"Stack allocation", "スタック割り当て"},

    // 制御フロー
    {"Building CFG", "CFGを構築"},
    {"Simplifying CFG", "CFGを簡略化"},
    {"CFG edge", "CFGエッジ"},
    {"Detecting loops", "ループを検出"},
    {"Loop header", "ループヘッダ"},
    {"Loop exit", "ループ出口"},

    // エラーと警告
    {"MIR error", "MIRエラー"},
    {"MIR warning", "MIR警告"},
    {"Type mismatch", "型の不一致"},
    {"Unresolved variable", "未解決の変数"},

    // デバッグ情報
    {"Dumping value", "値をダンプ"},
    {"Dumping block", "ブロックをダンプ"},
    {"Dumping graph", "グラフをダンプ"},
    {"Dumping instruction", "命令をダンプ"},
    {"Dumping local", "ローカル変数をダンプ"}};

inline const char* get(Id id) {
    return messages[static_cast<int>(id)][::cm::debug::g_lang];
}

inline void log(Id id, ::cm::debug::Level level = ::cm::debug::Level::Debug) {
    if (!::cm::debug::g_debug_mode || level < ::cm::debug::g_debug_level)
        return;
    ::cm::debug::log(::cm::debug::Stage::Mir, level, get(id));
}

inline void log(Id id, const std::string& detail,
                ::cm::debug::Level level = ::cm::debug::Level::Debug) {
    if (!::cm::debug::g_debug_mode || level < ::cm::debug::g_debug_level)
        return;
    ::cm::debug::log(::cm::debug::Stage::Mir, level, std::string(get(id)) + ": " + detail);
}

/// 値の詳細情報をダンプ（Traceレベル）
inline void dump_value(const std::string& name, const std::string& value) {
    if (!::cm::debug::g_debug_mode || ::cm::debug::Level::Trace < ::cm::debug::g_debug_level)
        return;
    ::cm::debug::log(::cm::debug::Stage::Mir, ::cm::debug::Level::Trace,
                     "Value " + name + " = " + value);
}

/// ブロックの詳細情報をダンプ（Traceレベル）
inline void dump_block(const std::string& block_name, const std::string& info) {
    if (!::cm::debug::g_debug_mode || ::cm::debug::Level::Trace < ::cm::debug::g_debug_level)
        return;
    ::cm::debug::log(::cm::debug::Stage::Mir, ::cm::debug::Level::Trace,
                     "Block " + block_name + ": " + info);
}

}  // namespace cm::debug::mir