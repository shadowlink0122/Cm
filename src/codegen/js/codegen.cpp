#include "codegen.hpp"

#include "builtins.hpp"
#include "runtime.hpp"
#include "types.hpp"

#include <algorithm>
#include <functional>
#include <iomanip>
#include <iostream>
#include <optional>
#include <set>
#include <stdexcept>

namespace cm::codegen::js {

using ast::TypeKind;

JSCodeGen::JSCodeGen(const JSCodeGenOptions& options)
    : options_(options), emitter_(options.indentSpaces) {}

bool JSCodeGen::isCssStruct(const std::string& struct_name) const {
    auto it = struct_map_.find(struct_name);
    return it != struct_map_.end() && it->second && it->second->is_css;
}

std::unordered_set<std::string> JSCodeGen::collectUsedRuntimeHelpers(
    const std::string& code) const {
    std::unordered_set<std::string> used;
    const std::string prefix = "__cm_";

    size_t pos = 0;
    while (true) {
        pos = code.find(prefix, pos);
        if (pos == std::string::npos) {
            break;
        }
        size_t start = pos;
        size_t end = pos + prefix.size();
        while (end < code.size()) {
            char c = code[end];
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                c == '_') {
                end++;
            } else {
                break;
            }
        }
        if (end > start) {
            used.insert(code.substr(start, end - start));
        }
        pos = end;
    }

    return used;
}

void JSCodeGen::expandRuntimeHelperDependencies(std::unordered_set<std::string>& used) const {
    if (used.count("__cm_web_set_html") || used.count("__cm_web_append_html")) {
        used.insert("__cm_dom_root");
    }
    if (used.count("__cm_output")) {
        used.insert("__cm_output_element");
    }
}

std::string JSCodeGen::toKebabCase(const std::string& name) const {
    std::string result;
    result.reserve(name.size());
    for (char c : name) {
        result += (c == '_') ? '-' : c;
    }
    return result;
}

std::string JSCodeGen::formatStructFieldKey(const mir::MirStruct& st,
                                            const std::string& field_name) const {
    if (!st.is_css) {
        return sanitizeIdentifier(field_name);
    }
    std::string kebab = toKebabCase(field_name);
    return "\"" + escapeString(kebab) + "\"";
}

std::string JSCodeGen::mapExternJsName(const std::string& name) const {
    std::string result = name;
    std::replace(result.begin(), result.end(), '_', '.');
    return result;
}

