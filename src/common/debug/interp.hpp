#pragma once

#include "../debug.hpp"

#include <any>
#include <sstream>
#include <string>

namespace cm::debug::interp {

/// Interpreter メッセージID
enum class Id {
    // 基本フロー
    Start,
    End,
    ExecuteBlock,
    ExecuteInstruction,
    EntryPoint,
    ProgramLoad,
    FunctionSearch,
    FunctionCheck,
    FunctionFound,
    ExecuteStart,
    ExecuteEnd,
    Exception,

    // 関数実行
    FunctionEnter,
    FunctionExit,
    FunctionArgs,
    ArgStore,
    ReturnInit,
    BlockEnter,
    BlockExecute,
    BlockStats,
    StmtExecute,
    TerminatorExecute,
    NoTerminator,

    // 代入と格納
    Assign,
    AssignDest,
    Store,
    StoreDirect,
    StoreProjection,
    StoreInitStruct,
    StoreConvertStruct,
    StoreField,
    StoreComplete,
    Load,
    LoadNotFound,
    LoadComplete,
    FieldAccess,
    FieldNotFound,
    NotStruct,

    // Rvalue評価
    RvalueEval,
    RvalueType,
    RvalueUse,
    RvalueUnknown,

    // 演算
    BinaryOp,
    BinaryLhs,
    BinaryRhs,
    UnaryOp,
    FormatConvert,

    // オペランド評価
    OperandEval,
    OperandMove,
    OperandCopy,
    OperandConst,
    OperandUnknown,

    // 変数ライフサイクル
    StorageLive,
    StorageDead,
    Nop,

    // 制御フロー
    Goto,
    SwitchInt,
    SwitchValue,
    SwitchCase,
    SwitchMatch,
    SwitchDefault,
    Return,

    // 関数呼び出し
    Call,
    CallTarget,
    CallArgs,
    CallArgEval,
    CallBuiltin,
    CallUser,
    CallStore,
    CallNotFound,
    CallNoName,
    CallSuccess,

    // 式評価（既存）
    EvalExpr,
    EvalBinary,
    EvalUnary,
    EvalLiteral,
    EvalIdent,
    EvalCall,
    EvalIndex,
    EvalField,

    // 文実行（既存）
    EvalStmt,
    ExecuteIf,
    ExecuteWhile,
    ExecuteFor,
    ExecuteMatch,
    ExecuteBreak,
    ExecuteContinue,

    // 関数呼び出し（既存）
    CallFunc,
    PushFrame,
    PopFrame,

    // 変数操作（既存）
    VarDeclare,
    VarAssign,
    VarLoad,
    VarStore,
    AllocLocal,
    FreeLocal,

    // MIR命令実行（既存）
    ExecAlloca,
    ExecLoad,
    ExecStore,
    ExecBinary,
    ExecUnary,
    ExecBranch,
    ExecJump,
    ExecPhi,

    // スタック操作（既存）
    StackPush,
    StackPop,
    StackPeek,

    // メモリ操作（既存）
    HeapAlloc,
    HeapFree,
    MemCopy,
    MemSet,

