#pragma once

#include "optimization_pass.hpp"

#include <unordered_map>

namespace cm::mir::opt {

// ============================================================
// コピー伝播最適化
// ============================================================
class CopyPropagation : public OptimizationPass {
   public:
    std::string name() const override { return "Copy Propagation"; }

    bool run(MirFunction& func) override {
        bool changed = false;

        // 各ローカル変数のコピー元を追跡
        // copies[x] = y は _x = _y を意味する
        std::unordered_map<LocalId, LocalId> copies;

        // 各基本ブロックを処理
        for (auto& block : func.basic_blocks) {
            if (!block)
                continue;
            changed |= process_block(*block, copies, func);
        }

        return changed;
    }

   private:
    bool process_block(BasicBlock& block, std::unordered_map<LocalId, LocalId>& copies,
                       const MirFunction& /* func */) {
        bool changed = false;

        // 各文を処理
        for (auto& stmt : block.statements) {
            if (stmt->kind == MirStatement::Assign) {
                auto& assign_data = std::get<MirStatement::AssignData>(stmt->data);

                // まず右辺のコピー伝播を実行
                changed |= propagate_in_rvalue(*assign_data.rvalue, copies);

                // 単純なコピー代入を検出: _x = _y
                if (assign_data.place.projections.empty() &&
                    assign_data.rvalue->kind == MirRvalue::Use) {
                    if (auto* use_data =
                            std::get_if<MirRvalue::UseData>(&assign_data.rvalue->data)) {
                        if (use_data->operand && use_data->operand->kind == MirOperand::Copy) {
                            if (auto* src_place = std::get_if<MirPlace>(&use_data->operand->data)) {
                                if (src_place->projections.empty()) {
                                    // _x = _y の形式
                                    LocalId target = assign_data.place.local;
                                    LocalId source = resolve_copy_chain(src_place->local, copies);

                                    // 自己代入でない場合
                                    if (target != source) {
                                        copies[target] = source;
                                    }
                                }
                            }
                        }
                    }
                } else {
                    // 複雑な代入の場合、コピー情報を無効化
                    if (assign_data.place.projections.empty()) {
                        copies.erase(assign_data.place.local);
                    }
                }
            }
        }

        // 終端命令でのコピー伝播
        if (block.terminator) {
            changed |= propagate_in_terminator(*block.terminator, copies);
        }

        return changed;
    }

    // コピーチェーンを解決（推移的なコピーを辿る）
    LocalId resolve_copy_chain(LocalId local, const std::unordered_map<LocalId, LocalId>& copies) {
        std::set<LocalId> visited;
        LocalId current = local;

        while (copies.count(current) > 0) {
            // 循環を検出
            if (visited.count(current) > 0) {
                break;
            }
            visited.insert(current);
            current = copies.at(current);
        }

        return current;
    }

    // Rvalue内のコピーを伝播
    bool propagate_in_rvalue(MirRvalue& rvalue,
                             const std::unordered_map<LocalId, LocalId>& copies) {
        bool changed = false;

        switch (rvalue.kind) {
            case MirRvalue::Use: {
                if (auto* use_data = std::get_if<MirRvalue::UseData>(&rvalue.data)) {
                    if (use_data->operand) {
                        changed |= propagate_in_operand(*use_data->operand, copies);
                    }
                }
                break;
            }
            case MirRvalue::BinaryOp: {
                auto& bin_data = std::get<MirRvalue::BinaryOpData>(rvalue.data);
                if (bin_data.lhs)
                    changed |= propagate_in_operand(*bin_data.lhs, copies);
                if (bin_data.rhs)
                    changed |= propagate_in_operand(*bin_data.rhs, copies);
                break;
            }
            case MirRvalue::UnaryOp: {
                auto& unary_data = std::get<MirRvalue::UnaryOpData>(rvalue.data);
                if (unary_data.operand) {
                    changed |= propagate_in_operand(*unary_data.operand, copies);
                }
                break;
            }
            case MirRvalue::Ref: {
                auto& ref_data = std::get<MirRvalue::RefData>(rvalue.data);
                changed |= propagate_in_place(ref_data.place, copies);
                break;
            }
            case MirRvalue::Aggregate: {
                auto& agg_data = std::get<MirRvalue::AggregateData>(rvalue.data);
                for (auto& op : agg_data.operands) {
                    if (op)
                        changed |= propagate_in_operand(*op, copies);
                }
                break;
            }
            case MirRvalue::FormatConvert: {
                auto& fmt_data = std::get<MirRvalue::FormatConvertData>(rvalue.data);
                if (fmt_data.operand) {
                    changed |= propagate_in_operand(*fmt_data.operand, copies);
                }
                break;
            }
            case MirRvalue::Cast: {
                auto& cast_data = std::get<MirRvalue::CastData>(rvalue.data);
                if (cast_data.operand) {
                    changed |= propagate_in_operand(*cast_data.operand, copies);
                }
                break;
            }
        }

        return changed;
    }

    // Operand内のコピーを伝播
    bool propagate_in_operand(MirOperand& operand,
                              const std::unordered_map<LocalId, LocalId>& copies) {
        if (operand.kind == MirOperand::Copy || operand.kind == MirOperand::Move) {
            if (auto* place = std::get_if<MirPlace>(&operand.data)) {
                return propagate_in_place(*place, copies);
            }
        }
        return false;
    }

    // Place内のコピーを伝播
    bool propagate_in_place(MirPlace& place, const std::unordered_map<LocalId, LocalId>& copies) {
        bool changed = false;

        // ベースのローカル変数を置き換え
        LocalId new_local = resolve_copy_chain(place.local, copies);
        if (new_local != place.local) {
            place.local = new_local;
            changed = true;
        }

        // インデックスのローカル変数を置き換え
        for (auto& proj : place.projections) {
            if (proj.kind == ProjectionKind::Index) {
                LocalId new_index = resolve_copy_chain(proj.index_local, copies);
                if (new_index != proj.index_local) {
                    proj.index_local = new_index;
                    changed = true;
                }
            }
        }

        return changed;
    }

    // 終端命令でのコピー伝播
    bool propagate_in_terminator(MirTerminator& term,
                                 const std::unordered_map<LocalId, LocalId>& copies) {
        bool changed = false;

        switch (term.kind) {
            case MirTerminator::SwitchInt: {
                auto& switch_data = std::get<MirTerminator::SwitchIntData>(term.data);
                if (switch_data.discriminant) {
                    changed |= propagate_in_operand(*switch_data.discriminant, copies);
                }
                break;
            }
            case MirTerminator::Call: {
                auto& call_data = std::get<MirTerminator::CallData>(term.data);
                // func_name は文字列なので伝播対象外
                for (auto& arg : call_data.args) {
                    if (arg)
                        changed |= propagate_in_operand(*arg, copies);
                }
                if (call_data.destination) {
                    changed |= propagate_in_place(*call_data.destination, copies);
                }
                break;
            }
            default:
                break;
        }

        return changed;
    }
};

}  // namespace cm::mir::opt