void JSCodeGen::compile(const mir::MirProgram& program) {
    // 出力クリア
    emitter_.clear();
    generated_functions_.clear();
    static_vars_.clear();
    function_map_.clear();
    used_runtime_helpers_.clear();

    // 構造体マップ構築
    for (const auto& st : program.structs) {
        if (st) {
            struct_map_[st->name] = st.get();
        }
    }

    // インターフェース名を収集
    for (const auto& iface : program.interfaces) {
        if (iface) {
            interface_names_.insert(iface->name);
        }
    }

    // 関数マップ構築（クロージャキャプチャ用）
    for (const auto& func : program.functions) {
        if (func) {
            function_map_[func->name] = func.get();
        }
    }

    // static変数を収集
    collectStaticVars(program);

    // プリアンブル
    emitPreamble();

    // static変数をグローバルスコープで宣言
    emitStaticVars();

    // 構造体コンストラクタ
    for (const auto& st : program.structs) {
        if (st) {
            emitStruct(*st);
        }
    }

    // vtable（インターフェースディスパッチ用）
    emitVTables(program);

    // 関数
    for (const auto& func : program.functions) {
        if (func && !func->is_extern) {
            emitFunction(*func, program);
        }
    }

    // 生成コードから必要なランタイムヘルパーを抽出
    used_runtime_helpers_ = collectUsedRuntimeHelpers(emitter_.getCode());
    bool needs_web_runtime = emitter_.getCode().find("cm.web.") != std::string::npos;
    expandRuntimeHelperDependencies(used_runtime_helpers_);
    emitRuntime(emitter_, used_runtime_helpers_);
    if (needs_web_runtime) {
        emitWebRuntime(emitter_);
    }

    // ポストアンブル（main呼び出し等）
    emitPostamble(program);

    // ファイル出力
    if (!options_.outputFile.empty()) {
        std::ofstream file(options_.outputFile);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open output file: " + options_.outputFile);
        }
        file << emitter_.getCode();
        file.close();

        if (options_.verbose) {
            std::cout << "Generated: " << options_.outputFile << std::endl;
        }
    }

    // HTMLラッパー生成
    if (options_.generateHTML) {
        std::string htmlFile = options_.outputFile;
        size_t pos = htmlFile.rfind('.');
        if (pos != std::string::npos) {
            htmlFile = htmlFile.substr(0, pos) + ".html";
        } else {
            htmlFile += ".html";
        }

        std::string scriptSrc = options_.outputFile.empty() ? "output.js" : options_.outputFile;
        size_t slash = scriptSrc.find_last_of("/\\");
        if (slash != std::string::npos) {
            scriptSrc = scriptSrc.substr(slash + 1);
        }

        std::ofstream html(htmlFile);
        if (html.is_open()) {
            html << "<!DOCTYPE html>\n";
            html << "<html>\n<head>\n";
            html << "    <meta charset=\"UTF-8\">\n";
            html << "    <title>Cm Application</title>\n";
            html << "</head>\n<body>\n";
            html << "    <script src=\"" << scriptSrc << "\"></script>\n";
            html << "</body>\n</html>\n";
            html.close();

            if (options_.verbose) {
                std::cout << "Generated: " << htmlFile << std::endl;
            }
        }
    }
}

void JSCodeGen::emitPreamble() {
    if (options_.useStrictMode) {
        emitter_.emitLine("\"use strict\";");
        emitter_.emitLine();
    }
}

void JSCodeGen::emitPostamble(const mir::MirProgram& program) {
    emitter_.emitLine();

    // main関数を探して呼び出し
    bool hasMain = false;
    for (const auto& func : program.functions) {
        if (func && func->name == "main") {
            hasMain = true;
            break;
        }
    }

    if (hasMain) {
        emitter_.emitLine("// Entry point");
        if (options_.esModule) {
            emitter_.emitLine("export { main };");
        }
        emitter_.emitLine("main();");
    }
}

void JSCodeGen::emitVTables(const mir::MirProgram& program) {
    if (program.vtables.empty()) {
        return;
    }

    emitter_.emitLine("// VTables for interface dispatch");

    for (const auto& vtable : program.vtables) {
        if (!vtable)
            continue;

        // すべてのメソッド実装が存在するか確認（ジェネリックテンプレートの除外）
        bool allMethodsExist = true;
        for (const auto& entry : vtable->entries) {
            if (function_map_.find(entry.impl_function_name) == function_map_.end()) {
                allMethodsExist = false;
                break;
            }
        }
        if (!allMethodsExist) {
            continue;
        }

        std::string vtableName = sanitizeIdentifier(vtable->type_name) + "_" +
                                 sanitizeIdentifier(vtable->interface_name) + "_vtable";

        emitter_.emitLine("const " + vtableName + " = {");
        emitter_.increaseIndent();

        for (size_t i = 0; i < vtable->entries.size(); ++i) {
            const auto& entry = vtable->entries[i];
            std::string line = sanitizeIdentifier(entry.method_name) + ": " +
                               sanitizeIdentifier(entry.impl_function_name);
            if (i < vtable->entries.size() - 1) {
                line += ",";
            }
            emitter_.emitLine(line);
        }

        emitter_.decreaseIndent();
        emitter_.emitLine("};");
    }
    emitter_.emitLine();
}

void JSCodeGen::emitStruct(const mir::MirStruct& st) {
    // デフォルト構造体コンストラクタは出力しない
    (void)st;
    return;
}

