// ============================================================
// importプリプロセッサ - import文のパースとモジュールソース加工ユーティリティ
// ============================================================

#include "internal/preprocessor/import.hpp"
#include "internal/preprocessor/import_internal.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cm::preprocessor {

ImportPreprocessor::ImportInfo ImportPreprocessor::parse_import_statement(
    const std::string& import_line) {
    ImportInfo info;

    // セミコロンを削除
    std::string line = import_line;
    while (!line.empty() && (line.back() == ';' || line.back() == ' ' || line.back() == '\t'))
        line.pop_back();

    // 相対パスチェック
    if (line.find("./") != std::string::npos || line.find("../") != std::string::npos) {
        info.is_relative = true;
    }

    // トリムヘルパー
    auto trim = [](const std::string& s) -> std::string {
        size_t start = s.find_first_not_of(" \t");
        if (start == std::string::npos)
            return "";
        size_t end = s.find_last_not_of(" \t");
        return s.substr(start, end - start + 1);
    };

    std::string trimmed = trim(line);

    do {
        // ========== from module import { items } ==========
        if (trimmed.rfind("from ", 0) == 0) {
            // from MODULE import { ITEMS }
            std::string rest = trim(trimmed.substr(5));
            size_t import_pos = rest.find(" import ");
            if (import_pos != std::string::npos) {
                info.module_name = trim(rest.substr(0, import_pos));
                info.is_from_import = true;
                std::string items_part = trim(rest.substr(import_pos + 8));
                // { items } の中身を抽出
                if (items_part.front() == '{' && items_part.back() == '}') {
                    std::string items_str = items_part.substr(1, items_part.size() - 2);
                    parse_import_items(items_str, info);
                }
            }
            break;
        }

        // import で始まる場合
        if (trimmed.rfind("import ", 0) == 0) {
            std::string rest = trim(trimmed.substr(7));

            // ========== import { items } from module ==========
            if (!rest.empty() && rest.front() == '{') {
                size_t close_brace = rest.find('}');
                if (close_brace != std::string::npos) {
                    std::string items_str = rest.substr(1, close_brace - 1);
                    std::string after_brace = trim(rest.substr(close_brace + 1));
                    if (after_brace.rfind("from ", 0) == 0) {
                        info.module_name = trim(after_brace.substr(5));
                        info.is_from_import = true;
                        parse_import_items(items_str, info);
                        break;
                    }
                }
            }

            // ========== import * from module ==========
            if (rest.rfind("* from ", 0) == 0) {
                info.module_name = trim(rest.substr(7));
                info.is_wildcard = true;
                info.is_from_import = true;
                break;
            }

            // ========== import module as alias ==========
            {
                size_t as_pos = rest.find(" as ");
                if (as_pos != std::string::npos) {
                    info.module_name = trim(rest.substr(0, as_pos));
                    info.alias = trim(rest.substr(as_pos + 4));
                    break;
                }
            }

            // ========== import path/*::{items} ==========
            {
                size_t wildcard_sel = rest.find("/*::{");
                if (wildcard_sel != std::string::npos) {
                    info.module_name = trim(rest.substr(0, wildcard_sel));
                    info.is_recursive_wildcard = true;
                    info.is_wildcard = true;
                    size_t close = rest.find('}', wildcard_sel + 5);
                    if (close != std::string::npos) {
                        std::string items_str =
                            rest.substr(wildcard_sel + 5, close - wildcard_sel - 5);
                        parse_import_items(items_str, info);
                    }
                    break;
                }
            }

            // ========== import path/* ==========
            if (rest.size() >= 2 && rest.substr(rest.size() - 2) == "/*") {
                info.module_name = trim(rest.substr(0, rest.size() - 2));
                info.is_recursive_wildcard = true;
                info.is_wildcard = true;
                break;
            }

            // ========== import module::{items} ==========
            {
                size_t sel_pos = rest.find("::{");
                if (sel_pos != std::string::npos) {
                    size_t close = rest.find('}', sel_pos + 3);
                    if (close != std::string::npos) {
                        // module::* (ワイルドカード) チェック
                        std::string items_str = rest.substr(sel_pos + 3, close - sel_pos - 3);
                        if (trim(items_str) == "*") {
                            info.module_name = trim(rest.substr(0, sel_pos));
                            info.is_wildcard = true;
                        } else {
                            info.module_name = trim(rest.substr(0, sel_pos));
                            parse_import_items(items_str, info);
                        }
                        break;
                    }
                }
            }

            // ========== import module::* ==========
            if (rest.size() >= 3 && rest.substr(rest.size() - 3) == "::*") {
                info.module_name = trim(rest.substr(0, rest.size() - 3));
                info.is_wildcard = true;
                break;
            }

            // ========== import module (シンプル) ==========
            info.module_name = rest;

            // ./path/module::submodule::item 形式をチェック
            std::string& name = info.module_name;
            size_t last_colon = name.rfind("::");
            if (last_colon != std::string::npos && last_colon > 0) {
                std::string last_part = name.substr(last_colon + 2);
                if (last_part == "*") {
                    info.is_wildcard = true;
                    info.module_name = name.substr(0, last_colon);
                } else if (!last_part.empty() && std::islower(last_part[0])) {
                    size_t first_colon = name.find("::");
                    if (!info.is_relative || first_colon != last_colon) {
                        info.items.push_back(last_part);
                        info.module_name = name.substr(0, last_colon);
                    }
                }
            }
        }
    } while (false);

    // 引用符を除去
    if (info.module_name.size() >= 2) {
        if ((info.module_name.front() == '"' && info.module_name.back() == '"') ||
            (info.module_name.front() == '\'' && info.module_name.back() == '\'')) {
            info.module_name = info.module_name.substr(1, info.module_name.size() - 2);
        }
    }

    return info;
}

