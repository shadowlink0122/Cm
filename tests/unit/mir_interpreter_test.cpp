#include <gtest/gtest.h>
#include "mir/mir_interpreter.hpp"
#include "mir/mir_nodes.hpp"

using namespace cm::mir;

// ============================================================
// テストヘルパー
// ============================================================
class MirInterpreterTest : public ::testing::Test {
protected:
    MirInterpreter interpreter;

    // 簡単なMIRプログラムを作成
    MirProgram create_simple_program() {
        MirProgram program;

        // main関数を作成
        auto func = std::make_unique<MirFunction>();
        func->name = "main";
        func->return_local = 0;  // _0が戻り値
        func->entry_block = 0;

        // 基本ブロック0を作成
        auto bb0 = std::make_unique<BasicBlock>();
        bb0->id = 0;

        // _1 = 42
        auto stmt1 = std::make_unique<MirStatement>();
        stmt1->kind = MirStatement::Assign;
        stmt1->data = MirStatement::AssignData{
            MirPlace{1, {}},  // _1
            std::make_unique<MirRvalue>(MirRvalue{
                MirRvalue::Use,
                MirRvalue::UseData{
                    std::make_unique<MirOperand>(MirOperand{
                        MirOperand::Constant,
                        MirConstant{int64_t(42)}
                    })
                }
            })
        };
        bb0->statements.push_back(std::move(stmt1));

        // _0 = _1 (戻り値設定)
        auto stmt2 = std::make_unique<MirStatement>();
        stmt2->kind = MirStatement::Assign;
        stmt2->data = MirStatement::AssignData{
            MirPlace{0, {}},  // _0
            std::make_unique<MirRvalue>(MirRvalue{
                MirRvalue::Use,
                MirRvalue::UseData{
                    std::make_unique<MirOperand>(MirOperand{
                        MirOperand::Copy,
                        MirPlace{1, {}}
                    })
                }
            })
        };
        bb0->statements.push_back(std::move(stmt2));

        // return
        bb0->terminator = std::make_unique<MirTerminator>();
        bb0->terminator->kind = MirTerminator::Return;

        func->basic_blocks.push_back(std::move(bb0));
        program.functions.push_back(std::move(func));

        return program;
    }

    // 算術演算を含むプログラム
    MirProgram create_arithmetic_program() {
        MirProgram program;

        auto func = std::make_unique<MirFunction>();
        func->name = "main";
        func->return_local = 0;
        func->entry_block = 0;

        auto bb0 = std::make_unique<BasicBlock>();
        bb0->id = 0;

        // _1 = 10
        auto stmt1 = std::make_unique<MirStatement>();
        stmt1->kind = MirStatement::Assign;
        stmt1->data = MirStatement::AssignData{
            MirPlace{1, {}},
            std::make_unique<MirRvalue>(MirRvalue{
                MirRvalue::Use,
                MirRvalue::UseData{
                    std::make_unique<MirOperand>(MirOperand{
                        MirOperand::Constant,
                        MirConstant{int64_t(10)}
                    })
                }
            })
        };
        bb0->statements.push_back(std::move(stmt1));

        // _2 = 20
        auto stmt2 = std::make_unique<MirStatement>();
        stmt2->kind = MirStatement::Assign;
        stmt2->data = MirStatement::AssignData{
            MirPlace{2, {}},
            std::make_unique<MirRvalue>(MirRvalue{
                MirRvalue::Use,
                MirRvalue::UseData{
                    std::make_unique<MirOperand>(MirOperand{
                        MirOperand::Constant,
                        MirConstant{int64_t(20)}
                    })
                }
            })
        };
        bb0->statements.push_back(std::move(stmt2));

        // _0 = _1 + _2
        auto stmt3 = std::make_unique<MirStatement>();
        stmt3->kind = MirStatement::Assign;
        stmt3->data = MirStatement::AssignData{
            MirPlace{0, {}},
            std::make_unique<MirRvalue>(MirRvalue{
                MirRvalue::BinaryOp,
                MirRvalue::BinaryOpData{
                    MirBinaryOp::Add,
                    std::make_unique<MirOperand>(MirOperand{
                        MirOperand::Copy,
                        MirPlace{1, {}}
                    }),
                    std::make_unique<MirOperand>(MirOperand{
                        MirOperand::Copy,
                        MirPlace{2, {}}
                    })
                }
            })
        };
        bb0->statements.push_back(std::move(stmt3));

        // return
        bb0->terminator = std::make_unique<MirTerminator>();
        bb0->terminator->kind = MirTerminator::Return;

        func->basic_blocks.push_back(std::move(bb0));
        program.functions.push_back(std::move(func));

        return program;
    }

