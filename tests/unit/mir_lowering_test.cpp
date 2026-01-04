#include "../../src/frontend/lexer/lexer.hpp"
#include "../../src/frontend/parser/parser.hpp"
#include "../../src/hir/lowering/lowering.hpp"
#include "../../src/mir/lowering/lowering.hpp"

#include <gtest/gtest.h>
#include <memory>
#include <sstream>

using namespace cm;

// ============================================================
// テストヘルパー
// ============================================================
class MirLoweringTest : public ::testing::Test {
   protected:
    std::unique_ptr<mir::MirProgram> parse_and_lower(const std::string& code) {
        // レクサー → パーサー → HIR → MIR
        Lexer lex(code);
        std::vector<Token> tokens = lex.tokenize();
        Parser p(tokens);
        auto ast = p.parse();

        hir::HirLowering hir_lowering;
        auto hir = hir_lowering.lower(ast);

        mir::MirLowering mir_lowering;
        auto mir = mir_lowering.lower(hir);

        return std::make_unique<mir::MirProgram>(std::move(mir));
    }

    // 基本ブロックの数をカウント
    size_t count_blocks(const mir::MirFunction& func) { return func.basic_blocks.size(); }

    // 特定のブロックの文の数をカウント
    size_t count_statements(const mir::MirFunction& func, mir::BlockId block_id) {
        if (auto* block = func.basic_blocks[block_id].get()) {
            return block->statements.size();
        }
        return 0;
    }
};

// ============================================================
// 基本的な関数のテスト
// ============================================================
TEST_F(MirLoweringTest, SimpleFunctionWithReturn) {
    const std::string code = R"(
        int main() {
            return 42;
        }
    )";

    auto mir = parse_and_lower(code);
    ASSERT_EQ(mir->functions.size(), 1u);

    const auto& func = *mir->functions[0];
    EXPECT_EQ(func.name, "main");

    // エントリーブロック (bb0) が存在
    EXPECT_GE(func.basic_blocks.size(), 1u);

    // return文があるはず
    auto* entry_block = func.basic_blocks[0].get();
    ASSERT_NE(entry_block, nullptr);
    ASSERT_NE(entry_block->terminator, nullptr);
    EXPECT_EQ(entry_block->terminator->kind, mir::MirTerminator::Return);
}

// ============================================================
// 変数宣言のテスト
// ============================================================
TEST_F(MirLoweringTest, VariableDeclaration) {
    const std::string code = R"(
        int main() {
            int x = 10;
            int y = x + 5;
            return y;
        }
    )";

    auto mir = parse_and_lower(code);
    const auto& func = *mir->functions[0];

    // ローカル変数が作成されているか
    EXPECT_GE(func.locals.size(), 3u);  // _0(戻り値), x, y + 一時変数

    // StorageLive文があるはず
    auto* entry_block = func.basic_blocks[0].get();
    bool has_storage_live = false;
    for (const auto& stmt : entry_block->statements) {
        if (stmt->kind == mir::MirStatement::StorageLive) {
            has_storage_live = true;
            break;
        }
    }
    EXPECT_TRUE(has_storage_live);
}

// ============================================================
// if文のテスト（CFG構築）
// ============================================================
TEST_F(MirLoweringTest, IfStatementCFG) {
    const std::string code = R"(
        int main() {
            int x = 10;
            if (x > 5) {
                x = 20;
            } else {
                x = 30;
            }
            return x;
        }
    )";

    auto mir = parse_and_lower(code);
    const auto& func = *mir->functions[0];

    // if文により複数のブロックが生成される
    // bb0: entry, bb1: then, bb2: else, bb3: merge
    EXPECT_GE(func.basic_blocks.size(), 4u);

    // エントリーブロックはSwitchInt終端を持つはず
    auto* entry_block = func.basic_blocks[0].get();
    if (entry_block->terminator) {
        EXPECT_EQ(entry_block->terminator->kind, mir::MirTerminator::SwitchInt);
    }
}

// ============================================================
// 二項演算の分解テスト
// ============================================================
TEST_F(MirLoweringTest, ComplexExpressionDecomposition) {
    const std::string code = R"(
        int main() {
            int x = 1 + 2 * 3;
            return x;
        }
    )";

    auto mir = parse_and_lower(code);
    const auto& func = *mir->functions[0];

    // 複雑な式は一時変数を使って分解される
    auto* entry_block = func.basic_blocks[0].get();

    // 複数の代入文があるはず（2*3の結果、1+の結果）
    int assign_count = 0;
    for (const auto& stmt : entry_block->statements) {
        if (stmt->kind == mir::MirStatement::Assign) {
            assign_count++;
        }
    }
    EXPECT_GE(assign_count, 2);  // 少なくとも2つの計算
}

