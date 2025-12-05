#pragma once

#include "../debug.hpp"

#include <string>

namespace cm::debug::interp {

/// Interpreter メッセージID
enum class Id { Start, End, EvalExpr, EvalStmt, CallFunc, Return, VarDeclare, VarAssign, Error };

/// メッセージテーブル [en, ja]
inline const char* messages[][2] = {
    // Start
    {"Starting interpreter", "インタプリタを開始"},
    // End
    {"Interpreter finished", "インタプリタを終了"},
    // EvalExpr
    {"Evaluating expression", "式を評価"},
    // EvalStmt
    {"Evaluating statement", "文を評価"},
    // CallFunc
    {"Calling function", "関数を呼び出し"},
    // Return
    {"Returning from function", "関数から戻る"},
    // VarDeclare
    {"Declaring variable", "変数を宣言"},
    // VarAssign
    {"Assigning variable", "変数に代入"},
    // Error
    {"Runtime error", "ランタイムエラー"},
};

inline const char* get(Id id) {
    return messages[static_cast<int>(id)][g_lang];
}

inline void log(Id id, Level level = Level::Debug) {
    if (!g_debug_mode || level < g_debug_level)
        return;
    debug::log(Stage::Interp, level, get(id));
}

inline void log(Id id, const std::string& detail, Level level = Level::Debug) {
    if (!g_debug_mode || level < g_debug_level)
        return;
    debug::log(Stage::Interp, level, std::string(get(id)) + ": " + detail);
}

}  // namespace cm::debug::interp
