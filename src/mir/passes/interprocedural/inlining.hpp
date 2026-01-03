#pragma once

#include "../../nodes.hpp"
#include "../core/base.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace cm::mir::opt {

// ============================================================
// 関数インライン化
// ============================================================
class FunctionInlining : public OptimizationPass {
   public:
    std::string name() const override { return "Function Inlining"; }

    bool run(MirFunction& /*func*/) override { return false; }

    bool run_on_program(MirProgram& program) override {
        bool changed = false;

        std::unordered_map<std::string, const MirFunction*> function_map;
        for (const auto& func : program.functions) {
            if (func) {
                function_map[func->name] = func.get();
            }
        }

        // インライン化回数の追跡（無限ループ防止）
        inline_counts.clear();
        max_inlines_reached = false;

        for (auto& func : program.functions) {
            if (func) {
                changed |= process_function(*func, function_map);
            }
        }

        if (max_inlines_reached) {
            std::cerr << "[INLINE] 警告: 一部の関数でインライン化上限に達しました\n";
        }

        return changed;
    }

   private:
    const size_t INLINE_THRESHOLD = 10;  // より小さい関数のみインライン化
    const size_t MAX_INLINE_PER_FUNCTION = 2;  // 同じ関数の最大インライン化回数を削減
    const size_t MAX_TOTAL_INLINES = 20;  // プログラム全体でのインライン化回数制限
    std::unordered_map<std::string, size_t> inline_counts;
    bool max_inlines_reached = false;

    bool process_function(MirFunction& caller,
                          const std::unordered_map<std::string, const MirFunction*>& func_map) {
        bool changed = false;
        size_t initial_block_count = caller.basic_blocks.size();

        for (size_t i = 0; i < initial_block_count; ++i) {
            if (process_block(caller, i, func_map)) {
                changed = true;
            }
        }

        return changed;
    }

    bool process_block(MirFunction& caller, BlockId block_id,
                       const std::unordered_map<std::string, const MirFunction*>& func_map) {
        if (block_id >= caller.basic_blocks.size() || !caller.basic_blocks[block_id])
            return false;

        auto& block = caller.basic_blocks[block_id];
        if (!block->terminator || block->terminator->kind != MirTerminator::Call) {
            return false;
        }

        auto& call_data = std::get<MirTerminator::CallData>(block->terminator->data);

        // func is unique_ptr<MirOperand>
        if (call_data.func->kind != MirOperand::Constant)
            return false;
        const auto& callee_const = std::get<MirConstant>(call_data.func->data);

        std::string callee_name;
        if (const auto* s = std::get_if<std::string>(&callee_const.value)) {
            callee_name = *s;
        }

        if (callee_name.empty())
            return false;
        if (callee_name == caller.name)
            return false;  // 自己再帰を防ぐ

        // プログラム全体のインライン化回数制限チェック
        size_t total_inlines = 0;
        for (const auto& [key, count] : inline_counts) {
            total_inlines += count;
        }
        if (total_inlines >= MAX_TOTAL_INLINES) {
            max_inlines_reached = true;
            return false;  // プログラム全体のインライン化上限に達した
        }

        // インライン化回数制限チェック
        auto inline_key = caller.name + "->" + callee_name;
        if (inline_counts[inline_key] >= MAX_INLINE_PER_FUNCTION) {
            max_inlines_reached = true;
            return false;  // この組み合わせのインライン化上限に達した
        }

        auto it = func_map.find(callee_name);
        if (it == func_map.end())
            return false;
        const MirFunction* callee = it->second;

        if (!should_inline(*callee))
            return false;

        // インライン化実行前に回数をインクリメント
        inline_counts[inline_key]++;

        perform_inlining(caller, block_id, *callee, call_data);
        return true;
    }

