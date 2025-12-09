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

}  // namespace cm::debug::interp
