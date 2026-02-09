#pragma once

#include "../core/base.hpp"

#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace cm::mir::opt {

// ============================================================
// 共通部分式削除 (CSE) / Global Value Numbering (GVN)
// 現在はLocal CSEのみ実装
// ============================================================
class GVN : public OptimizationPass {
   public:
    std::string name() const override { return "GVN/CSE"; }

    bool run(MirFunction& func) override {
        bool changed = false;

        for (auto& block : func.basic_blocks) {
            if (!block)
                continue;
            changed |= process_block(*block);
        }

        return changed;
    }

   private:
    bool process_block(BasicBlock& block) {
        bool changed = false;

        // 式の文字列表現 -> 結果を保持するLocalId
        std::unordered_map<std::string, LocalId> available_exprs;

        // 変数 -> その変数を使用している式の集合（無効化用）
        std::unordered_map<LocalId, std::unordered_set<std::string>> var_to_exprs;

        for (auto& stmt : block.statements) {
            if (stmt->kind == MirStatement::Nop)
                continue;

            // ASMステートメント: 出力オペランドの変数は変更されるため式を無効化
            if (stmt->kind == MirStatement::Asm) {
                const auto& asm_data = std::get<MirStatement::AsmData>(stmt->data);
                for (const auto& operand : asm_data.operands) {
                    if (!operand.constraint.empty() &&
                        (operand.constraint[0] == '+' || operand.constraint[0] == '=')) {
                        invalidate_exprs_using(operand.local_id, available_exprs, var_to_exprs);
                    }
                }
                continue;
            }

            // 1. この文が変数を変更する場合、その変数に依存する式を無効化
            if (stmt->kind == MirStatement::Assign) {
                auto& assign_data = std::get<MirStatement::AssignData>(stmt->data);

                // 代入先の変数を特定
                LocalId target_base = assign_data.place.local;
                invalidate_exprs_using(target_base, available_exprs, var_to_exprs);

                // Deref代入 (*p = ...) の場合、エイリアスの可能性があるため、全式を無効化（保守的）
                for (const auto& proj : assign_data.place.projections) {
                    if (proj.kind == ProjectionKind::Deref) {
                        available_exprs.clear();
                        var_to_exprs.clear();
                        break;
                    }
                }
            } else if (stmt->kind == MirStatement::StorageLive ||
                       stmt->kind == MirStatement::StorageDead) {
                // 変数の有効期限が変わる場合も依存式を無効化
                auto& storage_data = std::get<MirStatement::StorageData>(stmt->data);
                invalidate_exprs_using(storage_data.local, available_exprs, var_to_exprs);
            }

            // 2. 代入文の場合、CSEのチャンスを探す
            if (stmt->kind == MirStatement::Assign) {
                auto& assign_data = std::get<MirStatement::AssignData>(stmt->data);

                // 単純な変数への代入のみ対象 (_x = Expr)
                if (assign_data.place.projections.empty()) {
                    LocalId target = assign_data.place.local;

                    if (assign_data.rvalue) {
                        // 副作用のない式のみ対象（BinaryOp, UnaryOp, Cast）
                        std::string expr_key = stringify_rvalue(*assign_data.rvalue);

                        if (!expr_key.empty()) {
                            auto it = available_exprs.find(expr_key);
                            if (it != available_exprs.end()) {
                                // 共通部分式を発見！
                                // _x = Expr  -> _x = _available_local (Copy)
                                LocalId source = it->second;

                                // 自己参照でなければ置換
                                if (target != source) {
                                    // RvalueをUse(Copy(source))に置き換え
                                    auto new_operand = MirOperand::copy(MirPlace{source});
                                    assign_data.rvalue = MirRvalue::use(std::move(new_operand));
                                    changed = true;
                                }
                            } else {
                                // 新しい式を登録
                                available_exprs[expr_key] = target;

                                // 依存関係を登録
                                std::unordered_set<LocalId> dependencies;
                                collect_dependencies(*assign_data.rvalue, dependencies);
                                for (LocalId dep : dependencies) {
                                    var_to_exprs[dep].insert(expr_key);
                                }
                            }
                        }
                    }
                }
            }
        }

        return changed;  // Local CSE requires no global state handling
    }