    bool should_inline(const MirFunction& callee) {
        // ラムダ関数やクロージャ関数はインライン化しない
        // （O3最適化で無限ループの原因となる可能性があるため）
        if (callee.name.find("__lambda_") != std::string::npos ||
            callee.name.find("$_") != std::string::npos ||
            callee.name.find("closure") != std::string::npos) {
            return false;
        }

        size_t stmt_count = 0;
        for (const auto& b : callee.basic_blocks) {
            if (b)
                stmt_count += b->statements.size();
        }
        return stmt_count <= INLINE_THRESHOLD;
    }

    void perform_inlining(MirFunction& caller, BlockId call_block_id, const MirFunction& callee,
                          const MirTerminator::CallData& call_data) {
        LocalId local_offset = caller.locals.size();

        for (const auto& local : callee.locals) {
            auto new_local = local;
            new_local.id += local_offset;
            caller.locals.push_back(new_local);
        }

        BlockId block_offset = caller.basic_blocks.size();
        std::vector<BlockId> block_map(callee.basic_blocks.size());

        std::vector<BasicBlockPtr> new_blocks;
        new_blocks.reserve(callee.basic_blocks.size());

        // Pass 1: Clone blocks and map IDs
        for (size_t i = 0; i < callee.basic_blocks.size(); ++i) {
            if (!callee.basic_blocks[i]) {
                block_map[i] = INVALID_BLOCK;
                continue;
            }

            auto new_block = std::make_unique<BasicBlock>(block_offset + i);
            new_block->id = block_offset + i;
            block_map[i] = new_block->id;

            for (const auto& stmt : callee.basic_blocks[i]->statements) {
                new_block->statements.push_back(clone_statement(*stmt));
            }

            if (callee.basic_blocks[i]->terminator) {
                new_block->terminator = clone_terminator(*callee.basic_blocks[i]->terminator);
            }

            new_blocks.push_back(std::move(new_block));
        }

        // Pass 2: Remap
        for (auto& nb : new_blocks) {
            remap_block(*nb, local_offset, block_map, call_data);
        }

        // Append modified blocks to caller
        for (auto& nb : new_blocks) {
            caller.basic_blocks.push_back(std::move(nb));
        }

        // Ensure IDs match indices (should be correct by construction)
        for (size_t i = block_offset; i < caller.basic_blocks.size(); ++i) {
            if (caller.basic_blocks[i])
                caller.basic_blocks[i]->id = i;
        }

        // Arguments assignment
        if (callee.entry_block < block_map.size()) {
            BlockId entry_id = block_map[callee.entry_block];
            if (entry_id != INVALID_BLOCK) {
                auto& entry_block = caller.basic_blocks[entry_id];

                for (size_t i = 0; i < call_data.args.size(); ++i) {
                    if (i >= callee.arg_locals.size())
                        break;

                    LocalId target_local = callee.arg_locals[i] + local_offset;
                    MirPlace place{target_local};
                    MirOperandPtr op = clone_operand(*call_data.args[i]);
                    MirRvaluePtr rv = MirRvalue::use(std::move(op));

                    auto stmt = MirStatement::assign(std::move(place), std::move(rv));
                    entry_block->statements.insert(entry_block->statements.begin() + i,
                                                   std::move(stmt));
                }
            }
        }

        BlockId return_target = call_data.success;
        auto& call_block = caller.basic_blocks[call_block_id];

        BlockId entry_id = block_map[callee.entry_block];
        if (entry_id != INVALID_BLOCK) {
            call_block->terminator = MirTerminator::goto_block(entry_id);
        } else {
            call_block->terminator = MirTerminator::unreachable();
        }
    }

    MirStatementPtr clone_statement(const MirStatement& src) {
        auto stmt = std::make_unique<MirStatement>();
        stmt->kind = src.kind;
        stmt->span = src.span;

        if (std::holds_alternative<MirStatement::AssignData>(src.data)) {
            const auto& d = std::get<MirStatement::AssignData>(src.data);
            MirPlace p = clone_place(d.place);
            MirRvaluePtr rv = clone_rvalue(*d.rvalue);
            stmt->data = MirStatement::AssignData{std::move(p), std::move(rv)};
        } else if (std::holds_alternative<MirStatement::StorageData>(src.data)) {
            const auto& d = std::get<MirStatement::StorageData>(src.data);
            stmt->data = MirStatement::StorageData{d.local};
        }
        return stmt;
    }

