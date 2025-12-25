#pragma once

#include "../../common/debug/lex.hpp"
#include "token.hpp"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cm {

class Lexer {
   public:
    Lexer(std::string_view source) : source_(source), pos_(0) { init_keywords(); }

    std::vector<Token> tokenize() {
        debug::lex::log(debug::lex::Id::Start);
        debug::lex::log(debug::lex::Id::SourceLength, std::to_string(source_.size()),
                        debug::Level::Debug);

        std::vector<Token> tokens;
        while (!is_at_end()) {
            // 現在の位置をログ
            debug::lex::dump_position(get_line_number(pos_), get_column_number(pos_),
                                      "Scanning at pos " + std::to_string(pos_));

            Token tok = next_token();

            // 生成されたトークンをログ
            std::string tok_value = "";
            if (tok.kind == TokenKind::Ident) {
                tok_value = std::string(tok.get_string());
            }
            debug::lex::dump_token(token_kind_to_string(tok.kind), tok_value,
                                   get_line_number(tok.start), get_column_number(tok.start));

            tokens.push_back(tok);
            if (tok.kind == TokenKind::Eof)
                break;
        }

        if (tokens.empty() || tokens.back().kind != TokenKind::Eof) {
            tokens.push_back(make_token(TokenKind::Eof));
        }

        debug::lex::log(debug::lex::Id::End, std::to_string(tokens.size()) + " tokens");
        return tokens;
    }

   private:
    Token next_token() {
        skip_whitespace_and_comments();
        if (is_at_end())
            return make_token(TokenKind::Eof);

        uint32_t start = pos_;
        char c = advance();

        if (is_alpha(c))
            return scan_identifier(start);
        if (is_digit(c))
            return scan_number(start);
        if (c == '"')
            return scan_string(start);
        if (c == '`')
            return scan_raw_string(start);
        if (c == '\'')
            return scan_char(start);

        return scan_operator(start, c);
    }

    void init_keywords() {
        keywords_ = {
            {"as", TokenKind::KwAs},
            {"async", TokenKind::KwAsync},
            {"auto", TokenKind::KwAuto},
            {"await", TokenKind::KwAwait},
            {"break", TokenKind::KwBreak},
            {"case", TokenKind::KwCase},
            {"const", TokenKind::KwConst},
            {"constexpr", TokenKind::KwConstexpr},
            {"continue", TokenKind::KwContinue},
            {"default", TokenKind::KwDefault},
            {"defer", TokenKind::KwDefer},
            {"delete", TokenKind::KwDelete},
            {"else", TokenKind::KwElse},
            {"enum", TokenKind::KwEnum},
            {"export", TokenKind::KwExport},
            {"extern", TokenKind::KwExtern},
            {"false", TokenKind::KwFalse},
            {"for", TokenKind::KwFor},
            {"from", TokenKind::KwFrom},
            {"if", TokenKind::KwIf},
            {"impl", TokenKind::KwImpl},
            {"import", TokenKind::KwImport},
            {"in", TokenKind::KwIn},
            {"inline", TokenKind::KwInline},
            {"interface", TokenKind::KwInterface},
            {"macro", TokenKind::KwMacro},
            {"match", TokenKind::KwMatch},
            {"module", TokenKind::KwModule},
            {"mutable", TokenKind::KwMutable},
            {"namespace", TokenKind::KwNamespace},
            {"new", TokenKind::KwNew},
            {"null", TokenKind::KwNull},
            {"operator", TokenKind::KwOperator},
            {"overload", TokenKind::KwOverload},
            {"private", TokenKind::KwPrivate},
            {"pub", TokenKind::KwPub},
            {"return", TokenKind::KwReturn},
            {"sizeof", TokenKind::KwSizeof},
            {"static", TokenKind::KwStatic},
            {"struct", TokenKind::KwStruct},
            {"switch", TokenKind::KwSwitch},
            {"template", TokenKind::KwTemplate},
            {"this", TokenKind::KwThis},
            {"true", TokenKind::KwTrue},
            {"typedef", TokenKind::KwTypedef},
            {"typename", TokenKind::KwTypename},
            {"typeof", TokenKind::KwTypeof},
            {"use", TokenKind::KwUse},
            {"void", TokenKind::KwVoid},
            {"volatile", TokenKind::KwVolatile},
            {"where", TokenKind::KwWhere},
            {"while", TokenKind::KwWhile},
            {"with", TokenKind::KwWith},
            // コンパイラ組み込み関数
            {"__sizeof__", TokenKind::KwIntrinsicSizeof},
            {"__typeof__", TokenKind::KwIntrinsicTypeof},
            {"__typename__", TokenKind::KwIntrinsicTypename},
            {"__alignof__", TokenKind::KwIntrinsicAlignof},
            {"int", TokenKind::KwInt},
            {"uint", TokenKind::KwUint},
            {"tiny", TokenKind::KwTiny},
            {"utiny", TokenKind::KwUtiny},
            {"short", TokenKind::KwShort},
            {"ushort", TokenKind::KwUshort},
            {"long", TokenKind::KwLong},
            {"ulong", TokenKind::KwUlong},
            {"isize", TokenKind::KwIsize},
            {"usize", TokenKind::KwUsize},
            {"float", TokenKind::KwFloat},
            {"double", TokenKind::KwDouble},
            {"ufloat", TokenKind::KwUfloat},
            {"udouble", TokenKind::KwUdouble},
            {"bool", TokenKind::KwBool},
            {"char", TokenKind::KwChar},
            {"string", TokenKind::KwString},
            {"cstring", TokenKind::KwCstring},
        };
    }

