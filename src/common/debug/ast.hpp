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
    return messages[static_cast<int>(id)][g_lang];
}

inline void log(Id id, Level level = Level::Debug) {
    if (!g_debug_mode || level < g_debug_level)
        return;
    debug::log(Stage::Ast, level, get(id));
}

inline void log(Id id, const std::string& detail, Level level = Level::Debug) {
    if (!g_debug_mode || level < g_debug_level)
        return;
    debug::log(Stage::Ast, level, std::string(get(id)) + ": " + detail);
}

}  // namespace cm::debug::ast
