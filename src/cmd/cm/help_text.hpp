#pragma once

// ヘルプ本文の出力（本文データは src/cli/help_<lang>.txt で管理し、ビルド時に生成ヘッダへ埋め込まれる。
// {version} と {program} のプレースホルダを実行時に置換して出力する。言語選択は i18n::help_text() が行う）

#include "internal/base/i18n.hpp"

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

// 現在言語のヘルプ本文を出力する
inline void print_help_text(const char* program_name, const std::string& version) {
    std::string text = detail::replace_all(i18n::help_text(), "{version}", version);
    text = detail::replace_all(std::move(text), "{program}", program_name);
    std::cout << text;
}

}  // namespace cm::cli
