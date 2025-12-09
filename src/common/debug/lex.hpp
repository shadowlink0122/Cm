#pragma once

#include "../debug.hpp"

#include <string>

namespace cm::debug::lex {

/// Lexer メッセージID
enum class Id {
    Start,
    End,
    TokenFound,
    Keyword,
    Ident,
    Number,
    String,
    Operator,
    CommentSkip,
    WhitespaceSkip,
    Error
};

/// メッセージテーブル [en, ja]
inline const char* messages[][2] = {
    // Start
    {"Starting lexical analysis", "字句解析を開始"},
    // End
    {"Completed lexical analysis", "字句解析を完了"},
    // TokenFound
    {"Token found", "トークンを検出"},
    // Keyword
    {"Keyword detected", "キーワードを検出"},
    // Ident
    {"Identifier detected", "識別子を検出"},
    // Number
    {"Number literal detected", "数値リテラルを検出"},
    // String
    {"String literal detected", "文字列リテラルを検出"},
    // Operator
    {"Operator detected", "演算子を検出"},
    // CommentSkip
    {"Skipping comment", "コメントをスキップ"},
    // WhitespaceSkip
    {"Skipping whitespace", "空白をスキップ"},
    // Error
    {"Lexer error", "字句解析エラー"},
};

/// メッセージ取得
inline const char* get(Id id) {
    int idx = static_cast<int>(id);
    return messages[idx][::cm::debug::g_lang];
}

/// ログ出力
inline void log(Id id, ::cm::debug::Level level = ::cm::debug::Level::Debug) {
    if (!::cm::debug::g_debug_mode || level < ::cm::debug::g_debug_level)
        return;
    ::cm::debug::log(::cm::debug::Stage::Lexer, level, get(id));
}

inline void log(Id id, const std::string& detail,
                ::cm::debug::Level level = ::cm::debug::Level::Debug) {
    if (!::cm::debug::g_debug_mode || level < ::cm::debug::g_debug_level)
        return;
    ::cm::debug::log(::cm::debug::Stage::Lexer, level, std::string(get(id)) + ": " + detail);
}

}  // namespace cm::debug::lex
