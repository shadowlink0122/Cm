#include "builtins.hpp"
#include "codegen.hpp"
#include "runtime.hpp"
#include "types.hpp"

#include <algorithm>
#include <functional>
#include <iostream>
#include <set>
#include <stdexcept>

namespace cm::codegen::js {

using ast::TypeKind;

void JSCodeGen::collectUsedLocals(const mir::MirFunction& func,
                                  std::unordered_set<mir::LocalId>& used) const {
    for (const auto& block : func.basic_blocks) {
        if (!block)
            continue;
        for (const auto& stmt : block->statements) {
            if (!stmt || stmt->kind != mir::MirStatement::Assign) {
                continue;
            }
            const auto& assign_data = std::get<mir::MirStatement::AssignData>(stmt->data);
            // 代入先（LHS）のplaceも記録（projection経由のアクセスで使用される変数を含む）
            collectUsedLocalsInPlace(assign_data.place, used);
            if (assign_data.rvalue) {
                collectUsedLocalsInRvalue(*assign_data.rvalue, used);
            }
        }
        if (block->terminator) {
            collectUsedLocalsInTerminator(*block->terminator, used);
            // Call terminatorの戻り値先も記録
            if (block->terminator->kind == mir::MirTerminator::Call) {
                const auto& data = std::get<mir::MirTerminator::CallData>(block->terminator->data);
                if (data.destination) {
                    collectUsedLocalsInPlace(*data.destination, used);
                }
            }
        }
    }
}

void JSCodeGen::collectUsedLocalsInOperand(const mir::MirOperand& operand,
                                           std::unordered_set<mir::LocalId>& used) const {
    if (operand.kind == mir::MirOperand::Copy || operand.kind == mir::MirOperand::Move) {
        if (const auto* place = std::get_if<mir::MirPlace>(&operand.data)) {
            collectUsedLocalsInPlace(*place, used);
        }
    }
}

void JSCodeGen::collectUsedLocalsInPlace(const mir::MirPlace& place,
                                         std::unordered_set<mir::LocalId>& used) const {
    used.insert(place.local);
    for (const auto& proj : place.projections) {
        if (proj.kind == mir::ProjectionKind::Index) {
            used.insert(proj.index_local);
        }
    }
}

void JSCodeGen::collectUsedLocalsInRvalue(const mir::MirRvalue& rvalue,
                                          std::unordered_set<mir::LocalId>& used) const {
    switch (rvalue.kind) {
        case mir::MirRvalue::Use: {
            const auto& use_data = std::get<mir::MirRvalue::UseData>(rvalue.data);
            if (use_data.operand) {
                collectUsedLocalsInOperand(*use_data.operand, used);
            }
            break;
        }
        case mir::MirRvalue::BinaryOp: {
            const auto& bin_data = std::get<mir::MirRvalue::BinaryOpData>(rvalue.data);
            if (bin_data.lhs) {
                collectUsedLocalsInOperand(*bin_data.lhs, used);
            }
            if (bin_data.rhs) {
                collectUsedLocalsInOperand(*bin_data.rhs, used);
            }
            break;
        }
        case mir::MirRvalue::UnaryOp: {
            const auto& unary_data = std::get<mir::MirRvalue::UnaryOpData>(rvalue.data);
            if (unary_data.operand) {
                collectUsedLocalsInOperand(*unary_data.operand, used);
            }
            break;
        }
        case mir::MirRvalue::Ref: {
            const auto& ref_data = std::get<mir::MirRvalue::RefData>(rvalue.data);
            collectUsedLocalsInPlace(ref_data.place, used);
            break;
        }
        case mir::MirRvalue::Aggregate: {
            const auto& agg_data = std::get<mir::MirRvalue::AggregateData>(rvalue.data);
            for (const auto& op : agg_data.operands) {
                if (op) {
                    collectUsedLocalsInOperand(*op, used);
                }
            }
            break;
        }
        case mir::MirRvalue::FormatConvert: {
            const auto& fmt_data = std::get<mir::MirRvalue::FormatConvertData>(rvalue.data);
            if (fmt_data.operand) {
                collectUsedLocalsInOperand(*fmt_data.operand, used);
            }
            break;
        }
        case mir::MirRvalue::Cast: {
            const auto& cast_data = std::get<mir::MirRvalue::CastData>(rvalue.data);
            if (cast_data.operand) {
                collectUsedLocalsInOperand(*cast_data.operand, used);
            }
            break;
        }
    }
}

void JSCodeGen::collectUsedLocalsInTerminator(const mir::MirTerminator& term,
                                              std::unordered_set<mir::LocalId>& used) const {
    switch (term.kind) {
        case mir::MirTerminator::SwitchInt: {
            const auto& data = std::get<mir::MirTerminator::SwitchIntData>(term.data);
            if (data.discriminant) {
                collectUsedLocalsInOperand(*data.discriminant, used);
            }
            break;
        }
        case mir::MirTerminator::Call: {
            const auto& data = std::get<mir::MirTerminator::CallData>(term.data);
            if (data.func) {
                collectUsedLocalsInOperand(*data.func, used);
            }
            for (const auto& arg : data.args) {
                if (arg) {
                    collectUsedLocalsInOperand(*arg, used);
                }
            }
            break;
        }
        case mir::MirTerminator::Goto:
        case mir::MirTerminator::Return:
        case mir::MirTerminator::Unreachable:
            break;
    }
}

bool JSCodeGen::isLocalUsed(mir::LocalId local) const {
    return current_used_locals_.count(local) > 0;
}

void JSCodeGen::collectUseCounts(const mir::MirFunction& func) {
    auto record_place = [&](const mir::MirPlace& place) {
        current_use_counts_[place.local]++;
        if (!place.projections.empty()) {
            current_noninline_locals_.insert(place.local);
        }
        for (const auto& proj : place.projections) {
            if (proj.kind == mir::ProjectionKind::Index) {
                current_use_counts_[proj.index_local]++;
            }
        }
    };

    auto record_operand = [&](const mir::MirOperand& operand) {
        if (operand.kind == mir::MirOperand::Copy || operand.kind == mir::MirOperand::Move) {
            if (const auto* place = std::get_if<mir::MirPlace>(&operand.data)) {
                record_place(*place);
            }
        }
    };

    auto record_rvalue = [&](const mir::MirRvalue& rvalue) {
        switch (rvalue.kind) {
            case mir::MirRvalue::Use: {
                const auto& use_data = std::get<mir::MirRvalue::UseData>(rvalue.data);
                if (use_data.operand) {
                    record_operand(*use_data.operand);
                }
                break;
            }
            case mir::MirRvalue::BinaryOp: {
                const auto& bin_data = std::get<mir::MirRvalue::BinaryOpData>(rvalue.data);
                if (bin_data.lhs) {
                    record_operand(*bin_data.lhs);
                }
                if (bin_data.rhs) {
                    record_operand(*bin_data.rhs);
                }
                break;
            }
            case mir::MirRvalue::UnaryOp: {
                const auto& unary_data = std::get<mir::MirRvalue::UnaryOpData>(rvalue.data);
                if (unary_data.operand) {
                    record_operand(*unary_data.operand);
                }
                break;
            }
            case mir::MirRvalue::Ref: {
                const auto& ref_data = std::get<mir::MirRvalue::RefData>(rvalue.data);
                current_noninline_locals_.insert(ref_data.place.local);
                record_place(ref_data.place);
                break;
            }
            case mir::MirRvalue::Aggregate: {
                const auto& agg_data = std::get<mir::MirRvalue::AggregateData>(rvalue.data);
                for (const auto& op : agg_data.operands) {
                    if (op) {
                        record_operand(*op);
                    }
                }
                break;
            }
            case mir::MirRvalue::FormatConvert: {
                const auto& fmt_data = std::get<mir::MirRvalue::FormatConvertData>(rvalue.data);
                if (fmt_data.operand) {
                    record_operand(*fmt_data.operand);
                }
                break;
            }
            case mir::MirRvalue::Cast: {
                const auto& cast_data = std::get<mir::MirRvalue::CastData>(rvalue.data);
                if (cast_data.operand) {
                    record_operand(*cast_data.operand);
                }
                break;
            }
        }
    };

    for (const auto& block : func.basic_blocks) {
        if (!block)
            continue;
        for (const auto& stmt : block->statements) {
            if (!stmt || stmt->kind != mir::MirStatement::Assign) {
                continue;
            }
            const auto& assign_data = std::get<mir::MirStatement::AssignData>(stmt->data);
            // 代入先（LHS）のplaceも記録（projection付きの場合はnoninline扱い）
            record_place(assign_data.place);
            if (assign_data.rvalue) {
                record_rvalue(*assign_data.rvalue);
            }
        }
        if (block->terminator) {
            switch (block->terminator->kind) {
                case mir::MirTerminator::SwitchInt: {
                    const auto& data =
                        std::get<mir::MirTerminator::SwitchIntData>(block->terminator->data);
                    if (data.discriminant) {
                        record_operand(*data.discriminant);
                    }
                    break;
                }
                case mir::MirTerminator::Call: {
                    const auto& data =
                        std::get<mir::MirTerminator::CallData>(block->terminator->data);
                    if (data.func) {
                        record_operand(*data.func);
                    }
                    for (const auto& arg : data.args) {
                        if (arg) {
                            record_operand(*arg);
                        }
                    }
                    break;
                }
                case mir::MirTerminator::Goto:
                case mir::MirTerminator::Return:
                case mir::MirTerminator::Unreachable:
                    break;
            }
        }
    }
}

void JSCodeGen::collectDeclareOnAssign(const mir::MirFunction& func) {
    ControlFlowAnalyzer cfAnalyzer(func);
    if (!cfAnalyzer.isLinearFlow()) {
        return;
    }

    std::unordered_map<mir::LocalId, size_t> first_assign;
    std::unordered_map<mir::LocalId, size_t> first_use;
    std::unordered_set<mir::LocalId> used;

    size_t index = 0;
    auto blockOrder = cfAnalyzer.getLinearBlockOrder();
    for (mir::BlockId blockId : blockOrder) {
        if (blockId >= func.basic_blocks.size() || !func.basic_blocks[blockId]) {
            continue;
        }
        const auto& block = *func.basic_blocks[blockId];
        for (const auto& stmt : block.statements) {
            if (!stmt || stmt->kind != mir::MirStatement::Assign) {
                ++index;
                continue;
            }
            const auto& assign_data = std::get<mir::MirStatement::AssignData>(stmt->data);
            if (assign_data.rvalue) {
                used.clear();
                collectUsedLocalsInRvalue(*assign_data.rvalue, used);
                for (mir::LocalId local : used) {
                    if (first_use.count(local) == 0) {
                        first_use[local] = index;
                    }
                }
            }
            if (!assign_data.place.projections.empty()) {
                ++index;
                continue;
            }
            if (first_assign.count(assign_data.place.local) == 0) {
                first_assign[assign_data.place.local] = index;
            }
            ++index;
        }

        if (block.terminator) {
            used.clear();
            collectUsedLocalsInTerminator(*block.terminator, used);
            for (mir::LocalId local : used) {
                if (first_use.count(local) == 0) {
                    first_use[local] = index;
                }
            }
        }
        ++index;
    }

    for (const auto& local : func.locals) {
        bool isArg = std::find(func.arg_locals.begin(), func.arg_locals.end(), local.id) !=
                     func.arg_locals.end();
        if (isArg || local.is_static) {
            continue;
        }
        if (boxed_locals_.count(local.id) > 0) {
            continue;
        }
        auto it_assign = first_assign.find(local.id);
        if (it_assign == first_assign.end()) {
            continue;
        }
        auto it_use = first_use.find(local.id);
        if (it_use != first_use.end() && it_use->second < it_assign->second) {
            continue;
        }
        declare_on_assign_.insert(local.id);
    }
}

bool JSCodeGen::isInlineableRvalue(const mir::MirRvalue& rvalue) const {
    switch (rvalue.kind) {
        case mir::MirRvalue::Use:
        case mir::MirRvalue::BinaryOp:
        case mir::MirRvalue::UnaryOp:
        case mir::MirRvalue::Aggregate:
        case mir::MirRvalue::FormatConvert:
        case mir::MirRvalue::Cast:
            return true;
        case mir::MirRvalue::Ref:
            return false;
    }
    return false;
}

void JSCodeGen::collectInlineCandidates(const mir::MirFunction& func) {
    for (const auto& block : func.basic_blocks) {
        if (!block)
            continue;
        for (const auto& stmt : block->statements) {
            if (!stmt || stmt->kind != mir::MirStatement::Assign) {
                continue;
            }
            const auto& assign_data = std::get<mir::MirStatement::AssignData>(stmt->data);
            if (!assign_data.rvalue || !assign_data.place.projections.empty()) {
                continue;
            }
            mir::LocalId target = assign_data.place.local;
            if (target < func.locals.size()) {
                const auto& local = func.locals[target];
                bool isArg = std::find(func.arg_locals.begin(), func.arg_locals.end(), target) !=
                             func.arg_locals.end();
                bool is_generated = !local.name.empty() && local.name[0] == '_';
                if (isArg || local.is_static ||
                    ((local.is_user_variable && !is_generated) && target != func.return_local)) {
                    continue;
                }
            }
            if (boxed_locals_.count(target) > 0) {
                continue;
            }
            if (current_noninline_locals_.count(target) > 0) {
                continue;
            }
            auto it = current_use_counts_.find(target);
            if (target == func.return_local) {
                if (it != current_use_counts_.end() && it->second > 0) {
                    continue;
                }
            } else if (it == current_use_counts_.end() || it->second != 1) {
                continue;
            }
            if (!isInlineableRvalue(*assign_data.rvalue)) {
                continue;
            }
            inline_candidates_.insert(target);
        }
    }
}

void JSCodeGen::precomputeInlineValues(const mir::MirFunction& func) {
    if (inline_candidates_.empty()) {
        return;
    }

    ControlFlowAnalyzer cfAnalyzer(func);
    if (!cfAnalyzer.isLinearFlow()) {
        return;
    }

    std::unordered_map<mir::LocalId, size_t> first_assign;
    std::unordered_map<mir::LocalId, size_t> first_use;
    std::unordered_map<mir::LocalId, const mir::MirRvalue*> assign_rvalues;
    std::vector<std::pair<size_t, mir::LocalId>> assignment_order;

    std::unordered_set<mir::LocalId> used;
    size_t index = 0;
    auto blockOrder = cfAnalyzer.getLinearBlockOrder();
    for (mir::BlockId blockId : blockOrder) {
        if (blockId >= func.basic_blocks.size() || !func.basic_blocks[blockId]) {
            continue;
        }
        const auto& block = *func.basic_blocks[blockId];
        for (const auto& stmt : block.statements) {
            if (!stmt || stmt->kind != mir::MirStatement::Assign) {
                ++index;
                continue;
            }
            const auto& assign_data = std::get<mir::MirStatement::AssignData>(stmt->data);
            if (assign_data.rvalue) {
                used.clear();
                collectUsedLocalsInRvalue(*assign_data.rvalue, used);
                for (mir::LocalId local : used) {
                    if (inline_candidates_.count(local) > 0 && first_use.count(local) == 0) {
                        first_use[local] = index;
                    }
                }
            }
            if (!assign_data.place.projections.empty()) {
                ++index;
                continue;
            }
            mir::LocalId target = assign_data.place.local;
            if (inline_candidates_.count(target) == 0) {
                ++index;
                continue;
            }
            if (first_assign.count(target) == 0) {
                first_assign[target] = index;
                assign_rvalues[target] = assign_data.rvalue.get();
                assignment_order.emplace_back(index, target);
            }
            ++index;
        }

        if (block.terminator) {
            used.clear();
            collectUsedLocalsInTerminator(*block.terminator, used);
            for (mir::LocalId local : used) {
                if (inline_candidates_.count(local) > 0 && first_use.count(local) == 0) {
                    first_use[local] = index;
                }
            }
        }
        ++index;
    }

    std::sort(assignment_order.begin(), assignment_order.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });

    for (const auto& entry : assignment_order) {
        mir::LocalId target = entry.second;
        if (inline_values_.count(target) > 0) {
            continue;
        }
        auto it_assign = first_assign.find(target);
        auto it_use = first_use.find(target);
        if (it_assign == first_assign.end()) {
            continue;
        }
        if (target != func.return_local) {
            if (it_use == first_use.end()) {
                continue;
            }
            if (it_assign->second > it_use->second) {
                continue;
            }
        }
        auto it_rvalue = assign_rvalues.find(target);
        if (it_rvalue == assign_rvalues.end() || it_rvalue->second == nullptr) {
            continue;
        }
        used.clear();
        collectUsedLocalsInRvalue(*it_rvalue->second, used);
        if (used.count(target) > 0) {
            continue;
        }
        if (!isInlineableRvalue(*it_rvalue->second)) {
            continue;
        }
        inline_values_[target] = emitRvalue(*it_rvalue->second, func);
    }
}

}  // namespace cm::codegen::js
