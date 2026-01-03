#pragma once

#include "../core/base.hpp"
#include "../../../common/debug.hpp"

#include <algorithm>
#include <queue>
#include <set>

namespace cm::mir::opt {

// ============================================================
// デッドコード除去最適化
// ============================================================
class DeadCodeElimination : public OptimizationPass {
   public:
    std::string name() const override { return "Dead Code Elimination"; }

    bool run(MirFunction& func) override {
        bool changed = false;

        // 1. 到達不可能ブロックを除去
        changed |= remove_unreachable_blocks(func);

        // 2. 使用されていないローカル変数への代入を除去
        changed |= remove_dead_stores(func);

        // 3. 副作用のない未使用文を除去
        changed |= remove_dead_statements(func);

        return changed;
    }

   private:
    // 到達不可能ブロックを除去
    bool remove_unreachable_blocks(MirFunction& func) {
        // terminatorからsuccessorsを再構築
        for (auto& block : func.basic_blocks) {
            if (!block)
                continue;
            block->update_successors();
        }

        // 到達可能なブロックを探索
        std::set<BlockId> reachable;
        std::queue<BlockId> worklist;

        // エントリーブロックから開始
        worklist.push(func.entry_block);
        reachable.insert(func.entry_block);

        // BFSで到達可能なブロックをマーク
        while (!worklist.empty()) {
            BlockId current = worklist.front();
            worklist.pop();

            if (auto* block = func.get_block(current)) {
                for (BlockId succ : block->successors) {
                    if (reachable.find(succ) == reachable.end()) {
                        reachable.insert(succ);
                        worklist.push(succ);
                    }
                }
            }
        }

        // 到達不可能ブロックを削除
        bool changed = false;
        for (size_t i = 0; i < func.basic_blocks.size(); ++i) {
            if (reachable.find(i) == reachable.end()) {
                // ブロックを無効化（nullptrに）
                if (func.basic_blocks[i]) {
                    func.basic_blocks[i] = nullptr;
                    changed = true;
                }
            }
        }

        if (changed) {
            // CFGを再構築
            func.build_cfg();
        }

        return changed;
    }

    // 使用されていないローカル変数への代入を除去
    bool remove_dead_stores(MirFunction& func) {
        // 使用されているローカル変数を収集
        std::set<LocalId> used_locals;
        collect_used_locals(func, used_locals);

        // 戻り値は常に使用される
        used_locals.insert(func.return_local);

        // パラメータは常に使用される
        for (LocalId param : func.arg_locals) {
            used_locals.insert(param);
        }

        bool changed = false;

        // 各ブロックを処理
        for (auto& block : func.basic_blocks) {
            if (!block)
                continue;

            auto& stmts = block->statements;
            auto it = stmts.begin();

            while (it != stmts.end()) {
                bool should_remove = false;

                if ((*it)->kind == MirStatement::Assign) {
                    auto& assign_data = std::get<MirStatement::AssignData>((*it)->data);

                    // 単純な変数への代入
                    if (assign_data.place.projections.empty()) {
                        LocalId target = assign_data.place.local;

                        // 未使用のローカル変数への代入は削除
                        if (used_locals.find(target) == used_locals.end()) {
                            // ただし、副作用がある可能性がある場合は削除しない
                            if (!has_side_effects(assign_data.rvalue.get())) {
                                should_remove = true;
                            }
                        }
                    }
                } else if ((*it)->kind == MirStatement::StorageLive ||
                           (*it)->kind == MirStatement::StorageDead) {
                    auto& storage_data = std::get<MirStatement::StorageData>((*it)->data);

                    // 未使用変数のStorageLive/DeadもNopに
                    if (used_locals.find(storage_data.local) == used_locals.end()) {
                        (*it)->kind = MirStatement::Nop;
                        (*it)->data = std::monostate{};
                        changed = true;
                    }
                }

                if (should_remove) {
                    // 文をNopに置き換え
                    (*it)->kind = MirStatement::Nop;
                    (*it)->data = std::monostate{};
                    changed = true;
                }

                ++it;
            }
        }

        return changed;
    }

    // 副作用のない未使用文を除去
    bool remove_dead_statements(MirFunction& func) {
        bool changed = false;

        for (auto& block : func.basic_blocks) {
            if (!block)
                continue;

            auto& stmts = block->statements;

            // Nop文を除去
            auto new_end =
                std::remove_if(stmts.begin(), stmts.end(), [&changed](const MirStatementPtr& stmt) {
                    if (stmt->kind == MirStatement::Nop) {
                        changed = true;
                        return true;
                    }
                    return false;
                });

            stmts.erase(new_end, stmts.end());
        }

        return changed;
    }

    // 使用されているローカル変数を収集
    void collect_used_locals(const MirFunction& func, std::set<LocalId>& used) {
        for (const auto& block : func.basic_blocks) {
            if (!block)
                continue;

            // 文から収集
            for (const auto& stmt : block->statements) {
                collect_used_locals_in_statement(*stmt, used);
            }

            // 終端命令から収集
            if (block->terminator) {
                collect_used_locals_in_terminator(*block->terminator, used);
            }
        }
    }

