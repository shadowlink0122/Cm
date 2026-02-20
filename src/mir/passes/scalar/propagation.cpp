#include "propagation.hpp"

namespace cm::mir::opt {

bool CopyPropagation::run(MirFunction& func) {
    bool changed = false;

    // 複数回代入される変数を検出（ループ変数など）
    auto multiAssigned = detect_multi_assigned(func);

    // 関数引数はコピー伝播から除外（呼び出し元から任意の値が渡される）
    for (LocalId arg : func.arg_locals) {
        multiAssigned.insert(arg);
    }

    // 各基本ブロックを処理
    // 注意: コピー情報はブロック単位で管理（ブロック間の伝播は行わない）
    // これは保守的だが安全なアプローチ
    for (auto& block : func.basic_blocks) {
        if (!block)
            continue;
        // 各ブロックの開始時にコピー情報をクリア
        std::unordered_map<LocalId, LocalId> copies;
        changed |= process_block(*block, copies, func, multiAssigned);
    }

    return changed;
}

std::unordered_set<LocalId> CopyPropagation::detect_multi_assigned(const MirFunction& func) {
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
            // ASM出力制約（=r, +r等）も代入としてカウント（Bug1修正）
            if (stmt->kind == MirStatement::Asm) {
                const auto& asm_data = std::get<MirStatement::AsmData>(stmt->data);
                for (const auto& operand : asm_data.operands) {
                    if (!operand.constraint.empty() &&
                        (operand.constraint[0] == '+' || operand.constraint[0] == '=')) {
                        if (assigned.count(operand.local_id) > 0) {
                            multiAssigned.insert(operand.local_id);
                        } else {
                            assigned.insert(operand.local_id);
                        }
                    }
                }
            }
        }
    }
    return multiAssigned;
}

bool CopyPropagation::same_type(const hir::TypePtr& a, const hir::TypePtr& b) const {
    if (a == b)
        return true;
    if (!a || !b)
        return false;
    return a->kind == b->kind && a->name == b->name;
}

