#pragma once

#include "token.hpp"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cm {

// ターゲット情報（循環参照回避のためレキサー独自定義）
enum class LexerPlatform { Default, SV };

class Lexer {
   public:
    Lexer(std::string_view source, LexerPlatform platform = LexerPlatform::Default)
        : source_(source), pos_(0), platform_(platform) {
        init_keywords();
        // ソース先頭の //! platform: ディレクティブを解析
        detect_platform_directive();
    }

    // トークン化（メインエントリ）
    std::vector<Token> tokenize();

    // SVプラットフォームかどうかを返す
    bool is_sv() const { return platform_ == LexerPlatform::SV; }

   private:
    // 次のトークンを取得
    Token next_token();

    // キーワードテーブル初期化
    void init_keywords();

    // プラットフォームディレクティブ検出
    void detect_platform_directive();

    // SVキーワードをキーワードテーブルに追加
    void add_sv_keywords();

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

    // エスケープシーケンス処理（バックスラッシュ消費後に呼ぶ。\xHH/\uHHHH/\UHHHHHHHHはUTF-8バイト列を返す。
    // 不正なシーケンスはpending_error_tokens_へ診断を積み、従来互換のフォールバック文字列を返して継続する）
    std::string scan_escape_sequence();

    // エスケープ診断の登録（Errorトークンとしてトークン列へ挿入され、パーサが診断へ変換する）
    void escape_error(uint32_t esc_start, const std::string& message);

    // コードポイントのUTF-8エンコード
    static std::string encode_utf8(uint32_t cp);

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
    std::string normalize_raw_indent(std::string value, size_t indent);
    // リテラル開始行のインデント幅（行頭の空白文字数）を求める（raw文字列のdedent量）
    size_t raw_indent_at(uint32_t token_start) const;

    std::string_view source_;
    uint32_t pos_;
    LexerPlatform platform_;
    std::unordered_map<std::string, TokenKind> keywords_;
    // 字句エラー（不正エスケープ等）。tokenize()がトークン列へ挿入し、パーサが診断へ変換する
    std::vector<Token> pending_error_tokens_;
};

}  // namespace cm
