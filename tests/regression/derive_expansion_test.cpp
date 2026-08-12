#include "../../src/internal/macro/derive.hpp"
#include "../../src/internal/syntax/lexer/lexer.hpp"
#include "../../src/internal/syntax/parser/parser.hpp"

#include <gtest/gtest.h>

using namespace cm;

// ============================================================
// derive/withソース展開のスナップショット検証（derive-as-source-expansion 第1段）
// 合成ソースの意図しない変化を検出する
// ============================================================
namespace {

ast::Program parse_source(const std::string& code) {
    Lexer lexer(code);
    auto tokens = lexer.tokenize();
    Parser parser(std::move(tokens));
    auto program = parser.parse();
    EXPECT_FALSE(parser.has_errors());
    return program;
}

}  // namespace

TEST(DeriveExpansionTest, EqSnapshotForPlainStruct) {
    auto program = parse_source(R"(
struct Point with Eq {
    int x;
    int y;
}
)");
    const std::string expected =
        "impl Point for Eq {\n"
        "    operator bool ==(Point other) {\n"
        "        return self.x == other.x && self.y == other.y;\n"
        "    }\n"
        "}\n";
    EXPECT_EQ(macro_expand::synthesize_derive_impls(program), expected);
}

TEST(DeriveExpansionTest, EqSnapshotForEmptyStruct) {
    auto program = parse_source(R"(
struct Unit with Eq {
}
)");
    const std::string expected =
        "impl Unit for Eq {\n"
        "    operator bool ==(Unit other) {\n"
        "        return true;\n"
        "    }\n"
        "}\n";
    EXPECT_EQ(macro_expand::synthesize_derive_impls(program), expected);
}

TEST(DeriveExpansionTest, GenericStructExpandsToGenericImpl) {
    // ジェネリック構造体は#[__derived]マーカー付きの総称implへソース合成される（特殊化時検証はマーカー経由）
    auto program = parse_source(R"(
struct Pair<T, U> with Eq {
    T first;
    U second;
}
)");
    const std::string expected =
        "#[__derived]\n"
        "impl Pair<T, U> for Eq {\n"
        "    operator bool ==(Pair<T, U> other) {\n"
        "        return self.first == other.first && self.second == other.second;\n"
        "    }\n"
        "}\n";
    EXPECT_EQ(macro_expand::synthesize_derive_impls(program), expected);
}

TEST(DeriveExpansionTest, ExpandAddsImplAndStripsHandledTrait) {
    auto program = parse_source(R"(
struct Point with Eq {
    int x;
}
)");
    const size_t before = program.declarations.size();
    const int added = macro_expand::expand_derives(program);
    EXPECT_EQ(added, 1);
    EXPECT_EQ(program.declarations.size(), before + 1);
    // 展開済みトレイトはauto_implsから除去される（合成implが唯一の実装になる）
    auto* st = program.declarations[0]->as<ast::StructDecl>();
    ASSERT_NE(st, nullptr);
    EXPECT_TRUE(st->auto_impls.empty());
    // 追加された宣言はimplである
    auto* impl = program.declarations[before]->as<ast::ImplDecl>();
    EXPECT_NE(impl, nullptr);
}

TEST(DeriveExpansionTest, MarkerTraitRemainsForMirPath) {
    // 生成物の無いマーカートレイト（Copy）はauto_implsに残り、従来のimpl_info登録経路が処理する
    auto program = parse_source(R"(
struct Point with Eq, Copy {
    int x;
}
)");
    macro_expand::expand_derives(program);
    auto* st = program.declarations[0]->as<ast::StructDecl>();
    ASSERT_NE(st, nullptr);
    ASSERT_EQ(st->auto_impls.size(), 1u);
    EXPECT_EQ(st->auto_impls[0], "Copy");
}

TEST(DeriveExpansionTest, EqSnapshotForArrayField) {
    // 固定長配列フィールドは要素単位の比較へ展開する（配列全体の==は通常経路に無い）
    auto program = parse_source(R"(
struct Buf with Eq {
    int id;
    int[3] data;
}
)");
    const std::string expected =
        "impl Buf for Eq {\n"
        "    operator bool ==(Buf other) {\n"
        "        return self.id == other.id && self.data[0] == other.data[0] && "
        "self.data[1] == other.data[1] && self.data[2] == other.data[2];\n"
        "    }\n"
        "}\n";
    EXPECT_EQ(macro_expand::synthesize_derive_impls(program), expected);
}

TEST(DeriveExpansionTest, OrdSnapshot) {
    auto program = parse_source(R"(
struct Point with Ord {
    int x;
    int y;
}
)");
    const std::string expected =
        "impl Point for Ord {\n"
        "    operator bool <(Point other) {\n"
        "        if (self.x < other.x) {\n"
        "            return true;\n"
        "        }\n"
        "        if (self.x > other.x) {\n"
        "            return false;\n"
        "        }\n"
        "        if (self.y < other.y) {\n"
        "            return true;\n"
        "        }\n"
        "        if (self.y > other.y) {\n"
        "            return false;\n"
        "        }\n"
        "        return false;\n"
        "    }\n"
        "}\n";
    EXPECT_EQ(macro_expand::synthesize_derive_impls(program), expected);
}

TEST(DeriveExpansionTest, HashSnapshotWithNestedAndArray) {
    // FNV-1a（基数はi32ビットパターン維持の負数リテラル）。ネストはhash()再帰、配列は要素展開
    auto program = parse_source(R"(
struct Inner with Hash {
    int v;
}
)");
    const std::string expected =
        "impl Inner for Hash {\n"
        "    int hash() {\n"
        "        int h = -2128831035;\n"
        "        h = (h ^ (self.v as int)) * 16777619;\n"
        "        return h;\n"
        "    }\n"
        "}\n";
    EXPECT_EQ(macro_expand::synthesize_derive_impls(program), expected);
}

TEST(DeriveExpansionTest, DebugSnapshotUsesConcatenation) {
    // 挿入値が波括弧を含みうるネスト・文字列は直接連結、スカラは単独プレースホルダで整形
    auto program = parse_source(R"(
struct P with Debug {
    int x;
    string name;
}
)");
    const std::string expected =
        "impl P for Debug {\n"
        "    string debug() {\n"
        "        return \"P { x: \" + \"{self.x}\" + \", name: \" + self.name + \" }\";\n"
        "    }\n"
        "}\n";
    EXPECT_EQ(macro_expand::synthesize_derive_impls(program), expected);
}

TEST(DeriveExpansionTest, CssSnapshotSortsKeysAndAliases) {
    // kebab名昇順・boolは条件付き"key; "・to_css/is_css/isCssを提供
    auto program = parse_source(R"(
struct Style with Css {
    string font_size;
    bool bold;
}
)");
    const std::string synthesized = macro_expand::synthesize_derive_impls(program);
    EXPECT_NE(synthesized.find("if (self.bold) {"), std::string::npos);
    EXPECT_NE(synthesized.find("\"bold; \""), std::string::npos);
    EXPECT_NE(synthesized.find("\"font-size: \" + self.font_size + \"; \""), std::string::npos);
    EXPECT_NE(synthesized.find("bool isCss()"), std::string::npos);
    EXPECT_NE(synthesized.find("string to_css()"), std::string::npos);
    // bold（b...）がfont-size（f...）より先（kebab名昇順）
    EXPECT_LT(synthesized.find("bold; "), synthesized.find("font-size"));
}
