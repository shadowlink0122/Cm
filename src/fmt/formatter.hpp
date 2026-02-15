#pragma once

// ============================================================
// Formatter - コスメティック整形
// ============================================================
// K&Rスタイル、インデント正規化、空白整理

#include <iostream>
#include <string>
#include <vector>

namespace cm {
namespace fmt {

/// フォーマット結果
struct FormatResult {
    std::string formatted_code;
    bool modified = false;
    size_t changes_applied = 0;
};

/// Formatter - コスメティック整形（K&Rスタイル）
class Formatter {
   public:
    Formatter() = default;

    /// インデント幅を設定
    void set_indent_width(int width) { indent_width_ = width; }

    /// コードをフォーマット
    FormatResult format(const std::string& original_code);

    /// ファイルをフォーマットして上書き
    bool format_file(const std::string& filepath);

    /// 修正サマリーを表示
    void print_summary(const FormatResult& result, std::ostream& out = std::cout) const;

   private:
    int indent_width_ = 4;

    /// 行末の空白を削除
    std::string trim_trailing_whitespace(const std::string& code, size_t& changes);

    /// タブをスペースに変換
    std::string tabs_to_spaces(const std::string& code, size_t& changes);

    /// 連続空行を1行に制限
    std::string normalize_blank_lines(const std::string& code, size_t& changes);

    /// K&Rスタイル: 開き波括弧を同一行に
    std::string enforce_kr_braces(const std::string& code, size_t& changes);

    /// インデント正規化（ブレース深さを追跡）
    std::string normalize_indentation(const std::string& code, size_t& changes);

    /// 演算子周りの空白正規化
    std::string normalize_operator_spacing(const std::string& code, size_t& changes);

    /// セミコロン後に改行を強制（括弧内・波括弧内は除外）
    std::string enforce_semicolon_newline(const std::string& code, size_t& changes);

    /// 現在行のインデントレベルを取得
    size_t get_current_indent(const std::string& code) const;

    /// ファイル末尾に1つの改行を保証
    std::string ensure_trailing_newline(const std::string& code, size_t& changes);

    /// 行末コメントの位置揃え
    std::string align_inline_comments(const std::string& code, size_t& changes);
};

}  // namespace fmt
}  // namespace cm
