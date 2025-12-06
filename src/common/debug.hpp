#pragma once

#include <iostream>
#include <string>

namespace cm::debug {

/// デバッグモードフラグ
inline bool g_debug_mode = false;

/// 言語設定 (0=English, 1=Japanese, ...)
inline int g_lang = 0;

/// デバッグレベル
enum class Level { Trace, Debug, Info, Warn, Error };

/// 現在のデバッグレベル
inline Level g_debug_level = Level::Debug;

/// コンパイラの処理段階
enum class Stage { Lexer, Parser, Ast, TypeCheck, Hir, Mir, Lir, Interp, CodegenRust, CodegenTs };

/// 段階を文字列に変換
inline const char* stage_str(Stage s) {
    switch (s) {
        case Stage::Lexer:
            return "LEXER";
        case Stage::Parser:
            return "PARSER";
        case Stage::Ast:
            return "AST";
        case Stage::TypeCheck:
            return "TYPECHECK";
        case Stage::Hir:
            return "HIR";
        case Stage::Mir:
            return "MIR";
        case Stage::Lir:
            return "LIR";
        case Stage::Interp:
            return "INTERP";
        case Stage::CodegenRust:
            return "CODEGEN_RUST";
        case Stage::CodegenTs:
            return "CODEGEN_TS";
    }
    return "UNKNOWN";
}

/// レベルを文字列に変換
inline const char* level_str(Level l) {
    switch (l) {
        case Level::Trace:
            return "TRACE";
        case Level::Debug:
            return "DEBUG";
        case Level::Info:
            return "INFO";
        case Level::Warn:
            return "WARN";
        case Level::Error:
            return "ERROR";
    }
    return "UNKNOWN";
}

/// デバッグ出力
inline void log(Stage stage, Level level, const char* msg) {
    if (!g_debug_mode || level < g_debug_level)
        return;
    // レベルに応じてプレフィックスを付加（ただし[]は1つだけ）
    const char* prefix = "";
    switch (level) {
        case Level::Error: prefix = "ERROR: "; break;
        case Level::Warn: prefix = "WARN: "; break;
        default: break;
    }
    std::cerr << "[" << stage_str(stage) << "] " << prefix << msg << std::endl;
}

inline void log(Stage stage, Level level, const std::string& msg) {
    log(stage, level, msg.c_str());
}

/// 設定関数
inline void set_debug_mode(bool enabled) {
    g_debug_mode = enabled;
}
inline void set_lang(int lang) {
    g_lang = lang;
}
inline void set_level(Level level) {
    g_debug_level = level;
}

/// レベル解析
inline Level parse_level(const std::string& s) {
    if (s == "trace")
        return Level::Trace;
    if (s == "debug")
        return Level::Debug;
    if (s == "info")
        return Level::Info;
    if (s == "warn")
        return Level::Warn;
    if (s == "error")
        return Level::Error;
    return Level::Debug;
}

/// 多言語メッセージ取得
inline const char* msg(const char* texts[], int count = 2) {
    return (g_lang < count) ? texts[g_lang] : texts[0];
}

}  // namespace cm::debug
