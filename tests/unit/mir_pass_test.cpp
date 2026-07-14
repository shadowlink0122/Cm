// ============================================================
// MIR最適化パスの単体テスト
// ============================================================
// 各パスを手組みのMIR（フロントエンド・lowering非依存）に対して
// 単体で実行し、変換の性質を検証する。
// パイプライン全体（Cmソース→最適化）の検証は
// tests/regression/mir_optimization_test.cpp が担う。
// パス⇔テスト対応表は tests/regression/cases/mir_optimization/README.md を参照

#include "../../src/mir/nodes.hpp"
#include "../../src/mir/passes/cleanup/dce.hpp"
#include "../../src/mir/passes/cleanup/dse.hpp"
#include "../../src/mir/passes/cleanup/program_dce.hpp"
#include "../../src/mir/passes/cleanup/simplify_cfg.hpp"
#include "../../src/mir/passes/interprocedural/inlining.hpp"
#include "../../src/mir/passes/interprocedural/tail_call_elimination.hpp"
#include "../../src/mir/passes/loop/const_unroll.hpp"
#include "../../src/mir/passes/loop/licm.hpp"
#include "../../src/mir/passes/redundancy/gvn.hpp"
#include "../../src/mir/passes/scalar/folding.hpp"
#include "../../src/mir/passes/scalar/propagation.hpp"
#include "../../src/mir/passes/scalar/sccp.hpp"

#include <functional>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

using namespace cm;
using namespace cm::mir;