// 関数
void JSCodeGen::emitFunction(const mir::MirFunction& func, const mir::MirProgram& program) {
    if (generated_functions_.count(func.name)) {
        return;  // 重複防止
    }
    generated_functions_.insert(func.name);

    // アドレス取得されるローカル変数を収集（ボクシング用）
    boxed_locals_.clear();
    for (const auto& block : func.basic_blocks) {
        if (!block)
            continue;
        for (const auto& stmt : block->statements) {
            if (stmt->kind == mir::MirStatement::Assign) {
                const auto& data = std::get<mir::MirStatement::AssignData>(stmt->data);
                if (data.rvalue->kind == mir::MirRvalue::Ref) {
                    const auto& refData = std::get<mir::MirRvalue::RefData>(data.rvalue->data);
                    // ローカル変数のアドレス取得のみ対応
                    boxed_locals_.insert(refData.place.local);
                }
            }
        }
    }

    current_used_locals_.clear();
    current_use_counts_.clear();
    current_noninline_locals_.clear();
    inline_candidates_.clear();
    inline_values_.clear();
    declare_on_assign_.clear();
    declared_locals_.clear();
    collectUsedLocals(func, current_used_locals_);
    collectUseCounts(func);
    if (func.name.size() < 5 || func.name.rfind("__css") != func.name.size() - 5) {
        collectInlineCandidates(func);
    }
    precomputeInlineValues(func);
    if (!isVoidReturn(func)) {
        current_used_locals_.insert(func.return_local);
    }

    emitter_.emitLine("// Function: " + func.name);
    emitFunctionSignature(func);
    emitter_.emitLine(" {");
    emitter_.increaseIndent();
    emitFunctionBody(func, program);
    emitter_.decreaseIndent();
    emitter_.emitLine("}");
    emitter_.emitLine();
}

