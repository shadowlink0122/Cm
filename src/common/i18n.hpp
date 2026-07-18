#pragma once

// メッセージi18n基盤（設計04: docs/design/v0.16.2/04_message_i18n.md）
// 英語文字列を原文（キー）とし、日本語カタログは src/common/i18n_ja.tsv（英語原文<TAB>日本語訳）で管理する。
// TSVはビルド時に生成ヘッダ（textdata::kJaCatalogTsv）へ埋め込まれ、初回参照時にパースされる。
// カタログに無いキーは英語のまま返す（フォールバック）。
// 多数のターゲット（cm本体・各テストバイナリ）から使うためヘッダオンリー実装

#include "common/text_data.hpp"

#include <string>
#include <unordered_map>

namespace cm::i18n {

enum class Lang {
    En,  // 英語（デフォルト）
    Ja,  // 日本語
};

// 現在の言語（プロセス全体で共有）
inline Lang& current_lang() {
    static Lang lang = Lang::En;
    return lang;
}

inline void set_language(Lang lang) {
    current_lang() = lang;
}

inline Lang language() {
    return current_lang();
}

// "en"/"ja" 文字列からの言語パース（不明な値はfalseを返し変更しない）
inline bool set_language_from_string(const std::string& name) {
    if (name == "en") {
        current_lang() = Lang::En;
        return true;
    }
    if (name == "ja") {
        current_lang() = Lang::Ja;
        return true;
    }
    return false;
}

namespace detail {

// TSVフィールドのエスケープ（\n・\t・\\）を実文字へ展開する
inline std::string unescape_tsv_field(const std::string& field) {
    std::string out;
    out.reserve(field.size());
    for (size_t i = 0; i < field.size(); ++i) {
        if (field[i] == '\\' && i + 1 < field.size()) {
            char next = field[i + 1];
            if (next == 'n') {
                out += '\n';
                ++i;
                continue;
            }
            if (next == 't') {
                out += '\t';
                ++i;
                continue;
            }
            if (next == '\\') {
                out += '\\';
                ++i;
                continue;
            }
        }
        out += field[i];
    }
    return out;
}

}  // namespace detail

// 日本語カタログ: 英語原文 → 日本語訳（埋め込みTSVを初回参照時にパースする。登録が無いキーは英語のまま出力される）
inline const std::unordered_map<std::string, std::string>& ja_catalog() {
    static const std::unordered_map<std::string, std::string> catalog = [] {
        std::unordered_map<std::string, std::string> map;
        const std::string tsv = textdata::kJaCatalogTsv;
        size_t pos = 0;
        while (pos < tsv.size()) {
            size_t eol = tsv.find('\n', pos);
            if (eol == std::string::npos) {
                eol = tsv.size();
            }
            std::string line = tsv.substr(pos, eol - pos);
            pos = eol + 1;
            if (line.empty() || line[0] == '#') {
                continue;
            }
            size_t tab = line.find('\t');
            if (tab == std::string::npos) {
                continue;
            }
            map.emplace(detail::unescape_tsv_field(line.substr(0, tab)),
                        detail::unescape_tsv_field(line.substr(tab + 1)));
        }
        return map;
    }();
    return catalog;
}

// メッセージ取得: 現在の言語が日本語でカタログに訳があれば日本語、なければ英語原文を返す
inline const char* tr(const char* english) {
    if (current_lang() != Lang::Ja) {
        return english;
    }
    const auto& catalog = ja_catalog();
    auto it = catalog.find(english);
    return it != catalog.end() ? it->second.c_str() : english;
}

inline std::string tr(const std::string& english) {
    return tr(english.c_str());
}

}  // namespace cm::i18n
