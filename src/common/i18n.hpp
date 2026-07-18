#pragma once

// メッセージi18n基盤（設計04: docs/design/v0.16.2/04_message_i18n.md）
// 全メッセージは enum MsgId + 言語別文字列テーブルでC++側に集約管理する:
//   - src/common/messages/message_list.def — ID + 英語原文の単一ソース（X-macro）
//   - src/common/messages/messages_ja.hpp  — 日本語訳（MsgId→訳のペア表。無いIDは英語へフォールバック）
// テンプレートは {0} {1} ... のプレースホルダで動的値を受け取り、言語ごとの語順の違いに対応する。
// IDはenumなのでタイプミスはコンパイルエラーになる。
// ヘルプ本文は src/cli/help_<lang>.txt（ビルド時埋め込み）で管理する。
// 多数のターゲット（cm本体・各テストバイナリ）から使うためヘッダオンリー実装

#include "common/text_data.hpp"
#include "messages/message_ids.hpp"
#include "messages/messages_en.hpp"
#include "messages/messages_ja.hpp"

#include <cstring>
#include <string>

namespace cm::i18n {

namespace detail {

// 言語別の訳テーブル（MsgId順の直接参照配列を初回に構築する。nullptr = 訳なし→英語）
struct TranslationTable {
    const char* texts[kMessageCount] = {};
};

template <size_t N>
inline const TranslationTable& build_table(const std::pair<MsgId, const char*> (&pairs)[N]) {
    static const TranslationTable table = [&] {
        TranslationTable t;
        for (const auto& [id, text] : pairs) {
            t.texts[static_cast<size_t>(id)] = text;
        }
        return t;
    }();
    return table;
}

// msgf用の引数文字列化
inline std::string to_display(const std::string& value) {
    return value;
}
inline std::string to_display(const char* value) {
    return value;
}
inline std::string to_display(char value) {
    return std::string(1, value);
}
template <typename T>
inline std::string to_display(T value) {
    return std::to_string(value);
}

}  // namespace detail

// 対応言語（新しい言語を追加するには messages_<lang>.hpp を作り、この列挙と下のswitch群へ1行ずつ足す）
enum class Lang {
    En,
    Ja,
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

// "en"/"ja" 等の言語コードから言語を設定する。未知のコードはfalseを返し変更しない
inline bool set_language_from_string(const std::string& code) {
    if (code == "en") {
        current_lang() = Lang::En;
        return true;
    }
    if (code == "ja") {
        current_lang() = Lang::Ja;
        return true;
    }
    return false;
}

// 現在の言語コード
inline const char* language_code() {
    switch (current_lang()) {
        case Lang::Ja:
            return "ja";
        case Lang::En:
        default:
            return "en";
    }
}

// 現在言語のヘルプ本文テンプレート（無い言語は英語へフォールバック）
inline const char* help_text() {
    const char* code = language_code();
    for (int i = 0; i < textdata::kCatalogCount; ++i) {
        if (std::strcmp(code, textdata::kCatalogs[i].code) == 0) {
            return textdata::kCatalogs[i].help_text;
        }
    }
    return textdata::kCatalogs[0].help_text;
}

// ID→メッセージテンプレート（現在言語 → 英語の順でフォールバック）
inline const char* msg(MsgId id) {
    const size_t index = static_cast<size_t>(id);
    if (current_lang() == Lang::Ja) {
        const char* text = detail::build_table(kMessagesJa).texts[index];
        if (text) {
            return text;
        }
    }
    return kMessagesEn[index];
}

// テンプレート中の {0} {1} ... を引数で置換してメッセージを組み立てる
template <typename... Args>
inline std::string msgf(MsgId id, Args&&... args) {
    std::string text = msg(id);
    const std::string values[] = {detail::to_display(std::forward<Args>(args))...};
    for (size_t i = 0; i < sizeof...(Args); ++i) {
        const std::string placeholder = "{" + std::to_string(i) + "}";
        size_t pos = 0;
        while ((pos = text.find(placeholder, pos)) != std::string::npos) {
            text.replace(pos, placeholder.size(), values[i]);
            pos += values[i].size();
        }
    }
    return text;
}

}  // namespace cm::i18n