namespace {

// ============================================================
// MIR構築ヘルパー
// ============================================================

MirOperandPtr cint(int64_t v) {
    MirConstant c;
    c.value = v;
    c.type = hir::make_int();
    return MirOperand::constant(std::move(c));
}

MirOperandPtr cdouble(double v) {
    MirConstant c;
    c.value = v;
    c.type = hir::make_double();
    return MirOperand::constant(std::move(c));
}

MirOperandPtr use_of(LocalId l, hir::TypePtr type = hir::make_int()) {
    return MirOperand::copy(MirPlace{l}, std::move(type));
}

MirRvaluePtr rv_use(MirOperandPtr op) {
    return MirRvalue::use(std::move(op));
}

MirRvaluePtr rv_bin(MirBinaryOp op, MirOperandPtr lhs, MirOperandPtr rhs,
                    hir::TypePtr type = hir::make_int()) {
    return MirRvalue::binary(op, std::move(lhs), std::move(rhs), std::move(type));
}

void emit(MirFunction& f, BlockId b, LocalId target, MirRvaluePtr rv) {
    f.basic_blocks[b]->add_statement(MirStatement::assign(MirPlace{target}, std::move(rv)));
}

MirTerminatorPtr call_terminator(const std::string& callee, BlockId success,
                                 std::optional<MirPlace> dest = std::nullopt) {
    auto term = std::make_unique<MirTerminator>();
    term->kind = MirTerminator::Call;
    MirTerminator::CallData data;
    data.func = MirOperand::function_ref(callee);
    data.destination = dest;
    data.success = success;
    term->data = std::move(data);
    return term;
}

// 単純な関数の骨格を作成（_0 = 戻り値ローカル、entryブロックb0）
MirFunctionPtr make_function(const std::string& name = "f") {
    auto f = std::make_unique<MirFunction>();
    f->name = name;
    f->return_local = f->add_local("_ret", hir::make_int());
    f->add_block();  // b0 (entry)
    return f;
}

// ============================================================
// 検査ヘルパー
// ============================================================

int count_binary_ops(const MirFunction& func) {
    int count = 0;
    for (const auto& block : func.basic_blocks) {
        if (!block) {
            continue;
        }
        for (const auto& stmt : block->statements) {
            if (stmt->kind == MirStatement::Assign) {
                const auto& d = std::get<MirStatement::AssignData>(stmt->data);
                if (d.rvalue && d.rvalue->kind == MirRvalue::BinaryOp) {
                    count++;
                }
            }
        }
    }
    return count;
}

int count_statements(const MirFunction& func) {
    int count = 0;
    for (const auto& block : func.basic_blocks) {
        if (block) {
            count += static_cast<int>(block->statements.size());
        }
    }
    return count;
}

int count_nops(const MirFunction& func) {
    int count = 0;
    for (const auto& block : func.basic_blocks) {
        if (!block) {
            continue;
        }
        for (const auto& stmt : block->statements) {
            if (stmt->kind == MirStatement::Nop) {
                count++;
            }
        }
    }
    return count;
}

// 代入先localのrvalueが指定した整数定数のUseになっているか
bool is_const_use(const MirFunction& func, BlockId b, size_t stmt_index, int64_t expected) {
    const auto& stmt = func.basic_blocks[b]->statements.at(stmt_index);
    if (stmt->kind != MirStatement::Assign) {
        return false;
    }
    const auto& d = std::get<MirStatement::AssignData>(stmt->data);
    if (!d.rvalue || d.rvalue->kind != MirRvalue::Use) {
        return false;
    }
    const auto& u = std::get<MirRvalue::UseData>(d.rvalue->data);
    if (!u.operand || u.operand->kind != MirOperand::Constant) {
        return false;
    }
    const auto& c = std::get<MirConstant>(u.operand->data);
    const auto* v = std::get_if<int64_t>(&c.value);
    return v && *v == expected;
}

std::vector<BlockId> successors_of(const MirTerminator& term) {
    std::vector<BlockId> out;
    switch (term.kind) {
        case MirTerminator::Goto:
            out.push_back(std::get<MirTerminator::GotoData>(term.data).target);
            break;
        case MirTerminator::SwitchInt: {
            const auto& sw = std::get<MirTerminator::SwitchIntData>(term.data);
            for (const auto& [v, t] : sw.targets) {
                out.push_back(t);
            }
            out.push_back(sw.otherwise);
            break;
        }
        case MirTerminator::Call:
            out.push_back(std::get<MirTerminator::CallData>(term.data).success);
            break;
        default:
            break;
    }
    return out;
}

// エントリから到達可能なCFGにサイクルが存在するか（DFS三色判定）
bool has_reachable_cycle(const MirFunction& func) {
    if (func.basic_blocks.empty()) {
        return false;
    }
    std::vector<int> color(func.basic_blocks.size(), 0);
    std::function<bool(BlockId)> dfs = [&](BlockId b) -> bool {
        if (b >= func.basic_blocks.size() || !func.basic_blocks[b]) {
            return false;
        }
        color[b] = 1;
        if (func.basic_blocks[b]->terminator) {
            for (BlockId t : successors_of(*func.basic_blocks[b]->terminator)) {
                if (t >= color.size()) {
                    continue;
                }
                if (color[t] == 1) {
                    return true;
                }
                if (color[t] == 0 && dfs(t)) {
                    return true;
                }
            }
        }
        color[b] = 2;
        return false;
    };
    return dfs(func.entry_block);
}

int count_tail_calls(const MirFunction& func) {
    int count = 0;
    for (const auto& block : func.basic_blocks) {
        if (block && block->terminator && block->terminator->kind == MirTerminator::Call &&
            std::get<MirTerminator::CallData>(block->terminator->data).is_tail_call) {
            count++;
        }
    }
    return count;
}

MirFunction* find_function(MirProgram& program, const std::string& name) {
    for (auto& f : program.functions) {
        if (f && f->name == name) {
            return f.get();
        }
    }
    return nullptr;
}

}  // namespace

// ============================================================
// ConstantFolding（定数畳み込み）
// ============================================================

TEST(MirPassTest, ConstantFolding_FoldsConstantBinaryOp) {
    // _1 = 2 * 3; _2 = _1 + 4; → _1 = 6; _2 = 10;
    auto f = make_function();
    LocalId a = f->add_local("a", hir::make_int());
    LocalId b = f->add_local("b", hir::make_int());
    emit(*f, 0, a, rv_bin(MirBinaryOp::Mul, cint(2), cint(3)));
    emit(*f, 0, b, rv_bin(MirBinaryOp::Add, use_of(a), cint(4)));
    f->basic_blocks[0]->set_terminator(MirTerminator::return_value());

    opt::ConstantFolding folding;
    EXPECT_TRUE(folding.run(*f));
    EXPECT_TRUE(is_const_use(*f, 0, 0, 6));
    EXPECT_TRUE(is_const_use(*f, 0, 1, 10));
}

TEST(MirPassTest, ConstantFolding_FoldsComparison) {
    // _1 = (10 > 5); → _1 = true(1)
    auto f = make_function();
    LocalId a = f->add_local("a", hir::make_bool());
    emit(*f, 0, a, rv_bin(MirBinaryOp::Gt, cint(10), cint(5), hir::make_bool()));
    f->basic_blocks[0]->set_terminator(MirTerminator::return_value());

    opt::ConstantFolding folding;
    EXPECT_TRUE(folding.run(*f));
    EXPECT_EQ(count_binary_ops(*f), 0);
}

