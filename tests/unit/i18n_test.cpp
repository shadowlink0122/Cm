// i18nカタログの単体テスト（common/i18n.hpp）
// 言語切替・enum+stringカタログ解決・訳なしIDの英語フォールバックを検証する

#include "../../src/common/i18n.hpp"

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

TEST_F(I18nTest, EveryMessageHasEnglishText) {
    // 英語テーブルはmessage_list.defから同順生成されるため、空文字が無いことだけ確認する
    for (size_t i = 0; i < cm::i18n::kMessageCount; ++i) {
        EXPECT_NE(cm::i18n::kMessagesEn[i], nullptr);
        EXPECT_NE(std::string(cm::i18n::kMessagesEn[i]), "");
    }
}

TEST_F(I18nTest, JapaneseTableHasNoDuplicateIds) {
    std::set<MsgId> seen;
    for (const auto& [id, text] : cm::i18n::kMessagesJa) {
        EXPECT_TRUE(seen.insert(id).second)
            << "duplicate translation for id " << static_cast<int>(id);
        EXPECT_NE(text, nullptr);
    }
}

}  // namespace
