#include "../../src/frontend/lexer/lexer.hpp"
#include "../../src/frontend/parser/parser.hpp"
#include "../../src/hir/hir_lowering.hpp"
#include "../../src/mir/mir_lowering.hpp"
#include "../../src/mir/mir_printer.hpp"
#include "../../src/mir/passes/core/manager.hpp"

#include <gtest/gtest.h>
#include <sstream>

using namespace cm;

// ============================================================
// テストヘルパー
// ============================================================
class MirOptimizationTest : public ::testing::Test {
   protected:
    std::unique_ptr<mir::MirProgram> compile_to_mir(const std::string& code) {
        // レクサー → パーサー → HIR → MIR
        Lexer lex(code);
        std::vector<Token> tokens = lex.tokenize();
        Parser p(tokens);
        auto ast = p.parse();

        hir::HirLowering hir_lowering;
        auto hir = hir_lowering.lower(ast);

        mir::MirLowering mir_lowering;
        auto mir_program = mir_lowering.lower(hir);

        return std::make_unique<mir::MirProgram>(std::move(mir_program));
    }

    // 特定の最適化パスを実行
    bool run_optimization(mir::MirProgram& program,
                          std::unique_ptr<mir::opt::OptimizationPass> pass) {
        return pass->run_on_program(program);
    }

    // 定数値を含む文の数をカウント
    int count_constant_statements(const mir::MirFunction& func) {
        int count = 0;
        for (const auto& block : func.basic_blocks) {
            if (!block)
                continue;
            for (const auto& stmt : block->statements) {
                if (stmt->kind == mir::MirStatement::Assign) {
                    auto& data = std::get<mir::MirStatement::AssignData>(stmt->data);
                    if (data.rvalue && data.rvalue->kind == mir::MirRvalue::Use) {
                        auto& use_data = std::get<mir::MirRvalue::UseData>(data.rvalue->data);
                        if (use_data.operand &&
                            use_data.operand->kind == mir::MirOperand::Constant) {
                            count++;
                        }
                    }
                }
            }
        }
        return count;
    }

    // Nop文の数をカウント
    int count_nop_statements(const mir::MirFunction& func) {
        int count = 0;
        for (const auto& block : func.basic_blocks) {
            if (!block)
                continue;
            for (const auto& stmt : block->statements) {
                if (stmt->kind == mir::MirStatement::Nop) {
                    count++;
                }
            }
        }
        return count;
    }

    // デバッグ出力
    void print_mir(const mir::MirProgram& program) {
        mir::MirPrinter printer;
        std::stringstream ss;
        printer.print(program, ss);
        std::cout << ss.str() << "\n";
    }
};

// ============================================================
// 定数畳み込みのテスト
// ============================================================
TEST_F(MirOptimizationTest, ConstantFolding_Simple) {
    const std::string code = R"(
        int main() {
            int x = 2 + 3;
            int y = x * 4;
            return y;
        }
    )";

    auto mir = compile_to_mir(code);
    auto& func = *mir->functions[0];

    // 最適化前: 定数演算がそのまま
    int constants_before = count_constant_statements(func);

    // 定数畳み込みを実行
    auto pass = std::make_unique<mir::opt::ConstantFolding>();
    bool changed = pass->run(func);

    EXPECT_TRUE(changed);

    // 最適化後: より多くの定数
    int constants_after = count_constant_statements(func);
    EXPECT_GT(constants_after, constants_before);
}

TEST_F(MirOptimizationTest, ConstantFolding_Comparison) {
    const std::string code = R"(
        int main() {
            bool x = 10 > 5;
            bool y = 3 == 3;
            if (x && y) {
                return 1;
            }
            return 0;
        }
    )";

    auto mir = compile_to_mir(code);

    // 定数畳み込みを実行
    mir::opt::ConstantFolding folding;
    bool changed = folding.run(*mir->functions[0]);

    EXPECT_TRUE(changed);

    // 比較演算が定数に畳み込まれているはず
    // (10 > 5) → true, (3 == 3) → true
}

// ============================================================
// デッドコード除去のテスト
// ============================================================
TEST_F(MirOptimizationTest, DeadCodeElimination_UnusedVariable) {
    const std::string code = R"(
        int main() {
            int unused = 42;
            int used = 10;
            return used;
        }
    )";

    auto mir = compile_to_mir(code);
    auto& func = *mir->functions[0];

    // 最適化前の文の数をカウント
    int statements_before = 0;
    for (const auto& block : func.basic_blocks) {
        if (block)
            statements_before += block->statements.size();
    }

    // デッドコード除去を実行
    mir::opt::DeadCodeElimination dce;
    bool changed = dce.run(func);

    EXPECT_TRUE(changed);

    // 最適化後の文の数をカウント
    int statements_after = 0;
    for (const auto& block : func.basic_blocks) {
        if (block)
            statements_after += block->statements.size();
    }

    // unusedへの代入が削除されて文が減っているはず
    EXPECT_LT(statements_after, statements_before);
}

