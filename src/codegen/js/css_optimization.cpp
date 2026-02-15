#include "builtins.hpp"
#include "codegen.hpp"
#include "runtime.hpp"
#include "types.hpp"

#include <algorithm>
#include <functional>
#include <iomanip>
#include <iostream>
#include <set>
#include <stdexcept>

namespace cm::codegen::js {

using ast::TypeKind;

bool JSCodeGen::tryEmitCssReturn(const mir::MirFunction& func) {
    if (func.name.size() < 5 || func.name.rfind("__css") != func.name.size() - 5) {
        return false;
    }

    ControlFlowAnalyzer cfAnalyzer(func);
    if (!cfAnalyzer.isLinearFlow()) {
        return false;
    }

    std::unordered_map<mir::LocalId, const mir::MirRvalue*> defs;
    std::unordered_map<mir::LocalId, const mir::MirTerminator::CallData*> call_defs;
    std::unordered_map<mir::LocalId, int> def_counts;

    auto blockOrder = cfAnalyzer.getLinearBlockOrder();
    for (mir::BlockId blockId : blockOrder) {
        if (blockId >= func.basic_blocks.size() || !func.basic_blocks[blockId]) {
            continue;
        }
        const auto& block = *func.basic_blocks[blockId];
        for (const auto& stmt : block.statements) {
            if (!stmt || stmt->kind != mir::MirStatement::Assign) {
                continue;
            }
            const auto& assign_data = std::get<mir::MirStatement::AssignData>(stmt->data);
            if (!assign_data.place.projections.empty() || !assign_data.rvalue) {
                continue;
            }
            mir::LocalId target = assign_data.place.local;
            def_counts[target]++;
            if (def_counts[target] == 1) {
                defs[target] = assign_data.rvalue.get();
            } else {
                defs.erase(target);
            }
        }
        if (block.terminator && block.terminator->kind == mir::MirTerminator::Call) {
            const auto& call_data = std::get<mir::MirTerminator::CallData>(block.terminator->data);
            if (call_data.destination) {
                def_counts[call_data.destination->local]++;
                if (def_counts[call_data.destination->local] == 1) {
                    std::string callee;
                    if (call_data.func && call_data.func->kind == mir::MirOperand::FunctionRef) {
                        callee = std::get<std::string>(call_data.func->data);
                    }
                    if (callee.size() >= 5 && callee.rfind("__css") == callee.size() - 5) {
                        call_defs[call_data.destination->local] = &call_data;
                    } else {
                        return false;
                    }
                } else {
                    call_defs.erase(call_data.destination->local);
                    defs.erase(call_data.destination->local);
                }
            }
        }
    }

    std::unordered_set<mir::LocalId> visiting;

    std::function<std::string(const mir::MirOperand&)> render_operand =
        [&](const mir::MirOperand& operand) -> std::string {
        if (operand.kind == mir::MirOperand::Constant) {
            const auto& constant = std::get<mir::MirConstant>(operand.data);
            return emitConstant(constant);
        }
        if (operand.kind == mir::MirOperand::Copy || operand.kind == mir::MirOperand::Move) {
            const auto& place = std::get<mir::MirPlace>(operand.data);
            if (!place.projections.empty()) {
                return emitPlace(place, func);
            }
            auto call_it = call_defs.find(place.local);
            if (call_it != call_defs.end()) {
                const auto* call_data = call_it->second;
                std::string callee;
                if (call_data && call_data->func &&
                    call_data->func->kind == mir::MirOperand::FunctionRef) {
                    callee = std::get<std::string>(call_data->func->data);
                }
                if (!callee.empty()) {
                    std::string expr = sanitizeIdentifier(callee) + "(";
                    for (size_t i = 0; i < call_data->args.size(); ++i) {
                        if (i > 0) {
                            expr += ", ";
                        }
                        if (call_data->args[i]) {
                            expr += render_operand(*call_data->args[i]);
                        }
                    }
                    expr += ")";
                    return expr;
                }
            }

            auto it = defs.find(place.local);
            if (it != defs.end()) {
                if (visiting.count(place.local) > 0) {
                    return emitPlace(place, func);
                }
                visiting.insert(place.local);
                std::string expr = "";
                const auto* rv = it->second;
                if (rv) {
                    switch (rv->kind) {
                        case mir::MirRvalue::Use: {
                            const auto& use_data = std::get<mir::MirRvalue::UseData>(rv->data);
                            if (use_data.operand) {
                                expr = render_operand(*use_data.operand);
                            }
                            break;
                        }
                        case mir::MirRvalue::BinaryOp: {
                            const auto& bin_data = std::get<mir::MirRvalue::BinaryOpData>(rv->data);
                            if (bin_data.lhs && bin_data.rhs) {
                                expr = "(" + render_operand(*bin_data.lhs) + " " +
                                       emitBinaryOp(bin_data.op) + " " +
                                       render_operand(*bin_data.rhs) + ")";
                            }
                            break;
                        }
                        case mir::MirRvalue::UnaryOp: {
                            const auto& unary_data =
                                std::get<mir::MirRvalue::UnaryOpData>(rv->data);
                            if (unary_data.operand) {
                                expr = "(" + emitUnaryOp(unary_data.op) +
                                       render_operand(*unary_data.operand) + ")";
                            }
                            break;
                        }
                        case mir::MirRvalue::FormatConvert: {
                            const auto& fmt_data =
                                std::get<mir::MirRvalue::FormatConvertData>(rv->data);
                            if (fmt_data.operand) {
                                std::string inner = render_operand(*fmt_data.operand);
                                expr =
                                    "__cm_format(" + inner + ", \"" + fmt_data.format_spec + "\")";
                            }
                            break;
                        }
                        case mir::MirRvalue::Cast: {
                            const auto& cast_data = std::get<mir::MirRvalue::CastData>(rv->data);
                            if (cast_data.operand) {
                                expr = render_operand(*cast_data.operand);
                            }
                            break;
                        }
                        case mir::MirRvalue::Aggregate:
                        case mir::MirRvalue::Ref:
                            break;
                    }
                }
                visiting.erase(place.local);
                if (!expr.empty()) {
                    return expr;
                }
            }
            return emitPlace(place, func);
        }
        if (operand.kind == mir::MirOperand::FunctionRef) {
            const auto& funcName = std::get<std::string>(operand.data);
            return sanitizeIdentifier(funcName);
        }
        return "undefined";
    };

    std::function<bool(const mir::MirOperand&, std::vector<std::string>&)> collect_parts =
        [&](const mir::MirOperand& operand, std::vector<std::string>& parts) -> bool {
        if (operand.kind == mir::MirOperand::Constant) {
            const auto& constant = std::get<mir::MirConstant>(operand.data);
            parts.push_back(emitConstant(constant));
            return true;
        }
        if (operand.kind == mir::MirOperand::Copy || operand.kind == mir::MirOperand::Move) {
            const auto& place = std::get<mir::MirPlace>(operand.data);
            if (!place.projections.empty()) {
                parts.push_back(emitPlace(place, func));
                return true;
            }
            if (visiting.count(place.local) > 0) {
                parts.push_back(emitPlace(place, func));
                return true;
            }
            visiting.insert(place.local);

            auto call_it = call_defs.find(place.local);
            if (call_it != call_defs.end()) {
                const auto* call_data = call_it->second;
                std::string callee;
                if (call_data && call_data->func &&
                    call_data->func->kind == mir::MirOperand::FunctionRef) {
                    callee = std::get<std::string>(call_data->func->data);
                }
                if (!callee.empty()) {
                    std::string expr = sanitizeIdentifier(callee) + "(";
                    for (size_t i = 0; i < call_data->args.size(); ++i) {
                        if (i > 0) {
                            expr += ", ";
                        }
                        if (call_data->args[i]) {
                            expr += render_operand(*call_data->args[i]);
                        }
                    }
                    expr += ")";
                    parts.push_back(expr);
                    visiting.erase(place.local);
                    return true;
                }
            }

            auto it = defs.find(place.local);
            if (it != defs.end()) {
                const auto* rv = it->second;
                if (rv) {
                    switch (rv->kind) {
                        case mir::MirRvalue::Use: {
                            const auto& use_data = std::get<mir::MirRvalue::UseData>(rv->data);
                            if (use_data.operand) {
                                bool ok = collect_parts(*use_data.operand, parts);
                                visiting.erase(place.local);
                                return ok;
                            }
                            break;
                        }
                        case mir::MirRvalue::BinaryOp: {
                            const auto& bin_data = std::get<mir::MirRvalue::BinaryOpData>(rv->data);
                            if (bin_data.op == mir::MirBinaryOp::Add && bin_data.lhs &&
                                bin_data.rhs) {
                                bool ok_left = collect_parts(*bin_data.lhs, parts);
                                bool ok_right = collect_parts(*bin_data.rhs, parts);
                                visiting.erase(place.local);
                                return ok_left && ok_right;
                            }
                            break;
                        }
                        case mir::MirRvalue::FormatConvert: {
                            const auto& fmt_data =
                                std::get<mir::MirRvalue::FormatConvertData>(rv->data);
                            if (fmt_data.operand) {
                                std::string inner = render_operand(*fmt_data.operand);
                                parts.push_back("__cm_format(" + inner + ", \"" +
                                                fmt_data.format_spec + "\")");
                                visiting.erase(place.local);
                                return true;
                            }
                            break;
                        }
                        case mir::MirRvalue::Cast: {
                            const auto& cast_data = std::get<mir::MirRvalue::CastData>(rv->data);
                            if (cast_data.operand) {
                                parts.push_back(render_operand(*cast_data.operand));
                                visiting.erase(place.local);
                                return true;
                            }
                            break;
                        }
                        case mir::MirRvalue::UnaryOp:
                        case mir::MirRvalue::Aggregate:
                        case mir::MirRvalue::Ref:
                            break;
                    }
                }
            }

            visiting.erase(place.local);
            parts.push_back(emitPlace(place, func));
            return true;
        }
        if (operand.kind == mir::MirOperand::FunctionRef) {
            const auto& funcName = std::get<std::string>(operand.data);
            parts.push_back(sanitizeIdentifier(funcName));
            return true;
        }
        parts.push_back("undefined");
        return true;
    };

    auto it = defs.find(func.return_local);
    if (it == defs.end()) {
        return false;
    }

    const auto* rv = it->second;
    if (!rv) {
        return false;
    }

    std::string expr;
    std::vector<std::string> parts;
    if (rv->kind == mir::MirRvalue::Use) {
        const auto& use_data = std::get<mir::MirRvalue::UseData>(rv->data);
        if (!use_data.operand) {
            return false;
        }
        collect_parts(*use_data.operand, parts);
    } else {
        return false;
    }

    if (parts.empty()) {
        return false;
    }

    if (!parts.empty() && parts.front() == "\"\"") {
        parts.erase(parts.begin());
    }

    if (parts.size() == 1) {
        emitter_.emitLine("return " + parts[0] + ";");
        return true;
    }

    std::string joined = "return [";
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            joined += ", ";
        }
        joined += parts[i];
    }
    joined += "].join(\"\");";
    emitter_.emitLine(joined);
    return true;
}

