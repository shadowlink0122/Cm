#pragma once

#include "token.hpp"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cm {

class Lexer {
   public:
    Lexer(std::string_view source) : source_(source), pos_(0) { init_keywords(); }

    // トークン化（メインエントリ）
    std::vector<Token> tokenize();

   private:
    // 次のトークンを取得
    Token next_token();

    // キーワードテーブル初期化
    void init_keywords();

    // 空白とコメントをスキップ
    void skip_whitespace_and_comments();

    // 識別子スキャン
    Token scan_identifier(uint32_t start);

    // 数値リテラルスキャン
    Token scan_number(uint32_t start);

    // 文字列リテラルスキャン
    Token scan_string(uint32_t start);

    // raw文字列リテラルスキャン
    Token scan_raw_string(uint32_t start);

    // 文字リテラルスキャン
    Token scan_char(uint32_t start);

    // エスケープ文字処理
    char scan_escape_char();

    // 演算子スキャン
    Token scan_operator(uint32_t start, char c);

    // ユーティリティ
    bool is_at_end() const { return pos_ >= source_.size(); }
    char peek() const { return is_at_end() ? '\0' : source_[pos_]; }
    char peek_next() const { return pos_ + 1 >= source_.size() ? '\0' : source_[pos_ + 1]; }
    char advance() { return source_[pos_++]; }
    Token make_token(TokenKind kind) const { return Token(kind, pos_, pos_); }

    // 行番号・カラム番号取得
    int get_line_number(uint32_t position) const;
    int get_column_number(uint32_t position) const;

    // 文字マッチ
    bool match(char expected);

    // 文字判定ヘルパー
    static bool is_alpha(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    }
    static bool is_digit(char c) { return c >= '0' && c <= '9'; }
    static bool is_hex_digit(char c) {
        return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    }
    static bool is_octal_digit(char c) { return c >= '0' && c <= '7'; }
    static bool is_alnum(char c) { return is_alpha(c) || is_digit(c); }

    // raw文字列のインデント正規化
    std::string normalize_raw_indent(std::string value);

    std::string_view source_;
    uint32_t pos_;
    std::unordered_map<std::string, TokenKind> keywords_;
};

}  // namespace cm