TEST(MirPassTest, ConstantFolding_AlgebraicIdentity) {
    // 非定数オペランド x に対する恒等式が全てUse/定数へ簡約される
    auto f = make_function();
    LocalId x = f->add_local("x", hir::make_int());
    f->arg_locals.push_back(x);
    std::vector<std::pair<MirBinaryOp, std::pair<int64_t, bool>>> cases = {
        // {演算, {定数, 定数が右辺か}}
        {MirBinaryOp::Mul, {1, true}},    {MirBinaryOp::Mul, {1, false}},
        {MirBinaryOp::Add, {0, true}},    {MirBinaryOp::Add, {0, false}},
        {MirBinaryOp::Sub, {0, true}},    {MirBinaryOp::Div, {1, true}},
        {MirBinaryOp::Mod, {1, true}},    {MirBinaryOp::Mul, {0, true}},
        {MirBinaryOp::Shl, {0, true}},    {MirBinaryOp::Shr, {0, true}},
        {MirBinaryOp::BitOr, {0, true}},  {MirBinaryOp::BitXor, {0, true}},
        {MirBinaryOp::BitAnd, {0, true}},
    };
    for (const auto& [op, rhs_info] : cases) {
        LocalId t = f->add_local("t", hir::make_int());
        if (rhs_info.second) {
            emit(*f, 0, t, rv_bin(op, use_of(x), cint(rhs_info.first)));
        } else {
            emit(*f, 0, t, rv_bin(op, cint(rhs_info.first), use_of(x)));
        }
    }
    f->basic_blocks[0]->set_terminator(MirTerminator::return_value());

    ASSERT_EQ(count_binary_ops(*f), 13);
    opt::ConstantFolding folding;
    EXPECT_TRUE(folding.run(*f));
    EXPECT_EQ(count_binary_ops(*f), 0);
    // 文数は変わらない（rvalueの書き換えのみ）
    EXPECT_EQ(count_statements(*f), 13);
}

TEST(MirPassTest, ConstantFolding_FloatIdentityNotSimplified) {
    // 浮動小数点の x+0.0 / x*1.0 はNaN・-0.0の意味論があるため簡約しない
    auto f = make_function();
    LocalId x = f->add_local("x", hir::make_double());
    f->arg_locals.push_back(x);
    LocalId y = f->add_local("y", hir::make_double());
    LocalId z = f->add_local("z", hir::make_double());
    emit(*f, 0, y,
         rv_bin(MirBinaryOp::Add, use_of(x, hir::make_double()), cdouble(0.0), hir::make_double()));
    emit(*f, 0, z,
         rv_bin(MirBinaryOp::Mul, use_of(x, hir::make_double()), cdouble(1.0), hir::make_double()));
    f->basic_blocks[0]->set_terminator(MirTerminator::return_value());

    opt::ConstantFolding folding;
    folding.run(*f);
    EXPECT_EQ(count_binary_ops(*f), 2);
}

TEST(MirPassTest, ConstantFolding_TerminatorFoldControl) {
    // 定数discriminantのSwitchIntは既定でGotoへ畳み込まれ、
    // fold_terminators=false（SVバックエンド用の契約）では保持される
    auto build = [] {
        auto f = make_function();
        BlockId b1 = f->add_block();
        BlockId b2 = f->add_block();
        f->basic_blocks[0]->set_terminator(MirTerminator::switch_int(cint(1), {{1, b1}}, b2));
        f->basic_blocks[b1]->set_terminator(MirTerminator::return_value());
        f->basic_blocks[b2]->set_terminator(MirTerminator::return_value());
        return f;
    };

    auto f1 = build();
    opt::ConstantFolding fold_all;
    fold_all.run(*f1);
    EXPECT_EQ(f1->basic_blocks[0]->terminator->kind, MirTerminator::Goto);

    auto f2 = build();
    opt::ConstantFolding keep_cfg(/*fold_terminators=*/false);
    keep_cfg.run(*f2);
    EXPECT_EQ(f2->basic_blocks[0]->terminator->kind, MirTerminator::SwitchInt);
}

// ============================================================
// CopyPropagation（コピー伝播）
// ============================================================

