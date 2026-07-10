#include "../../src/frontend/lexer/lexer.hpp"
#include "../../src/frontend/parser/parser.hpp"
#include "../../src/hir/lowering/lowering.hpp"

#include <fstream>
#include <gtest/gtest.h>
#include <memory>
#include <sstream>

using namespace cm;

// ============================================================
// HIR lowering 統合テスト
// ============================================================
// Cmソースは tests/integration/cases/hir_lowering/ の .cm ファイルに分割
class HirLoweringTest : public ::testing::Test {
   protected:
    std::string load_case(const std::string& name) {
        std::string path = std::string(CM_HIR_CASE_DIR) + "/" + name + ".cm";
        std::ifstream ifs(path);
        EXPECT_TRUE(ifs.is_open()) << "ケースファイルを読み込めません: " << path;
        std::stringstream ss;
        ss << ifs.rdbuf();
        return ss.str();
    }

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

    std::unique_ptr<hir::HirProgram> lower_case(const std::string& name) {
        return parse_and_lower(load_case(name));
    }
};

// ============================================================
// 関数宣言のテスト
// ============================================================
TEST_F(HirLoweringTest, SimpleFunctionDecl) {
    auto hir = lower_case("simple_function_decl");
    ASSERT_EQ(hir->declarations.size(), 1u);

    auto* func = std::get<std::unique_ptr<hir::HirFunction>>(hir->declarations[0]->kind).get();
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->name, "main");
    EXPECT_EQ(func->params.size(), 0u);
    EXPECT_EQ(func->body.size(), 1u);
}

// 関数パラメータのテスト
TEST_F(HirLoweringTest, FunctionWithParams) {
    auto hir = lower_case("function_with_params");
    ASSERT_EQ(hir->declarations.size(), 1u);

    auto* func = std::get<std::unique_ptr<hir::HirFunction>>(hir->declarations[0]->kind).get();
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->name, "add");
    EXPECT_EQ(func->params.size(), 2u);
    EXPECT_EQ(func->params[0].name, "x");
    EXPECT_EQ(func->params[1].name, "y");
}

// ============================================================
// 構造体宣言のテスト
// ============================================================
TEST_F(HirLoweringTest, StructDecl) {
    auto hir = lower_case("struct_decl");
    ASSERT_EQ(hir->declarations.size(), 1u);

    auto* st = std::get<std::unique_ptr<hir::HirStruct>>(hir->declarations[0]->kind).get();
    ASSERT_NE(st, nullptr);
    EXPECT_EQ(st->name, "Point");
    EXPECT_EQ(st->fields.size(), 2u);
    EXPECT_EQ(st->fields[0].name, "x");
    EXPECT_EQ(st->fields[1].name, "y");
}

// ============================================================
// 文のテスト
// ============================================================

// let文のテスト
TEST_F(HirLoweringTest, LetStatement) {
    auto hir = lower_case("let_statement");
    auto* func = std::get<std::unique_ptr<hir::HirFunction>>(hir->declarations[0]->kind).get();
    ASSERT_EQ(func->body.size(), 2u);

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
    auto hir = lower_case("if_statement");
    auto* func = std::get<std::unique_ptr<hir::HirFunction>>(hir->declarations[0]->kind).get();
    ASSERT_EQ(func->body.size(), 1u);

    auto* if_stmt = std::get<std::unique_ptr<hir::HirIf>>(func->body[0]->kind).get();
    ASSERT_NE(if_stmt, nullptr);
    ASSERT_NE(if_stmt->cond, nullptr);
    EXPECT_EQ(if_stmt->then_block.size(), 1u);
    EXPECT_EQ(if_stmt->else_block.size(), 1u);
}

// ============================================================
// 式のテスト
// ============================================================

// 二項演算のテスト
TEST_F(HirLoweringTest, BinaryExpression) {
    auto hir = lower_case("binary_expression");
    auto* func = std::get<std::unique_ptr<hir::HirFunction>>(hir->declarations[0]->kind).get();
    auto* let = std::get<std::unique_ptr<hir::HirLet>>(func->body[0]->kind).get();
    ASSERT_NE(let->init, nullptr);

    auto* binary = std::get<std::unique_ptr<hir::HirBinary>>(let->init->kind).get();
    ASSERT_NE(binary, nullptr);
    EXPECT_EQ(binary->op, hir::HirBinaryOp::Add);
}

// 複合代入演算子の脱糖テスト
TEST_F(HirLoweringTest, CompoundAssignmentDesugaring) {
    auto hir = lower_case("compound_assignment");
    auto* func = std::get<std::unique_ptr<hir::HirFunction>>(hir->declarations[0]->kind).get();
    ASSERT_EQ(func->body.size(), 2u);

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
    auto hir = lower_case("while_loop_desugaring");
    auto* func = std::get<std::unique_ptr<hir::HirFunction>>(hir->declarations[0]->kind).get();
    ASSERT_EQ(func->body.size(), 2u);

    // whileは HirWhileに変換される
    auto* while_stmt = std::get<std::unique_ptr<hir::HirWhile>>(func->body[1]->kind).get();
    ASSERT_NE(while_stmt, nullptr);
    // 条件式が存在する
    ASSERT_NE(while_stmt->cond, nullptr);
    // ループ本体が存在する
    EXPECT_GE(while_stmt->body.size(), 1u);
}

// for文の脱糖
TEST_F(HirLoweringTest, ForLoopDesugaring) {
    auto hir = lower_case("for_loop_desugaring");
    auto* func = std::get<std::unique_ptr<hir::HirFunction>>(hir->declarations[0]->kind).get();

    // forループはHirForとして保持される
    ASSERT_GE(func->body.size(), 1u);

    // for文を取得
    auto* for_stmt = std::get_if<std::unique_ptr<hir::HirFor>>(&func->body[0]->kind);
    ASSERT_NE(for_stmt, nullptr);
    ASSERT_NE(*for_stmt, nullptr);
    // 初期化、条件、更新が存在する
    EXPECT_NE((*for_stmt)->init, nullptr);
    EXPECT_NE((*for_stmt)->cond, nullptr);
    EXPECT_NE((*for_stmt)->update, nullptr);
    // ループ本体が存在する
    EXPECT_GE((*for_stmt)->body.size(), 1u);
}

// ============================================================
// ブロック文のテスト
// ============================================================
TEST_F(HirLoweringTest, BlockStatement) {
    auto hir = lower_case("block_statement");
    auto* func = std::get<std::unique_ptr<hir::HirFunction>>(hir->declarations[0]->kind).get();
    ASSERT_EQ(func->body.size(), 1u);

    auto* block = std::get<std::unique_ptr<hir::HirBlock>>(func->body[0]->kind).get();
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->stmts.size(), 2u);
}

// ============================================================
// エラーケースのテスト
// ============================================================

// 空のプログラム
TEST_F(HirLoweringTest, EmptyProgram) {
    const std::string code = "";
    auto hir = parse_and_lower(code);
    EXPECT_EQ(hir->declarations.size(), 0u);
}

// ============================================================
// 複数宣言のテスト
// ============================================================
TEST_F(HirLoweringTest, MultipleDeclarations) {
    auto hir = lower_case("multiple_declarations");
    EXPECT_EQ(hir->declarations.size(), 3u);
}
