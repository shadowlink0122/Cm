#pragma once

#include "../debug.hpp"

#include <string>

namespace cm::debug::lex {

/// Lexer メッセージID
enum class Id {
    // 基本フロー
    Start,
    End,
    FileOpen,
    FileClose,

    // トークン検出
    TokenFound,
    Keyword,
    Ident,
    Number,
    String,
    Char,
    Operator,
    Delimiter,
    Symbol,

    // リテラル処理
    IntLiteral,
    FloatLiteral,
    BoolLiteral,
    StringEscape,
    CharEscape,
    MultilineString,

    // スキップ処理
    CommentSkip,
    WhitespaceSkip,
    LineComment,
    BlockComment,
    DocComment,

    // 位置情報
    Position,
    NewLine,

    // エラー処理
    Error,
    Warning,
    UnterminatedString,
    InvalidChar,
    InvalidEscape,
    UnexpectedEOF,

    // 拡張デバッグメッセージ
    SourceLength,         // ソースコードの長さ
    ScanStart,            // スキャン開始
    CharScan,             // 文字スキャン
    TokenText,            // トークンテキスト
    KeywordMatch,         // キーワードマッチ詳細
    IdentCreate,          // 識別子作成
    HexNumber,            // 16進数検出
    BinaryNumber,         // 2進数検出
    FloatDetected,        // 浮動小数点検出
    ExponentDetected,     // 指数検出
    NewlineSkip,          // 改行スキップ
    LineCommentContent,   // 行コメント内容
    BlockCommentContent,  // ブロックコメント内容
    SkipEnd               // スキップ終了
};

/// メッセージテーブル [en, ja]
inline const char* messages[][2] = {
    // 基本フロー
    {"Starting lexical analysis", "字句解析を開始"},
    {"Completed lexical analysis", "字句解析を完了"},
    {"Opening file", "ファイルを開く"},
    {"Closing file", "ファイルを閉じる"},

    // トークン検出
    {"Token found", "トークンを検出"},
    {"Keyword detected", "キーワードを検出"},
    {"Identifier detected", "識別子を検出"},
    {"Number detected", "数値を検出"},
    {"String literal detected", "文字列リテラルを検出"},
    {"Character literal detected", "文字リテラルを検出"},
    {"Operator detected", "演算子を検出"},
    {"Delimiter detected", "デリミタを検出"},
    {"Symbol detected", "シンボルを検出"},

    // リテラル処理
    {"Integer literal", "整数リテラル"},
    {"Float literal", "浮動小数点リテラル"},
    {"Boolean literal", "真偽値リテラル"},
    {"Processing string escape", "文字列エスケープを処理"},
    {"Processing char escape", "文字エスケープを処理"},
    {"Multiline string detected", "複数行文字列を検出"},

    // スキップ処理
    {"Skipping comment", "コメントをスキップ"},
    {"Skipping whitespace", "空白をスキップ"},
    {"Line comment detected", "行コメントを検出"},
    {"Block comment detected", "ブロックコメントを検出"},
    {"Doc comment detected", "ドキュメントコメントを検出"},

    // 位置情報
    {"Current position", "現在位置"},
    {"New line", "改行"},

    // エラー処理
    {"Lexer error", "字句解析エラー"},
    {"Lexer warning", "字句解析警告"},
    {"Unterminated string", "文字列が閉じられていません"},
    {"Invalid character", "無効な文字"},
    {"Invalid escape sequence", "無効なエスケープシーケンス"},
    {"Unexpected EOF", "予期しないEOF"},

    // 拡張デバッグメッセージ
    {"Source length", "ソースコードの長さ"},
    {"Scan start", "スキャン開始"},
    {"Character scan", "文字スキャン"},
    {"Token text", "トークンテキスト"},
    {"Keyword match", "キーワードマッチ詳細"},
    {"Identifier create", "識別子作成"},
    {"Hex number", "16進数検出"},
    {"Binary number", "2進数検出"},
    {"Float detected", "浮動小数点検出"},
    {"Exponent detected", "指数検出"},
    {"Newline skip", "改行スキップ"},
    {"Line comment content", "行コメント内容"},
    {"Block comment content", "ブロックコメント内容"},
    {"Skip end", "スキップ終了"}};

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

/// トークン情報をダンプ（Traceレベル）
inline void dump_token(const std::string& type, const std::string& value, int line = -1,
                       int col = -1) {
    if (!::cm::debug::g_debug_mode || ::cm::debug::Level::Trace < ::cm::debug::g_debug_level)
        return;
    std::string msg = "Token[" + type + "] = \"" + value + "\"";
    if (line >= 0 && col >= 0) {
        msg += " @ " + std::to_string(line) + ":" + std::to_string(col);
    }
    ::cm::debug::log(::cm::debug::Stage::Lexer, ::cm::debug::Level::Trace, msg);
}

/// 位置情報をダンプ（Traceレベル）
inline void dump_position(int line, int col, const std::string& context = "") {
    if (!::cm::debug::g_debug_mode || ::cm::debug::Level::Trace < ::cm::debug::g_debug_level)
        return;
    std::string msg = "Position: " + std::to_string(line) + ":" + std::to_string(col);
    if (!context.empty()) {
        msg += " (" + context + ")";
    }
    ::cm::debug::log(::cm::debug::Stage::Lexer, ::cm::debug::Level::Trace, msg);
}

}  // namespace cm::debug::lex