    void collect_used_locals_in_statement(const MirStatement& stmt, std::set<LocalId>& used) {
        if (stmt.kind == MirStatement::Assign) {
            const auto& assign_data = std::get<MirStatement::AssignData>(stmt.data);

            // 右辺から使用を収集
            if (assign_data.rvalue) {
                collect_used_locals_in_rvalue(*assign_data.rvalue, used);
            }

            // 左辺のプロジェクションから使用を収集
            for (const auto& proj : assign_data.place.projections) {
                if (proj.kind == ProjectionKind::Index) {
                    used.insert(proj.index_local);
                } else if (proj.kind == ProjectionKind::Deref) {
                    // Derefプロジェクションがある場合、ポインタ変数自体が使用される
                    used.insert(assign_data.place.local);
                }
            }
            // フィールドプロジェクションがある場合も、ベース変数が使用される
            if (!assign_data.place.projections.empty()) {
                used.insert(assign_data.place.local);
            }
        }
    }

    void collect_used_locals_in_rvalue(const MirRvalue& rvalue, std::set<LocalId>& used) {
        switch (rvalue.kind) {
            case MirRvalue::Use: {
                if (const auto* use_data = std::get_if<MirRvalue::UseData>(&rvalue.data)) {
                    if (use_data->operand) {
                        collect_used_locals_in_operand(*use_data->operand, used);
                    }
                }
                break;
            }
            case MirRvalue::BinaryOp: {
                const auto& bin_data = std::get<MirRvalue::BinaryOpData>(rvalue.data);
                if (bin_data.lhs)
                    collect_used_locals_in_operand(*bin_data.lhs, used);
                if (bin_data.rhs)
                    collect_used_locals_in_operand(*bin_data.rhs, used);
                break;
            }
            case MirRvalue::UnaryOp: {
                const auto& unary_data = std::get<MirRvalue::UnaryOpData>(rvalue.data);
                if (unary_data.operand) {
                    collect_used_locals_in_operand(*unary_data.operand, used);
                }
                break;
            }
            case MirRvalue::Ref: {
                const auto& ref_data = std::get<MirRvalue::RefData>(rvalue.data);
                used.insert(ref_data.place.local);
                for (const auto& proj : ref_data.place.projections) {
                    if (proj.kind == ProjectionKind::Index) {
                        used.insert(proj.index_local);
                    }
                }
                break;
            }
            case MirRvalue::Aggregate: {
                const auto& agg_data = std::get<MirRvalue::AggregateData>(rvalue.data);
                for (const auto& op : agg_data.operands) {
                    if (op)
                        collect_used_locals_in_operand(*op, used);
                }
                break;
            }
            case MirRvalue::FormatConvert: {
                const auto& fmt_data = std::get<MirRvalue::FormatConvertData>(rvalue.data);
                if (fmt_data.operand) {
                    collect_used_locals_in_operand(*fmt_data.operand, used);
                }
                break;
            }
            case MirRvalue::Cast: {
                const auto& cast_data = std::get<MirRvalue::CastData>(rvalue.data);
                if (cast_data.operand) {
                    collect_used_locals_in_operand(*cast_data.operand, used);
                }
                break;
            }
        }
    }

    void collect_used_locals_in_operand(const MirOperand& op, std::set<LocalId>& used) {
        if (op.kind == MirOperand::Move || op.kind == MirOperand::Copy) {
            if (const auto* place = std::get_if<MirPlace>(&op.data)) {
                used.insert(place->local);
                for (const auto& proj : place->projections) {
                    if (proj.kind == ProjectionKind::Index) {
                        used.insert(proj.index_local);
                    }
                }
            }
        }
    }

    void collect_used_locals_in_terminator(const MirTerminator& term, std::set<LocalId>& used) {
        switch (term.kind) {
            case MirTerminator::SwitchInt: {
                const auto& switch_data = std::get<MirTerminator::SwitchIntData>(term.data);
                if (switch_data.discriminant) {
                    collect_used_locals_in_operand(*switch_data.discriminant, used);
                }
                break;
            }
            case MirTerminator::Call: {
                const auto& call_data = std::get<MirTerminator::CallData>(term.data);
                // func オペランドから使用を収集（関数ポインタ経由の呼び出しの場合）
                if (call_data.func) {
                    collect_used_locals_in_operand(*call_data.func, used);
                }
                // 引数から使用を収集
                for (const auto& arg : call_data.args) {
                    if (arg)
                        collect_used_locals_in_operand(*arg, used);
                }
                break;
            }
            default:
                break;
        }
    }

    // Rvalueが副作用を持つかチェック
    bool has_side_effects(const MirRvalue* rvalue) const {
        if (!rvalue)
            return false;

        // 現在の実装では、関数呼び出し以外は副作用なしと仮定
        // TODO: より詳細な副作用解析
        return false;
    }
};

}  // namespace cm::mir::opt
