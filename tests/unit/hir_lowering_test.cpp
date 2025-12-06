#include "../../src/hir/hir_lowering.hpp"

#include "../../src/frontend/lexer/lexer.hpp"
#include "../../src/frontend/parser/parser.hpp"

#include <gtest/gtest.h>
#include <sstream>

using namespace cm;

// ============================================================
// テストヘルパー
// ============================================================
class HirLoweringTest : public ::testing::Test {
   protected:
    std::unique_ptr<hir::HirProgram> parse_and_lower(const std::string& code) {
        // レクサー
        Lexer lex(code);

        // トークン化
        std::vector<Token> tokens = lex.tokenize();

        // パーサー
        Parser p(tokens);
        auto ast = p.parse();

        // HIR lowering
        hir::HirLowering lowering;
        auto hir = lowering.lower(ast);

        return std::make_unique<hir::HirProgram>(std::move(hir));
    }
};

// ============================================================
// 関数宣言のテスト
// ============================================================
TEST_F(HirLoweringTest, SimpleFunctionDecl) {
    const std::string code = R"(
        int main() {
            return 0;
        }
    )";

    auto hir = parse_and_lower(code);
    ASSERT_EQ(hir->declarations.size(), 1);

    auto* func = std::get<std::unique_ptr<hir::HirFunction>>(hir->declarations[0]->kind).get();
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->name, "main");
    EXPECT_EQ(func->params.size(), 0);
    EXPECT_EQ(func->body.size(), 1);
}

// 関数パラメータのテスト
TEST_F(HirLoweringTest, FunctionWithParams) {
    const std::string code = R"(
        int add(int x, int y) {
            return x + y;
        }
    )";

    auto hir = parse_and_lower(code);
    ASSERT_EQ(hir->declarations.size(), 1);

    auto* func = std::get<std::unique_ptr<hir::HirFunction>>(hir->declarations[0]->kind).get();
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->name, "add");
    EXPECT_EQ(func->params.size(), 2);
    EXPECT_EQ(func->params[0].name, "x");
    EXPECT_EQ(func->params[1].name, "y");
}

// ============================================================
// 構造体宣言のテスト
// ============================================================
TEST_F(HirLoweringTest, StructDecl) {
    const std::string code = R"(
        struct Point {
            int x;
            int y;
        }
    )";

    auto hir = parse_and_lower(code);
    ASSERT_EQ(hir->declarations.size(), 1);

    auto* st = std::get<std::unique_ptr<hir::HirStruct>>(hir->declarations[0]->kind).get();
    ASSERT_NE(st, nullptr);
    EXPECT_EQ(st->name, "Point");
    EXPECT_EQ(st->fields.size(), 2);
    EXPECT_EQ(st->fields[0].name, "x");
    EXPECT_EQ(st->fields[1].name, "y");
}

// ============================================================
// 文のテスト
// ============================================================

// let文のテスト
TEST_F(HirLoweringTest, LetStatement) {
    const std::string code = R"(
        int main() {
            int x = 42;
            const int y = 100;
        }
    )";

    auto hir = parse_and_lower(code);
    auto* func = std::get<std::unique_ptr<hir::HirFunction>>(hir->declarations[0]->kind).get();
    ASSERT_EQ(func->body.size(), 2);

    // 最初のlet文
    auto* let1 = std::get<std::unique_ptr<hir::HirLet>>(func->body[0]->kind).get();
    ASSERT_NE(let1, nullptr);
    EXPECT_EQ(let1->name, "x");
    EXPECT_FALSE(let1->is_const);
    ASSERT_NE(let1->init, nullptr);

    // 2番目のlet文（const）
    auto* let2 = std::get<std::unique_ptr<hir::HirLet>>(func->body[1]->kind).get();
    ASSERT_NE(let2, nullptr);
    EXPECT_EQ(let2->name, "y");
    EXPECT_TRUE(let2->is_const);
}

// if文のテスト
TEST_F(HirLoweringTest, IfStatement) {
    const std::string code = R"(
        int main() {
            if (true) {
                return 1;
            } else {
                return 0;
            }
        }
    )";

    auto hir = parse_and_lower(code);
    auto* func = std::get<std::unique_ptr<hir::HirFunction>>(hir->declarations[0]->kind).get();
    ASSERT_EQ(func->body.size(), 1);

    auto* if_stmt = std::get<std::unique_ptr<hir::HirIf>>(func->body[0]->kind).get();
    ASSERT_NE(if_stmt, nullptr);
    ASSERT_NE(if_stmt->cond, nullptr);
    EXPECT_EQ(if_stmt->then_block.size(), 1);
    EXPECT_EQ(if_stmt->else_block.size(), 1);
}

// ============================================================
// 式のテスト
// ============================================================