// ============================================================
// ループのテスト
// ============================================================
TEST_F(MirLoweringTest, LoopStructure) {
    const std::string code = R"(
        int main() {
            int i = 0;
            while (i < 10) {
                i = i + 1;
            }
            return i;
        }
    )";

    auto mir = parse_and_lower(code);
    const auto& func = *mir->functions[0];

    // ループは複数のブロックを生成
    // bb0: entry, bb1: loop_header, bb2: loop_exit
    EXPECT_GE(func.basic_blocks.size(), 3u);

    // ループヘッダはgotoで自己参照するはず
    bool has_back_edge = false;
    for (const auto& block : func.basic_blocks) {
        if (block->terminator && block->terminator->kind == mir::MirTerminator::Goto) {
            auto& goto_data = std::get<mir::MirTerminator::GotoData>(block->terminator->data);
            if (goto_data.target <= block->id) {  // 後方へのジャンプ
                has_back_edge = true;
                break;
            }
        }
    }
    EXPECT_TRUE(has_back_edge);
}

// ============================================================
// 三項演算子のテスト
// ============================================================
TEST_F(MirLoweringTest, TernaryOperator) {
    const std::string code = R"(
        int main() {
            int x = 10;
            int y = x > 5 ? 100 : 200;
            return y;
        }
    )";

    auto mir = parse_and_lower(code);
    const auto& func = *mir->functions[0];

    // 三項演算子は分岐構造を生成
    EXPECT_GE(func.basic_blocks.size(), 4u);  // entry, then, else, merge
}

// ============================================================
// CFGの接続テスト
// ============================================================
TEST_F(MirLoweringTest, CFGConnectivity) {
    const std::string code = R"(
        int main() {
            int x = 10;
            if (true) {
                x = 20;
            }
            return x;
        }
    )";

    auto mir = parse_and_lower(code);
    auto& func = *mir->functions[0];

    // CFGを構築
    func.build_cfg();

    // 各ブロックのsuccessor/predecessorが正しく設定されているか
    for (const auto& block : func.basic_blocks) {
        // successorがある場合、そのブロックのpredecessorに自分が含まれているはず
        for (mir::BlockId succ_id : block->successors) {
            if (succ_id < func.basic_blocks.size()) {
                const auto& succ_block = func.basic_blocks[succ_id];
                auto it = std::find(succ_block->predecessors.begin(),
                                    succ_block->predecessors.end(), block->id);
                EXPECT_NE(it, succ_block->predecessors.end());
            }
        }
    }
}

// ============================================================
// 空の関数のテスト
// ============================================================
TEST_F(MirLoweringTest, EmptyFunction) {
    const std::string code = R"(
        void main() {
        }
    )";

    auto mir = parse_and_lower(code);
    const auto& func = *mir->functions[0];

    // 少なくとも1つのブロック（エントリー）
    EXPECT_GE(func.basic_blocks.size(), 1u);

    // Return終端を持つ
    auto* entry = func.basic_blocks[0].get();
    ASSERT_NE(entry->terminator, nullptr);
    EXPECT_EQ(entry->terminator->kind, mir::MirTerminator::Return);
}

// ============================================================
// ローカル変数のスコープテスト
// ============================================================
TEST_F(MirLoweringTest, LocalVariableScope) {
    const std::string code = R"(
        int main() {
            {
                int x = 10;
            }
            {
                int y = 20;
            }
            return 0;
        }
    )";

    auto mir = parse_and_lower(code);
    const auto& func = *mir->functions[0];

    // ブロックスコープ内の変数も正しく処理される
    EXPECT_GE(func.locals.size(), 3u);  // _0, x, y
}

// ============================================================
// 複数の関数のテスト
// ============================================================
TEST_F(MirLoweringTest, MultipleFunctions) {
    const std::string code = R"(
        int add(int a, int b) {
            return a + b;
        }

        int main() {
            return add(1, 2);
        }
    )";

    auto mir = parse_and_lower(code);
    EXPECT_EQ(mir->functions.size(), 2u);

    // 各関数が正しく変換されているか
    for (const auto& func : mir->functions) {
        EXPECT_FALSE(func->name.empty());
        EXPECT_GE(func->basic_blocks.size(), 1u);
    }
}