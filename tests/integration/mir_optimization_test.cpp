#include "../../src/frontend/lexer/lexer.hpp"
#include "../../src/frontend/parser/parser.hpp"
#include "../../src/hir/lowering/lowering.hpp"
#include "../../src/mir/lowering/lowering.hpp"
#include "../../src/mir/passes/cleanup/dce.hpp"
#include "../../src/mir/passes/cleanup/dse.hpp"
#include "../../src/mir/passes/cleanup/program_dce.hpp"
#include "../../src/mir/passes/cleanup/simplify_cfg.hpp"
#include "../../src/mir/passes/core/manager.hpp"
#include "../../src/mir/passes/interprocedural/inlining.hpp"
#include "../../src/mir/passes/interprocedural/tail_call_elimination.hpp"
#include "../../src/mir/passes/loop/const_unroll.hpp"
#include "../../src/mir/passes/loop/licm.hpp"
#include "../../src/mir/passes/redundancy/gvn.hpp"
#include "../../src/mir/passes/scalar/folding.hpp"
#include "../../src/mir/passes/scalar/propagation.hpp"
#include "../../src/mir/passes/scalar/sccp.hpp"
#include "../../src/mir/printer.hpp"

#include <fstream>
#include <functional>
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

// ============================================================
// 残りの最適化パスの単体テスト（v0.16.0で網羅化）
// パス一覧と対応テストは cases/mir_optimization/README.md を参照
// ============================================================
namespace {

// 関数名でMirFunctionを検索
mir::MirFunction* find_function(mir::MirProgram& program, const std::string& name) {
    for (auto& func : program.functions) {
        if (func && func->name == name) {
            return func.get();
        }
    }
    return nullptr;
}

// 指定関数名へのCall終端子の数をカウント
int count_calls_to(const mir::MirFunction& func, const std::string& callee) {
    int count = 0;
    for (const auto& block : func.basic_blocks) {
        if (!block || !block->terminator || block->terminator->kind != mir::MirTerminator::Call) {
            continue;
        }
        const auto& call = std::get<mir::MirTerminator::CallData>(block->terminator->data);
        if (call.func && call.func->kind == mir::MirOperand::FunctionRef) {
            if (const auto* name = std::get_if<std::string>(&call.func->data)) {
                if (*name == callee) {
                    count++;
                }
            }
        }
    }
    return count;
}

// is_tail_callマーク付きCall終端子の数をカウント
int count_tail_calls(const mir::MirFunction& func) {
    int count = 0;
    for (const auto& block : func.basic_blocks) {
        if (!block || !block->terminator || block->terminator->kind != mir::MirTerminator::Call) {
            continue;
        }
        const auto& call = std::get<mir::MirTerminator::CallData>(block->terminator->data);
        if (call.is_tail_call) {
            count++;
        }
    }
    return count;
}

// 終端命令の遷移先一覧
std::vector<mir::BlockId> successors_of(const mir::MirTerminator& term) {
    std::vector<mir::BlockId> out;
    switch (term.kind) {
        case mir::MirTerminator::Goto:
            out.push_back(std::get<mir::MirTerminator::GotoData>(term.data).target);
            break;
        case mir::MirTerminator::SwitchInt: {
            const auto& sw = std::get<mir::MirTerminator::SwitchIntData>(term.data);
            for (const auto& [v, t] : sw.targets) {
                out.push_back(t);
            }
            out.push_back(sw.otherwise);
            break;
        }
        case mir::MirTerminator::Call: {
            const auto& c = std::get<mir::MirTerminator::CallData>(term.data);
            out.push_back(c.success);
            break;
        }
        default:
            break;
    }
    return out;
}

// エントリから到達可能なCFGにサイクル（ループ）が存在するか（DFS三色判定）
bool has_reachable_cycle(const mir::MirFunction& func) {
    if (func.basic_blocks.empty()) {
        return false;
    }
    std::vector<int> color(func.basic_blocks.size(), 0);  // 0=white 1=gray 2=black
    std::function<bool(mir::BlockId)> dfs = [&](mir::BlockId b) -> bool {
        if (b >= func.basic_blocks.size() || !func.basic_blocks[b]) {
            return false;
        }
        color[b] = 1;
        const auto& term = func.basic_blocks[b]->terminator;
        if (term) {
            for (mir::BlockId t : successors_of(*term)) {
                if (t >= color.size()) {
                    continue;
                }
                if (color[t] == 1) {
                    return true;  // 後退エッジ=サイクル
                }
                if (color[t] == 0 && dfs(t)) {
                    return true;
                }
            }
        }
        color[b] = 2;
        return false;
    };
    return dfs(0);
}

}  // namespace

TEST_F(MirOptimizationTest, GVN_RedundantExpression) {
    auto mir = compile_case("gvn_redundant_expr");
    auto& func = *mir->functions[0];

    // MIRは各使用ごとに引数を一時変数へコピーするため、
    // パイプライン実運用（fixpoint反復）と同様にコピー伝播で
    // オペランドを正規化してからGVNを適用する
    mir::opt::CopyPropagation cp;
    cp.run(func);

    int binops_before = count_binary_op_statements(func);

    mir::opt::GVN gvn;
    bool changed = gvn.run(func);

    EXPECT_TRUE(changed);
    // 2回目の a+b がコピーに置き換わり、BinaryOp文が減る
    EXPECT_LT(count_binary_op_statements(func), binops_before);
}

