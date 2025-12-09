#pragma once

#include "../debug.hpp"

#include <string>

namespace cm::debug::hir {

/// HIR メッセージID
enum class Id { LowerStart, LowerEnd, NodeCreate, Optimize };

/// メッセージテーブル [en, ja]
inline const char* messages[][2] = {
    // LowerStart
    {"Starting HIR lowering", "HIR変換を開始"},
    // LowerEnd
    {"Completed HIR lowering", "HIR変換を完了"},
    // NodeCreate
    {"Creating HIR node", "HIRノードを作成"},
    // Optimize
    {"Optimizing HIR", "HIRを最適化"},
};

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

}  // namespace cm::debug::hir
