#pragma once

#include <iostream>
#include <string>

namespace cm::debug {

/// デバッグレベル
enum class Level { Trace, Debug, Info, Warn, Error };

// グローバル可変状態はアクセサ関数の関数ローカルstaticへ集約する（013 §4.3-5）。
// 参照を返すため読み取りは debug_mode()、書き込みは debug_mode() = true の形で行う

/// デバッグモードフラグ
inline bool& debug_mode() {
    static bool v = false;
    return v;
}

/// 言語設定 (0=English, 1=Japanese, ...)
inline int& lang() {
    static int v = 0;
    return v;
}

/// 現在のデバッグレベル
inline Level& debug_level() {
    static Level v = Level::Debug;
    return v;
}

/// コンパイラの処理段階
enum class Stage {
    Lexer,
    Parser,
    Ast,
    TypeCheck,
    Hir,
    Mir,
    Mono,  // Monomorphization
    Lir,
    Interp,
    CodegenRust,
    CodegenTs,
    CodegenCpp
};

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
        case Stage::Mono:
            return "MONO";
        case Stage::Lir:
            return "LIR";
        case Stage::Interp:
            return "INTERP";
        case Stage::CodegenRust:
            return "CODEGEN_RUST";
        case Stage::CodegenTs:
            return "CODEGEN_TS";
        case Stage::CodegenCpp:
            return "CODEGEN_CPP";
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
    if (!debug_mode() || level < debug_level())
        return;
    // レベルに応じてプレフィックスを付加（ただし[]は1つだけ）
    const char* prefix = "";
    switch (level) {
        case Level::Error:
            prefix = "ERROR: ";
            break;
        case Level::Warn:
            prefix = "WARN: ";
            break;
        default:
            break;
    }
    std::cerr << "[" << stage_str(stage) << "] " << prefix << msg << std::endl;
}

inline void log(Stage stage, Level level, const std::string& msg) {
    log(stage, level, msg.c_str());
}

/// 設定関数
inline void set_debug_mode(bool enabled) {
    debug_mode() = enabled;
}
inline void set_lang(int new_lang) {
    lang() = new_lang;
}
inline void set_level(Level level) {
    debug_level() = level;
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
    return (lang() < count) ? texts[lang()] : texts[0];
}

}  // namespace cm::debug

// 簡易デバッグ出力マクロ
inline void debug_msg(const std::string& stage, const std::string& msg) {
    if (cm::debug::debug_mode()) {
        std::cerr << "[" << stage << "] " << msg << std::endl;
    }
}