TEST(MirPassTest, CopyPropagation_PropagatesThroughChain) {
    // _b = _a; _c = _b; _d = _c + 1; → _dの演算オペランドが_aになる
    auto f = make_function();
    LocalId a = f->add_local("a", hir::make_int());
    LocalId b = f->add_local("b", hir::make_int());
    LocalId c = f->add_local("c", hir::make_int());
    LocalId d = f->add_local("d", hir::make_int());
    emit(*f, 0, a, rv_use(cint(42)));
    emit(*f, 0, b, rv_use(use_of(a)));
    emit(*f, 0, c, rv_use(use_of(b)));
    emit(*f, 0, d, rv_bin(MirBinaryOp::Add, use_of(c), cint(1)));
    f->basic_blocks[0]->set_terminator(MirTerminator::return_value());

    opt::CopyPropagation cp;
    EXPECT_TRUE(cp.run(*f));

    const auto& stmt = f->basic_blocks[0]->statements[3];
    const auto& data = std::get<MirStatement::AssignData>(stmt->data);
    const auto& bin = std::get<MirRvalue::BinaryOpData>(data.rvalue->data);
    const auto& place = std::get<MirPlace>(bin.lhs->data);
    EXPECT_EQ(place.local, a);
}

// ============================================================
// GVN（共通部分式除去）
// ============================================================

TEST(MirPassTest, GVN_EliminatesRedundantExpression) {
    // _x = _a + _b; _y = _a + _b; → _y = copy _x
    auto f = make_function();
    LocalId a = f->add_local("a", hir::make_int());
    LocalId b = f->add_local("b", hir::make_int());
    LocalId x = f->add_local("x", hir::make_int());
    LocalId y = f->add_local("y", hir::make_int());
    f->arg_locals.push_back(a);
    f->arg_locals.push_back(b);
    emit(*f, 0, x, rv_bin(MirBinaryOp::Add, use_of(a), use_of(b)));
    emit(*f, 0, y, rv_bin(MirBinaryOp::Add, use_of(a), use_of(b)));
    f->basic_blocks[0]->set_terminator(MirTerminator::return_value());

    ASSERT_EQ(count_binary_ops(*f), 2);
    opt::GVN gvn;
    EXPECT_TRUE(gvn.run(*f));
    EXPECT_EQ(count_binary_ops(*f), 1);
}

TEST(MirPassTest, GVN_InvalidatedByReassignment) {
    // オペランドが再代入された後の同一式は共通化してはならない
    auto f = make_function();
    LocalId a = f->add_local("a", hir::make_int());
    LocalId x = f->add_local("x", hir::make_int());
    LocalId y = f->add_local("y", hir::make_int());
    emit(*f, 0, a, rv_use(cint(1)));
    emit(*f, 0, x, rv_bin(MirBinaryOp::Add, use_of(a), cint(10)));
    emit(*f, 0, a, rv_use(cint(2)));  // aを再代入
    emit(*f, 0, y, rv_bin(MirBinaryOp::Add, use_of(a), cint(10)));
    f->basic_blocks[0]->set_terminator(MirTerminator::return_value());

    opt::GVN gvn;
    gvn.run(*f);
    // _yの式は共通化されず残る
    EXPECT_EQ(count_binary_ops(*f), 2);
}

// ============================================================
// DSE（デッドストア除去）
// ============================================================

TEST(MirPassTest, DSE_RemovesOverwrittenStore) {
    // _x = 1; _x = 2; _0 = _x; → 最初のストアがNop化される
    auto f = make_function();
    LocalId x = f->add_local("x", hir::make_int());
    emit(*f, 0, x, rv_use(cint(1)));
    emit(*f, 0, x, rv_use(cint(2)));
    emit(*f, 0, f->return_local, rv_use(use_of(x)));
    f->basic_blocks[0]->set_terminator(MirTerminator::return_value());

    opt::DeadStoreElimination dse;
    EXPECT_TRUE(dse.run(*f));
    EXPECT_EQ(count_nops(*f), 1);
    EXPECT_EQ(f->basic_blocks[0]->statements[0]->kind, MirStatement::Nop);
}

// ============================================================
// DCE（デッドコード除去）
// ============================================================