    MirTerminatorPtr clone_terminator(const MirTerminator& src) {
        auto term = std::make_unique<MirTerminator>();
        term->kind = src.kind;
        term->span = src.span;

        if (std::holds_alternative<MirTerminator::GotoData>(src.data)) {
            term->data = std::get<MirTerminator::GotoData>(src.data);
        } else if (std::holds_alternative<MirTerminator::SwitchIntData>(src.data)) {
            const auto& d = std::get<MirTerminator::SwitchIntData>(src.data);
            MirTerminator::SwitchIntData nd;
            nd.discriminant = clone_operand(*d.discriminant);
            nd.targets = d.targets;
            nd.otherwise = d.otherwise;
            term->data = std::move(nd);
        } else if (std::holds_alternative<MirTerminator::CallData>(src.data)) {
            const auto& d = std::get<MirTerminator::CallData>(src.data);
            MirTerminator::CallData nd;
            nd.func = clone_operand(*d.func);
            for (const auto& a : d.args)
                nd.args.push_back(clone_operand(*a));
            if (d.destination)
                nd.destination = clone_place(*d.destination);
            nd.success = d.success;
            nd.unwind = d.unwind;
            nd.interface_name = d.interface_name;
            nd.method_name = d.method_name;
            nd.is_virtual = d.is_virtual;
            term->data = std::move(nd);
        }
        return term;
    }

    MirOperandPtr clone_operand(const MirOperand& src) {
        auto op = std::make_unique<MirOperand>();
        op->kind = src.kind;
        if (std::holds_alternative<MirPlace>(src.data)) {
            op->data = clone_place(std::get<MirPlace>(src.data));
        } else if (std::holds_alternative<MirConstant>(src.data)) {
            op->data = std::get<MirConstant>(src.data);
        } else if (std::holds_alternative<std::string>(src.data)) {
            op->data = std::get<std::string>(src.data);
        }
        return op;
    }

    MirPlace clone_place(const MirPlace& src) { return src; }

    MirRvaluePtr clone_rvalue(const MirRvalue& src) {
        auto rv = std::make_unique<MirRvalue>();
        rv->kind = src.kind;
        if (std::holds_alternative<MirRvalue::UseData>(src.data)) {
            rv->data =
                MirRvalue::UseData{clone_operand(*std::get<MirRvalue::UseData>(src.data).operand)};
        } else if (std::holds_alternative<MirRvalue::BinaryOpData>(src.data)) {
            const auto& d = std::get<MirRvalue::BinaryOpData>(src.data);
            rv->data = MirRvalue::BinaryOpData{d.op, clone_operand(*d.lhs), clone_operand(*d.rhs),
                                               d.result_type};
        } else if (std::holds_alternative<MirRvalue::UnaryOpData>(src.data)) {
            const auto& d = std::get<MirRvalue::UnaryOpData>(src.data);
            rv->data = MirRvalue::UnaryOpData{d.op, clone_operand(*d.operand)};
        } else if (std::holds_alternative<MirRvalue::CastData>(src.data)) {
            const auto& d = std::get<MirRvalue::CastData>(src.data);
            rv->data = MirRvalue::CastData{clone_operand(*d.operand), d.target_type};
        } else if (std::holds_alternative<MirRvalue::RefData>(src.data)) {
            const auto& d = std::get<MirRvalue::RefData>(src.data);
            rv->data = MirRvalue::RefData{d.borrow, d.place};
        } else if (std::holds_alternative<MirRvalue::AggregateData>(src.data)) {
            const auto& d = std::get<MirRvalue::AggregateData>(src.data);
            MirRvalue::AggregateData nd;
            nd.kind = d.kind;
            for (const auto& op : d.operands)
                nd.operands.push_back(clone_operand(*op));
            rv->data = std::move(nd);
        } else if (std::holds_alternative<MirRvalue::FormatConvertData>(src.data)) {
            const auto& d = std::get<MirRvalue::FormatConvertData>(src.data);
            rv->data = MirRvalue::FormatConvertData{clone_operand(*d.operand), d.format_spec};
        }
        return rv;
    }