    // 条件分岐を含むプログラム
    MirProgram create_conditional_program() {
        MirProgram program;

        auto func = std::make_unique<MirFunction>();
        func->name = "main";
        func->return_local = 0;
        func->entry_block = 0;

        // BB0: 条件評価
        auto bb0 = std::make_unique<BasicBlock>();
        bb0->id = 0;

        // _1 = true
        auto stmt1 = std::make_unique<MirStatement>();
        stmt1->kind = MirStatement::Assign;
        stmt1->data = MirStatement::AssignData{
            MirPlace{1, {}},
            std::make_unique<MirRvalue>(MirRvalue{
                MirRvalue::Use,
                MirRvalue::UseData{
                    std::make_unique<MirOperand>(MirOperand{
                        MirOperand::Constant,
                        MirConstant{true}
                    })
                }
            })
        };
        bb0->statements.push_back(std::move(stmt1));

        // if _1 goto bb1; else goto bb2
        bb0->terminator = std::make_unique<MirTerminator>();
        bb0->terminator->kind = MirTerminator::SwitchInt;
        bb0->terminator->data = MirTerminator::SwitchIntData{
            std::make_unique<MirOperand>(MirOperand{
                MirOperand::Copy,
                MirPlace{1, {}}
            }),
            {{1, 1}},  // true -> BB1
            2          // false -> BB2
        };

        // BB1: trueブランチ
        auto bb1 = std::make_unique<BasicBlock>();
        bb1->id = 1;

        // _0 = 100
        auto stmt2 = std::make_unique<MirStatement>();
        stmt2->kind = MirStatement::Assign;
        stmt2->data = MirStatement::AssignData{
            MirPlace{0, {}},
            std::make_unique<MirRvalue>(MirRvalue{
                MirRvalue::Use,
                MirRvalue::UseData{
                    std::make_unique<MirOperand>(MirOperand{
                        MirOperand::Constant,
                        MirConstant{int64_t(100)}
                    })
                }
            })
        };
        bb1->statements.push_back(std::move(stmt2));

        // goto bb3
        bb1->terminator = std::make_unique<MirTerminator>();
        bb1->terminator->kind = MirTerminator::Goto;
        bb1->terminator->data = MirTerminator::GotoData{3};

        // BB2: falseブランチ
        auto bb2 = std::make_unique<BasicBlock>();
        bb2->id = 2;

        // _0 = 200
        auto stmt3 = std::make_unique<MirStatement>();
        stmt3->kind = MirStatement::Assign;
        stmt3->data = MirStatement::AssignData{
            MirPlace{0, {}},
            std::make_unique<MirRvalue>(MirRvalue{
                MirRvalue::Use,
                MirRvalue::UseData{
                    std::make_unique<MirOperand>(MirOperand{
                        MirOperand::Constant,
                        MirConstant{int64_t(200)}
                    })
                }
            })
        };
        bb2->statements.push_back(std::move(stmt3));

        // goto bb3
        bb2->terminator = std::make_unique<MirTerminator>();
        bb2->terminator->kind = MirTerminator::Goto;
        bb2->terminator->data = MirTerminator::GotoData{3};

        // BB3: 終了
        auto bb3 = std::make_unique<BasicBlock>();
        bb3->id = 3;

        // return
        bb3->terminator = std::make_unique<MirTerminator>();
        bb3->terminator->kind = MirTerminator::Return;

        func->basic_blocks.push_back(std::move(bb0));
        func->basic_blocks.push_back(std::move(bb1));
        func->basic_blocks.push_back(std::move(bb2));
        func->basic_blocks.push_back(std::move(bb3));
        program.functions.push_back(std::move(func));

        return program;
    }
};

// ============================================================
// 基本実行テスト
// ============================================================
TEST_F(MirInterpreterTest, ExecuteSimpleProgram) {
    auto program = create_simple_program();
    auto result = interpreter.execute(program);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(std::any_cast<int64_t>(result.return_value), 42);
}

// ============================================================
// 算術演算テスト
// ============================================================
TEST_F(MirInterpreterTest, ExecuteArithmetic) {
    auto program = create_arithmetic_program();
    auto result = interpreter.execute(program);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(std::any_cast<int64_t>(result.return_value), 30);
}

// ============================================================
// 条件分岐テスト
// ============================================================
TEST_F(MirInterpreterTest, ExecuteConditional) {
    auto program = create_conditional_program();
    auto result = interpreter.execute(program);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(std::any_cast<int64_t>(result.return_value), 100);
}

// ============================================================
// エラーケーステスト
// ============================================================
TEST_F(MirInterpreterTest, MissingMainFunction) {
    MirProgram program;  // 空のプログラム
    auto result = interpreter.execute(program);

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error_message.find("main"), std::string::npos);
}