    void invalidate_exprs_using(
        LocalId local, std::unordered_map<std::string, LocalId>& available_exprs,
        std::unordered_map<LocalId, std::unordered_set<std::string>>& var_to_exprs) {
        // この変数を使用しているすべての式を削除
        auto it = var_to_exprs.find(local);
        if (it != var_to_exprs.end()) {
            for (const auto& expr_key : it->second) {
                available_exprs.erase(expr_key);
            }
            var_to_exprs.erase(it);
        }

        // この変数が「結果」となっている式も削除（上書きされたため）
        for (auto it = available_exprs.begin(); it != available_exprs.end();) {
            if (it->second == local) {
                it = available_exprs.erase(it);
            } else {
                ++it;
            }
        }
    }

    void collect_dependencies(const MirRvalue& rvalue, std::unordered_set<LocalId>& deps) {
        if (const auto* bin = std::get_if<MirRvalue::BinaryOpData>(&rvalue.data)) {
            if (bin->lhs)
                collect_operand_deps(*bin->lhs, deps);
            if (bin->rhs)
                collect_operand_deps(*bin->rhs, deps);
        } else if (const auto* un = std::get_if<MirRvalue::UnaryOpData>(&rvalue.data)) {
            if (un->operand)
                collect_operand_deps(*un->operand, deps);
        } else if (const auto* cast = std::get_if<MirRvalue::CastData>(&rvalue.data)) {
            if (cast->operand)
                collect_operand_deps(*cast->operand, deps);
        }
    }

    void collect_operand_deps(const MirOperand& op, std::unordered_set<LocalId>& deps) {
        if (const auto* place = std::get_if<MirPlace>(&op.data)) {
            deps.insert(place->local);
            for (const auto& p : place->projections) {
                if (p.kind == ProjectionKind::Index)
                    deps.insert(p.index_local);
            }
        }
    }

    std::string stringify_rvalue(const MirRvalue& rvalue) {
        std::stringstream ss;

        if (const auto* bin = std::get_if<MirRvalue::BinaryOpData>(&rvalue.data)) {
            ss << "BinOp(" << (int)bin->op << ",";
            if (bin->lhs)
                ss << stringify_operand(*bin->lhs);
            ss << ",";
            if (bin->rhs)
                ss << stringify_operand(*bin->rhs);
            ss << ")";
            return ss.str();
        } else if (const auto* un = std::get_if<MirRvalue::UnaryOpData>(&rvalue.data)) {
            ss << "UnOp(" << (int)un->op << ",";
            if (un->operand)
                ss << stringify_operand(*un->operand);
            ss << ")";
            return ss.str();
        } else if (const auto* cast = std::get_if<MirRvalue::CastData>(&rvalue.data)) {
            ss << "Cast(";
            if (cast->operand)
                ss << stringify_operand(*cast->operand);
            ss << ",";
            if (cast->target_type)
                ss << cm::hir::type_to_string(*cast->target_type);
            ss << ")";
            return ss.str();
        }

        return "";
    }

    std::string stringify_operand(const MirOperand& op) {
        std::stringstream ss;
        if (op.kind == MirOperand::Constant) {
            if (const auto* c = std::get_if<MirConstant>(&op.data)) {
                if (auto* i = std::get_if<int64_t>(&c->value))
                    ss << "C(int," << *i << ")";
                else if (auto* d = std::get_if<double>(&c->value))
                    ss << "C(dbl," << *d << ")";
                else if (auto* b = std::get_if<bool>(&c->value))
                    ss << "C(bool," << *b << ")";
                else if (auto* s = std::get_if<std::string>(&c->value))
                    ss << "C(str," << *s << ")";
                else
                    ss << "C(other)";
            }
        } else if (op.kind == MirOperand::Copy || op.kind == MirOperand::Move) {
            if (const auto* p = std::get_if<MirPlace>(&op.data)) {
                ss << "L(" << p->local;
                for (const auto& proj : p->projections) {
                    if (proj.kind == ProjectionKind::Field)
                        ss << ".f" << proj.field_id;
                    else if (proj.kind == ProjectionKind::Index)
                        ss << "[" << proj.index_local << "]";
                    else if (proj.kind == ProjectionKind::Deref)
                        ss << "*";
                }
                ss << ")";
            }
        }
        return ss.str();
    }
};

}  // namespace cm::mir::opt
