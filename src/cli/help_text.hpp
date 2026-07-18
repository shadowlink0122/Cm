#pragma once

// ヘルプ本文の出力（本文データは src/cli/help_en.txt / help_ja.txt で管理し、
// ビルド時に生成ヘッダ textdata::kHelpEn / kHelpJa へ埋め込まれる。
// {version} と {program} のプレースホルダを実行時に置換して出力する）

#include "common/text_data.hpp"

#include <iostream>
#include <string>

namespace cm::cli {

namespace detail {

// テンプレート中のプレースホルダを全て置換する
inline std::string replace_all(std::string text, const std::string& key, const std::string& value) {
    size_t pos = 0;
    while ((pos = text.find(key, pos)) != std::string::npos) {
        text.replace(pos, key.size(), value);
        pos += value.size();
    }
    return text;
}

}  // namespace detail

// ヘルプ本文を言語別テンプレートから出力する
inline void print_help_text(const char* help_template, const char* program_name,
                            const std::string& version) {
    std::string text = detail::replace_all(help_template, "{version}", version);
    text = detail::replace_all(std::move(text), "{program}", program_name);
    std::cout << text;
}

inline void print_help_en(const char* program_name, const std::string& version) {
    print_help_text(textdata::kHelpEn, program_name, version);
}

inline void print_help_ja(const char* program_name, const std::string& version) {
    print_help_text(textdata::kHelpJa, program_name, version);
}

}  // namespace cm::cli
