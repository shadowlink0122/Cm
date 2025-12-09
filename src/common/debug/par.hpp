#pragma once

#include "../debug.hpp"

#include <string>

namespace cm::debug::par {

/// Parser メッセージID
enum class Id {
    Start,
    End,
    FuncDef,
    StructDef,
    EnumDef,
    InterfaceDef,
    ImplDef,
    Expr,
    Stmt,
    Block,
    Error,
    Recover
};

/// メッセージテーブル [en, ja]
inline const char* messages[][2] = {
    // Start
    {"Starting parsing", "構文解析を開始"},
    // End
    {"Completed parsing", "構文解析を完了"},
    // FuncDef
    {"Parsing function definition", "関数定義を解析"},
    // StructDef
    {"Parsing struct definition", "構造体定義を解析"},
    // EnumDef
    {"Parsing enum definition", "列挙型定義を解析"},
    // InterfaceDef
    {"Parsing interface definition", "インターフェース定義を解析"},
    // ImplDef
    {"Parsing impl definition", "impl定義を解析"},
    // Expr
    {"Parsing expression", "式を解析"},
    // Stmt
    {"Parsing statement", "文を解析"},
    // Block
    {"Parsing block", "ブロックを解析"},
    // Error
    {"Parse error", "構文解析エラー"},
    // Recover
    {"Recovering from error", "エラーから回復"},
};

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

}  // namespace cm::debug::par
