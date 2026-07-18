// i18nカタログの単体テスト（common/i18n.hpp）
// 言語切替・カタログ解決・未登録キーの英語フォールバックを検証する

#include "../../src/common/i18n.hpp"

#include <gtest/gtest.h>
#include <string>

namespace {

using cm::i18n::Lang;

// テスト間で言語状態が漏れないように元へ戻すフィクスチャ
class I18nTest : public ::testing::Test {
   protected:
    void SetUp() override { cm::i18n::set_language(Lang::En); }
    void TearDown() override { cm::i18n::set_language(Lang::En); }
};

TEST_F(I18nTest, DefaultLanguageIsEnglish) {
    EXPECT_EQ(cm::i18n::language(), Lang::En);
    EXPECT_STREQ(cm::i18n::tr("error: no command specified\n"), "error: no command specified\n");
}

TEST_F(I18nTest, JapaneseCatalogLookup) {
    cm::i18n::set_language(Lang::Ja);
    EXPECT_STREQ(cm::i18n::tr("error: no command specified\n"),
                 "エラー: コマンドが指定されていません\n");
    EXPECT_EQ(cm::i18n::tr(std::string("checking: ")), "チェック中: ");
}

TEST_F(I18nTest, UnknownKeyFallsBackToEnglish) {
    cm::i18n::set_language(Lang::Ja);
    EXPECT_STREQ(cm::i18n::tr("this key does not exist in the catalog"),
                 "this key does not exist in the catalog");
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

TEST_F(I18nTest, DiagnosticTemplateTranslated) {
    cm::i18n::set_language(Lang::Ja);
    EXPECT_STREQ(cm::i18n::tr("variable '{0}' is unused"), "変数 '{0}' は使用されていません");
}

}  // namespace
