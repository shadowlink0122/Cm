#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

namespace cm {

/// トークンの種類
enum class TokenKind {
    // リテラル
    IntLiteral,     // 123
    FloatLiteral,   // 1.23
    StringLiteral,  // "hello"
    CharLiteral,    // 'a'

    // 識別子
    Ident,  // foo, bar

    // キーワード
    KwAsync,
    KwAwait,
    KwBreak,
    KwConst,
    KwContinue,
    KwDelete,
    KwElse,
    KwEnum,
    KwExport,
    KwExtern,
    KwFalse,
    KwFor,
    KwIf,
    KwImpl,
    KwImport,
    KwInline,
    KwInterface,
    KwMatch,
    KwMutable,
    KwNew,
    KwNull,
    KwPrivate,
    KwReturn,
    KwStatic,
    KwStruct,
    KwThis,
    KwTrue,
    KwVoid,
    KwVolatile,
    KwWhile,
    KwWith,

    // 型キーワード
    KwInt,
    KwUint,
    KwTiny,
    KwUtiny,
    KwShort,
    KwUshort,
    KwLong,
    KwUlong,
    KwFloat,
    KwDouble,
    KwBool,
    KwChar,
    KwString,

    // 演算子
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    Amp,
    Pipe,
    Caret,
    Tilde,
    LtLt,
    GtGt,
    AmpAmp,
    PipePipe,
    Bang,
    Eq,
    EqEq,
    BangEq,
    Lt,
    Gt,
    LtEq,
    GtEq,
    PlusEq,
    MinusEq,
    StarEq,
    SlashEq,
    PercentEq,
    AmpEq,
    PipeEq,
    CaretEq,
    LtLtEq,
    GtGtEq,
    PlusPlus,
    MinusMinus,
    Question,
    Colon,
    ColonColon,
    Arrow,

    // 区切り
    LParen,
    RParen,
    LBrace,
    RBrace,
    LBracket,
    RBracket,
    Comma,
    Semicolon,
    Dot,

    // 特殊
    Eof,
    Error,
};

/// トークンの値
using TokenValue = std::variant<std::monostate,  // 値なし
                                int64_t,         // 整数リテラル
                                double,          // 浮動小数点リテラル
                                std::string      // 文字列/識別子
                                >;

/// トークン
struct Token {
    TokenKind kind;
    uint32_t start;  // 開始位置
    uint32_t end;    // 終了位置
    TokenValue value;

    Token(TokenKind k, uint32_t s, uint32_t e)
        : kind(k), start(s), end(e), value(std::monostate{}) {}

    Token(TokenKind k, uint32_t s, uint32_t e, int64_t v) : kind(k), start(s), end(e), value(v) {}

    Token(TokenKind k, uint32_t s, uint32_t e, double v) : kind(k), start(s), end(e), value(v) {}

    Token(TokenKind k, uint32_t s, uint32_t e, std::string v)
        : kind(k), start(s), end(e), value(std::move(v)) {}

    std::string_view get_string() const {
        if (auto* s = std::get_if<std::string>(&value))
            return *s;
        return "";
    }

    int64_t get_int() const {
        if (auto* i = std::get_if<int64_t>(&value))
            return *i;
        return 0;
    }

    double get_float() const {
        if (auto* f = std::get_if<double>(&value))
            return *f;
        return 0.0;
    }
};

/// TokenKindを文字列に変換
const char* token_kind_to_string(TokenKind kind);

}  // namespace cm