TEST_F(MirOptimizationTest, DeadStoreElimination_OverwrittenStore) {
    auto mir = compile_case("dse_dead_store");
    auto& func = *mir->functions[0];

    int nops_before = count_nop_statements(func);

    mir::opt::DeadStoreElimination dse;
    bool changed = dse.run(func);

    EXPECT_TRUE(changed);
    // 使用前に上書きされる最初の代入がNop化される
    EXPECT_GT(count_nop_statements(func), nops_before);
}

TEST_F(MirOptimizationTest, SCCP_ConditionalConstant) {
    auto mir = compile_case("sccp_conditional_constant");
    auto& func = *mir->functions[0];

    mir::opt::SparseConditionalConstantPropagation sccp;
    bool changed = sccp.run(func);

    // 定数条件（1 < 2）の評価とブロックを跨ぐ定数伝播が行われる
    EXPECT_TRUE(changed);
}

TEST_F(MirOptimizationTest, LICM_InvariantHoist) {
    auto mir = compile_case("licm_invariant_hoist");
    auto& func = *mir->functions[0];

    int stmts_before = count_total_statements(func);

    mir::opt::LoopInvariantCodeMotion licm;
    bool changed = licm.run(func);

    EXPECT_TRUE(changed);
    // 巻き上げは移動であって削除ではない（文数は保存される）
    EXPECT_EQ(count_total_statements(func), stmts_before);
}

TEST_F(MirOptimizationTest, TailCallElimination_SelfTailCall) {
    auto mir = compile_case("tce_self_tail_call");
    auto* func = find_function(*mir, "count_down");
    ASSERT_NE(func, nullptr);

    EXPECT_EQ(count_tail_calls(*func), 0);

    mir::opt::TailCallElimination tce;
    bool changed = tce.run(*func);

    EXPECT_TRUE(changed);
    // 自己再帰の末尾呼び出しがis_tail_callとしてマークされる
    // （LLVMコード生成で tail call 属性になる）
    EXPECT_GE(count_tail_calls(*func), 1);
}

TEST_F(MirOptimizationTest, FunctionInlining_CurrentlyDormant) {
    // 既知の問題を固定するテスト: インライン化パスは呼び出し先を旧形式の
    // Constant(文字列)として期待するが、現行のMIR loweringはFunctionRefを
    // 発行するため、実質的に全呼び出しが対象外（パスは休眠状態）。
    // FunctionRefを認識させて有効化するとperform_inliningの潜在バグ
    // （デストラクタ順序破壊・SIGSEGV等）が露出するため、有効化は
    // perform_inliningの再設計とセットで行う（inlining.cppのコメント参照）。
    // 有効化された際はこのテストを展開検証（Call終端子の減少）へ書き換えること
    auto mir = compile_case("inlining_small_callee");
    auto* caller = find_function(*mir, "caller");
    ASSERT_NE(caller, nullptr);

    int calls_before = count_calls_to(*caller, "add");
    EXPECT_EQ(calls_before, 2);

    mir::opt::FunctionInlining inlining;
    bool changed = inlining.run_on_program(*mir);

    EXPECT_FALSE(changed);
    EXPECT_EQ(count_calls_to(*caller, "add"), calls_before);
}

TEST_F(MirOptimizationTest, ConstantLoopUnroll_ConstantTrip) {
    auto mir = compile_case("unroll_constant_trip");
    auto& func = *mir->functions[0];

    EXPECT_TRUE(has_reachable_cycle(func));
    int stmts_before = count_total_statements(func);

    mir::opt::ConstantLoopUnroll unroll(/*max_trips=*/64);
    bool changed = unroll.run(func);

    EXPECT_TRUE(changed);
    // 到達可能なCFGにサイクルが無い（＝ループが完全に展開された）。
    // 旧ループブロックは未到達のまま残るためID順の後方エッジ数では判定できない
    EXPECT_FALSE(has_reachable_cycle(func));
    EXPECT_GT(count_total_statements(func), stmts_before);
}

TEST_F(MirOptimizationTest, ProgramDCE_UnusedFunction) {
    auto mir = compile_case("program_dce_unused_function");
    ASSERT_NE(find_function(*mir, "unused_fn"), nullptr);
    ASSERT_NE(find_function(*mir, "used"), nullptr);

    mir::opt::ProgramDeadCodeElimination program_dce;
    bool changed = program_dce.run(*mir);

    EXPECT_TRUE(changed);
    // mainから到達しない関数のみ削除される
    EXPECT_EQ(find_function(*mir, "unused_fn"), nullptr);
    EXPECT_NE(find_function(*mir, "used"), nullptr);
    EXPECT_NE(find_function(*mir, "main"), nullptr);
}
