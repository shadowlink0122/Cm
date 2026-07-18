#include "../../src/frontend/lexer/lexer.hpp"
#include "../../src/frontend/parser/parser.hpp"
#include "../../src/hir/lowering/lowering.hpp"
#include "../../src/mir/lowering/lowering.hpp"
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
// MIR最適化パイプラインの統合テスト
// ============================================================
// Cmソース（tests/regression/cases/mir_optimization/ の .cm ファイル）をフロントエンド〜MIR loweringに通した上で、最適化パイプライン全体（複数パスの組み合わせ・収束反復）の動作を検証する。
// 各パス単体の検証は tests/unit/mir_pass_test.cpp（手組みMIR）が担う。
// パス⇔テスト対応表は cases/mir_optimization/README.md を参照
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
};

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