// アイテムリストをパースするヘルパー関数
void ImportPreprocessor::parse_import_items(const std::string& items_str, ImportInfo& info) {
    std::stringstream ss(items_str);
    std::string item;
    while (std::getline(ss, item, ',')) {
        // トリム
        item.erase(0, item.find_first_not_of(" \t\n\r"));
        item.erase(item.find_last_not_of(" \t\n\r") + 1);
        if (!item.empty()) {
            // item as alias の形式をチェック
            size_t as_pos = item.find(" as ");
            if (as_pos != std::string::npos) {
                std::string name = item.substr(0, as_pos);
                std::string alias = item.substr(as_pos + 4);
                // トリム
                name.erase(name.find_last_not_of(" \t") + 1);
                alias.erase(0, alias.find_first_not_of(" \t"));
                info.items.push_back(name);
                info.item_aliases.push_back({name, alias});
            } else {
                info.items.push_back(item);
            }
        }
    }
}

std::string ImportPreprocessor::add_module_prefix(const std::string& source,
                                                  const std::string& module_name) {
    // すでにexportキーワードは削除されているので、関数と定数の宣言にモジュール名をプレフィックスとして追加する

    std::string result;
    std::istringstream input(source);
    std::string line;

    while (std::getline(input, line)) {
        // const定数の宣言を検出してプレフィックスを追加
        std::regex const_regex(R"(^(\s*const\s+\w+\s+)(\w+)(\s*=.*)$)");
        std::smatch const_match;
        if (std::regex_match(line, const_match, const_regex)) {
            result += const_match[1].str() + module_name + "::" + const_match[2].str() +
                      const_match[3].str() + "\n";
            continue;
        }

        // 関数宣言を検出してプレフィックスを追加
        // 型 関数名(パラメータ) { の形式
        std::regex func_regex(R"(^(\s*\w+\s+)(\w+)(\s*\([^)]*\)\s*\{.*)$)");
        std::smatch func_match;
        if (std::regex_match(line, func_match, func_regex)) {
            // main関数は除外
            if (func_match[2].str() != "main") {
                result += func_match[1].str() + module_name + "::" + func_match[2].str() +
                          func_match[3].str() + "\n";
                continue;
            }
        }

        // その他の行はそのまま出力
        result += line + "\n";
    }

    return result;
}

std::string ImportPreprocessor::extract_module_namespace(const std::string& module_source) {
    // module M; 宣言を検出
    std::regex module_regex(R"(^\s*module\s+(\w+)\s*;)");
    std::istringstream input(module_source);
    std::string line;

    while (std::getline(input, line)) {
        std::smatch match;
        if (std::regex_match(line, match, module_regex)) {
            return match[1].str();
        }
    }

    return "";  // module宣言がない
}

std::string ImportPreprocessor::extract_namespace_content(const std::string& source,
                                                          const std::string& namespace_name) {
    // 指定した名前空間内の内容を抽出
    // namespace X { ... } の ... 部分を返す

    std::stringstream result;
    std::istringstream input(source);
    std::string line;
    bool in_target_namespace = false;
    int brace_depth = 0;

    // namespace X { パターン
    std::regex ns_start_regex(R"(^\s*namespace\s+(\w+)\s*\{)");

    while (std::getline(input, line)) {
        std::smatch match;

        if (!in_target_namespace && std::regex_search(line, match, ns_start_regex)) {
            if (match[1].str() == namespace_name) {
                in_target_namespace = true;
                brace_depth = 1;
                // 開き括弧の後の内容があれば追加
                size_t brace_pos = line.find('{');
                if (brace_pos != std::string::npos && brace_pos + 1 < line.length()) {
                    std::string after_brace = line.substr(brace_pos + 1);
                    if (!after_brace.empty() &&
                        after_brace.find_first_not_of(" \t\n\r") != std::string::npos) {
                        result << after_brace << "\n";
                    }
                }
                continue;
            }
        }

        if (in_target_namespace) {
            // 括弧の深さを追跡
            for (char c : line) {
                if (c == '{')
                    brace_depth++;
                else if (c == '}')
                    brace_depth--;
            }

            // 名前空間の終了を検出
            if (brace_depth == 0) {
                // 閉じ括弧の前の内容を追加
                size_t close_pos = line.find('}');
                if (close_pos > 0) {
                    std::string before_close = line.substr(0, close_pos);
                    if (!before_close.empty() &&
                        before_close.find_first_not_of(" \t\n\r") != std::string::npos) {
                        result << before_close << "\n";
                    }
                }
                break;
            } else {
                result << line << "\n";
            }
        }
    }

    return result.str();
}

}  // namespace cm::preprocessor
