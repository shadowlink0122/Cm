#pragma once

// ============================================================
// Formatter共有ヘルパー: リテラル走査（幅付きリテラル・エスケープ判定）
// ============================================================

#include <cctype>
#include <string>

namespace cm {
namespace fmt {

// SV幅付きリテラル（8'd170, 4'b1010, 16'hFFFF）の ' かどうかを判定する。
// 文字リテラルの開始と誤認すると括弧カウントが狂うため、lexerの規則（数字 + ' + 基数文字 + 値）に合わせて除外する
inline bool is_sized_literal_quote(const std::string& s, size_t i) {
    if (i >= s.size() || s[i] != '\'')
        return false;
    if (i == 0 || !std::isdigit(static_cast<unsigned char>(s[i - 1])))
        return false;
    if (i + 2 >= s.size())
        return false;
    char base = s[i + 1];
    if (base != 'd' && base != 'D' && base != 'b' && base != 'B' && base != 'h' && base != 'H')
        return false;
    return std::isalnum(static_cast<unsigned char>(s[i + 2])) != 0;
}

// s[i] の直前に連続するバックスラッシュ数が奇数なら s[i] はエスケープされている。
// 単純な s[i-1] == '\\' 判定では '\\'（エスケープされたバックスラッシュ）の後ろの引用符を
// 誤ってエスケープ扱いし、in_char/in_string が閉じずに括弧カウントが狂う。
inline bool is_escaped_char(const std::string& s, size_t i) {
    size_t backslashes = 0;
    while (i > 0 && s[i - 1] == '\\') {
        backslashes++;
        i--;
    }
    return (backslashes % 2) == 1;
}

}  // namespace fmt
}  // namespace cm
