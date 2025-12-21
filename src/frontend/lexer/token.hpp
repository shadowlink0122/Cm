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
    KwAs,  // import alias
    KwAsync,
    KwAuto,  // auto type inference
    KwAwait,
    KwBreak,
    KwCase,  // switch case
    KwConst,
    KwContinue,
    KwDefault,  // switch default
    KwDefer,    // defer statement
    KwDelete,
    KwElse,
    KwEnum,
    KwExport,
    KwExtern,
    KwFalse,
    KwFor,
    KwFrom,  // re-export from
    KwIf,
    KwImpl,
    KwImport,
    KwIn,  // for-in loop
    KwInline,
    KwInterface,
    KwMacro,      // macro definition
    KwConstexpr,  // constexpr keyword
    KwMatch,
    KwModule,  // module declaration
    KwMutable,
    KwNamespace,  // namespace declaration
    KwNew,
    KwNull,
    KwOperator,  // operator overloading in interface/impl
    KwOverload,  // function overloading
    KwPrivate,
    KwPub,  // public visibility
    KwReturn,
    KwSizeof,  // sizeof operator
    KwStatic,
    KwStruct,
    KwSwitch,    // switch statement
    KwTemplate,  // template declaration
    KwThis,
    KwTrue,
    KwTypedef,   // type alias
    KwTypename,  // template typename
    KwTypeof,    // typeof operator
    KwUse,       // use statement (similar to import)
    KwVoid,
    KwVolatile,
    KwWhere,  // where clause for type constraints
    KwWhile,
    KwWith,
    
    // コンパイラ組み込み関数（真のインライン）
    KwIntrinsicSizeof,    // __sizeof__
    KwIntrinsicTypeof,    // __typeof__
    KwIntrinsicTypename,  // __typename__
    KwIntrinsicAlignof,   // __alignof__

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
    KwUfloat,
    KwUdouble,
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
    Arrow,      // =>
    ThinArrow,  // -> for pointer member access
    At,         // @ for attributes
    Ellipsis,   // ... for variadic
    Hash,       // # for preprocessor directives

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
