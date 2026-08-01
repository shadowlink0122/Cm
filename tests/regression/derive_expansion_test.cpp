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

TEST(DeriveExpansionTest, GenericStructIsNotExpanded) {
    // 総称演算子implのモノモーフ化が未対応のため、ジェネリック構造体は手組み生成経路に残す
    auto program = parse_source(R"(
struct Pair<T, U> with Eq {
    T first;
    U second;
}
)");
    EXPECT_EQ(macro_expand::synthesize_derive_impls(program), "");
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

TEST(DeriveExpansionTest, OtherTraitsRemainForMirPath) {
    // 未移行トレイト（Ord等）はauto_implsに残り、従来の手組みMIR経路が処理する
    auto program = parse_source(R"(
struct Point with Eq, Ord {
    int x;
}
)");
    macro_expand::expand_derives(program);
    auto* st = program.declarations[0]->as<ast::StructDecl>();
    ASSERT_NE(st, nullptr);
    ASSERT_EQ(st->auto_impls.size(), 1u);
    EXPECT_EQ(st->auto_impls[0], "Ord");
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