    void skip_whitespace_and_comments() {
        while (!is_at_end()) {
            char c = peek();
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                advance();
            } else if (c == '/' && peek_next() == '/') {
                debug::lex::log(debug::lex::Id::CommentSkip, "line", debug::Level::Trace);
                while (!is_at_end() && peek() != '\n')
                    advance();
            } else if (c == '/' && peek_next() == '*') {
                debug::lex::log(debug::lex::Id::CommentSkip, "block", debug::Level::Trace);
                advance();
                advance();
                while (!is_at_end()) {
                    if (peek() == '*' && peek_next() == '/') {
                        advance();
                        advance();
                        break;
                    }
                    advance();
                }
            } else {
                break;
            }
        }
    }

    Token scan_identifier(uint32_t start) {
        debug::lex::log(debug::lex::Id::ScanStart, "identifier", debug::Level::Trace);

        while (!is_at_end() && is_alnum(peek())) {
            char c = peek();
            advance();
            debug::lex::log(debug::lex::Id::CharScan, std::string(1, c), debug::Level::Trace);
        }

        std::string text(source_.substr(start, pos_ - start));
        debug::lex::log(debug::lex::Id::TokenText, text, debug::Level::Trace);

        auto it = keywords_.find(text);
        if (it != keywords_.end()) {
            debug::lex::log(debug::lex::Id::Keyword, text, debug::Level::Debug);
            debug::lex::log(debug::lex::Id::KeywordMatch,
                            text + " -> " + token_kind_to_string(it->second), debug::Level::Trace);
            return Token(it->second, start, pos_);
        }

        debug::lex::log(debug::lex::Id::Ident, text, debug::Level::Debug);
        debug::lex::log(debug::lex::Id::IdentCreate, "Variable/Function name: " + text,
                        debug::Level::Trace);
        return Token(TokenKind::Ident, start, pos_, std::move(text));
    }

    Token scan_number(uint32_t start) {
        debug::lex::log(debug::lex::Id::ScanStart, "number", debug::Level::Trace);
        bool is_float = false;

        // 16進数チェック
        if (source_[start] == '0' && (peek() == 'x' || peek() == 'X')) {
            debug::lex::log(debug::lex::Id::HexNumber, "detected hex prefix", debug::Level::Trace);
            advance();
            while (!is_at_end() && is_hex_digit(peek())) {
                debug::lex::log(debug::lex::Id::CharScan, std::string(1, peek()),
                                debug::Level::Trace);
                advance();
            }
            std::string text(source_.substr(start, pos_ - start));
            int64_t val = std::stoll(text, nullptr, 16);
            debug::lex::log(debug::lex::Id::Number, text + " = " + std::to_string(val),
                            debug::Level::Debug);
            return Token(TokenKind::IntLiteral, start, pos_, val);
        }

        // 2進数チェック
        if (source_[start] == '0' && (peek() == 'b' || peek() == 'B')) {
            debug::lex::log(debug::lex::Id::BinaryNumber, "detected binary prefix",
                            debug::Level::Trace);
            advance();
            while (!is_at_end() && (peek() == '0' || peek() == '1')) {
                debug::lex::log(debug::lex::Id::CharScan, std::string(1, peek()),
                                debug::Level::Trace);
                advance();
            }
            std::string text(source_.substr(start + 2, pos_ - start - 2));
            int64_t val = std::stoll(text, nullptr, 2);
            debug::lex::log(debug::lex::Id::Number, "0b" + text + " = " + std::to_string(val),
                            debug::Level::Debug);
            return Token(TokenKind::IntLiteral, start, pos_, val);
        }

        // 10進数の整数部分
        while (!is_at_end() && is_digit(peek())) {
            debug::lex::log(debug::lex::Id::CharScan, std::string(1, peek()), debug::Level::Trace);
            advance();
        }

        // 小数点チェック
        if (!is_at_end() && peek() == '.' && is_digit(peek_next())) {
            is_float = true;
            debug::lex::log(debug::lex::Id::FloatDetected, "decimal point found",
                            debug::Level::Trace);
            advance();
            while (!is_at_end() && is_digit(peek())) {
                debug::lex::log(debug::lex::Id::CharScan, std::string(1, peek()),
                                debug::Level::Trace);
                advance();
            }
        }

        // 指数部チェック
        if (!is_at_end() && (peek() == 'e' || peek() == 'E')) {
            is_float = true;
            debug::lex::log(debug::lex::Id::ExponentDetected, "exponent notation found",
                            debug::Level::Trace);
            advance();
            if (peek() == '+' || peek() == '-') {
                debug::lex::log(debug::lex::Id::CharScan, std::string(1, peek()),
                                debug::Level::Trace);
                advance();
            }
            while (!is_at_end() && is_digit(peek())) {
                debug::lex::log(debug::lex::Id::CharScan, std::string(1, peek()),
                                debug::Level::Trace);
                advance();
            }
        }

        std::string text(source_.substr(start, pos_ - start));

        if (is_float) {
            double val = std::stod(text);
            debug::lex::log(debug::lex::Id::Number, text + " (float) = " + std::to_string(val),
                            debug::Level::Debug);
            return Token(TokenKind::FloatLiteral, start, pos_, val);
        } else {
            int64_t val = std::stoll(text);
            debug::lex::log(debug::lex::Id::Number, text + " (int) = " + std::to_string(val),
                            debug::Level::Debug);
            return Token(TokenKind::IntLiteral, start, pos_, val);
        }
    }

    Token scan_string(uint32_t start) {
        std::string value;
        while (!is_at_end() && peek() != '"') {
            if (peek() == '\n') {
                debug::lex::log(debug::lex::Id::Error, "unterminated string", debug::Level::Error);
                break;
            }
            if (peek() == '\\') {
                if (peek_next() == '{') {
                    advance();
                    advance();
                    value += "{{";
                    continue;
                }
                if (peek_next() == '}') {
                    advance();
                    advance();
                    value += "}}";
                    continue;
                }
                advance();
                if (!is_at_end())
                    value += scan_escape_char();
            } else {
                value += advance();
            }
        }
        if (!is_at_end())
            advance();
        debug::lex::log(debug::lex::Id::String, "\"...\"", debug::Level::Trace);
        return Token(TokenKind::StringLiteral, start, pos_, std::move(value));
    }

    Token scan_raw_string(uint32_t start) {
        std::string value;
        while (!is_at_end() && peek() != '`') {
            if (peek() == '$' && peek_next() == '{') {
                // raw文字列でも ${...} は補間用として保持する
                advance();
                advance();
                value += "${";
                while (!is_at_end() && peek() != '}') {
                    if (peek() == '\\') {
                        advance();
                        if (!is_at_end())
                            value += scan_escape_char();
                    } else {
                        value += advance();
                    }
                }
                if (!is_at_end()) {
                    advance();
                    value += "}";
                }
                continue;
            }
            if (peek() == '\\') {
                if (peek_next() == '{') {
                    advance();
                    advance();
                    value += "{{";
                    continue;
                }
                if (peek_next() == '}') {
                    advance();
                    advance();
                    value += "}}";
                    continue;
                }
                advance();
                if (!is_at_end())
                    value += scan_escape_char();
            } else if (peek() == '{') {
                advance();
                value += "{{";
            } else if (peek() == '}') {
                advance();
                value += "}}";
            } else {
                value += advance();
            }
        }
        if (!is_at_end())
            advance();
        value = normalize_raw_indent(std::move(value));
        debug::lex::log(debug::lex::Id::String, "`...`", debug::Level::Trace);
        return Token(TokenKind::StringLiteral, start, pos_, std::move(value));
    }

    Token scan_char(uint32_t start) {
        char value = 0;
        if (!is_at_end()) {
            value = (peek() == '\\') ? (advance(), scan_escape_char()) : advance();
        }
        if (!is_at_end() && peek() == '\'')
            advance();
        else
            debug::lex::log(debug::lex::Id::Error, "unterminated char", debug::Level::Error);
        return Token(TokenKind::CharLiteral, start, pos_, std::string(1, value));
    }

    char scan_escape_char() {
        char c = advance();
        switch (c) {
            case 'n':
                return '\n';
            case 't':
                return '\t';
            case 'r':
                return '\r';
            case '\\':
                return '\\';
            case '"':
                return '"';
            case '\'':
                return '\'';
            case '0':
                return '\0';
            default:
                return c;
        }
    }

    Token scan_operator(uint32_t start, char c) {
        auto make = [&](TokenKind kind) {
            debug::lex::log(debug::lex::Id::Operator, token_kind_to_string(kind),
                            debug::Level::Trace);
            return Token(kind, start, pos_);
        };

        switch (c) {
            case '(':
                return make(TokenKind::LParen);
            case ')':
                return make(TokenKind::RParen);
            case '{':
                return make(TokenKind::LBrace);
            case '}':
                return make(TokenKind::RBrace);
            case '[':
                return make(TokenKind::LBracket);
            case ']':
                return make(TokenKind::RBracket);
            case ',':
                return make(TokenKind::Comma);
            case ';':
                return make(TokenKind::Semicolon);
            case '.':
                // Check for ellipsis ...
                if (peek() == '.' && peek_next() == '.') {
                    advance();  // consume second .
                    advance();  // consume third .
                    return make(TokenKind::Ellipsis);
                }
                return make(TokenKind::Dot);
            case '@':
                return make(TokenKind::At);
            case '#':
                return make(TokenKind::Hash);
            case '~':
                return make(TokenKind::Tilde);
            case '?':
                return make(TokenKind::Question);
            case '+':
                return match('+')   ? make(TokenKind::PlusPlus)
                       : match('=') ? make(TokenKind::PlusEq)
                                    : make(TokenKind::Plus);
            case '-':
                return match('>')   ? make(TokenKind::ThinArrow)
                       : match('-') ? make(TokenKind::MinusMinus)
                       : match('=') ? make(TokenKind::MinusEq)
                                    : make(TokenKind::Minus);
            case '*':
                return match('=') ? make(TokenKind::StarEq) : make(TokenKind::Star);
            case '/':
                return match('=') ? make(TokenKind::SlashEq) : make(TokenKind::Slash);
            case '%':
                return match('=') ? make(TokenKind::PercentEq) : make(TokenKind::Percent);
            case '&':
                return match('&')   ? make(TokenKind::AmpAmp)
                       : match('=') ? make(TokenKind::AmpEq)
                                    : make(TokenKind::Amp);
            case '|':
                return match('|')   ? make(TokenKind::PipePipe)
                       : match('=') ? make(TokenKind::PipeEq)
                                    : make(TokenKind::Pipe);
            case '^':
                return match('=') ? make(TokenKind::CaretEq) : make(TokenKind::Caret);
            case '=':
                return match('=')   ? make(TokenKind::EqEq)
                       : match('>') ? make(TokenKind::Arrow)
                                    : make(TokenKind::Eq);
            case '!':
                return match('=') ? make(TokenKind::BangEq) : make(TokenKind::Bang);
            case '<':
                return match('<')   ? (match('=') ? make(TokenKind::LtLtEq) : make(TokenKind::LtLt))
                       : match('=') ? make(TokenKind::LtEq)
                                    : make(TokenKind::Lt);
            case '>':
                return match('>')   ? (match('=') ? make(TokenKind::GtGtEq) : make(TokenKind::GtGt))
                       : match('=') ? make(TokenKind::GtEq)
                                    : make(TokenKind::Gt);
            case ':':
                return match(':') ? make(TokenKind::ColonColon) : make(TokenKind::Colon);
            default:
                debug::lex::log(debug::lex::Id::Error, std::string("unexpected '") + c + "'",
                                debug::Level::Error);
                return Token(TokenKind::Error, start, pos_);
        }
    }

    bool is_at_end() const { return pos_ >= source_.size(); }
    char peek() const { return is_at_end() ? '\0' : source_[pos_]; }

    int get_line_number(uint32_t position) const {
        int line = 1;
        for (uint32_t i = 0; i < position && i < source_.size(); ++i) {
            if (source_[i] == '\n')
                line++;
        }
        return line;
    }

    int get_column_number(uint32_t position) const {
        int col = 1;
        for (uint32_t i = position; i > 0; --i) {
            if (source_[i - 1] == '\n')
                break;
            col++;
        }
        return col;
    }
    char peek_next() const { return pos_ + 1 >= source_.size() ? '\0' : source_[pos_ + 1]; }
    char advance() { return source_[pos_++]; }
    bool match(char expected) {
        if (is_at_end() || source_[pos_] != expected)
            return false;
        ++pos_;
        return true;
    }
    Token make_token(TokenKind kind) const { return Token(kind, pos_, pos_); }

    static bool is_alpha(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    }
    static bool is_digit(char c) { return c >= '0' && c <= '9'; }
    static bool is_hex_digit(char c) {
        return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    }
    static bool is_alnum(char c) { return is_alpha(c) || is_digit(c); }

    std::string normalize_raw_indent(std::string value) {
        size_t first_newline = value.find('\n');
        if (first_newline == std::string::npos) {
            return value;
        }

        size_t min_indent = std::string::npos;
        size_t pos = first_newline + 1;
        while (pos < value.size()) {
            size_t line_end = value.find('\n', pos);
            size_t line_start = pos;
            size_t idx = line_start;
            while (idx < value.size() && (value[idx] == ' ' || value[idx] == '\t')) {
                idx++;
            }
            if (idx < value.size() && value[idx] != '\n' && value[idx] != '\r') {
                size_t indent = idx - line_start;
                if (min_indent == std::string::npos || indent < min_indent) {
                    min_indent = indent;
                }
            }
            if (line_end == std::string::npos) {
                break;
            }
            pos = line_end + 1;
        }

        if (min_indent == std::string::npos || min_indent == 0) {
            return value;
        }

        std::string result;
        result.reserve(value.size());
        result.append(value.substr(0, first_newline + 1));

        pos = first_newline + 1;
        while (pos < value.size()) {
            size_t line_end = value.find('\n', pos);
            size_t line_start = pos;
            size_t idx = line_start;
            size_t drop = min_indent;
            while (drop > 0 && idx < value.size() && (value[idx] == ' ' || value[idx] == '\t')) {
                idx++;
                drop--;
            }
            size_t slice_end = (line_end == std::string::npos) ? value.size() : line_end;
            result.append(value.substr(idx, slice_end - idx));
            if (line_end == std::string::npos) {
                break;
            }
            result.push_back('\n');
            pos = line_end + 1;
        }

        return result;
    }

    std::string_view source_;
    uint32_t pos_;
    std::unordered_map<std::string, TokenKind> keywords_;
};

}  // namespace cm
