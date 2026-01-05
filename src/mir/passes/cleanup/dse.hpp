#pragma once

#include "../core/base.hpp"

#include <unordered_map>
#include <unordered_set>
#include <variant>

namespace cm::mir::opt {

// ============================================================
// デッドストア除去（Dead Store Elimination）- ローカル版
// ============================================================
class DeadStoreElimination : public OptimizationPass {
   public:
    std::string name() const override { return "Dead Store Elimination"; }

    bool run(MirFunction& func) override {
        bool changed = false;

        for (auto& block : func.basic_blocks) {
            if (!block)
                continue;
            changed |= process_block(*block, func);
        }

        return changed;
    }

   private:
    bool process_block(BasicBlock& block, const MirFunction& func) {
        bool changed = false;

        // ローカル変数ID -> 最後の定義命令（MirStatementへのポインタ）
        // この定義が使用される前に上書きされた場合、この定義は削除可能
        std::unordered_map<LocalId, MirStatement*> last_def;

        for (auto& stmt : block.statements) {
            if (stmt->kind == MirStatement::Nop)
                continue;

            // 1. 文で使用される変数を収集
            std::unordered_set<LocalId> used;
            bool uses_deref = false;

            collect_uses(*stmt, used, uses_deref);

            // Deref使用がある場合、エイリアスの可能性があるので全定義をフラッシュ（保守的）
            if (uses_deref) {
                last_def.clear();
            } else {
                // 通常の使用：使用された変数の定義は「生きている」ので追跡から外す
                for (LocalId use : used) {
                    last_def.erase(use);
                }
            }

            // 2. 定義（代入）を処理
            if (stmt->kind == MirStatement::Assign) {
                auto& assign_data = std::get<MirStatement::AssignData>(stmt->data);

                // 単純な変数への代入の場合
                if (assign_data.place.projections.empty()) {
                    LocalId target = assign_data.place.local;

                    // 以前の定義があり、かつその定義以降に使用されていない場合
                    auto it = last_def.find(target);
                    if (it != last_def.end()) {
                        // 以前の定義はデッドストア
                        // 副作用がないことを確認
                        it->second->kind = MirStatement::Nop;
                        it->second->data = std::monostate{};
                        changed = true;
                    }

                    // 新しい定義を登録（副作用がない場合のみ追跡）
                    if (!has_side_effects(*assign_data.rvalue)) {
                        last_def[target] = stmt.get();
                    } else {
                        last_def.erase(target);
                    }
                } else {
                    last_def.erase(assign_data.place.local);
                }
            } else if (stmt->kind == MirStatement::StorageLive ||
                       stmt->kind == MirStatement::StorageDead) {
                auto& storage_data = std::get<MirStatement::StorageData>(stmt->data);
                if (stmt->kind == MirStatement::StorageDead) {
                    auto it = last_def.find(storage_data.local);
                    if (it != last_def.end()) {
                        // Dead store
                        it->second->kind = MirStatement::Nop;
                        it->second->data = std::monostate{};
                        changed = true;
                        last_def.erase(it);
                    }
                } else {
                    auto it = last_def.find(storage_data.local);
                    if (it != last_def.end()) {
                        last_def.erase(it);
                    }
                }
            }
        }

        // 最後にTerminatorをチェック
        if (block.terminator) {
            std::unordered_set<LocalId> used;
            bool uses_deref = false;
            collect_terminator_uses(*block.terminator, used, uses_deref);

            // Return uses return_local implicitly
            if (block.terminator->kind == MirTerminator::Return) {
                used.insert(func.return_local);
            }

            if (uses_deref || block.terminator->kind == MirTerminator::Call) {
                last_def.clear();

                // Call destination write check
                if (block.terminator->kind == MirTerminator::Call) {
                    auto& d = std::get<MirTerminator::CallData>(block.terminator->data);
                    // Call is terminator, so we check if it overwrites any previous def
                    if (d.destination && d.destination->projections.empty()) {
                        // Call with destination overwrites the destination local.
                        // However, since we cleared last_def (due to assumed read/alias),
                        // we simply cannot optimize previous stores based on this Call write.
                        // But if we knew Call didn't read 'dest', we could.
                        // For now, safety first: do nothing.
                    }
                }
            } else {
                for (LocalId use : used) {
                    last_def.erase(use);
                }
            }
        }

        return changed;
    }

