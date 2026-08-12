// --sanitize=bounds 計装パスの実装（M1）
// スライスアクセス呼び出し（Callターミネータ）を持つブロックを分割し、
//   B:  ... 既存文 ...; Call cm_slice_len(slice) -> len_local, success=B1
//   B1: t1 = Lt(index, 0); SwitchInt(t1, {0 -> B2}, otherwise -> ERR)
//   B2: t2 = Lt(index, len); SwitchInt(t2, {0 -> ERR}, otherwise -> CONT)
//   CONT: 元のスライスアクセス呼び出し（ターミネータ）
//   ERR:  Call cm_bounds_error(index, len) -> Unreachable
// の形へ書き換える。indexが負にならない符号なし定数の場合も一律に検査する（単純さ優先）。

#include "bounds.hpp"

#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cm::mir::opt {

namespace {

// 検査対象のスライスアクセス関数か（第0引数=スライス、第1引数=インデックス）
bool is_indexed_slice_access(const std::string& name) {
    if (name.rfind("cm_slice_get_", 0) == 0) {
        // subsliceは(start, end)の2インデックスで契約が異なるため対象外
        return name != "cm_slice_get_subslice";
    }
    return name == "cm_slice_delete";
}

// Move/Copy/Constantのオペランドを読み取り専用のクローンとして複製する
MirOperandPtr clone_operand_for_bounds(const MirOperand& op) {
    switch (op.kind) {
        case MirOperand::Move:
        case MirOperand::Copy:
            return MirOperand::copy(std::get<MirPlace>(op.data), op.type);
        case MirOperand::Constant: {
            auto cloned = std::make_unique<MirOperand>();
            cloned->kind = MirOperand::Constant;
            cloned->data = std::get<MirConstant>(op.data);
            cloned->type = op.type;
            return cloned;
        }
        default:
            return nullptr;
    }
}

// cm_bounds_error(index, len) -> Unreachable のエラーブロックを作成する
BlockId create_bounds_error_block(MirFunction& func, MirOperandPtr index, MirOperandPtr len) {
    BlockId error_block = func.add_block();
    BlockId unreachable_block = func.add_block();

    auto call_term = std::make_unique<MirTerminator>();
    call_term->kind = MirTerminator::Call;
    MirTerminator::CallData call_data;
    call_data.func = MirOperand::function_ref("cm_bounds_error");
    call_data.args.push_back(std::move(index));
    call_data.args.push_back(std::move(len));
    call_data.destination = std::nullopt;
    call_data.success = unreachable_block;
    call_term->data = std::move(call_data);
    func.get_block(error_block)->set_terminator(std::move(call_term));

    func.get_block(unreachable_block)->set_terminator(MirTerminator::unreachable());
    return error_block;
}

}  // namespace

bool BoundsCheckInstrumentation::run(MirFunction& func) {
    bool changed = false;
    std::unordered_set<const MirTerminator*> done;

    for (size_t bi = 0; bi < func.basic_blocks.size(); ++bi) {
        auto* block = func.basic_blocks[bi].get();
        if (!block || !block->terminator || done.count(block->terminator.get())) {
            continue;
        }
        if (block->terminator->kind != MirTerminator::Call) {
            continue;
        }
        auto& call = std::get<MirTerminator::CallData>(block->terminator->data);
        if (!call.func || call.func->kind != MirOperand::FunctionRef) {
            continue;
        }
        const std::string& callee = std::get<std::string>(call.func->data);
        if (!is_indexed_slice_access(callee) || call.args.size() < 2 || !call.args[0] ||
            !call.args[1]) {
            continue;
        }
        done.insert(block->terminator.get());

        auto slice_clone = clone_operand_for_bounds(*call.args[0]);
        auto index_for_cmp1 = clone_operand_for_bounds(*call.args[1]);
        auto index_for_cmp2 = clone_operand_for_bounds(*call.args[1]);
        auto index_for_err = clone_operand_for_bounds(*call.args[1]);
        if (!slice_clone || !index_for_cmp1 || !index_for_cmp2 || !index_for_err) {
            continue;
        }

        // 元のアクセス呼び出しをCONTブロックへ移す
        BlockId cont_block = func.add_block();
        block = func.basic_blocks[bi].get();
        func.get_block(cont_block)->set_terminator(std::move(block->terminator));

        // len取得・比較・エラーブロックを構築する
        LocalId len_local = func.add_local("_bounds_len", hir::make_long(), true, false);
        BlockId cmp1_block = func.add_block();
        BlockId cmp2_block = func.add_block();
        block = func.basic_blocks[bi].get();

        BlockId error_block =
            create_bounds_error_block(func, std::move(index_for_err),
                                      MirOperand::copy(MirPlace{len_local}, hir::make_long()));

        // B: Call cm_slice_len(slice) -> len_local, success=cmp1
        {
            auto len_term = std::make_unique<MirTerminator>();
            len_term->kind = MirTerminator::Call;
            MirTerminator::CallData len_call;
            len_call.func = MirOperand::function_ref("cm_slice_len");
            len_call.args.push_back(std::move(slice_clone));
            len_call.destination = MirPlace{len_local};
            len_call.success = cmp1_block;
            len_term->data = std::move(len_call);
            func.get_block(block->id)->set_terminator(std::move(len_term));
        }

        // cmp1: t1 = Lt(index, 0); t1が真（負）ならERR
        {
            auto* cmp1 = func.get_block(cmp1_block);
            LocalId t1 = func.add_local("_bounds_neg", hir::make_bool(), true, false);
            auto zero = std::make_unique<MirOperand>();
            zero->kind = MirOperand::Constant;
            zero->data = MirConstant{int64_t{0}, hir::make_long(), {}};
            zero->type = hir::make_long();
            cmp1->add_statement(MirStatement::assign(
                MirPlace{t1}, MirRvalue::binary(MirBinaryOp::Lt, std::move(index_for_cmp1),
                                                std::move(zero), hir::make_bool())));
            auto sw = std::make_unique<MirTerminator>();
            sw->kind = MirTerminator::SwitchInt;
            sw->data =
                MirTerminator::SwitchIntData{MirOperand::copy(MirPlace{t1}, hir::make_bool()),
                                             {{0, cmp2_block}},
                                             error_block,
                                             {}};
            cmp1->set_terminator(std::move(sw));
        }

        // cmp2: t2 = Lt(index, len); t2が偽（index >= len）ならERR
        {
            auto* cmp2 = func.get_block(cmp2_block);
            LocalId t2 = func.add_local("_bounds_in", hir::make_bool(), true, false);
            cmp2->add_statement(MirStatement::assign(
                MirPlace{t2},
                MirRvalue::binary(MirBinaryOp::Lt, std::move(index_for_cmp2),
                                  MirOperand::copy(MirPlace{len_local}, hir::make_long()),
                                  hir::make_bool())));
            auto sw = std::make_unique<MirTerminator>();
            sw->kind = MirTerminator::SwitchInt;
            sw->data =
                MirTerminator::SwitchIntData{MirOperand::copy(MirPlace{t2}, hir::make_bool()),
                                             {{0, error_block}},
                                             cont_block,
                                             {}};
            cmp2->set_terminator(std::move(sw));
        }

        changed = true;
    }

    if (changed) {
        func.build_cfg();
    }
    return changed;
}

void instrument_bounds_checks(MirProgram& program) {
    BoundsCheckInstrumentation pass;
    for (auto& func : program.functions) {
        if (func) {
            pass.run(*func);
        }
    }
}

}  // namespace cm::mir::opt