TEST(MirPassTest, DCE_RemovesUnusedAssignment) {
    // 使用されないローカルへの代入が削除される
    auto f = make_function();
    LocalId unused = f->add_local("unused", hir::make_int());
    emit(*f, 0, unused, rv_bin(MirBinaryOp::Mul, cint(3), cint(4)));
    emit(*f, 0, f->return_local, rv_use(cint(0)));
    f->basic_blocks[0]->set_terminator(MirTerminator::return_value());

    int before = count_statements(*f);
    opt::DeadCodeElimination dce;
    EXPECT_TRUE(dce.run(*f));
    EXPECT_LT(count_statements(*f), before);
}

TEST(MirPassTest, DCE_RemovesUnreachableBlock) {
    // どこからも参照されないブロックが削除される
    auto f = make_function();
    f->basic_blocks[0]->set_terminator(MirTerminator::return_value());
    BlockId dead = f->add_block();
    emit(*f, dead, f->return_local, rv_use(cint(99)));
    f->basic_blocks[dead]->set_terminator(MirTerminator::return_value());

    opt::DeadCodeElimination dce;
    dce.run(*f);
    // 到達不能ブロックは削除される（nullptr化または除去）
    bool dead_alive = dead < f->basic_blocks.size() && f->basic_blocks[dead] &&
                      !f->basic_blocks[dead]->statements.empty();
    EXPECT_FALSE(dead_alive);
}

// ============================================================
// SCCP（疎条件付き定数伝播）
// ============================================================

TEST(MirPassTest, SCCP_PropagatesAcrossBlocks) {
    // b0: _c = 1; switch(_c) [1→b1] else b2
    // b1: _x = 5; goto b3 / b2: _x = 7; goto b3 / b3: _y = _x + 1
    // 分岐は定数でb1側のみ到達し、_x=5・_y=6が伝播される
    auto f = make_function();
    LocalId c = f->add_local("c", hir::make_int());
    LocalId x = f->add_local("x", hir::make_int());
    LocalId y = f->add_local("y", hir::make_int());
    BlockId b1 = f->add_block();
    BlockId b2 = f->add_block();
    BlockId b3 = f->add_block();
    emit(*f, 0, c, rv_use(cint(1)));
    f->basic_blocks[0]->set_terminator(MirTerminator::switch_int(use_of(c), {{1, b1}}, b2));
    emit(*f, b1, x, rv_use(cint(5)));
    f->basic_blocks[b1]->set_terminator(MirTerminator::goto_block(b3));
    emit(*f, b2, x, rv_use(cint(7)));
    f->basic_blocks[b2]->set_terminator(MirTerminator::goto_block(b3));
    emit(*f, b3, y, rv_bin(MirBinaryOp::Add, use_of(x), cint(1)));
    f->basic_blocks[b3]->set_terminator(MirTerminator::return_value());

    opt::SparseConditionalConstantPropagation sccp;
    EXPECT_TRUE(sccp.run(*f));
    // 到達しないb2を除外して_x=5と確定し、_y = 5+1 = 6 へ畳み込まれる
    EXPECT_TRUE(is_const_use(*f, b3, 0, 6));
}

// ============================================================
// SimplifyControlFlow（CFG簡約）
// ============================================================

TEST(MirPassTest, SimplifyCFG_CollapsesGotoChain) {
    // b0 -> b1(空) -> b2(空) -> b3 のGoto連鎖が短絡される
    auto f = make_function();
    BlockId b1 = f->add_block();
    BlockId b2 = f->add_block();
    BlockId b3 = f->add_block();
    f->basic_blocks[0]->set_terminator(MirTerminator::goto_block(b1));
    f->basic_blocks[b1]->set_terminator(MirTerminator::goto_block(b2));
    f->basic_blocks[b2]->set_terminator(MirTerminator::goto_block(b3));
    emit(*f, b3, f->return_local, rv_use(cint(0)));
    f->basic_blocks[b3]->set_terminator(MirTerminator::return_value());

    opt::SimplifyControlFlow simplify;
    EXPECT_TRUE(simplify.run(*f));
    // Goto連鎖はブロックマージで完全に潰れ、b3の内容と終端が
    // エントリブロックへ取り込まれる
    EXPECT_EQ(f->basic_blocks[0]->terminator->kind, MirTerminator::Return);
    EXPECT_EQ(f->basic_blocks[0]->statements.size(), 1u);
}

// ============================================================
// LICM（ループ不変式移動）
// ============================================================

