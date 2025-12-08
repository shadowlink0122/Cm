#pragma once

#include "../debug.hpp"

#include <string>

namespace cm::debug::ast {

enum class Id { NodeCreate, TreeDump, Validate };

inline const char* messages[][2] = {
    {"Creating AST node", "ASTノードを作成"},
    {"Dumping AST", "ASTを出力"},
    {"Validating AST", "ASTを検証"},
};

inline const char* get(Id id) {
    return messages[static_cast<int>(id)][::cm::debug::g_lang];
}

inline void log(Id id, ::cm::debug::Level level = ::cm::debug::Level::Debug) {
    if (!::cm::debug::g_debug_mode || level < ::cm::debug::g_debug_level)
        return;
    ::cm::debug::log(::cm::debug::Stage::Ast, level, get(id));
}

inline void log(Id id, const std::string& detail, ::cm::debug::Level level = ::cm::debug::Level::Debug) {
    if (!::cm::debug::g_debug_mode || level < ::cm::debug::g_debug_level)
        return;
    ::cm::debug::log(::cm::debug::Stage::Ast, level, std::string(get(id)) + ": " + detail);
}

}  // namespace cm::debug::ast