void JSCodeGen::emitFunctionSignature(const mir::MirFunction& func) {
    std::string safeName = sanitizeIdentifier(func.name);
    emitter_.stream() << "function " << safeName << "(";

    // 引数
    for (size_t i = 0; i < func.arg_locals.size(); ++i) {
        if (i > 0)
            emitter_.stream() << ", ";
        mir::LocalId argId = func.arg_locals[i];
        if (argId < func.locals.size()) {
            emitter_.stream() << sanitizeIdentifier(func.locals[argId].name);
        } else {
            emitter_.stream() << "arg" << i;
        }
    }
    emitter_.stream() << ")";
}

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
            if (assign_data.rvalue) {
                collectUsedLocalsInRvalue(*assign_data.rvalue, used);
            }
        }
        if (block->terminator) {
            collectUsedLocalsInTerminator(*block->terminator, used);
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

bool JSCodeGen::isVoidReturn(const mir::MirFunction& func) const {
    if (func.return_local < func.locals.size()) {
        const auto& local = func.locals[func.return_local];
        if (!local.type) {
            return !hasReturnLocalWrite(func);
        }
        if (local.type->kind == ast::TypeKind::Void) {
            return true;
        }
        return !hasReturnLocalWrite(func);
    }
    return true;
}

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

bool JSCodeGen::hasReturnLocalWrite(const mir::MirFunction& func) const {
    for (const auto& block : func.basic_blocks) {
        if (!block)
            continue;
        for (const auto& stmt : block->statements) {
            if (!stmt || stmt->kind != mir::MirStatement::Assign) {
                continue;
            }
            const auto& assign_data = std::get<mir::MirStatement::AssignData>(stmt->data);
            if (assign_data.place.projections.empty() &&
                assign_data.place.local == func.return_local) {
                return true;
            }
        }
    }
    return false;
}

void JSCodeGen::emitFunctionBody(const mir::MirFunction& func, const mir::MirProgram& program) {
    // 制御フロー解析
    ControlFlowAnalyzer cfAnalyzer(func);

    collectDeclareOnAssign(func);

    if (tryEmitObjectLiteralReturn(func)) {
        return;
    }

    if (tryEmitCssReturn(func)) {
        return;
    }

    // ローカル変数の宣言（引数とstatic変数を除く）
    // 線形フローの場合は使用される変数のみ宣言
    bool isLinear = cfAnalyzer.isLinearFlow();

    bool declared_any = false;
    for (const auto& local : func.locals) {
        // 引数はスキップ
        bool isArg = std::find(func.arg_locals.begin(), func.arg_locals.end(), local.id) !=
                     func.arg_locals.end();
        if (isArg) {
            // 引数がボクシング対象の場合、配列にラップ（再代入）
            if (boxed_locals_.count(local.id)) {
                std::string varName;
                if (local.id < func.arg_locals.size()) {
                    // named arg? getLocalVarName usually handles this but here signatures used
                    // simple names let's stick to getLocalVarName-ish logic or signature logic.
                    // implementation uses sanitizeIdentifier(local.name) for signature.
                    varName = sanitizeIdentifier(local.name);
                } else {
                    // signature logic was:
                    // mir::LocalId argId = func.arg_locals[i];
                    // ... sanitizeIdentifier(func.locals[argId].name)
                    varName = sanitizeIdentifier(local.name);
                }
                emitter_.emitLine("    " + varName + " = [" + varName + "];");
                emitter_.emitLine("    " + varName + ".__boxed = true;");
            }
            continue;
        }

        // static変数はグローバルで宣言済みなのでスキップ
        if (local.is_static)
            continue;

        if (!isLocalUsed(local.id)) {
            continue;
        }

        if (inline_values_.count(local.id) > 0) {
            continue;
        }
        if (declare_on_assign_.count(local.id) > 0) {
            continue;
        }

        // 変数宣言: 常にIDをサフィックスとして追加（引数以外）
        std::string defaultVal;
        if (local.type) {
            // インターフェース型がStructとして報告される場合に対応
            if (local.type->kind == ast::TypeKind::Struct &&
                interface_names_.count(local.type->name) > 0) {
                // インターフェース型はfat objectとして初期化
                defaultVal = "{data: null, vtable: null}";
            } else {
                defaultVal = jsDefaultValue(*local.type);
            }
        } else {
            defaultVal = "null";
        }
        std::string varName = sanitizeIdentifier(local.name) + "_" + std::to_string(local.id);

        if (boxed_locals_.count(local.id)) {
            emitter_.emitLine("let " + varName + " = [" + defaultVal + "];");
            emitter_.emitLine(varName + ".__boxed = true;");
            declared_any = true;
        } else {
            emitter_.emitLine("let " + varName + " = " + defaultVal + ";");
            declared_any = true;
        }
    }

    if (declared_any) {
        emitter_.emitLine();
    }

    // 線形フローの場合は直接出力
    if (isLinear) {
        auto blockOrder = cfAnalyzer.getLinearBlockOrder();
        for (mir::BlockId blockId : blockOrder) {
            if (blockId < func.basic_blocks.size() && func.basic_blocks[blockId]) {
                emitLinearBlock(*func.basic_blocks[blockId], func, program);
            }
        }
        return;
    }

    // ループがある場合は構造化フローを試みる
    if (cfAnalyzer.hasLoops()) {
        emitStructuredFlow(func, program, cfAnalyzer);
        return;
    }

    // 複雑な制御フローの場合はswitch/dispatchパターンを使用
    bool needLabels = func.basic_blocks.size() > 1;

    if (needLabels) {
        emitter_.emitLine("let __block = 0;");
        emitter_.emitLine("__dispatch: while (true) {");
        emitter_.increaseIndent();
        emitter_.emitLine("switch (__block) {");
        emitter_.increaseIndent();
    }

    for (const auto& block : func.basic_blocks) {
        if (block) {
            emitBasicBlock(*block, func, program);
        }
    }

    if (needLabels) {
        emitter_.emitLine("default:");
        emitter_.increaseIndent();
        emitter_.emitLine("break __dispatch;");
        emitter_.decreaseIndent();
        emitter_.decreaseIndent();
        emitter_.emitLine("}");  // switch
        emitter_.decreaseIndent();
        emitter_.emitLine("}");  // while
    }
}

// 構造化制御フローの生成
void JSCodeGen::emitStructuredFlow(const mir::MirFunction& func, const mir::MirProgram& program,
                                   const ControlFlowAnalyzer& cfAnalyzer) {
    std::set<mir::BlockId> emittedBlocks;

    // エントリブロックから開始
    mir::BlockId current = func.entry_block;

    while (current != mir::INVALID_BLOCK && emittedBlocks.count(current) == 0) {
        if (current >= func.basic_blocks.size() || !func.basic_blocks[current]) {
            break;
        }

        const auto& block = *func.basic_blocks[current];

        // ループヘッダーの場合は while ループを生成
        if (cfAnalyzer.isLoopHeader(current)) {
            emitLoopBlock(current, func, program, cfAnalyzer, emittedBlocks);

            // ループ後の続きを探す
            // ループヘッダーのターミネーターから出口を見つける
            if (block.terminator && block.terminator->kind == mir::MirTerminator::SwitchInt) {
                const auto& data =
                    std::get<mir::MirTerminator::SwitchIntData>(block.terminator->data);
                // SwitchIntのotherwiseがループ外への出口
                if (!cfAnalyzer.isLoopHeader(data.otherwise)) {
                    current = data.otherwise;
                    continue;
                }
                // targetsの中でループ外のものを探す
                for (const auto& [val, target] : data.targets) {
                    if (!cfAnalyzer.isLoopHeader(target) && emittedBlocks.count(target) == 0) {
                        current = target;
                        break;
                    }
                }
            }
            break;  // ループ後はフォールアウト
        }

        // 通常のブロック
        emittedBlocks.insert(current);
        emitBlockStatements(block, func);

        // ターミネーターに従って次のブロックを決定
        if (!block.terminator)
            break;

        switch (block.terminator->kind) {
            case mir::MirTerminator::Goto: {
                const auto& data = std::get<mir::MirTerminator::GotoData>(block.terminator->data);
                current = data.target;
                break;
            }
            case mir::MirTerminator::Call: {
                const auto& data = std::get<mir::MirTerminator::CallData>(block.terminator->data);
                emitTerminator(*block.terminator, func, program);
                current = data.success;
                break;
            }
            case mir::MirTerminator::Return: {
                emitLinearTerminator(*block.terminator, func, program);
                current = mir::INVALID_BLOCK;
                break;
            }
            case mir::MirTerminator::SwitchInt: {
                // if/else構造として処理
                emitIfElseBlock(block, func, program, cfAnalyzer, emittedBlocks);
                current = mir::INVALID_BLOCK;  // 別の経路で処理
                break;
            }
            default:
                current = mir::INVALID_BLOCK;
                break;
        }
    }
}

// ループブロックの生成（while (true) { ... if (!cond) break; ... } パターン）
void JSCodeGen::emitLoopBlock(mir::BlockId headerBlock, const mir::MirFunction& func,
                              const mir::MirProgram& program, const ControlFlowAnalyzer& cfAnalyzer,
                              std::set<mir::BlockId>& emittedBlocks) {
    emitter_.emitLine("while (true) {");
    emitter_.increaseIndent();

    // ループ内のブロックを処理
    std::set<mir::BlockId> loopBlocks;
    std::vector<mir::BlockId> workList = {headerBlock};

    // ループに含まれるブロックを収集
    while (!workList.empty()) {
        mir::BlockId bid = workList.back();
        workList.pop_back();

        if (loopBlocks.count(bid) > 0)
            continue;
        if (bid >= func.basic_blocks.size() || !func.basic_blocks[bid])
            continue;

        loopBlocks.insert(bid);

        const auto& block = *func.basic_blocks[bid];
        if (!block.terminator)
            continue;

        // 後続ブロックをワークリストに追加（ループ外は除く）
        for (mir::BlockId succ : block.successors) {
            if (loopBlocks.count(succ) == 0) {
                // バックエッジ先（ヘッダー）または他のループ内ブロック
                if (succ == headerBlock || cfAnalyzer.getDominators(succ).count(headerBlock) > 0) {
                    workList.push_back(succ);
                }
            }
        }
    }

    // ループ内ブロックを順番に出力
    mir::BlockId current = headerBlock;
    std::set<mir::BlockId> visitedInLoop;

    while (current != mir::INVALID_BLOCK && visitedInLoop.count(current) == 0) {
        if (current >= func.basic_blocks.size() || !func.basic_blocks[current])
            break;

        visitedInLoop.insert(current);
        emittedBlocks.insert(current);
        const auto& block = *func.basic_blocks[current];

        // ステートメントを出力
        emitBlockStatements(block, func);

        if (!block.terminator)
            break;

        switch (block.terminator->kind) {
            case mir::MirTerminator::SwitchInt: {
                const auto& data =
                    std::get<mir::MirTerminator::SwitchIntData>(block.terminator->data);
                std::string discrim = emitOperand(*data.discriminant, func);

                // 条件分岐: ループを抜けるか続けるか
                // targets[0]がループ継続、otherwiseがループ脱出の場合
                if (current == headerBlock && loopBlocks.count(data.otherwise) == 0) {
                    // if (!cond) break; パターン
                    emitter_.emitLine("if (" + discrim + " === 0) break;");
                    // else側（ループ継続）へ進む
                    if (!data.targets.empty()) {
                        current = data.targets[0].second;
                    } else {
                        current = mir::INVALID_BLOCK;
                    }
                } else if (current == headerBlock && !data.targets.empty() &&
                           loopBlocks.count(data.targets[0].second) == 0) {
                    // 逆パターン: 条件が真でループ脱出
                    emitter_.emitLine("if (" + discrim + " !== 0) break;");
                    current = data.otherwise;
                } else {
                    // 通常のif/else
                    for (const auto& [value, target] : data.targets) {
                        emitter_.emitLine("if (" + discrim + " === " + std::to_string(value) +
                                          ") {");
                        emitter_.increaseIndent();
                        if (loopBlocks.count(target) == 0) {
                            emitter_.emitLine("break;");
                        }
                        emitter_.decreaseIndent();
                        emitter_.emitLine("}");
                    }
                    current = data.otherwise;
                }
                break;
            }
            case mir::MirTerminator::Goto: {
                const auto& data = std::get<mir::MirTerminator::GotoData>(block.terminator->data);
                if (data.target == headerBlock) {
                    // バックエッジ: continue (明示的にcontinueは不要、whileの終わりで自動的に戻る)
                    current = mir::INVALID_BLOCK;  // ループの終わり
                } else if (loopBlocks.count(data.target) > 0) {
                    current = data.target;
                } else {
                    // ループ外へ脱出
                    emitter_.emitLine("break;");
                    current = mir::INVALID_BLOCK;
                }
                break;
            }
            case mir::MirTerminator::Call: {
                const auto& data = std::get<mir::MirTerminator::CallData>(block.terminator->data);
                // 関数呼び出しを出力
                emitTerminator(*block.terminator, func, program);
                if (data.success == headerBlock) {
                    current = mir::INVALID_BLOCK;
                } else {
                    current = data.success;
                }
                break;
            }
            case mir::MirTerminator::Return: {
                emitLinearTerminator(*block.terminator, func, program);
                current = mir::INVALID_BLOCK;
                break;
            }
            default:
                current = mir::INVALID_BLOCK;
                break;
        }
    }

    emitter_.decreaseIndent();
    emitter_.emitLine("}");  // while
}

// if/else構造の生成
void JSCodeGen::emitIfElseBlock(const mir::BasicBlock& block, const mir::MirFunction& func,
                                const mir::MirProgram& program,
                                [[maybe_unused]] const ControlFlowAnalyzer& cfAnalyzer,
                                std::set<mir::BlockId>& emittedBlocks) {
    if (!block.terminator || block.terminator->kind != mir::MirTerminator::SwitchInt) {
        return;
    }

    const auto& data = std::get<mir::MirTerminator::SwitchIntData>(block.terminator->data);
    std::string discrim = emitOperand(*data.discriminant, func);

    // 単純な if/else パターン
    for (const auto& [value, target] : data.targets) {
        emitter_.emitLine("if (" + discrim + " === " + std::to_string(value) + ") {");
        emitter_.increaseIndent();

        if (target < func.basic_blocks.size() && func.basic_blocks[target] &&
            emittedBlocks.count(target) == 0) {
            emittedBlocks.insert(target);
            emitLinearBlock(*func.basic_blocks[target], func, program);
        }

        emitter_.decreaseIndent();
        emitter_.emitLine("} else {");
        emitter_.increaseIndent();
    }

    // otherwise
    if (data.otherwise < func.basic_blocks.size() && func.basic_blocks[data.otherwise] &&
        emittedBlocks.count(data.otherwise) == 0) {
        emittedBlocks.insert(data.otherwise);
        emitLinearBlock(*func.basic_blocks[data.otherwise], func, program);
    }

    // 閉じ括弧
    for (size_t i = 0; i < data.targets.size(); ++i) {
        emitter_.decreaseIndent();
        emitter_.emitLine("}");
    }
}

// ブロック内のステートメントのみ出力（ターミネーターなし）
void JSCodeGen::emitBlockStatements(const mir::BasicBlock& block, const mir::MirFunction& func) {
    for (const auto& stmt : block.statements) {
        if (stmt) {
            emitStatement(*stmt, func);
        }
    }
}

std::string JSCodeGen::getLocalVarName(const mir::MirFunction& func, mir::LocalId localId) {
    if (localId >= func.locals.size()) {
        return "_local" + std::to_string(localId);
    }

    const auto& local = func.locals[localId];

    // static変数の場合はグローバル名を返す
    if (local.is_static) {
        return getStaticVarName(func, localId);
    }

    // 引数の場合は名前のみ、それ以外はIDサフィックス付き
    bool isArg = std::find(func.arg_locals.begin(), func.arg_locals.end(), local.id) !=
                 func.arg_locals.end();
    if (isArg) {
        return sanitizeIdentifier(local.name);
    } else {
        return sanitizeIdentifier(local.name) + "_" + std::to_string(local.id);
    }
}

bool JSCodeGen::isStaticVar(const mir::MirFunction& func, mir::LocalId localId) {
    if (localId >= func.locals.size()) {
        return false;
    }
    return func.locals[localId].is_static;
}

std::string JSCodeGen::getStaticVarName(const mir::MirFunction& func, mir::LocalId localId) {
    if (localId >= func.locals.size()) {
        return "_static_unknown";
    }
    const auto& local = func.locals[localId];
    return "__static_" + sanitizeIdentifier(func.name) + "_" + sanitizeIdentifier(local.name);
}

void JSCodeGen::collectStaticVars(const mir::MirProgram& program) {
    for (const auto& func : program.functions) {
        if (!func || func->is_extern)
            continue;

        for (const auto& local : func->locals) {
            if (local.is_static) {
                std::string globalName = "__static_" + sanitizeIdentifier(func->name) + "_" +
                                         sanitizeIdentifier(local.name);
                std::string defaultVal = local.type ? jsDefaultValue(*local.type) : "null";
                static_vars_[globalName] = defaultVal;
            }
        }
    }
}

void JSCodeGen::emitStaticVars() {
    if (static_vars_.empty())
        return;

    emitter_.emitLine("// Static variables");
    for (const auto& [name, defaultVal] : static_vars_) {
        emitter_.emitLine("let " + name + " = " + defaultVal + ";");
    }
    emitter_.emitLine();
}

}  // namespace cm::codegen::js
