#include "../../src/internal/syntax/ast/decl.hpp"
#include "../../src/internal/syntax/ast/nested.hpp"
#include "../../src/internal/syntax/lexer/lexer.hpp"
#include "../../src/internal/syntax/parser/parser.hpp"

#include <fstream>
#include <gtest/gtest.h>
#include <sstream>

using namespace cm;

// ============================================================
// ネスト型宣言hoistパスの回帰テスト
// ============================================================
// Cmソースは tests/regression/cases/nested_hoist/ の .cm ファイルに分割
class NestedHoistTest : public ::testing::Test {
   protected:
    std::string load_case(const std::string& name) {
        std::string path = std::string(CM_NESTED_HOIST_CASE_DIR) + "/" + name + ".cm";
        std::ifstream ifs(path);
        EXPECT_TRUE(ifs.is_open()) << "ケースファイルを読み込めません: " << path;
        std::stringstream ss;
        ss << ifs.rdbuf();
        return ss.str();
    }

    ast::Program parse_and_hoist(const std::string& name) {
        // Tokenはソース文字列へのstring_viewを保持するため、パース完了までソースを生存させる
        std::string code = load_case(name);
        Lexer lex(code);
        std::vector<Token> tokens = lex.tokenize();
        Parser p(tokens);
        auto program = p.parse();
        std::string diag_text;
        for (const auto& d : p.diagnostics()) {
            diag_text += d.message + "\n";
        }
        EXPECT_FALSE(p.has_errors()) << "パースエラーが発生しました: " << name << "\n" << diag_text;
        ast::hoist_nested_types(program);
        return program;
    }

    static const ast::StructDecl* struct_at(const ast::Program& prog, size_t i) {
        return const_cast<ast::Decl&>(*prog.declarations[i]).as<ast::StructDecl>();
    }

    static const ast::EnumDecl* enum_at(const ast::Program& prog, size_t i) {
        return const_cast<ast::Decl&>(*prog.declarations[i]).as<ast::EnumDecl>();
    }
};

// struct内structがOuter::Inner名でトップレベルへ展開され、フィールド型参照が書き換わる
TEST_F(NestedHoistTest, StructInStruct) {
    auto prog = parse_and_hoist("struct_hoist");
    ASSERT_EQ(prog.declarations.size(), 3u);  // Outer::Inner, Outer, main

    const auto* inner = struct_at(prog, 0);
    ASSERT_NE(inner, nullptr);
    EXPECT_EQ(inner->name, "Outer::Inner");
    EXPECT_TRUE(inner->nested_types.empty());

    const auto* outer = struct_at(prog, 1);
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ(outer->name, "Outer");
    EXPECT_TRUE(outer->nested_types.empty());
    ASSERT_EQ(outer->fields.size(), 1u);
    ASSERT_NE(outer->fields[0].type, nullptr);
    EXPECT_EQ(outer->fields[0].type->name, "Outer::Inner");
}

// enum内enumは値スロットを消費せず、内側は独立enumとして展開される
TEST_F(NestedHoistTest, EnumInEnum) {
    auto prog = parse_and_hoist("enum_hoist");
    ASSERT_EQ(prog.declarations.size(), 3u);  // Category::Sub, Category, main

    const auto* sub = enum_at(prog, 0);
    ASSERT_NE(sub, nullptr);
    EXPECT_EQ(sub->name, "Category::Sub");
    ASSERT_EQ(sub->members.size(), 2u);
    EXPECT_EQ(sub->members[0].name, "MEM");
    EXPECT_EQ(sub->members[0].value.value_or(-1), 10);
    EXPECT_EQ(sub->members[1].value.value_or(-1), 11);

    const auto* outer = enum_at(prog, 1);
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ(outer->name, "Category");
    ASSERT_EQ(outer->members.size(), 2u);
    EXPECT_EQ(outer->members[0].name, "A");
    EXPECT_EQ(outer->members[0].value.value_or(-1), 5);
    EXPECT_EQ(outer->members[1].value.value_or(-1), 6);
}

// C/C++スタイル宣言子: 宣言子はフィールドになり、匿名型は__anon_名合成のうえOuter::__anon_名へ展開される。トップレベル宣言子はグローバル変数を合成する
TEST_F(NestedHoistTest, CStyleDeclarators) {
    auto prog = parse_and_hoist("declarator_hoist");
    // Outer::Inner, Outer::__anon_pair, Outer, __anon_G, (global G), main
    ASSERT_EQ(prog.declarations.size(), 6u);

    const auto* inner = struct_at(prog, 0);
    ASSERT_NE(inner, nullptr);
    EXPECT_EQ(inner->name, "Outer::Inner");

    const auto* pair = struct_at(prog, 1);
    ASSERT_NE(pair, nullptr);
    EXPECT_EQ(pair->name, "Outer::__anon_pair");

    const auto* outer = struct_at(prog, 2);
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ(outer->name, "Outer");
    ASSERT_EQ(outer->fields.size(), 3u);
    EXPECT_EQ(outer->fields[0].name, "a");
    EXPECT_EQ(outer->fields[0].type->name, "Outer::Inner");
    EXPECT_EQ(outer->fields[1].name, "b");
    EXPECT_EQ(outer->fields[1].type->name, "Outer::Inner");
    EXPECT_EQ(outer->fields[2].name, "pair");
    EXPECT_EQ(outer->fields[2].type->name, "Outer::__anon_pair");

    const auto* anon_g = struct_at(prog, 3);
    ASSERT_NE(anon_g, nullptr);
    EXPECT_EQ(anon_g->name, "__anon_G");

    auto* gvar = const_cast<ast::Decl&>(*prog.declarations[4]).as<ast::GlobalVarDecl>();
    ASSERT_NE(gvar, nullptr);
    EXPECT_EQ(gvar->name, "G");
    ASSERT_NE(gvar->type, nullptr);
    EXPECT_EQ(gvar->type->name, "__anon_G");
    EXPECT_EQ(gvar->init_expr, nullptr);
}

// 3段ネストは内側優先の順（Outer::Mid::Inner → Outer::Mid → Outer）で展開され、部分修飾参照も書き換わる
TEST_F(NestedHoistTest, DeepNesting) {
    auto prog = parse_and_hoist("deep_hoist");
    ASSERT_EQ(prog.declarations.size(), 4u);  // Outer::Mid::Inner, Outer::Mid, Outer, main

    const auto* inner = struct_at(prog, 0);
    ASSERT_NE(inner, nullptr);
    EXPECT_EQ(inner->name, "Outer::Mid::Inner");

    const auto* mid = struct_at(prog, 1);
    ASSERT_NE(mid, nullptr);
    EXPECT_EQ(mid->name, "Outer::Mid");
    ASSERT_EQ(mid->fields.size(), 1u);
    EXPECT_EQ(mid->fields[0].type->name, "Outer::Mid::Inner");

    const auto* outer = struct_at(prog, 2);
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ(outer->name, "Outer");
    ASSERT_EQ(outer->fields.size(), 2u);
    // 部分修飾参照 Mid::Inner も Outer::Mid::Inner へ書き換わる
    EXPECT_EQ(outer->fields[0].type->name, "Outer::Mid");
    EXPECT_EQ(outer->fields[1].type->name, "Outer::Mid::Inner");
}
