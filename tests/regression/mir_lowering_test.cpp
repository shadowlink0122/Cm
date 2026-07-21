#include "../../src/internal/hir/lowering/lowering.hpp"
#include "../../src/internal/mir/lowering/lowering.hpp"
#include "../../src/internal/syntax/lexer/lexer.hpp"
#include "../../src/internal/syntax/parser/parser.hpp"
#include "../../src/internal/types/checking/checker.hpp"

#include <fstream>
#include <gtest/gtest.h>
#include <memory>
#include <sstream>

using namespace cm;

// ============================================================
// MIR lowering 統合テスト
// ============================================================
// Cmソースは tests/regression/cases/mir_lowering/ の .cm ファイルに分割
class MirLoweringTest : public ::testing::Test {
   protected:
    std::string load_case(const std::string& name) {
        std::string path = std::string(CM_MIR_LOWERING_CASE_DIR) + "/" + name + ".cm";
        std::ifstream ifs(path);
        EXPECT_TRUE(ifs.is_open()) << "ケースファイルを読み込めません: " << path;
        std::stringstream ss;
        ss << ifs.rdbuf();
        return ss.str();
    }

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

    // 型検査を通してからloweringする（式の型情報に依存するケース用。実コンパイルと同じ順序）
    std::unique_ptr<mir::MirProgram> check_and_lower(const std::string& code) {
        Lexer lex(code);
        std::vector<Token> tokens = lex.tokenize();
        Parser p(tokens);
        auto ast = p.parse();

        TypeChecker checker;
        EXPECT_TRUE(checker.check(ast));

        hir::HirLowering hir_lowering;
        auto hir = hir_lowering.lower(ast);

        mir::MirLowering mir_lowering;
        auto mir = mir_lowering.lower(hir);

        return std::make_unique<mir::MirProgram>(std::move(mir));
    }

    std::unique_ptr<mir::MirProgram> lower_case(const std::string& name) {
        return parse_and_lower(load_case(name));
    }

    std::unique_ptr<mir::MirProgram> check_and_lower_case(const std::string& name) {
        return check_and_lower(load_case(name));
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
    auto mir = lower_case("simple_function_with_return");
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
    auto mir = lower_case("variable_declaration");
    const auto& func = *mir->functions[0];

    // ローカル変数が作成されているか
    EXPECT_GE(func.locals.size(), 3u);  // _0(戻り値), x, y + 一時変数

    // 代入文があるはず（変数への初期化）
    auto* entry_block = func.basic_blocks[0].get();
    bool has_assign = false;
    for (const auto& stmt : entry_block->statements) {
        if (stmt->kind == mir::MirStatement::Assign) {
            has_assign = true;
            break;
        }
    }
    EXPECT_TRUE(has_assign);
}

// ============================================================
// if文のテスト（CFG構築）
// ============================================================
TEST_F(MirLoweringTest, IfStatementCFG) {
    auto mir = lower_case("if_statement_cfg");
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
    auto mir = lower_case("complex_expression_decomposition");
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
    auto mir = lower_case("loop_structure");
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
    auto mir = lower_case("ternary_operator");
    const auto& func = *mir->functions[0];

    // 三項演算子は分岐構造を生成
    EXPECT_GE(func.basic_blocks.size(), 4u);  // entry, then, else, merge
}

// ============================================================
// CFGの接続テスト
// ============================================================
TEST_F(MirLoweringTest, CFGConnectivity) {
    auto mir = lower_case("cfg_connectivity");
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
    auto mir = lower_case("empty_function");
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
    auto mir = lower_case("local_variable_scope");
    const auto& func = *mir->functions[0];

    // ブロックスコープ内の変数も正しく処理される
    EXPECT_GE(func.locals.size(), 3u);  // _0, x, y
}

// ============================================================
// 文単位一時文字列のdropパス（C12）
// ============================================================
namespace {
// 関数のMIR全体から指定関数の呼び出し回数を数える
int count_calls(const mir::MirFunction& func, const std::string& callee) {
    int count = 0;
    for (const auto& block : func.basic_blocks) {
        if (!block->terminator || block->terminator->kind != mir::MirTerminator::Call) {
            continue;
        }
        const auto& call = std::get<mir::MirTerminator::CallData>(block->terminator->data);
        if (call.func && call.func->kind == mir::MirOperand::FunctionRef &&
            std::get<std::string>(call.func->data) == callee) {
            count++;
        }
    }
    return count;
}
}  // namespace

TEST_F(MirLoweringTest, TempStringDropExprStmt) {
    // println(a + " " + b) の式文では中間concatとprintln引数の両一時が解放される
    auto mir = check_and_lower_case("temp_string_drop_expr_stmt");
    const auto& func = *mir->functions[0];
    EXPECT_EQ(count_calls(func, "cm_string_concat"), 2);
    EXPECT_EQ(count_calls(func, "cm_string_free"), 2);
}

TEST_F(MirLoweringTest, TempStringDropLetEscape) {
    // let束縛へエスケープした一時は解放されない（println(s)のsは名前付き変数）
    auto mir = check_and_lower_case("temp_string_drop_let_escape");
    const auto& func = *mir->functions[0];
    EXPECT_EQ(count_calls(func, "cm_string_concat"), 1);
    EXPECT_EQ(count_calls(func, "cm_string_free"), 0);
}

TEST_F(MirLoweringTest, TempStringDropTernaryArm) {
    // 三項演算子の腕で確保された一時は条件付き実行のため解放対象にしない
    // （文末で未初期化ポインタをfreeする危険を避ける）
    auto mir = check_and_lower_case("temp_string_drop_ternary_arm");
    const auto& func = *mir->functions[0];
    EXPECT_EQ(count_calls(func, "cm_string_concat"), 2);
    EXPECT_EQ(count_calls(func, "cm_string_free"), 0);
}

// ============================================================
// 複数の関数のテスト
// ============================================================
TEST_F(MirLoweringTest, MultipleFunctions) {
    auto mir = lower_case("multiple_functions");
    EXPECT_EQ(mir->functions.size(), 2u);

    // 各関数が正しく変換されているか
    for (const auto& func : mir->functions) {
        EXPECT_FALSE(func->name.empty());
        EXPECT_GE(func->basic_blocks.size(), 1u);
    }
}