TEST(MirPassTest, LICM_HoistsInvariantOutOfHeader) {
    // 現実装はループヘッダブロック内の文のみを巻き上げ対象とする
    // （本体ブロックの不変式は対象外。README参照）。
    // ヘッダ内の _inv = _x * _y がプリヘッダへ移動される
    auto f = make_function();
    LocalId x = f->add_local("x", hir::make_int());
    LocalId y = f->add_local("y", hir::make_int());
    LocalId n = f->add_local("n", hir::make_int());
    f->arg_locals = {x, y, n};
    LocalId acc = f->add_local("acc", hir::make_int());
    LocalId i = f->add_local("i", hir::make_int());
    LocalId cond = f->add_local("cond", hir::make_bool());
    LocalId inv = f->add_local("inv", hir::make_int());

    BlockId header = f->add_block();
    BlockId body = f->add_block();
    BlockId exit = f->add_block();

    emit(*f, 0, acc, rv_use(cint(0)));
    emit(*f, 0, i, rv_use(cint(0)));
    f->basic_blocks[0]->set_terminator(MirTerminator::goto_block(header));

    emit(*f, header, inv, rv_bin(MirBinaryOp::Mul, use_of(x), use_of(y)));
    emit(*f, header, cond, rv_bin(MirBinaryOp::Lt, use_of(i), use_of(n), hir::make_bool()));
    f->basic_blocks[header]->set_terminator(
        MirTerminator::switch_int(use_of(cond, hir::make_bool()), {{1, body}}, exit));

    emit(*f, body, acc, rv_bin(MirBinaryOp::Add, use_of(acc), use_of(inv)));
    emit(*f, body, i, rv_bin(MirBinaryOp::Add, use_of(i), cint(1)));
    f->basic_blocks[body]->set_terminator(MirTerminator::goto_block(header));

    emit(*f, exit, f->return_local, rv_use(use_of(acc)));
    f->basic_blocks[exit]->set_terminator(MirTerminator::return_value());
    f->build_cfg();

    int stmts_before = count_statements(*f);
    size_t header_stmts_before = f->basic_blocks[header]->statements.size();

    opt::LoopInvariantCodeMotion licm;
    EXPECT_TRUE(licm.run(*f));
    // 巻き上げは移動であって削除ではない
    EXPECT_EQ(count_statements(*f), stmts_before);
    EXPECT_LT(f->basic_blocks[header]->statements.size(), header_stmts_before);
}

// ============================================================
// ConstantLoopUnroll（定数ループ展開）
// ============================================================

TEST(MirPassTest, ConstUnroll_UnrollsConstantTripLoop) {
    // while (_i < 4) { _acc += _i; _i += 1; } が完全展開され、
    // 到達可能なCFGからサイクルが消える
    auto f = make_function();
    LocalId acc = f->add_local("acc", hir::make_int());
    LocalId i = f->add_local("i", hir::make_int());
    LocalId cond = f->add_local("cond", hir::make_bool());

    BlockId header = f->add_block();
    BlockId body = f->add_block();
    BlockId exit = f->add_block();

    emit(*f, 0, acc, rv_use(cint(0)));
    emit(*f, 0, i, rv_use(cint(0)));
    f->basic_blocks[0]->set_terminator(MirTerminator::goto_block(header));

    emit(*f, header, cond, rv_bin(MirBinaryOp::Lt, use_of(i), cint(4), hir::make_bool()));
    // 展開器は「switch [1→本体], otherwise 出口」の形のみ受理する
    f->basic_blocks[header]->set_terminator(
        MirTerminator::switch_int(use_of(cond, hir::make_bool()), {{1, body}}, exit));

    emit(*f, body, acc, rv_bin(MirBinaryOp::Add, use_of(acc), use_of(i)));
    emit(*f, body, i, rv_bin(MirBinaryOp::Add, use_of(i), cint(1)));
    f->basic_blocks[body]->set_terminator(MirTerminator::goto_block(header));

    emit(*f, exit, f->return_local, rv_use(use_of(acc)));
    f->basic_blocks[exit]->set_terminator(MirTerminator::return_value());
    f->build_cfg();

    ASSERT_TRUE(has_reachable_cycle(*f));
    int stmts_before = count_statements(*f);

    opt::ConstantLoopUnroll unroll(/*max_trips=*/64);
    EXPECT_TRUE(unroll.run(*f));
    // 展開後、到達可能なCFGは非循環（旧ループブロックは未到達で残りDCEが除去する）
    EXPECT_FALSE(has_reachable_cycle(*f));
    EXPECT_GT(count_statements(*f), stmts_before);
}

