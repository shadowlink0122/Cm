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

        std::vector<Token> tokens;
        while (!is_at_end()) {
            Token tok = next_token();
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
        if (c == '\'')
            return scan_char(start);

        return scan_operator(start, c);
    }

    void init_keywords() {
        keywords_ = {
            {"async", TokenKind::KwAsync},
            {"await", TokenKind::KwAwait},
            {"break", TokenKind::KwBreak},
            {"const", TokenKind::KwConst},
            {"continue", TokenKind::KwContinue},
            {"delete", TokenKind::KwDelete},
            {"else", TokenKind::KwElse},
            {"enum", TokenKind::KwEnum},
            {"export", TokenKind::KwExport},
            {"extern", TokenKind::KwExtern},
            {"false", TokenKind::KwFalse},
            {"for", TokenKind::KwFor},
            {"if", TokenKind::KwIf},
            {"impl", TokenKind::KwImpl},
            {"import", TokenKind::KwImport},
            {"inline", TokenKind::KwInline},
            {"interface", TokenKind::KwInterface},
            {"match", TokenKind::KwMatch},
            {"mutable", TokenKind::KwMutable},
            {"new", TokenKind::KwNew},
            {"null", TokenKind::KwNull},
            {"private", TokenKind::KwPrivate},
            {"return", TokenKind::KwReturn},
            {"static", TokenKind::KwStatic},
            {"struct", TokenKind::KwStruct},
            {"this", TokenKind::KwThis},
            {"true", TokenKind::KwTrue},
            {"void", TokenKind::KwVoid},
            {"volatile", TokenKind::KwVolatile},
            {"while", TokenKind::KwWhile},
            {"with", TokenKind::KwWith},
            {"int", TokenKind::KwInt},
            {"uint", TokenKind::KwUint},
            {"tiny", TokenKind::KwTiny},
            {"utiny", TokenKind::KwUtiny},
            {"short", TokenKind::KwShort},
            {"ushort", TokenKind::KwUshort},
            {"long", TokenKind::KwLong},
            {"ulong", TokenKind::KwUlong},
            {"float", TokenKind::KwFloat},
            {"double", TokenKind::KwDouble},
            {"bool", TokenKind::KwBool},
            {"char", TokenKind::KwChar},
            {"string", TokenKind::KwString},
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
        while (!is_at_end() && is_alnum(peek()))
            advance();
        std::string text(source_.substr(start, pos_ - start));

        auto it = keywords_.find(text);
        if (it != keywords_.end()) {
            debug::lex::log(debug::lex::Id::Keyword, text, debug::Level::Trace);
            return Token(it->second, start, pos_);
        }

        debug::lex::log(debug::lex::Id::Ident, text, debug::Level::Trace);
        return Token(TokenKind::Ident, start, pos_, std::move(text));
    }

    Token scan_number(uint32_t start) {
        bool is_float = false;

        if (source_[start] == '0' && (peek() == 'x' || peek() == 'X')) {
            advance();
            while (!is_at_end() && is_hex_digit(peek()))
                advance();
            std::string text(source_.substr(start, pos_ - start));
            int64_t val = std::stoll(text, nullptr, 16);
            debug::lex::log(debug::lex::Id::Number, text, debug::Level::Trace);
            return Token(TokenKind::IntLiteral, start, pos_, val);
        }

        if (source_[start] == '0' && (peek() == 'b' || peek() == 'B')) {
            advance();
            while (!is_at_end() && (peek() == '0' || peek() == '1'))
                advance();
            std::string text(source_.substr(start + 2, pos_ - start - 2));
            int64_t val = std::stoll(text, nullptr, 2);
            debug::lex::log(debug::lex::Id::Number, "0b" + text, debug::Level::Trace);
            return Token(TokenKind::IntLiteral, start, pos_, val);
        }

        while (!is_at_end() && is_digit(peek()))
            advance();

        if (!is_at_end() && peek() == '.' && is_digit(peek_next())) {
            is_float = true;
            advance();
            while (!is_at_end() && is_digit(peek()))
                advance();
        }

        if (!is_at_end() && (peek() == 'e' || peek() == 'E')) {
            is_float = true;
            advance();
            if (peek() == '+' || peek() == '-')
                advance();
            while (!is_at_end() && is_digit(peek()))
                advance();
        }

        std::string text(source_.substr(start, pos_ - start));
        debug::lex::log(debug::lex::Id::Number, text, debug::Level::Trace);

        if (is_float) {
            return Token(TokenKind::FloatLiteral, start, pos_, std::stod(text));
        } else {
            return Token(TokenKind::IntLiteral, start, pos_, std::stoll(text));
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
                return make(TokenKind::Dot);
            case '~':
                return make(TokenKind::Tilde);
            case '?':
                return make(TokenKind::Question);
            case '+':
                return match('+')   ? make(TokenKind::PlusPlus)
                       : match('=') ? make(TokenKind::PlusEq)
                                    : make(TokenKind::Plus);
            case '-':
                return match('-')   ? make(TokenKind::MinusMinus)
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

    std::string_view source_;
    uint32_t pos_;
    std::unordered_map<std::string, TokenKind> keywords_;
};

}  // namespace cm
