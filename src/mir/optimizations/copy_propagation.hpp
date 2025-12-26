#pragma once

#include "optimization_pass.hpp"

#include <unordered_map>
#include <unordered_set>

namespace cm::mir::opt {

// ============================================================
// コピー伝播最適化
// ============================================================
class CopyPropagation : public OptimizationPass {
   public:
    std::string name() const override { return "Copy Propagation"; }

    bool run(MirFunction& func) override {
        bool changed = false;

        // 複数回代入される変数を検出（ループ変数など）
        std::unordered_set<LocalId> multiAssigned = detect_multi_assigned(func);

        // 各ローカル変数のコピー元を追跡
        // copies[x] = y は _x = _y を意味する
        std::unordered_map<LocalId, LocalId> copies;

        // 各基本ブロックを処理
        for (auto& block : func.basic_blocks) {
            if (!block)
                continue;
            changed |= process_block(func, *block, copies, multiAssigned);
        }

        return changed;
    }

   private:
    // 型が同一かチェック（安全なコピー伝播のため）
    bool same_type(const hir::TypePtr& a, const hir::TypePtr& b) const {
        if (a == b) {
            return true;
        }
        if (!a || !b) {
            return false;
        }
        if (a->kind != b->kind) {
            return false;
        }
        switch (a->kind) {
            case hir::TypeKind::Pointer:
            case hir::TypeKind::Reference:
                return same_type(a->element_type, b->element_type);
            case hir::TypeKind::Array:
                return a->array_size == b->array_size &&
                       same_type(a->element_type, b->element_type);
            case hir::TypeKind::Struct:
            case hir::TypeKind::Interface:
            case hir::TypeKind::TypeAlias:
            case hir::TypeKind::Generic: {
                if (a->name != b->name) {
                    return false;
                }
                if (a->type_args.size() != b->type_args.size()) {
                    return false;
                }
                for (size_t i = 0; i < a->type_args.size(); ++i) {
                    if (!same_type(a->type_args[i], b->type_args[i])) {
                        return false;
                    }
                }
                return true;
            }
            case hir::TypeKind::Function: {
                if (!same_type(a->return_type, b->return_type)) {
                    return false;
                }
                if (a->param_types.size() != b->param_types.size()) {
                    return false;
                }
                for (size_t i = 0; i < a->param_types.size(); ++i) {
                    if (!same_type(a->param_types[i], b->param_types[i])) {
                        return false;
                    }
                }
                return true;
            }
            default:
                return true;
        }
    }

    // 関数内で複数回代入される変数を検出
    std::unordered_set<LocalId> detect_multi_assigned(const MirFunction& func) {
        std::unordered_set<LocalId> assigned;
        std::unordered_set<LocalId> multiAssigned;

        for (const auto& block : func.basic_blocks) {
            if (!block)
                continue;
            for (const auto& stmt : block->statements) {
                if (stmt->kind == MirStatement::Assign) {
                    auto& assign_data = std::get<MirStatement::AssignData>(stmt->data);
                    if (assign_data.place.projections.empty()) {
                        LocalId target = assign_data.place.local;
                        if (assigned.count(target) > 0) {
                            multiAssigned.insert(target);
                        } else {
                            assigned.insert(target);
                        }
                    }
                }
            }
        }

        return multiAssigned;
    }

    bool process_block(const MirFunction& func, BasicBlock& block,
                       std::unordered_map<LocalId, LocalId>& copies,
                       const std::unordered_set<LocalId>& multiAssigned) {
        bool changed = false;

        // 各文を処理
        for (auto& stmt : block.statements) {
            if (stmt->kind == MirStatement::Assign) {
                auto& assign_data = std::get<MirStatement::AssignData>(stmt->data);

                // 左辺のPlaceにprojectionがある場合、ベースローカルにもコピー伝播を適用
                // 例: _4.* = _7 で _4 が _6 のコピーなら _6.* = _7 に置き換え
                if (!assign_data.place.projections.empty()) {
                    changed |= propagate_in_place(assign_data.place, copies);
                }

                // 右辺のコピー伝播を実行
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

                                    // 複数回代入される変数は追跡しない
                                    if (multiAssigned.count(target) > 0 ||
                                        multiAssigned.count(source) > 0) {
                                        // ループ変数など、再代入される変数は無視
                                    } else if (target != source) {
                                        // 型が一致しないコピーは伝播しない
                                        if (target < func.locals.size() &&
                                            source < func.locals.size()) {
                                            const auto& target_type = func.locals[target].type;
                                            const auto& source_type = func.locals[source].type;
                                            if (!same_type(target_type, source_type)) {
                                                continue;
                                            }
                                        } else {
                                            continue;
                                        }
                                        // 自己代入でない場合
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
        // 注意: ループ変数など再代入される変数では、データフロー解析が不正確なため
        // インデックスのコピー伝播はスキップする
        // TODO: 適切なデータフロー解析を実装してからインデックスの置き換えを有効化
        // for (auto& proj : place.projections) {
        //     if (proj.kind == ProjectionKind::Index) {
        //         LocalId new_index = resolve_copy_chain(proj.index_local, copies);
        //         if (new_index != proj.index_local) {
        //             proj.index_local = new_index;
        //             changed = true;
        //         }
        //     }
        // }

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
                // 関数オペランドに対してもコピー伝播を行う（関数ポインタ経由の呼び出し対応）
                if (call_data.func) {
                    changed |= propagate_in_operand(*call_data.func, copies);
                }
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