bool JSCodeGen::tryEmitObjectLiteralReturn(const mir::MirFunction& func) {
    ControlFlowAnalyzer cfAnalyzer(func);
    if (!cfAnalyzer.isLinearFlow()) {
        return false;
    }

    if (func.return_local >= func.locals.size()) {
        return false;
    }
    const auto& return_local = func.locals[func.return_local];
    if (!return_local.type || return_local.type->kind != ast::TypeKind::Struct) {
        return false;
    }

    std::optional<mir::LocalId> source_local;
    size_t return_assign_index = 0;
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
            if (!assign_data.place.projections.empty()) {
                ++index;
                continue;
            }
            if (assign_data.place.local != func.return_local) {
                ++index;
                continue;
            }
            if (source_local.has_value()) {
                return false;
            }
            if (!assign_data.rvalue || assign_data.rvalue->kind != mir::MirRvalue::Use) {
                return false;
            }
            const auto& use_data = std::get<mir::MirRvalue::UseData>(assign_data.rvalue->data);
            if (!use_data.operand) {
                return false;
            }
            if (use_data.operand->kind != mir::MirOperand::Copy &&
                use_data.operand->kind != mir::MirOperand::Move) {
                return false;
            }
            const auto* place = std::get_if<mir::MirPlace>(&use_data.operand->data);
            if (!place || !place->projections.empty()) {
                return false;
            }
            source_local = place->local;
            return_assign_index = index;
            ++index;
        }
        ++index;
    }

    if (!source_local.has_value()) {
        return false;
    }
    if (*source_local == func.return_local || *source_local >= func.locals.size()) {
        return false;
    }
    const auto& source_info = func.locals[*source_local];
    if (!source_info.type || source_info.type->kind != ast::TypeKind::Struct) {
        return false;
    }
    auto struct_it = struct_map_.find(source_info.type->name);
    if (struct_it == struct_map_.end() || !struct_it->second) {
        return false;
    }
    const auto* mirStruct = struct_it->second;

    struct FieldAssign {
        std::string key;
        std::string value;
    };
    std::vector<FieldAssign> fields;
    std::unordered_set<mir::LocalId> used;

    index = 0;
    for (mir::BlockId blockId : blockOrder) {
        if (blockId >= func.basic_blocks.size() || !func.basic_blocks[blockId]) {
            continue;
        }
        const auto& block = *func.basic_blocks[blockId];
        for (const auto& stmt : block.statements) {
            if (!stmt) {
                ++index;
                continue;
            }
            if (index > return_assign_index && stmt->kind == mir::MirStatement::Assign) {
                return false;
            }
            if (stmt->kind != mir::MirStatement::Assign) {
                ++index;
                continue;
            }
            const auto& assign_data = std::get<mir::MirStatement::AssignData>(stmt->data);
            if (assign_data.rvalue) {
                used.clear();
                collectUsedLocalsInRvalue(*assign_data.rvalue, used);
                for (mir::LocalId used_local : used) {
                    if (used_local == *source_local) {
                        return false;
                    }
                    if (used_local < func.locals.size()) {
                        const auto& local_info = func.locals[used_local];
                        bool isArg = std::find(func.arg_locals.begin(), func.arg_locals.end(),
                                               used_local) != func.arg_locals.end();
                        if (!isArg && !local_info.is_static) {
                            return false;
                        }
                    }
                }
            }
            if (assign_data.place.local != *source_local) {
                ++index;
                continue;
            }
            if (assign_data.place.projections.size() != 1 ||
                assign_data.place.projections[0].kind != mir::ProjectionKind::Field) {
                return false;
            }
            if (index > return_assign_index) {
                return false;
            }
            const auto& proj = assign_data.place.projections[0];
            if (proj.field_id >= mirStruct->fields.size()) {
                return false;
            }
            const auto& field_name = mirStruct->fields[proj.field_id].name;
            std::string key = formatStructFieldKey(*mirStruct, field_name);
            std::string value = "undefined";
            if (assign_data.rvalue) {
                value = emitRvalue(*assign_data.rvalue, func);
            }
            fields.push_back(FieldAssign{key, value});
            ++index;
        }
        if (block.terminator) {
            used.clear();
            collectUsedLocalsInTerminator(*block.terminator, used);
            if (used.count(*source_local) > 0) {
                return false;
            }
            if (block.terminator->kind != mir::MirTerminator::Return) {
                return false;
            }
        }
        ++index;
    }

    if (fields.empty()) {
        return false;
    }

    for (mir::LocalId argId : func.arg_locals) {
        if (argId >= func.locals.size()) {
            continue;
        }
        if (boxed_locals_.count(argId) == 0) {
            continue;
        }
        const auto& local = func.locals[argId];
        std::string varName = sanitizeIdentifier(local.name);
        emitter_.emitLine(varName + " = [" + varName + "];");
        emitter_.emitLine(varName + ".__boxed = true;");
    }

    std::string literal = "{ ";
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i > 0) {
            literal += ", ";
        }
        literal += fields[i].key + ": " + fields[i].value;
    }
    literal += " }";
    emitter_.emitLine("return " + literal + ";");
    return true;
}

}  // namespace cm::codegen::js
