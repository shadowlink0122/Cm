// ============================================================
// 名前変換ユーティリティ
// ============================================================
// 命名規則に基づく名前変換関数

#pragma once

#include <algorithm>
#include <cctype>
#include <string>

namespace cm {
namespace lint {

/// CamelCase/PascalCase を snake_case に変換
/// CalculateSum → calculate_sum
/// myVariableName → my_variable_name
/// HTTPRequest → http_request
inline std::string to_snake_case(const std::string& name) {
    if (name.empty())
        return name;

    std::string result;
    result.reserve(name.size() + 5);  // 余裕を持って確保

    for (size_t i = 0; i < name.size(); ++i) {
        char c = name[i];

        if (std::isupper(c)) {
            // 大文字の前にアンダースコアを挿入（先頭以外）
            if (i > 0) {
                // 連続する大文字の扱い: HTTPRequest → http_request
                bool prev_upper = std::isupper(name[i - 1]);
                bool next_lower = (i + 1 < name.size()) && std::islower(name[i + 1]);

                if (!prev_upper || next_lower) {
                    result += '_';
                }
            }
            result += static_cast<char>(std::tolower(c));
        } else {
            result += c;
        }
    }

    // 先頭のアンダースコアを削除
    if (!result.empty() && result[0] == '_') {
        result = result.substr(1);
    }

    return result;
}

/// snake_case/camelCase を UPPER_SNAKE_CASE に変換
/// maxValue → MAX_VALUE
/// http_timeout → HTTP_TIMEOUT
inline std::string to_upper_snake_case(const std::string& name) {
    // まず snake_case に変換
    std::string snake = to_snake_case(name);

    // 全て大文字に変換
    std::string result;
    result.reserve(snake.size());

    for (char c : snake) {
        result += static_cast<char>(std::toupper(c));
    }

    return result;
}

/// snake_case を PascalCase に変換
/// my_struct → MyStruct
/// http_client → HttpClient
inline std::string to_pascal_case(const std::string& name) {
    if (name.empty())
        return name;

    std::string result;
    result.reserve(name.size());

    bool capitalize_next = true;

    for (char c : name) {
        if (c == '_') {
            capitalize_next = true;
        } else {
            if (capitalize_next) {
                result += static_cast<char>(std::toupper(c));
                capitalize_next = false;
            } else {
                result += static_cast<char>(std::tolower(c));
            }
        }
    }

    return result;
}

/// 名前が snake_case かどうか判定
inline bool is_snake_case(const std::string& name) {
    if (name.empty())
        return true;

    for (size_t i = 0; i < name.size(); ++i) {
        char c = name[i];
        // 小文字、数字、アンダースコアのみ許可
        if (!std::islower(c) && !std::isdigit(c) && c != '_') {
            return false;
        }
        // 先頭のアンダースコアは許可しない
        if (i == 0 && c == '_') {
            return false;
        }
        // 連続するアンダースコアは許可しない
        if (c == '_' && i > 0 && name[i - 1] == '_') {
            return false;
        }
    }
    return true;
}

/// 名前が UPPER_SNAKE_CASE かどうか判定
inline bool is_upper_snake_case(const std::string& name) {
    if (name.empty())
        return true;

    for (size_t i = 0; i < name.size(); ++i) {
        char c = name[i];
        // 大文字、数字、アンダースコアのみ許可
        if (!std::isupper(c) && !std::isdigit(c) && c != '_') {
            return false;
        }
        // 先頭のアンダースコアは許可しない
        if (i == 0 && c == '_') {
            return false;
        }
    }
    return true;
}

/// 名前が PascalCase かどうか判定
inline bool is_pascal_case(const std::string& name) {
    if (name.empty())
        return true;

    // 先頭は大文字
    if (!std::isupper(name[0])) {
        return false;
    }

    // アンダースコアは許可しない
    for (char c : name) {
        if (c == '_') {
            return false;
        }
    }

    return true;
}

}  // namespace lint
}  // namespace cm
