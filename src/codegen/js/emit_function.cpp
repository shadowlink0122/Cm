#include "builtins.hpp"
#include "codegen.hpp"
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

void JSCodeGen::emitFunction(const mir::MirFunction& func, const mir::MirProgram& program) {
    if (generated_functions_.count(func.name)) {
        return;  // 重複防止
    }
    generated_functions_.insert(func.name);

    // アドレス取得されるローカル変数を収集（ボクシング用）
    // ※構造体型はJSオブジェクトとして参照渡しされるのでボクシング不要
    boxed_locals_.clear();
    for (const auto& block : func.basic_blocks) {
        if (!block)
            continue;
        for (const auto& stmt : block->statements) {
            if (stmt->kind == mir::MirStatement::Assign) {
                const auto& data = std::get<mir::MirStatement::AssignData>(stmt->data);
                if (data.rvalue->kind == mir::MirRvalue::Ref) {
                    const auto& refData = std::get<mir::MirRvalue::RefData>(data.rvalue->data);
                    mir::LocalId localId = refData.place.local;
                    // 構造体型・配列型・ポインタ-to-構造体型はJSオブジェクト（参照型）なのでボクシング不要
                    // ※monomorphizationでself引数がPointer型に変換されるケースに対応
                    if (localId < func.locals.size() && func.locals[localId].type &&
                        (func.locals[localId].type->kind == ast::TypeKind::Struct ||
                         func.locals[localId].type->kind == ast::TypeKind::Array ||
                         (func.locals[localId].type->kind == ast::TypeKind::Pointer &&
                          func.locals[localId].type->element_type &&
                          func.locals[localId].type->element_type->kind ==
                              ast::TypeKind::Struct))) {
                        continue;
                    }
                    boxed_locals_.insert(localId);
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

// implメソッドのself引数ソースを事前分析
// パターン: _tempN = Use(Copy(original_struct)) → Call(StructType__method, [Copy(_tempN), ...])
// このパターンで_tempNがimplメソッドのself引数としてのみ使われる場合、
// _tempNへの代入時に__cm_cloneをスキップして参照渡しにする
void JSCodeGen::collectImplSelfSources(const mir::MirFunction& func) {
    impl_self_sources_.clear();

    // ステップ1: Call terminator → 第1引数がCopyの構造体ローカルを収集
    std::unordered_set<mir::LocalId> self_arg_locals;
    for (const auto& block : func.basic_blocks) {
        if (!block)
            continue;
        if (!block->terminator)
            continue;
        if (block->terminator->kind != mir::MirTerminator::Call)
            continue;

        const auto& data = std::get<mir::MirTerminator::CallData>(block->terminator->data);

        // implメソッドかチェック（関数名に__を含む）
        std::string funcName;
        if (data.func->kind == mir::MirOperand::FunctionRef) {
            funcName = std::get<std::string>(data.func->data);
        }
        if (funcName.find("__") == std::string::npos)
            continue;

        // 第1引数（self）のローカルIDを取得
        if (data.args.empty() || !data.args[0])
            continue;
        if (data.args[0]->kind != mir::MirOperand::Copy &&
            data.args[0]->kind != mir::MirOperand::Move)
            continue;

        const auto& place = std::get<mir::MirPlace>(data.args[0]->data);
        if (!place.projections.empty())
            continue;

        mir::LocalId selfLocal = place.local;
        if (selfLocal >= func.locals.size())
            continue;

        // 引数自体は除外（引数はクローン対象外なので不要）
        bool isArg = std::find(func.arg_locals.begin(), func.arg_locals.end(), selfLocal) !=
                     func.arg_locals.end();
        if (isArg)
            continue;

        // 構造体型かチェック
        const auto& local = func.locals[selfLocal];
        if (!local.type || local.type->kind != ast::TypeKind::Struct)
            continue;

        self_arg_locals.insert(selfLocal);
    }

    // ステップ2: self_arg_localsのソース変数を追跡
    // _tempN = Use(Copy(source)) の source をimpl_self_sources_に追加
    for (const auto& block : func.basic_blocks) {
        if (!block)
            continue;
        for (const auto& stmt : block->statements) {
            if (!stmt || stmt->kind != mir::MirStatement::Assign)
                continue;
            const auto& assign = std::get<mir::MirStatement::AssignData>(stmt->data);

            // ターゲットがself_arg_localsに含まれるか
            if (assign.place.projections.empty() && self_arg_locals.count(assign.place.local) > 0) {
                // Rvalueが Use(Copy(source)) の場合、sourceをno-cloneに
                if (assign.rvalue->kind == mir::MirRvalue::Use) {
                    const auto& useData = std::get<mir::MirRvalue::UseData>(assign.rvalue->data);
                    if (useData.operand->kind == mir::MirOperand::Copy) {
                        const auto& srcPlace = std::get<mir::MirPlace>(useData.operand->data);
                        if (srcPlace.projections.empty()) {
                            // ソースローカルもself引数ローカルもno-cloneにマーク
                            impl_self_sources_.insert(assign.place.local);
                        }
                    }
                }
            }
        }
    }
}

void JSCodeGen::emitFunctionSignature(const mir::MirFunction& func) {
    std::string safeName = sanitizeIdentifier(func.name);
    // async関数の場合、asyncキーワードを付与
    if (func.is_async) {
        emitter_.stream() << "async ";
    }
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
    collectImplSelfSources(func);

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
                emitter_.emitLine(varName + " = [" + varName + "];");
                emitter_.emitLine(varName + ".__boxed = true;");
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

        // グローバル変数の場合、MirGlobalVarから初期値を取得
        bool foundGlobalInit = false;
        if (local.is_global) {
            for (const auto& gv : program.global_vars) {
                if (gv && gv->name == local.name && gv->init_value) {
                    defaultVal = emitConstant(*gv->init_value);
                    foundGlobalInit = true;
                    break;
                }
            }
        }

        if (!foundGlobalInit) {
            if (local.type) {
                // インターフェース型がStructとして報告される場合に対応
                if (local.type->kind == ast::TypeKind::Struct &&
                    interface_names_.count(local.type->name) > 0) {
                    // インターフェース型はfat objectとして初期化
                    defaultVal = "{data: null, vtable: null}";
                } else if (local.type->kind == ast::TypeKind::Struct) {
                    // 構造体型：ネストフィールドも含めた完全なデフォルト値を生成
                    defaultVal = getStructDefaultValue(*local.type);
                } else if (local.type->kind == ast::TypeKind::Array && local.type->element_type &&
                           local.type->element_type->kind == ast::TypeKind::Struct &&
                           local.type->array_size && *local.type->array_size > 0) {
                    // 構造体の配列：各要素を完全なデフォルト値で初期化
                    std::string elemDefault = getStructDefaultValue(*local.type->element_type);
                    defaultVal = "Array.from({length: " + std::to_string(*local.type->array_size) +
                                 "}, () => (" + elemDefault + "))";
                } else {
                    defaultVal = jsDefaultValue(*local.type);
                }
            } else {
                defaultVal = "null";
            }
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

    // 非線形フローはすべてswitch/dispatchパターンで処理
    // （emitStructuredFlowは不完全で __dispatch ラベルエラーの原因になるため廃止）

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

}  // namespace cm::codegen::js