    // エラー処理（既存）
    Error,
    Warning,
    RuntimeError,
    TypeError,
    NullPointer,
    OutOfBounds,
    StackOverflow
};

/// メッセージテーブル [en, ja]
inline const char* messages[][2] = {
    // 基本フロー
    {"Starting interpreter", "インタプリタを開始"},
    {"Interpreter finished", "インタプリタを終了"},
    {"Executing block", "ブロックを実行"},
    {"Executing instruction", "命令を実行"},
    {"Entry point", "エントリポイント"},
    {"Program loaded", "プログラムを読み込み"},
    {"Searching for function", "関数を検索"},
    {"Checking function", "関数をチェック"},
    {"Function found", "関数を発見"},
    {"Starting execution", "実行開始"},
    {"Execution completed", "実行完了"},
    {"Exception caught", "例外を捕捉"},

    // 関数実行
    {"Entering function", "関数に入る"},
    {"Exiting function", "関数から出る"},
    {"Function arguments", "関数引数"},
    {"Storing argument", "引数を格納"},
    {"Initializing return value", "戻り値を初期化"},
    {"Entering block", "ブロックに入る"},
    {"Executing block", "ブロックを実行"},
    {"Block statistics", "ブロック統計"},
    {"Executing statement", "ステートメントを実行"},
    {"Executing terminator", "終端命令を実行"},
    {"No terminator", "終端命令なし"},

    // 代入と格納
    {"Assignment", "代入"},
    {"Assignment destination", "代入先"},
    {"Store", "格納"},
    {"Direct store", "直接格納"},
    {"Store with projection", "プロジェクション付き格納"},
    {"Initialize struct", "構造体を初期化"},
    {"Convert to struct", "構造体に変換"},
    {"Store to field", "フィールドに格納"},
    {"Store complete", "格納完了"},
    {"Load", "読み込み"},
    {"Load not found", "読み込み対象なし"},
    {"Load complete", "読み込み完了"},
    {"Field access", "フィールドアクセス"},
    {"Field not found", "フィールドなし"},
    {"Not a struct", "構造体ではない"},

    // Rvalue評価
    {"Evaluating rvalue", "Rvalueを評価"},
    {"Rvalue type", "Rvalueのタイプ"},
    {"Rvalue use", "Rvalue使用"},
    {"Unknown rvalue", "不明なRvalue"},

    // 演算
    {"Binary operation", "二項演算"},
    {"Left operand", "左オペランド"},
    {"Right operand", "右オペランド"},
    {"Unary operation", "単項演算"},
    {"Format conversion", "フォーマット変換"},

    // オペランド評価
    {"Evaluating operand", "オペランドを評価"},
    {"Move operand", "ムーブオペランド"},
    {"Copy operand", "コピーオペランド"},
    {"Constant operand", "定数オペランド"},
    {"Unknown operand", "不明なオペランド"},

    // 変数ライフサイクル
    {"Storage live", "ストレージ有効"},
    {"Storage dead", "ストレージ無効"},
    {"NOP", "NOP"},

    // 制御フロー
    {"Goto", "Goto"},
    {"Switch int", "Switch整数"},
    {"Switch value", "Switch値"},
    {"Switch case", "Switchケース"},
    {"Switch match", "Switchマッチ"},
    {"Switch default", "Switchデフォルト"},
    {"Return", "リターン"},

    // 関数呼び出し
    {"Call", "呼び出し"},
    {"Call target", "呼び出し対象"},
    {"Call arguments", "呼び出し引数"},
    {"Evaluating argument", "引数を評価"},
    {"Calling builtin", "組み込み関数を呼び出し"},
    {"Calling user function", "ユーザー関数を呼び出し"},
    {"Storing result", "結果を格納"},
    {"Function not found", "関数なし"},
    {"No function name", "関数名なし"},
    {"Call success", "呼び出し成功"},

    // 式評価（既存）
    {"Evaluating expression", "式を評価"},
    {"Evaluating binary operation", "二項演算を評価"},
    {"Evaluating unary operation", "単項演算を評価"},
    {"Evaluating literal", "リテラルを評価"},
    {"Evaluating identifier", "識別子を評価"},
    {"Evaluating call", "関数呼び出しを評価"},
    {"Evaluating index", "インデックスを評価"},
    {"Evaluating field", "フィールドを評価"},

    // 文実行（既存）
    {"Evaluating statement", "文を評価"},
    {"Executing if", "if文を実行"},
    {"Executing while", "while文を実行"},
    {"Executing for", "for文を実行"},
    {"Executing match", "match文を実行"},
    {"Executing break", "break文を実行"},
    {"Executing continue", "continue文を実行"},

    // 関数呼び出し（既存）
    {"Calling function", "関数を呼び出し"},
    {"Pushing stack frame", "スタックフレームをプッシュ"},
    {"Popping stack frame", "スタックフレームをポップ"},

    // 変数操作（既存）
    {"Declaring variable", "変数を宣言"},
    {"Assigning variable", "変数に代入"},
    {"Loading variable", "変数を読み込み"},
    {"Storing variable", "変数を保存"},
    {"Allocating local", "ローカル変数を割り当て"},
    {"Freeing local", "ローカル変数を解放"},

    // MIR命令実行（既存）
    {"Executing alloca", "alloca命令を実行"},
    {"Executing load", "load命令を実行"},
    {"Executing store", "store命令を実行"},
    {"Executing binary op", "二項演算命令を実行"},
    {"Executing unary op", "単項演算命令を実行"},
    {"Executing branch", "branch命令を実行"},
    {"Executing jump", "jump命令を実行"},
    {"Executing PHI", "PHI命令を実行"},

    // スタック操作（既存）
    {"Pushing to stack", "スタックにプッシュ"},
    {"Popping from stack", "スタックからポップ"},
    {"Peeking stack", "スタックをピーク"},

    // メモリ操作（既存）
    {"Allocating heap", "ヒープを割り当て"},
    {"Freeing heap", "ヒープを解放"},
    {"Copying memory", "メモリをコピー"},
    {"Setting memory", "メモリを設定"},

    // エラー処理（既存）
    {"Runtime error", "ランタイムエラー"},
    {"Runtime warning", "ランタイム警告"},
    {"Runtime error occurred", "ランタイムエラーが発生"},
    {"Type error", "型エラー"},
    {"Null pointer dereference", "nullポインタ参照"},
    {"Index out of bounds", "インデックス範囲外"},
    {"Stack overflow", "スタックオーバーフロー"}};

inline const char* get(Id id) {
    return messages[static_cast<int>(id)][::cm::debug::g_lang];
}

inline void log(Id id, ::cm::debug::Level level = ::cm::debug::Level::Debug) {
    if (!::cm::debug::g_debug_mode || level < ::cm::debug::g_debug_level)
        return;
    ::cm::debug::log(::cm::debug::Stage::Interp, level, get(id));
}

inline void log(Id id, const std::string& detail,
                ::cm::debug::Level level = ::cm::debug::Level::Debug) {
    if (!::cm::debug::g_debug_mode || level < ::cm::debug::g_debug_level)
        return;
    ::cm::debug::log(::cm::debug::Stage::Interp, level, std::string(get(id)) + ": " + detail);
}

/// 値をダンプ（Traceレベル） - std::any版
template <typename T>
inline void dump_value(const std::string& name, const T& value) {
    if (!::cm::debug::g_debug_mode || ::cm::debug::Level::Trace < ::cm::debug::g_debug_level)
        return;

    std::stringstream ss;
    // std::any型の値を文字列に変換
    if constexpr (std::is_same_v<T, std::any>) {
        const std::any& val = value;
        if (val.type() == typeid(int64_t)) {
            ss << std::any_cast<int64_t>(val);
        } else if (val.type() == typeid(int32_t)) {
            ss << std::any_cast<int32_t>(val);
        } else if (val.type() == typeid(double)) {
            ss << std::any_cast<double>(val);
        } else if (val.type() == typeid(bool)) {
            ss << (std::any_cast<bool>(val) ? "true" : "false");
        } else if (val.type() == typeid(char)) {
            ss << "'" << std::any_cast<char>(val) << "'";
        } else if (val.type() == typeid(std::string)) {
            ss << "\"" << std::any_cast<std::string>(val) << "\"";
        } else if (!val.has_value()) {
            ss << "<empty>";
        } else {
            ss << "<unknown type>";
        }
    } else {
        ss << value;
    }

    std::string msg = name + " = " + ss.str();
    ::cm::debug::log(::cm::debug::Stage::Interp, ::cm::debug::Level::Trace, msg);
}

/// 値をダンプ（Traceレベル） - 文字列版（後方互換性）
inline void dump_value(const std::string& name, const std::string& value,
                       const std::string& type = "") {
    if (!::cm::debug::g_debug_mode || ::cm::debug::Level::Trace < ::cm::debug::g_debug_level)
        return;
    std::string msg = "Value[" + name + "] = " + value;
    if (!type.empty()) {
        msg += " : " + type;
    }
    ::cm::debug::log(::cm::debug::Stage::Interp, ::cm::debug::Level::Trace, msg);
}

/// スタックフレームをダンプ（Traceレベル）
inline void dump_frame(int depth, const std::string& func_name, int local_count = -1) {
    if (!::cm::debug::g_debug_mode || ::cm::debug::Level::Trace < ::cm::debug::g_debug_level)
        return;
    std::string msg = "Frame[" + std::to_string(depth) + "]: " + func_name;
    if (local_count >= 0) {
        msg += " (locals=" + std::to_string(local_count) + ")";
    }
    ::cm::debug::log(::cm::debug::Stage::Interp, ::cm::debug::Level::Trace, msg);
}

/// MIR命令をダンプ（Traceレベル）
inline void dump_instruction(const std::string& inst, const std::string& operands = "") {
    if (!::cm::debug::g_debug_mode || ::cm::debug::Level::Trace < ::cm::debug::g_debug_level)
        return;
    std::string msg = "Inst: " + inst;
    if (!operands.empty()) {
        msg += " " + operands;
    }
    ::cm::debug::log(::cm::debug::Stage::Interp, ::cm::debug::Level::Trace, msg);
}

/// メモリアドレスをダンプ（Traceレベル）
inline void dump_memory(void* addr, size_t size, const std::string& context = "") {
    if (!::cm::debug::g_debug_mode || ::cm::debug::Level::Trace < ::cm::debug::g_debug_level)
        return;
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%p", addr);
    std::string msg = "Memory[" + std::string(buffer) + "]: size=" + std::to_string(size);
    if (!context.empty()) {
        msg += " (" + context + ")";
    }
    ::cm::debug::log(::cm::debug::Stage::Interp, ::cm::debug::Level::Trace, msg);
}

}  // namespace cm::debug::interp