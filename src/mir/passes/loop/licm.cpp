#include "licm.hpp"

namespace cm::mir::opt {

bool LoopInvariantCodeMotion::run(MirFunction& func) {
    bool changed = false;

    // 1. Dominator Tree & Loop Analysis構築
    cm::mir::DominatorTree dom_tree(func);
    cm::mir::LoopAnalysis loop_analysis(func, dom_tree);

    // 2. ループごとに処理（内側から外側へ）
    for (auto* loop : loop_analysis.get_top_level_loops()) {
        if (process_loop(func, loop)) {
            changed = true;
        }
    }

    return changed;
}

bool LoopInvariantCodeMotion::process_loop(MirFunction& func, cm::mir::Loop* loop) {
    bool changed = false;

    // まずサブループを処理
    for (auto* sub : loop->sub_loops) {
        if (process_loop(func, sub)) {
            changed = true;
        }
    }

    // Pre-headerの取得または作成
    BlockId pre_header = get_or_create_pre_header(func, loop);
    if (pre_header == INVALID_BLOCK) {
        return changed;
    }

    // ループ内で変更される変数を特定
    std::set<LocalId> modified_locals;
    for (BlockId b : loop->blocks) {
        const auto& bb = *func.basic_blocks[b];
        for (const auto& stmt : bb.statements) {
            if (stmt->kind == MirStatement::Assign) {
                auto& assign = std::get<MirStatement::AssignData>(stmt->data);
                modified_locals.insert(assign.place.local);
            }
        }
        if (bb.terminator && bb.terminator->kind == MirTerminator::Call) {
            auto& call = std::get<MirTerminator::CallData>(bb.terminator->data);
            if (call.destination) {
                modified_locals.insert(call.destination->local);
            }
        }
    }

    // Headerブロック内の命令を走査して移動可能なものを探す
    BlockId header_id = loop->header;
    auto& header_stmts = func.basic_blocks[header_id]->statements;

    std::vector<int> to_move_indices;

    for (int i = 0; i < (int)header_stmts.size(); ++i) {
        const auto& stmt = header_stmts[i];
        if (stmt->kind == MirStatement::Assign) {
            auto& assign = std::get<MirStatement::AssignData>(stmt->data);

            if (is_invariant(*assign.rvalue, modified_locals)) {
                if (has_memory_access(*assign.rvalue))
                    continue;

                to_move_indices.push_back(i);
            }
        }
    }

    if (!to_move_indices.empty()) {
        changed = true;
        auto& pre_header_bb = *func.basic_blocks[pre_header];

        for (int idx : to_move_indices) {
            pre_header_bb.statements.push_back(std::move(header_stmts[idx]));
        }

        // Headerから削除
        std::vector<std::unique_ptr<MirStatement>> new_stmts;
        auto move_it = to_move_indices.begin();

        for (int i = 0; i < (int)header_stmts.size(); ++i) {
            if (move_it != to_move_indices.end() && *move_it == i) {
                move_it++;
            } else {
                if (header_stmts[i]) {
                    new_stmts.push_back(std::move(header_stmts[i]));
                }
            }
        }
        header_stmts = std::move(new_stmts);
    }

    return changed;
}

BlockId LoopInvariantCodeMotion::get_or_create_pre_header(MirFunction& func, cm::mir::Loop* loop) {
    BlockId header_id = loop->header;

    // HeaderへのPredecessors（バックエッジ以外）を取得
    std::vector<BlockId> entering_preds;

    for (size_t i = 0; i < func.basic_blocks.size(); ++i) {
        if (!func.basic_blocks[i])
            continue;
        if (loop->contains((BlockId)i))
            continue;

        const auto& bb = *func.basic_blocks[i];
        if (bb.terminator) {
            bool is_pred = false;
            switch (bb.terminator->kind) {
                case MirTerminator::Goto:
                    if (std::get<MirTerminator::GotoData>(bb.terminator->data).target == header_id)
                        is_pred = true;
                    break;
                case MirTerminator::SwitchInt: {
                    auto& data = std::get<MirTerminator::SwitchIntData>(bb.terminator->data);
                    if (data.otherwise == header_id)
                        is_pred = true;
                    for (auto& pair : data.targets)
                        if (pair.second == header_id)
                            is_pred = true;
                    break;
                }
                case MirTerminator::Call: {
                    auto& data = std::get<MirTerminator::CallData>(bb.terminator->data);
                    if (data.success == header_id)
                        is_pred = true;
                    break;
                }
                default:
                    break;
            }
            if (is_pred) {
                entering_preds.push_back(i);
            }
        }
    }

    if (entering_preds.empty()) {
        if (header_id == 0)
            return INVALID_BLOCK;
        return INVALID_BLOCK;
    }

    // 既存のPre-headerがあるか確認
    if (entering_preds.size() == 1) {
        BlockId pred = entering_preds[0];
        const auto& pred_bb = *func.basic_blocks[pred];
        if (pred_bb.terminator->kind == MirTerminator::Goto) {
            return pred;
        }
    }

    // Pre-headerを新規作成
    BlockId new_id = func.basic_blocks.size();
    auto pre_header = std::make_unique<BasicBlock>(new_id);

    pre_header->terminator = MirTerminator::goto_block(header_id);

    // 各PredecessorのターゲットをPre-headerに書き換え
    for (BlockId p : entering_preds) {
        auto& bb = *func.basic_blocks[p];
        if (bb.terminator) {
            switch (bb.terminator->kind) {
                case MirTerminator::Goto:
                    std::get<MirTerminator::GotoData>(bb.terminator->data).target = new_id;
                    break;
                case MirTerminator::SwitchInt: {
                    auto& data = std::get<MirTerminator::SwitchIntData>(bb.terminator->data);
                    if (data.otherwise == header_id)
                        data.otherwise = new_id;
                    for (auto& pair : data.targets)
                        if (pair.second == header_id)
                            pair.second = new_id;
                    break;
                }
                case MirTerminator::Call: {
                    auto& data = std::get<MirTerminator::CallData>(bb.terminator->data);
                    if (data.success == header_id)
                        data.success = new_id;
                    break;
                }
                default:
                    break;
            }
        }
    }

    func.basic_blocks.push_back(std::move(pre_header));
    return new_id;
}

bool LoopInvariantCodeMotion::is_invariant(const MirRvalue& rvalue,
                                           const std::set<LocalId>& modified_locals) {
    switch (rvalue.kind) {
        case MirRvalue::Use: {
            auto& data = std::get<MirRvalue::UseData>(rvalue.data);
            return is_invariant(*data.operand, modified_locals);
        }
        case MirRvalue::BinaryOp: {
            auto& data = std::get<MirRvalue::BinaryOpData>(rvalue.data);
            return is_invariant(*data.lhs, modified_locals) &&
                   is_invariant(*data.rhs, modified_locals);
        }
        case MirRvalue::UnaryOp: {
            auto& data = std::get<MirRvalue::UnaryOpData>(rvalue.data);
            return is_invariant(*data.operand, modified_locals);
        }
        case MirRvalue::Cast: {
            auto& data = std::get<MirRvalue::CastData>(rvalue.data);
            return is_invariant(*data.operand, modified_locals);
        }
        case MirRvalue::FormatConvert: {
            auto& data = std::get<MirRvalue::FormatConvertData>(rvalue.data);
            return is_invariant(*data.operand, modified_locals);
        }
        case MirRvalue::Ref:
            return false;
        default:
            return false;
    }
}

bool LoopInvariantCodeMotion::is_invariant(const MirOperand& operand,
                                           const std::set<LocalId>& modified_locals) {
    if (operand.kind == MirOperand::Constant || operand.kind == MirOperand::FunctionRef)
        return true;

    if (operand.kind == MirOperand::Copy || operand.kind == MirOperand::Move) {
        auto& place = std::get<MirPlace>(operand.data);
        if (!place.projections.empty())
            return false;

        return modified_locals.find(place.local) == modified_locals.end();
    }
    return false;
}

bool LoopInvariantCodeMotion::has_memory_access(const MirRvalue& rvalue) {
    return rvalue.kind == MirRvalue::Ref;
}

}  // namespace cm::mir::opt