TEST_F(MirInterpreterTest, InvalidBlockId) {
    MirProgram program;
    auto func = std::make_unique<MirFunction>();
    func->name = "main";
    func->return_local = 0;
    func->entry_block = 999;  // 無効なブロックID

    program.functions.push_back(std::move(func));
    auto result = interpreter.execute(program);

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error_message.find("無効なブロックID"), std::string::npos);
}

// ============================================================
// 二項演算テスト
// ============================================================
TEST_F(MirInterpreterTest, BinaryOperations) {
    // 各種二項演算をテスト
    struct TestCase {
        MirBinaryOp op;
        int64_t lhs;
        int64_t rhs;
        int64_t expected;
    };

    TestCase cases[] = {
        {MirBinaryOp::Add, 10, 5, 15},
        {MirBinaryOp::Sub, 10, 5, 5},
        {MirBinaryOp::Mul, 10, 5, 50},
        {MirBinaryOp::Div, 10, 5, 2},
        {MirBinaryOp::Mod, 10, 3, 1},
    };

    for (const auto& tc : cases) {
        MirProgram program;
        auto func = std::make_unique<MirFunction>();
        func->name = "main";
        func->return_local = 0;
        func->entry_block = 0;

        auto bb0 = std::make_unique<BasicBlock>();
        bb0->id = 0;

        // _0 = lhs op rhs
        auto stmt = std::make_unique<MirStatement>();
        stmt->kind = MirStatement::Assign;
        stmt->data = MirStatement::AssignData{
            MirPlace{0, {}},
            std::make_unique<MirRvalue>(MirRvalue{
                MirRvalue::BinaryOp,
                MirRvalue::BinaryOpData{
                    tc.op,
                    std::make_unique<MirOperand>(MirOperand{
                        MirOperand::Constant,
                        MirConstant{tc.lhs}
                    }),
                    std::make_unique<MirOperand>(MirOperand{
                        MirOperand::Constant,
                        MirConstant{tc.rhs}
                    })
                }
            })
        };
        bb0->statements.push_back(std::move(stmt));

        bb0->terminator = std::make_unique<MirTerminator>();
        bb0->terminator->kind = MirTerminator::Return;

        func->basic_blocks.push_back(std::move(bb0));
        program.functions.push_back(std::move(func));

        auto result = interpreter.execute(program);
        EXPECT_TRUE(result.success);
        EXPECT_EQ(std::any_cast<int64_t>(result.return_value), tc.expected);
    }
}

// ============================================================
// 比較演算テスト
// ============================================================
TEST_F(MirInterpreterTest, ComparisonOperations) {
    struct TestCase {
        MirBinaryOp op;
        int64_t lhs;
        int64_t rhs;
        bool expected;
    };

    TestCase cases[] = {
        {MirBinaryOp::Eq, 10, 10, true},
        {MirBinaryOp::Eq, 10, 5, false},
        {MirBinaryOp::Ne, 10, 5, true},
        {MirBinaryOp::Lt, 5, 10, true},
        {MirBinaryOp::Le, 10, 10, true},
        {MirBinaryOp::Gt, 10, 5, true},
        {MirBinaryOp::Ge, 10, 10, true},
    };

    for (const auto& tc : cases) {
        MirProgram program;
        auto func = std::make_unique<MirFunction>();
        func->name = "main";
        func->return_local = 0;
        func->entry_block = 0;

        auto bb0 = std::make_unique<BasicBlock>();
        bb0->id = 0;

        // _0 = lhs op rhs
        auto stmt = std::make_unique<MirStatement>();
        stmt->kind = MirStatement::Assign;
        stmt->data = MirStatement::AssignData{
            MirPlace{0, {}},
            std::make_unique<MirRvalue>(MirRvalue{
                MirRvalue::BinaryOp,
                MirRvalue::BinaryOpData{
                    tc.op,
                    std::make_unique<MirOperand>(MirOperand{
                        MirOperand::Constant,
                        MirConstant{tc.lhs}
                    }),
                    std::make_unique<MirOperand>(MirOperand{
                        MirOperand::Constant,
                        MirConstant{tc.rhs}
                    })
                }
            })
        };
        bb0->statements.push_back(std::move(stmt));

        bb0->terminator = std::make_unique<MirTerminator>();
        bb0->terminator->kind = MirTerminator::Return;

        func->basic_blocks.push_back(std::move(bb0));
        program.functions.push_back(std::move(func));

        auto result = interpreter.execute(program);
        EXPECT_TRUE(result.success);
        EXPECT_EQ(std::any_cast<bool>(result.return_value), tc.expected);
    }
}