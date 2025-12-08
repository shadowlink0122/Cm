#pragma once

#include "../debug.hpp"

#include <string>

namespace cm::debug::tc {

enum class Id { Start, End, CheckExpr, CheckStmt, CheckDecl, TypeInfer, TypeError, Resolved };

inline const char* messages[][2] = {
    {"Starting type check", "型チェックを開始"},
    {"Completed type check", "型チェックを完了"},
    {"Checking expression", "式を検査"},
    {"Checking statement", "文を検査"},
    {"Checking declaration", "宣言を検査"},
    {"Type inferred", "型を推論"},
    {"Type error", "型エラー"},
    {"Type resolved", "型を解決"},
};

inline const char* get(Id id) {
    return messages[static_cast<int>(id)][::cm::debug::g_lang];
}

inline void log(Id id, ::cm::debug::Level level = ::cm::debug::Level::Debug) {
    if (!::cm::debug::g_debug_mode || level < ::cm::debug::g_debug_level)
        return;
    ::cm::debug::log(::cm::debug::Stage::TypeCheck, level, get(id));
}

inline void log(Id id, const std::string& detail, ::cm::debug::Level level = ::cm::debug::Level::Debug) {
    if (!::cm::debug::g_debug_mode || level < ::cm::debug::g_debug_level)
        return;
    ::cm::debug::log(::cm::debug::Stage::TypeCheck, level, std::string(get(id)) + ": " + detail);
}

}  // namespace cm::debug::tc