    void collect_uses(const MirStatement& stmt, std::unordered_set<LocalId>& used,
                      bool& uses_deref) {
        if (stmt.kind == MirStatement::Assign) {
            auto& assign_data = std::get<MirStatement::AssignData>(stmt.data);
            if (assign_data.rvalue) {
                collect_rvalue_uses(*assign_data.rvalue, used, uses_deref);
            }

            for (const auto& proj : assign_data.place.projections) {
                if (proj.kind == ProjectionKind::Index) {
                    used.insert(proj.index_local);
                } else if (proj.kind == ProjectionKind::Deref) {
                    uses_deref = true;
                }
            }
            if (!assign_data.place.projections.empty()) {
                used.insert(assign_data.place.local);
            }
        }
    }

    void collect_rvalue_uses(const MirRvalue& rvalue, std::unordered_set<LocalId>& used,
                             bool& uses_deref) {
        switch (rvalue.kind) {
            case MirRvalue::Use: {
                if (auto* d = std::get_if<MirRvalue::UseData>(&rvalue.data))
                    if (d->operand)
                        collect_operand_uses(*d->operand, used, uses_deref);
                break;
            }
            case MirRvalue::BinaryOp: {
                auto& d = std::get<MirRvalue::BinaryOpData>(rvalue.data);
                if (d.lhs)
                    collect_operand_uses(*d.lhs, used, uses_deref);
                if (d.rhs)
                    collect_operand_uses(*d.rhs, used, uses_deref);
                break;
            }
            case MirRvalue::UnaryOp: {
                auto& d = std::get<MirRvalue::UnaryOpData>(rvalue.data);
                if (d.operand)
                    collect_operand_uses(*d.operand, used, uses_deref);
                break;
            }
            case MirRvalue::Cast: {
                auto& d = std::get<MirRvalue::CastData>(rvalue.data);
                if (d.operand)
                    collect_operand_uses(*d.operand, used, uses_deref);
                break;
            }
            case MirRvalue::Ref: {
                auto& d = std::get<MirRvalue::RefData>(rvalue.data);
                used.insert(d.place.local);
                for (const auto& p : d.place.projections) {
                    if (p.kind == ProjectionKind::Index)
                        used.insert(p.index_local);
                    if (p.kind == ProjectionKind::Deref)
                        uses_deref = true;
                }
                break;
            }
            case MirRvalue::Aggregate: {
                auto& d = std::get<MirRvalue::AggregateData>(rvalue.data);
                for (const auto& op : d.operands)
                    if (op)
                        collect_operand_uses(*op, used, uses_deref);
                break;
            }
            default:
                uses_deref = true;
                break;
        }
    }

    void collect_operand_uses(const MirOperand& op, std::unordered_set<LocalId>& used,
                              bool& uses_deref) {
        if (op.kind == MirOperand::Move || op.kind == MirOperand::Copy) {
            if (const auto* place = std::get_if<MirPlace>(&op.data)) {
                used.insert(place->local);
                for (const auto& proj : place->projections) {
                    if (proj.kind == ProjectionKind::Index) {
                        used.insert(proj.index_local);
                    } else if (proj.kind == ProjectionKind::Deref) {
                        uses_deref = true;
                    }
                }
            }
        }
    }

    void collect_terminator_uses(const MirTerminator& term, std::unordered_set<LocalId>& used,
                                 bool& uses_deref) {
        switch (term.kind) {
            case MirTerminator::SwitchInt: {
                const auto& d = std::get<MirTerminator::SwitchIntData>(term.data);
                if (d.discriminant)
                    collect_operand_uses(*d.discriminant, used, uses_deref);
                break;
            }
            case MirTerminator::Call: {
                const auto& d = std::get<MirTerminator::CallData>(term.data);
                for (const auto& arg : d.args) {
                    if (arg)
                        collect_operand_uses(*arg, used, uses_deref);
                }
                break;
            }
            case MirTerminator::Return: {
                // Return uses return local implicitly, checked in process_block
                break;
            }
            default:
                break;
        }
    }

    bool has_side_effects(const MirRvalue& /* rvalue */) const { return false; }
};

}  // namespace cm::mir::opt