bool CopyPropagation::process_block(BasicBlock& block, std::unordered_map<LocalId, LocalId>& copies,
                                    const MirFunction& func,
                                    const std::unordered_set<LocalId>& multiAssigned) {
    bool changed = false;

    // 各文を処理
    for (auto& stmt : block.statements) {
        // ASMステートメント: no_optフラグに関わらず、出力オペランドの変数は
        // 常にコピー情報から削除する必要がある
        // （インラインアセンブリは実行時に変数を変更するため）
        if (stmt->kind == MirStatement::Asm) {
            const auto& asm_data = std::get<MirStatement::AsmData>(stmt->data);
            for (const auto& operand : asm_data.operands) {
                if (!operand.constraint.empty() &&
                    (operand.constraint[0] == '+' || operand.constraint[0] == '=')) {
                    copies.erase(operand.local_id);
                }
            }
            continue;
        }

        // no_optフラグがtrueの場合は最適化スキップ
        if (stmt->no_opt) {
            // mustブロック内の代入はコピー情報から除外
            if (stmt->kind == MirStatement::Assign) {
                auto& assign_data = std::get<MirStatement::AssignData>(stmt->data);
                if (assign_data.place.projections.empty()) {
                    copies.erase(assign_data.place.local);
                }
            }
            continue;
        }

        if (stmt->kind == MirStatement::Assign) {
            auto& assign_data = std::get<MirStatement::AssignData>(stmt->data);

            // デリファレンス書き込みチェック（_p.* = ... の形式）
            // エイリアスの可能性があるため、全てのコピー情報をクリアする
            bool has_deref = false;
            for (const auto& proj : assign_data.place.projections) {
                if (proj.kind == ProjectionKind::Deref) {
                    has_deref = true;
                    break;
                }
            }
            if (has_deref) {
                // ポインタ経由の書き込みは任意のローカル変数に影響する可能性がある
                // 保守的にすべてのコピー情報をクリア
                copies.clear();
                continue;
            }

            // まず右辺のコピー伝播を実行
            changed |= propagate_in_rvalue(*assign_data.rvalue, copies);

            // 単純なコピー代入を検出: _x = _y
            if (assign_data.place.projections.empty() &&
                assign_data.rvalue->kind == MirRvalue::Use) {
                if (auto* use_data = std::get_if<MirRvalue::UseData>(&assign_data.rvalue->data)) {
                    if (use_data->operand && use_data->operand->kind == MirOperand::Copy) {
                        if (auto* src_place = std::get_if<MirPlace>(&use_data->operand->data)) {
                            if (src_place->projections.empty()) {
                                // _x = _y の形式
                                LocalId target = assign_data.place.local;
                                LocalId source = resolve_copy_chain(src_place->local, copies);

                                // 複数回代入される変数は除外
                                if (multiAssigned.count(target) > 0 ||
                                    multiAssigned.count(source) > 0) {
                                    continue;
                                }

                                // 型が一致することを確認
                                if (target < func.locals.size() && source < func.locals.size()) {
                                    if (!same_type(func.locals[target].type,
                                                   func.locals[source].type)) {
                                        continue;
                                    }
                                }

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
                // 特にキャスト（型変換）を含む場合は、ポインタ型変換の可能性があるため
                // コピー伝播を行わない
                if (assign_data.place.projections.empty()) {
                    copies.erase(assign_data.place.local);

                    // キャストを含む場合は、ソースのコピー情報も無効化
                    // これにより、ポインタ代入チェーンの誤った最適化を防ぐ
                    if (assign_data.rvalue->kind == MirRvalue::Cast) {
                        auto& cast_data = std::get<MirRvalue::CastData>(assign_data.rvalue->data);
                        if (cast_data.operand && cast_data.operand->kind == MirOperand::Copy) {
                            if (auto* src_place = std::get_if<MirPlace>(&cast_data.operand->data)) {
                                if (src_place->projections.empty()) {
                                    // キャスト元もコピー伝播対象から除外
                                    copies.erase(src_place->local);
                                }
                            }
                        }
                    }
                } else {
                    // フィールドやインデックスへの代入の場合
                    // ベース変数に関するコピー情報を無効化
                    // _a = _b の後に _b.field = ... があると、_a の情報が古くなる
                    LocalId modified_base = assign_data.place.local;

                    // copies[X] = modified_base となっている X を全て削除
                    std::vector<LocalId> to_remove;
                    for (const auto& [target, source] : copies) {
                        if (source == modified_base) {
                            to_remove.push_back(target);
                        }
                    }
                    for (LocalId id : to_remove) {
                        copies.erase(id);
                    }

                    // modified_base 自体のコピー情報も削除
                    copies.erase(modified_base);
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

LocalId CopyPropagation::resolve_copy_chain(LocalId local,
                                            const std::unordered_map<LocalId, LocalId>& copies) {
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

bool CopyPropagation::propagate_in_rvalue(MirRvalue& rvalue,
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
            // 参照取得の場合、コピー伝播は行わない
            // _1 と _2 は異なるメモリ位置を持つ可能性があるため
            // &_1 と &_2 は異なるアドレスを生成する
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

bool CopyPropagation::propagate_in_operand(MirOperand& operand,
                                           const std::unordered_map<LocalId, LocalId>& copies) {
    if (operand.kind == MirOperand::Copy || operand.kind == MirOperand::Move) {
        if (auto* place = std::get_if<MirPlace>(&operand.data)) {
            return propagate_in_place(*place, copies);
        }
    }
    return false;
}

bool CopyPropagation::propagate_in_place(MirPlace& place,
                                         const std::unordered_map<LocalId, LocalId>& copies) {
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

bool CopyPropagation::propagate_in_terminator(MirTerminator& term,
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

}  // namespace cm::mir::opt
