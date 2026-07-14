#include "../../src/frontend/lexer/lexer.hpp"
#include "../../src/frontend/parser/parser.hpp"
#include "../../src/hir/lowering/lowering.hpp"
#include "../../src/mir/lowering/lowering.hpp"
#include "../../src/mir/passes/cleanup/dce.hpp"
#include "../../src/mir/passes/cleanup/simplify_cfg.hpp"
#include "../../src/mir/passes/core/manager.hpp"
#include "../../src/mir/passes/scalar/folding.hpp"
#include "../../src/mir/passes/scalar/propagation.hpp"
#include "../../src/mir/printer.hpp"

#include <fstream>
#include <gtest/gtest.h>
#include <memory>
#include <sstream>

using namespace cm;

// ============================================================
// MIR最適化パス 統合テスト
// ============================================================
// Cmソースは tests/integration/cases/mir_optimization/ の .cm ファイルに分割
class MirOptimizationTest : public ::testing::Test {
   protected:
    std::string load_case(const std::string& name) {
        std::string path = std::string(CM_MIR_OPT_CASE_DIR) + "/" + name + ".cm";
        std::ifstream ifs(path);
        EXPECT_TRUE(ifs.is_open()) << "ケースファイルを読み込めません: " << path;
        std::stringstream ss;
        ss << ifs.rdbuf();
        return ss.str();
    }

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

    std::unique_ptr<mir::MirProgram> compile_case(const std::string& name) {
        return compile_to_mir(load_case(name));
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
    auto mir = compile_case("constant_folding_simple");
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
    auto mir = compile_case("constant_folding_comparison");

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
    auto mir = compile_case("dce_unused_variable");
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
    auto mir = compile_case("dce_unreachable_block");
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
    auto mir = compile_case("copy_propagation_simple");
    auto& func = *mir->functions[0];

    // コピー伝播を実行
    mir::opt::CopyPropagation cp;
    bool changed = cp.run(func);

    EXPECT_TRUE(changed);

    // y, zの使用がxに置き換わっているはず
}

TEST_F(MirOptimizationTest, CopyPropagation_Chain) {
    auto mir = compile_case("copy_propagation_chain");

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
    auto mir = compile_case("pipeline_standard");

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
    auto mir = compile_case("pipeline_fixpoint");

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
    auto mir = compile_case("simplify_control_flow_goto_chain");

    // 制御フロー簡略化を実行
    mir::opt::SimplifyControlFlow scf;
    scf.run(*mir->functions[0]);

    // 単純なGotoチェーンが簡略化される
}

// ============================================================
// 統合テスト
// ============================================================
TEST_F(MirOptimizationTest, IntegrationTest_ComplexOptimization) {
    auto mir = compile_case("integration_complex_optimization");

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

// ============================================================
// 代数的恒等式の簡約テスト（v0.16.0）
// ============================================================
namespace {

// BinaryOp rvalueを持つ代入文の数をカウント
int count_binary_op_statements(const mir::MirFunction& func) {
    int count = 0;
    for (const auto& block : func.basic_blocks) {
        if (!block)
            continue;
        for (const auto& stmt : block->statements) {
            if (stmt->kind == mir::MirStatement::Assign) {
                auto& data = std::get<mir::MirStatement::AssignData>(stmt->data);
                if (data.rvalue && data.rvalue->kind == mir::MirRvalue::BinaryOp) {
                    count++;
                }
            }
        }
    }
    return count;
}

// 全ブロックの文数合計
int count_total_statements(const mir::MirFunction& func) {
    int count = 0;
    for (const auto& block : func.basic_blocks) {
        if (block) {
            count += static_cast<int>(block->statements.size());
        }
    }
    return count;
}

// 終端命令の種類列（CFG形状の指紋）
std::vector<int> terminator_kinds(const mir::MirFunction& func) {
    std::vector<int> kinds;
    for (const auto& block : func.basic_blocks) {
        if (block && block->terminator) {
            kinds.push_back(static_cast<int>(block->terminator->kind));
        }
    }
    return kinds;
}

}  // namespace

TEST_F(MirOptimizationTest, ConstantFolding_AlgebraicIdentity) {
    auto mir = compile_case("algebraic_identity");
    auto& func = *mir->functions[0];

    int binops_before = count_binary_op_statements(func);

    mir::opt::ConstantFolding folding;
    bool changed = folding.run(func);

    EXPECT_TRUE(changed);

    // 恒等式13件（x*1, 1*x, x+0, 0+x, x-0, x/1, x%1, x*0,
    // x>>0, x<<0, x|0, x^0, x&0）が全てUse/定数へ簡約される
    int binops_after = count_binary_op_statements(func);
    EXPECT_LE(binops_after, binops_before - 13)
        << "before=" << binops_before << " after=" << binops_after;
}

TEST_F(MirOptimizationTest, ConstantFolding_StatementPreserving) {
    // SVバックエンドが依存する契約:
    // fold_terminators=false のConstantFoldingは文数・CFG形状を変えない
    auto mir = compile_case("statement_preserving");
    auto& func = *mir->functions[0];

    int stmts_before = count_total_statements(func);
    size_t blocks_before = func.basic_blocks.size();
    auto terms_before = terminator_kinds(func);

    mir::opt::ConstantFolding folding(/*fold_terminators=*/false);
    bool changed = folding.run(func);

    EXPECT_TRUE(changed);  // 定数畳み込み・恒等式簡約は行われる
    EXPECT_EQ(count_total_statements(func), stmts_before);
    EXPECT_EQ(func.basic_blocks.size(), blocks_before);
    EXPECT_EQ(terminator_kinds(func), terms_before);
}

TEST_F(MirOptimizationTest, ConstantFolding_FloatIdentityNotSimplified) {
    // 浮動小数点の x+0.0 / x*1.0 はNaN・-0.0の意味論があるため簡約しない
    auto mir = compile_case("float_identity");
    auto& func = *mir->functions[0];

    int binops_before = count_binary_op_statements(func);
    mir::opt::ConstantFolding folding;
    folding.run(func);
    EXPECT_EQ(count_binary_op_statements(func), binops_before);
}