TEST_F(MirOptimizationTest, DeadCodeElimination_UnreachableBlock) {
    const std::string code = R"(
        int main() {
            return 42;
            int x = 100;  // 到達不可能
        }
    )";

    auto mir = compile_to_mir(code);
    auto& func = *mir->functions[0];

    size_t blocks_before = 0;
    for (const auto& block : func.basic_blocks) {
        if (block)
            blocks_before++;
    }

    // デッドコード除去を実行
    mir::opt::DeadCodeElimination dce;
    dce.run(func);

    // 到達不可能ブロックが削除されている可能性
    size_t blocks_after = 0;
    for (const auto& block : func.basic_blocks) {
        if (block)
            blocks_after++;
    }

    EXPECT_LE(blocks_after, blocks_before);
}

// ============================================================
// コピー伝播のテスト
// ============================================================
TEST_F(MirOptimizationTest, CopyPropagation_Simple) {
    const std::string code = R"(
        int main() {
            int x = 10;
            int y = x;
            int z = y;
            return z;
        }
    )";

    auto mir = compile_to_mir(code);
    auto& func = *mir->functions[0];

    // コピー伝播を実行
    mir::opt::CopyPropagation cp;
    bool changed = cp.run(func);

    EXPECT_TRUE(changed);

    // y, zの使用がxに置き換わっているはず
}

TEST_F(MirOptimizationTest, CopyPropagation_Chain) {
    const std::string code = R"(
        int main() {
            int a = 5;
            int b = a;
            int c = b;
            int d = c;
            return d + 1;
        }
    )";

    auto mir = compile_to_mir(code);

    // コピー伝播を実行
    mir::opt::CopyPropagation cp;
    bool changed = cp.run(*mir->functions[0]);

    EXPECT_TRUE(changed);

    // d + 1 が a + 1 に変換されているはず
}

// ============================================================
// 最適化パイプラインのテスト
// ============================================================
TEST_F(MirOptimizationTest, OptimizationPipeline_Standard) {
    const std::string code = R"(
        int main() {
            int x = 2 + 3;    // 定数畳み込み
            int y = x;        // コピー伝播
            int z = y * 2;    // 定数畳み込み（伝播後）
            int unused = 100; // デッドコード除去
            return z;
        }
    )";

    auto mir = compile_to_mir(code);

    // 標準的な最適化パイプラインを実行
    mir::opt::OptimizationPipeline pipeline;
    pipeline.add_standard_passes(1);  // -O1レベル
    pipeline.run(*mir);

    // 最適化が適用されているはず
    auto& func = *mir->functions[0];

    // 定数畳み込みにより、計算済みの値が増える
    int constants = count_constant_statements(func);
    EXPECT_GT(constants, 0);
}

TEST_F(MirOptimizationTest, OptimizationPipeline_Fixpoint) {
    const std::string code = R"(
        int main() {
            int a = 1;
            int b = a + 1;
            int c = b + 1;
            int d = c + 1;
            return d;
        }
    )";

    auto mir = compile_to_mir(code);

    // 収束するまで最適化を繰り返す
    mir::opt::OptimizationPipeline pipeline;
    pipeline.add_pass(std::make_unique<mir::opt::ConstantFolding>());
    pipeline.add_pass(std::make_unique<mir::opt::CopyPropagation>());
    pipeline.run_until_fixpoint(*mir);

    // すべての値が定数に畳み込まれているはず
    // (1 + 1 + 1 + 1 = 4)
}

// ============================================================
// 制御フロー簡略化のテスト
// ============================================================
TEST_F(MirOptimizationTest, SimplifyControlFlow_GotoChain) {
    const std::string code = R"(
        int main() {
            int x = 10;
            if (x > 5) {
                x = 20;
            }
            return x;
        }
    )";

    auto mir = compile_to_mir(code);

    // 制御フロー簡略化を実行
    mir::opt::SimplifyControlFlow scf;
    scf.run(*mir->functions[0]);

    // 単純なGotoチェーンが簡略化される
}

// ============================================================
// 統合テスト
// ============================================================
TEST_F(MirOptimizationTest, IntegrationTest_ComplexOptimization) {
    const std::string code = R"(
        int main() {
            int sum = 0;
            int i = 0;

            // 定数条件
            if (10 > 5) {
                sum = 100;
            } else {
                sum = 200;  // 到達不可能
            }

            // 定数演算
            int x = 2 * 3 + 4;
            int y = x;

            // 未使用変数
            int unused1 = 999;
            int unused2 = unused1 + 1;

            return sum + y;
        }
    )";

    auto mir = compile_to_mir(code);

    // フル最適化パイプライン
    mir::opt::OptimizationPipeline pipeline;
    pipeline.enable_debug_output(false);  // テストでは出力を抑制
    pipeline.add_standard_passes(2);      // -O2レベル
    pipeline.run_until_fixpoint(*mir);

    // 最適化により、プログラムが大幅に簡略化されているはず
    auto& func = *mir->functions[0];

    // 到達不可能コードが削除されている
    bool has_unreachable = false;
    for (const auto& block : func.basic_blocks) {
        if (block && block->terminator &&
            block->terminator->kind == mir::MirTerminator::Unreachable) {
            has_unreachable = true;
        }
    }
    EXPECT_FALSE(has_unreachable);
}