#include "codegen.hpp"

#include "builtins.hpp"
#include "runtime.hpp"
#include "types.hpp"

#include <algorithm>
#include <iomanip>
#include <iostream>
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

        std::ofstream html(htmlFile);
        if (html.is_open()) {
            html << "<!DOCTYPE html>\n";
            html << "<html>\n<head>\n";
            html << "    <meta charset=\"UTF-8\">\n";
            html << "    <title>Cm Application</title>\n";
            html << "</head>\n<body>\n";
            html << "    <script src=\""
                 << (options_.outputFile.empty() ? "output.js" : options_.outputFile)
                 << "\"></script>\n";
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

    // ヘルパー関数: __cm_unwrap (ボックス化された値を展開)
    emitter_.emitLine("function __cm_unwrap(val) {");
    emitter_.emitLine("    if (val && val.__boxed) return val[0];");
    emitter_.emitLine("    return val;");
    emitter_.emitLine("}");
    emitter_.emitLine("");

    // ランタイムヘルパーを出力
    emitRuntime(emitter_);
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
    std::string safeName = sanitizeIdentifier(st.name);

    emitter_.emitLine("// Struct: " + st.name);
    emitter_.emitLine("function " + safeName + "() {");
    emitter_.increaseIndent();
    emitter_.emitLine("return {");
    emitter_.increaseIndent();

    for (size_t i = 0; i < st.fields.size(); ++i) {
        const auto& field = st.fields[i];
        std::string defaultVal = jsDefaultValue(*field.type);
        std::string line = formatStructFieldKey(st, field.name) + ": " + defaultVal;
        if (i < st.fields.size() - 1) {
            line += ",";
        }
        emitter_.emitLine(line);
    }

    emitter_.decreaseIndent();
    emitter_.emitLine("};");
    emitter_.decreaseIndent();
    emitter_.emitLine("}");
    emitter_.emitLine();
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

void JSCodeGen::emitFunctionBody(const mir::MirFunction& func, const mir::MirProgram& program) {
    // 制御フロー解析
    ControlFlowAnalyzer cfAnalyzer(func);

    // ローカル変数の宣言（引数とstatic変数を除く）
    // 線形フローの場合は使用される変数のみ宣言
    bool isLinear = cfAnalyzer.isLinearFlow();

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
        } else {
            emitter_.emitLine("let " + varName + " = " + defaultVal + ";");
        }
    }

    if (!func.locals.empty()) {
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