    void remap_block(BasicBlock& block, LocalId local_offset, const std::vector<BlockId>& block_map,
                     const MirTerminator::CallData& call_data) {
        for (auto& stmt : block.statements) {
            remap_statement(*stmt, local_offset);
        }

        if (block.terminator) {
            if (block.terminator->kind == MirTerminator::Return) {
                if (call_data.destination) {
                    LocalId callee_ret_id = 0;
                    MirPlace dest = *call_data.destination;
                    MirPlace src{callee_ret_id + local_offset};
                    MirRvaluePtr rv = MirRvalue::use(MirOperand::move(src));
                    auto stmt = MirStatement::assign(std::move(dest), std::move(rv));
                    block.statements.push_back(std::move(stmt));
                }
                block.terminator = MirTerminator::goto_block(call_data.success);
            } else {
                remap_terminator(*block.terminator, local_offset, block_map);
            }
        }
    }

    void remap_statement(MirStatement& stmt, LocalId offset) {
        if (auto* d = std::get_if<MirStatement::AssignData>(&stmt.data)) {
            remap_place(d->place, offset);
            remap_rvalue(*d->rvalue, offset);
        } else if (auto* d = std::get_if<MirStatement::StorageData>(&stmt.data)) {
            d->local += offset;
        }
    }

    void remap_terminator(MirTerminator& term, LocalId lo, const std::vector<BlockId>& bm) {
        if (auto* d = std::get_if<MirTerminator::GotoData>(&term.data)) {
            if (d->target < bm.size())
                d->target = bm[d->target];
        } else if (auto* d = std::get_if<MirTerminator::SwitchIntData>(&term.data)) {
            remap_operand(*d->discriminant, lo);
            for (auto& t : d->targets)
                if (t.second < bm.size())
                    t.second = bm[t.second];
            if (d->otherwise < bm.size())
                d->otherwise = bm[d->otherwise];
        } else if (auto* d = std::get_if<MirTerminator::CallData>(&term.data)) {
            remap_operand(*d->func, lo);
            for (auto& a : d->args)
                remap_operand(*a, lo);
            if (d->destination)
                remap_place(*d->destination, lo);
            if (d->success < bm.size())
                d->success = bm[d->success];
            if (d->unwind && *d->unwind < bm.size())
                *d->unwind = bm[*d->unwind];
        }
    }

    void remap_place(MirPlace& p, LocalId offset) {
        p.local += offset;
        for (auto& proj : p.projections)
            if (proj.kind == ProjectionKind::Index)
                proj.index_local += offset;
    }

    void remap_operand(MirOperand& op, LocalId offset) {
        if (auto* p = std::get_if<MirPlace>(&op.data))
            remap_place(*p, offset);
    }

    void remap_rvalue(MirRvalue& rv, LocalId offset) {
        if (auto* d = std::get_if<MirRvalue::UseData>(&rv.data))
            remap_operand(*d->operand, offset);
        else if (auto* d = std::get_if<MirRvalue::BinaryOpData>(&rv.data)) {
            remap_operand(*d->lhs, offset);
            remap_operand(*d->rhs, offset);
        } else if (auto* d = std::get_if<MirRvalue::UnaryOpData>(&rv.data))
            remap_operand(*d->operand, offset);
        else if (auto* d = std::get_if<MirRvalue::CastData>(&rv.data))
            remap_operand(*d->operand, offset);
        else if (auto* d = std::get_if<MirRvalue::RefData>(&rv.data))
            remap_place(d->place, offset);
        else if (auto* d = std::get_if<MirRvalue::AggregateData>(&rv.data)) {
            for (auto& op : d->operands)
                remap_operand(*op, offset);
        } else if (auto* d = std::get_if<MirRvalue::FormatConvertData>(&rv.data))
            remap_operand(*d->operand, offset);
    }
};

}  // namespace cm::mir::opt
