#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace cm {

/// トークンの種類
enum class TokenKind {
    // リテラル
    IntLiteral,        // 123
    MaskedBinLiteral,  // 0b1?00（don't care付き2進。matchパターン専用）
    FloatLiteral,      // 1.23
    StringLiteral,     // "hello"
    CharLiteral,       // 'a'

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
    KwIs,         // ユニオン型の実行時型判別 (expr is Type)
    KwMacro,      // macro definition
    KwConstexpr,  // constexpr keyword
    KwMatch,
    KwModule,  // module declaration
    KwMove,    // move ownership
    KwMust,    // must execute (no optimization) for inline asm
    KwMutable,
    KwNamespace,  // namespace declaration
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
    KwSelf,
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
    KwIsize,  // ポインタサイズ符号付き (FFI用)
    KwUsize,  // ポインタサイズ符号なし (FFI用)
    KwFloat,
    KwDouble,
    KwUfloat,
    KwUdouble,
    KwBool,
    KwChar,
    KwString,
    KwCstring,  // NULL終端文字列 (FFI用)

    // SV固有キーワード（SystemVerilogターゲットのみ）
    KwPosedge,      // posedge信号型
    KwNegedge,      // negedge信号型
    KwWire,         // wire修飾型
    KwReg,          // reg修飾型
    KwAlways,       // always ロジックブロック修飾子（自動判別）
    KwAlwaysFF,     // always_ff 順序回路（明示指定）
    KwAlwaysComb,   // always_comb 組み合わせ回路（明示指定）
    KwAlwaysLatch,  // always_latch ラッチ（明示指定）
    KwAssign,       // assign 連続代入
    KwInitial,      // initial シミュレーション初期化
    KwBit,          // bit<N> 任意ビット幅型

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
    PlusColon,  // +: （ビットスライスのインデックスドパートセレクト）
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

/// SV幅付きリテラル情報（幅付きリテラルにのみ存在）
struct BitLiteralInfo {
    int width;             // ビット幅 (例: 8)
    char base;             // ベース文字 ('d','b','h')
    std::string original;  // 元のリテラル文字列（2進/16進表記保持用）

    BitLiteralInfo(int w, char b, std::string orig)
        : width(w), base(b), original(std::move(orig)) {}
};

/// トークン
struct Token {
    TokenKind kind;
    uint32_t start;  // 開始位置
    uint32_t end;    // 終了位置
    TokenValue value;
    bool is_unsigned = false;                // hex/binary/octalリテラルで32bit超の場合true
    std::optional<BitLiteralInfo> bit_info;  // SV幅付きリテラル情報（nullopt = 通常トークン）

    Token(TokenKind k, uint32_t s, uint32_t e)
        : kind(k), start(s), end(e), value(std::monostate{}) {}

    Token(TokenKind k, uint32_t s, uint32_t e, int64_t v) : kind(k), start(s), end(e), value(v) {}

    Token(TokenKind k, uint32_t s, uint32_t e, int64_t v, bool unsigned_flag)
        : kind(k), start(s), end(e), value(v), is_unsigned(unsigned_flag) {}

    // SV幅付きリテラル用コンストラクタ
    Token(TokenKind k, uint32_t s, uint32_t e, int64_t v, bool unsigned_flag, int width, char base,
          std::string original)
        : kind(k),
          start(s),
          end(e),
          value(v),
          is_unsigned(unsigned_flag),
          bit_info(BitLiteralInfo(width, base, std::move(original))) {}

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
