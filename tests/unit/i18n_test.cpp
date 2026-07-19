// i18nカタログの単体テスト（common/i18n.hpp）
// 言語切替・table[メッセージ][言語] の解決・訳なしIDの英語フォールバックを検証する

#include "../../src/internal/base/i18n.hpp"

#include <gtest/gtest.h>
#include <set>
#include <string>

namespace {

using cm::i18n::Lang;
using cm::i18n::MsgId;

// テスト間で言語状態が漏れないように元へ戻すフィクスチャ
class I18nTest : public ::testing::Test {
   protected:
    void SetUp() override { cm::i18n::set_language(Lang::En); }
    void TearDown() override { cm::i18n::set_language(Lang::En); }
};

TEST_F(I18nTest, DefaultLanguageIsEnglish) {
    EXPECT_EQ(cm::i18n::language(), Lang::En);
    EXPECT_STREQ(cm::i18n::msg(MsgId::CliNoCommandSpecified), "error: no command specified\n");
}

TEST_F(I18nTest, JapaneseCatalogLookup) {
    cm::i18n::set_language(Lang::Ja);
    EXPECT_STREQ(cm::i18n::msg(MsgId::CliNoCommandSpecified),
                 "エラー: コマンドが指定されていません\n");
}

TEST_F(I18nTest, SetLanguageFromString) {
    EXPECT_TRUE(cm::i18n::set_language_from_string("ja"));
    EXPECT_EQ(cm::i18n::language(), Lang::Ja);
    EXPECT_TRUE(cm::i18n::set_language_from_string("en"));
    EXPECT_EQ(cm::i18n::language(), Lang::En);
    // 不明な値は変更しない
    cm::i18n::set_language(Lang::Ja);
    EXPECT_FALSE(cm::i18n::set_language_from_string("fr"));
    EXPECT_EQ(cm::i18n::language(), Lang::Ja);
}

TEST_F(I18nTest, PlaceholderSubstitution) {
    const std::string text = cm::i18n::msgf(MsgId::CliPathDoesNotExist, "foo/bar.cm");
    EXPECT_NE(text.find("foo/bar.cm"), std::string::npos);
    EXPECT_EQ(text.find("{0}"), std::string::npos);
}

TEST_F(I18nTest, DiagnosticTemplateTranslated) {
    cm::i18n::set_language(Lang::Ja);
    EXPECT_STREQ(cm::i18n::msg(MsgId::DiagW001), "変数 '{0}' は使用されていません");
}

TEST_F(I18nTest, TableIsFullyPopulated) {
    // 全メッセージ×全言語で本文が引けること（英語はstatic_assertで保証済み。
    // 日本語がnullptrの行があっても英語フォールバックで空にはならない）
    for (size_t i = 0; i < cm::i18n::kMessageCount; ++i) {
        for (size_t l = 0; l < cm::i18n::kLangCount; ++l) {
            cm::i18n::set_language(static_cast<Lang>(l));
            const char* text = cm::i18n::msg(static_cast<MsgId>(i));
            EXPECT_NE(text, nullptr);
            EXPECT_NE(std::string(text), "");
        }
    }
}

TEST_F(I18nTest, PlaceholderConsistencyAcrossLanguages) {
    // 訳のプレースホルダは英語（原文）に存在するものだけを使えること
    // （en側に無い {N} を訳が参照すると、msgfで置換されず {N} のまま出力される）
    auto collect = [](const char* text) {
        std::set<int> found;
        if (!text) {
            return found;
        }
        const std::string s = text;
        size_t pos = 0;
        while ((pos = s.find('{', pos)) != std::string::npos) {
            size_t end = s.find('}', pos);
            if (end == std::string::npos) {
                break;
            }
            const std::string inner = s.substr(pos + 1, end - pos - 1);
            if (!inner.empty() && inner.find_first_not_of("0123456789") == std::string::npos) {
                found.insert(std::stoi(inner));
            }
            pos = end + 1;
        }
        return found;
    };
    const size_t en = static_cast<size_t>(Lang::En);
    for (size_t i = 0; i < cm::i18n::kMessageCount; ++i) {
        const auto en_ph = collect(cm::i18n::kMessages[i][en]);
        for (size_t l = 0; l < cm::i18n::kLangCount; ++l) {
            if (l == en) {
                continue;
            }
            for (int n : collect(cm::i18n::kMessages[i][l])) {
                EXPECT_TRUE(en_ph.count(n))
                    << "message " << i << " lang " << l << " uses placeholder {" << n
                    << "} that does not exist in the English template";
            }
        }
    }
}

}  // namespace
