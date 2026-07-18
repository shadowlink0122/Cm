// ============================================================
// naming_rules.hpp の単体テスト
// check/lint --strict の命名規則判定（L001）が使うケース判定関数を検証する
// ============================================================

#include "frontend/types/naming_rules.hpp"

#include <gtest/gtest.h>

using cm::naming::is_pascal_case;
using cm::naming::is_snake_case;
using cm::naming::is_upper_snake_case;
using cm::naming::strip_leading_underscores;

// ------------------------------------------------------------
// strip_leading_underscores
// ------------------------------------------------------------
TEST(NamingRules, StripLeadingUnderscores) {
    EXPECT_EQ(strip_leading_underscores("_x"), "x");
    EXPECT_EQ(strip_leading_underscores("__x"), "x");
    EXPECT_EQ(strip_leading_underscores("x"), "x");
    EXPECT_EQ(strip_leading_underscores("___"), "");
    EXPECT_EQ(strip_leading_underscores(""), "");
}

// ------------------------------------------------------------
// snake_case
// ------------------------------------------------------------
TEST(NamingRules, SnakeCaseAccepts) {
    EXPECT_TRUE(is_snake_case("total_count"));
    EXPECT_TRUE(is_snake_case("x"));
    EXPECT_TRUE(is_snake_case("value2"));
    EXPECT_TRUE(is_snake_case("led_ready"));
    // 先頭アンダースコアは除去後に判定される
    EXPECT_TRUE(is_snake_case("_unused"));
    EXPECT_TRUE(is_snake_case("_"));
    EXPECT_TRUE(is_snake_case(""));
}

TEST(NamingRules, SnakeCaseRejects) {
    EXPECT_FALSE(is_snake_case("camelCase"));
    EXPECT_FALSE(is_snake_case("PascalCase"));
    EXPECT_FALSE(is_snake_case("UPPER_SNAKE"));
    EXPECT_FALSE(is_snake_case("B"));
    EXPECT_FALSE(is_snake_case("bad-name"));
    EXPECT_FALSE(is_snake_case("2start"));
}

// ------------------------------------------------------------
// PascalCase
// ------------------------------------------------------------
TEST(NamingRules, PascalCaseAccepts) {
    EXPECT_TRUE(is_pascal_case("Point"));
    EXPECT_TRUE(is_pascal_case("AdderIo"));
    EXPECT_TRUE(is_pascal_case("T"));
    EXPECT_TRUE(is_pascal_case("Vec2"));
    // 略語の連続大文字は許容（RGB等）
    EXPECT_TRUE(is_pascal_case("RGB"));
    EXPECT_TRUE(is_pascal_case(""));
}

TEST(NamingRules, PascalCaseRejects) {
    EXPECT_FALSE(is_pascal_case("snake_case"));
    EXPECT_FALSE(is_pascal_case("camelCase"));
    EXPECT_FALSE(is_pascal_case("Bad_Pascal"));
    EXPECT_FALSE(is_pascal_case("UPPER_SNAKE"));
    EXPECT_FALSE(is_pascal_case("adder_io"));
}

// ------------------------------------------------------------
// UPPER_SNAKE_CASE
// ------------------------------------------------------------
TEST(NamingRules, UpperSnakeCaseAccepts) {
    EXPECT_TRUE(is_upper_snake_case("MAX_COUNT"));
    EXPECT_TRUE(is_upper_snake_case("CLK_FREQ"));
    EXPECT_TRUE(is_upper_snake_case("N"));
    EXPECT_TRUE(is_upper_snake_case("CTRL_00"));
    EXPECT_TRUE(is_upper_snake_case(""));
}

TEST(NamingRules, UpperSnakeCaseRejects) {
    EXPECT_FALSE(is_upper_snake_case("MaxCount"));
    EXPECT_FALSE(is_upper_snake_case("max_count"));
    EXPECT_FALSE(is_upper_snake_case("camelCase"));
}
