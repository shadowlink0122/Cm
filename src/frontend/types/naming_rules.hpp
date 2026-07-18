#pragma once

// ============================================================
// 命名規則の判定（check/lint --strict の L001 naming-convention）
// 判定は先頭のアンダースコアを除去した後に行う（_unused 等を許容）
// ============================================================

#include <cctype>
#include <string>

namespace cm {
namespace naming {

// 先頭のアンダースコアを取り除いた判定対象部分を返す
inline std::string strip_leading_underscores(const std::string& name) {
    size_t i = 0;
    while (i < name.size() && name[i] == '_') {
        ++i;
    }
    return name.substr(i);
}

// snake_case: 小文字・数字・アンダースコアのみ（先頭は小文字）
inline bool is_snake_case(const std::string& raw) {
    std::string name = strip_leading_underscores(raw);
    if (name.empty()) {
        return true;
    }
    if (!std::islower(static_cast<unsigned char>(name[0]))) {
        return false;
    }
    for (char c : name) {
        if (!std::islower(static_cast<unsigned char>(c)) &&
            !std::isdigit(static_cast<unsigned char>(c)) && c != '_') {
            return false;
        }
    }
    return true;
}

// PascalCase: 先頭が大文字、アンダースコアなし
inline bool is_pascal_case(const std::string& raw) {
    std::string name = strip_leading_underscores(raw);
    if (name.empty()) {
        return true;
    }
    if (!std::isupper(static_cast<unsigned char>(name[0]))) {
        return false;
    }
    for (char c : name) {
        if (c == '_') {
            return false;
        }
        if (!std::isalnum(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    return true;
}

// UPPER_SNAKE_CASE: 大文字・数字・アンダースコアのみ（先頭は大文字）
inline bool is_upper_snake_case(const std::string& raw) {
    std::string name = strip_leading_underscores(raw);
    if (name.empty()) {
        return true;
    }
    if (!std::isupper(static_cast<unsigned char>(name[0]))) {
        return false;
    }
    for (char c : name) {
        if (!std::isupper(static_cast<unsigned char>(c)) &&
            !std::isdigit(static_cast<unsigned char>(c)) && c != '_') {
            return false;
        }
    }
    return true;
}

}  // namespace naming
}  // namespace cm