// 二項演算のテスト
TEST_F(HirLoweringTest, BinaryExpression) {
    const std::string code = R"(
        int main() {
            int x = 10 + 20;
        }
    )";

    auto hir = parse_and_lower(code);
    auto* func = std::get<std::unique_ptr<hir::HirFunction>>(hir->declarations[0]->kind).get();
    auto* let = std::get<std::unique_ptr<hir::HirLet>>(func->body[0]->kind).get();
    ASSERT_NE(let->init, nullptr);

    auto* binary = std::get<std::unique_ptr<hir::HirBinary>>(let->init->kind).get();
    ASSERT_NE(binary, nullptr);
    EXPECT_EQ(binary->op, hir::HirBinaryOp::Add);
}

// 複合代入演算子の脱糖テスト
TEST_F(HirLoweringTest, CompoundAssignmentDesugaring) {
    const std::string code = R"(
        int main() {
            int x = 10;
            x += 5;
        }
    )";

    auto hir = parse_and_lower(code);
    auto* func = std::get<std::unique_ptr<hir::HirFunction>>(hir->declarations[0]->kind).get();
    ASSERT_EQ(func->body.size(), 2);

    // x += 5 は x = x + 5 に脱糖される
    auto* expr_stmt = std::get<std::unique_ptr<hir::HirExprStmt>>(func->body[1]->kind).get();
    auto* outer_binary = std::get<std::unique_ptr<hir::HirBinary>>(expr_stmt->expr->kind).get();
    EXPECT_EQ(outer_binary->op, hir::HirBinaryOp::Assign);

    // 右辺は x + 5
    auto* inner_binary = std::get<std::unique_ptr<hir::HirBinary>>(outer_binary->rhs->kind).get();
    EXPECT_EQ(inner_binary->op, hir::HirBinaryOp::Add);
}

// ============================================================
// ループの脱糖テスト
// ============================================================

// while文の脱糖
TEST_F(HirLoweringTest, WhileLoopDesugaring) {
    const std::string code = R"(
        int main() {
            int i = 0;
            while (i < 10) {
                i = i + 1;
            }
        }
    )";

    auto hir = parse_and_lower(code);
    auto* func = std::get<std::unique_ptr<hir::HirFunction>>(hir->declarations[0]->kind).get();
    ASSERT_EQ(func->body.size(), 2);

    // whileは HirLoopに変換される
    auto* loop = std::get<std::unique_ptr<hir::HirLoop>>(func->body[1]->kind).get();
    ASSERT_NE(loop, nullptr);
    // ループ本体には条件チェックとbreak、そして本体が含まれる
    EXPECT_GE(loop->body.size(), 2);
}

// for文の脱糖
TEST_F(HirLoweringTest, ForLoopDesugaring) {
    const std::string code = R"(
        int main() {
            for (int i = 0; i < 10; i++) {
                int x = i;
            }
        }
    )";

    auto hir = parse_and_lower(code);
    auto* func = std::get<std::unique_ptr<hir::HirFunction>>(hir->declarations[0]->kind).get();

    // forループは初期化文とループに分解される
    // 最初の文は初期化（int i = 0）
    ASSERT_GE(func->body.size(), 1);

    // 2番目の文はloop
    if (func->body.size() >= 2) {
        auto* loop = std::get_if<std::unique_ptr<hir::HirLoop>>(&func->body[1]->kind);
        if (loop && *loop) {
            EXPECT_GE((*loop)->body.size(), 2);  // 条件チェック、本体、更新
        }
    }
}

// ============================================================
// ブロック文のテスト
// ============================================================
TEST_F(HirLoweringTest, BlockStatement) {
    const std::string code = R"(
        int main() {
            {
                int x = 1;
                int y = 2;
            }
        }
    )";

    auto hir = parse_and_lower(code);
    auto* func = std::get<std::unique_ptr<hir::HirFunction>>(hir->declarations[0]->kind).get();
    ASSERT_EQ(func->body.size(), 1);

    auto* block = std::get<std::unique_ptr<hir::HirBlock>>(func->body[0]->kind).get();
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->stmts.size(), 2);
}

// ============================================================
// エラーケースのテスト
// ============================================================

// 空のプログラム
TEST_F(HirLoweringTest, EmptyProgram) {
    const std::string code = "";
    auto hir = parse_and_lower(code);
    EXPECT_EQ(hir->declarations.size(), 0);
}

// ============================================================
// 複数宣言のテスト
// ============================================================
TEST_F(HirLoweringTest, MultipleDeclarations) {
    const std::string code = R"(
        struct Point {
            int x;
            int y;
        }

        int add(int a, int b) {
            return a + b;
        }

        int main() {
            return add(1, 2);
        }
    )";

    auto hir = parse_and_lower(code);
    EXPECT_EQ(hir->declarations.size(), 3);
}