// ============================================================
// TailCallElimination（末尾呼び出し）
// ============================================================

TEST(MirPassTest, TCE_MarksSelfTailCall) {
    // 自己再帰の末尾呼び出しが is_tail_call としてマークされる
    // （LLVMコード生成で tail call 属性になる）
    auto f = make_function("count_down");
    LocalId ret = f->return_local;
    BlockId after = f->add_block();
    f->basic_blocks[0]->set_terminator(call_terminator("count_down", after, MirPlace{ret}));
    f->basic_blocks[after]->set_terminator(MirTerminator::return_value());

    ASSERT_EQ(count_tail_calls(*f), 0);
    opt::TailCallElimination tce;
    EXPECT_TRUE(tce.run(*f));
    EXPECT_EQ(count_tail_calls(*f), 1);
}

TEST(MirPassTest, TCE_IgnoresNonSelfCall) {
    // 他関数への呼び出しはマークされない
    auto f = make_function("caller");
    BlockId after = f->add_block();
    f->basic_blocks[0]->set_terminator(call_terminator("other", after, MirPlace{f->return_local}));
    f->basic_blocks[after]->set_terminator(MirTerminator::return_value());

    opt::TailCallElimination tce;
    EXPECT_FALSE(tce.run(*f));
    EXPECT_EQ(count_tail_calls(*f), 0);
}

// ============================================================
// FunctionInlining（インライン化）
// ============================================================

TEST(MirPassTest, FunctionInlining_CurrentlyDormant) {
    // 既知の問題を固定するテスト: インライン化パスは呼び出し先を旧形式の
    // Constant(文字列)として期待するが、現行のMIR loweringはFunctionRefを
    // 発行するため、実質的に全呼び出しが対象外（パスは休眠状態）。
    // FunctionRefを認識させて有効化するとperform_inliningの潜在バグ
    // （デストラクタ順序破壊・SIGSEGV等）が露出するため、有効化は
    // perform_inliningの再設計とセットで行う（inlining.cppのコメント参照）。
    // 有効化された際はこのテストを展開検証（Call終端子の減少）へ書き換えること
    MirProgram program;
    {
        auto callee = make_function("add");
        emit(*callee, 0, callee->return_local, rv_use(cint(1)));
        callee->basic_blocks[0]->set_terminator(MirTerminator::return_value());
        program.functions.push_back(std::move(callee));
    }
    {
        auto caller = make_function("caller");
        BlockId after = caller->add_block();
        caller->basic_blocks[0]->set_terminator(
            call_terminator("add", after, MirPlace{caller->return_local}));
        caller->basic_blocks[after]->set_terminator(MirTerminator::return_value());
        program.functions.push_back(std::move(caller));
    }

    opt::FunctionInlining inlining;
    EXPECT_FALSE(inlining.run_on_program(program));
    auto* caller = find_function(program, "caller");
    ASSERT_NE(caller, nullptr);
    EXPECT_EQ(caller->basic_blocks[0]->terminator->kind, MirTerminator::Call);
}

// ============================================================
// ProgramDCE（未到達関数除去）
// ============================================================

TEST(MirPassTest, ProgramDCE_RemovesUnreachableFunction) {
    MirProgram program;
    {
        auto used = make_function("used");
        emit(*used, 0, used->return_local, rv_use(cint(1)));
        used->basic_blocks[0]->set_terminator(MirTerminator::return_value());
        program.functions.push_back(std::move(used));
    }
    {
        auto unused_fn = make_function("unused_fn");
        emit(*unused_fn, 0, unused_fn->return_local, rv_use(cint(2)));
        unused_fn->basic_blocks[0]->set_terminator(MirTerminator::return_value());
        program.functions.push_back(std::move(unused_fn));
    }
    {
        auto main_fn = make_function("main");
        BlockId after = main_fn->add_block();
        main_fn->basic_blocks[0]->set_terminator(
            call_terminator("used", after, MirPlace{main_fn->return_local}));
        main_fn->basic_blocks[after]->set_terminator(MirTerminator::return_value());
        program.functions.push_back(std::move(main_fn));
    }

    opt::ProgramDeadCodeElimination program_dce;
    EXPECT_TRUE(program_dce.run(program));
    EXPECT_EQ(find_function(program, "unused_fn"), nullptr);
    EXPECT_NE(find_function(program, "used"), nullptr);
    EXPECT_NE(find_function(program, "main"), nullptr);
}
