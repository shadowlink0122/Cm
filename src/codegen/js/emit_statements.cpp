#include "builtins.hpp"
#include "codegen.hpp"
#include "types.hpp"

#include <algorithm>

namespace cm::codegen::js {

using ast::TypeKind;

void JSCodeGen::emitBasicBlock(const mir::BasicBlock& block, const mir::MirFunction& func,
                               [[maybe_unused]] const mir::MirProgram& program) {
    bool needLabels = func.basic_blocks.size() > 1;

    if (needLabels) {
        emitter_.emitLine("case " + std::to_string(block.id) + ":");
        emitter_.increaseIndent();
    }

    // 文
    for (const auto& stmt : block.statements) {
        if (stmt) {
            emitStatement(*stmt, func);
        }
    }

    // 終端命令
    if (block.terminator) {
        emitTerminator(*block.terminator, func, program);
    }

    if (needLabels) {
        emitter_.emitLine("break;");
        emitter_.decreaseIndent();
    }
}

void JSCodeGen::emitStatement(const mir::MirStatement& stmt, const mir::MirFunction& func) {
    switch (stmt.kind) {
        case mir::MirStatement::Assign: {
            const auto& data = std::get<mir::MirStatement::AssignData>(stmt.data);
            mir::LocalId target_local = data.place.local;

            if (data.place.projections.empty() && inline_values_.count(target_local) > 0) {
                break;
            }

            std::string place = emitPlace(data.place, func);

            // クロージャ変数への代入かチェック
            if (target_local < func.locals.size() && data.place.projections.empty() &&
                func.locals[target_local].is_closure && data.rvalue->kind == mir::MirRvalue::Use) {
                const auto& useData = std::get<mir::MirRvalue::UseData>(data.rvalue->data);
                if (useData.operand->kind == mir::MirOperand::FunctionRef) {
                    const auto& funcName = std::get<std::string>(useData.operand->data);
                    const auto& local = func.locals[target_local];
                    std::string rvalue = emitLambdaRef(funcName, func, local.captured_locals);
                    emitter_.emitLine(place + " = " + rvalue + ";");
                    break;
                }
            }

            std::string rvalue = emitRvalue(*data.rvalue, func);
            if (data.place.projections.empty() && declare_on_assign_.count(target_local) > 0 &&
                declared_locals_.count(target_local) == 0) {
                emitter_.emitLine("let " + place + " = " + rvalue + ";");
                declared_locals_.insert(target_local);
            } else {
                emitter_.emitLine(place + " = " + rvalue + ";");
            }
            break;
        }
        case mir::MirStatement::StorageLive:
        case mir::MirStatement::StorageDead:
        case mir::MirStatement::Nop:
            // JSではこれらは無視
            break;
    }
}

void JSCodeGen::emitTerminator(const mir::MirTerminator& term, const mir::MirFunction& func,
                               [[maybe_unused]] const mir::MirProgram& program) {
    switch (term.kind) {
        case mir::MirTerminator::Return: {
            // 戻り値を返す
            if (func.return_local < func.locals.size()) {
                const auto& local = func.locals[func.return_local];
                if (local.type && local.type->kind == ast::TypeKind::Void) {
                    emitter_.emitLine("return;");
                } else {
                    auto it = inline_values_.find(func.return_local);
                    if (it != inline_values_.end()) {
                        emitter_.emitLine("return " + it->second + ";");
                    } else {
                        std::string retVar = getLocalVarName(func, func.return_local);
                        emitter_.emitLine("return " + retVar + ";");
                    }
                }
            } else {
                emitter_.emitLine("return;");
            }
            break;
        }

        case mir::MirTerminator::Goto: {
            const auto& data = std::get<mir::MirTerminator::GotoData>(term.data);
            if (func.basic_blocks.size() > 1) {
                emitter_.emitLine("__block = " + std::to_string(data.target) + ";");
                emitter_.emitLine("continue __dispatch;");
            }
            break;
        }

        case mir::MirTerminator::SwitchInt: {
            const auto& data = std::get<mir::MirTerminator::SwitchIntData>(term.data);
            std::string discrim = emitOperand(*data.discriminant, func);

            // 判別式の型がboolかどうかを判定
            bool isBoolType = false;
            if (data.discriminant->kind == mir::MirOperand::Copy ||
                data.discriminant->kind == mir::MirOperand::Move) {
                const auto& place = std::get<mir::MirPlace>(data.discriminant->data);
                if (place.local < func.locals.size()) {
                    const auto& local = func.locals[place.local];
                    if (local.type && local.type->kind == ast::TypeKind::Bool) {
                        isBoolType = true;
                    }
                }
            }

            for (const auto& [value, target] : data.targets) {
                // bool型の場合のみtruthy/falsy比較を使用
                if (isBoolType && value == 1) {
                    emitter_.emitLine("if (" + discrim + ") {");
                } else if (isBoolType && value == 0) {
                    emitter_.emitLine("if (!" + discrim + ") {");
                } else {
                    emitter_.emitLine("if (" + discrim + " === " + std::to_string(value) + ") {");
                }
                emitter_.increaseIndent();
                emitter_.emitLine("__block = " + std::to_string(target) + ";");
                emitter_.emitLine("continue __dispatch;");
                emitter_.decreaseIndent();
                emitter_.emitLine("}");
            }
            emitter_.emitLine("__block = " + std::to_string(data.otherwise) + ";");
            emitter_.emitLine("continue __dispatch;");
            break;
        }

        case mir::MirTerminator::Call: {
            const auto& data = std::get<mir::MirTerminator::CallData>(term.data);

            // 関数名取得
            std::string funcName;
            if (data.func->kind == mir::MirOperand::FunctionRef) {
                funcName = std::get<std::string>(data.func->data);
            } else {
                funcName = emitOperand(*data.func, func);
            }

            // 引数
            std::vector<std::string> args;
            bool isFormatFunc = (funcName == "cm_println_format" || funcName == "cm_print_format");

            // 呼び出し先関数を取得
            const mir::MirFunction* calleeFunc = nullptr;
            auto it_func = function_map_.find(funcName);
            if (it_func != function_map_.end()) {
                calleeFunc = it_func->second;
                if (calleeFunc->is_extern &&
                    (calleeFunc->package_name == "js" || calleeFunc->package_name.empty())) {
                    funcName = mapExternJsName(calleeFunc->name);
                }
            }

            for (size_t i = 0; i < data.args.size(); ++i) {
                const auto& arg = data.args[i];
                if (arg) {
                    std::string argStr = emitOperand(*arg, func);

                    // 引数の型変換（Struct -> Interfaceの暗黙キャスト）
                    if (calleeFunc && i < calleeFunc->arg_locals.size()) {
                        mir::LocalId targetLocalId = calleeFunc->arg_locals[i];
                        if (targetLocalId < calleeFunc->locals.size()) {
                            const auto& targetLocal = calleeFunc->locals[targetLocalId];
                            if (targetLocal.type && targetLocal.type->kind == TypeKind::Interface) {
                                auto sourceType = getOperandType(*arg, func);
                                if (sourceType && sourceType->kind == TypeKind::Struct) {
                                    std::string vtableName =
                                        sanitizeIdentifier(sourceType->name) + "_" +
                                        sanitizeIdentifier(targetLocal.type->name) + "_vtable";
                                    argStr = "{ data: " + argStr + ", vtable: " + vtableName + " }";
                                }
                            }
                        }
                    }

                    // フォーマット関数の場合、char型引数を文字に変換
                    // 引数インデックス2以降が実際のフォーマット値
                    if (isFormatFunc && i >= 2) {
                        // オペランドの型をチェック
                        if (arg->kind == mir::MirOperand::Copy ||
                            arg->kind == mir::MirOperand::Move) {
                            const auto& place = std::get<mir::MirPlace>(arg->data);
                            if (place.local < func.locals.size()) {
                                const auto& local = func.locals[place.local];
                                if (local.type && local.type->kind == ast::TypeKind::Char) {
                                    // char型は文字に変換
                                    argStr = "String.fromCharCode(" + argStr + ")";
                                }
                            }
                        }
                    }

                    args.push_back(argStr);
                }
            }

            // 組み込み関数のチェック
            std::string callExpr;
            if (isBuiltinFunction(funcName)) {
                callExpr = emitBuiltinCall(funcName, args);
            } else if (data.is_virtual && !args.empty()) {
                // 仮想ディスパッチ: receiver.vtable.method(receiver.data, ...)
                // args[0]がreceiverで、fat object {data, vtable}
                std::string receiver = args[0];
                std::string methodName;

                if (!data.method_name.empty()) {
                    methodName = data.method_name;
                } else {
                    // メソッド名を抽出（Interface__method形式から）
                    size_t sepPos = funcName.find("__");
                    methodName =
                        (sepPos != std::string::npos) ? funcName.substr(sepPos + 2) : funcName;
                    // サフィックスを除去（method_SType形式から）
                    size_t suffixPos = methodName.rfind("_S");
                    if (suffixPos != std::string::npos) {
                        methodName = methodName.substr(0, suffixPos);
                    }
                }

                // vtableから関数を取得し、dataを第一引数として呼び出す
                callExpr = receiver + ".vtable." + sanitizeIdentifier(methodName) + "(" + receiver +
                           ".data";
                for (size_t i = 1; i < args.size(); ++i) {
                    callExpr += ", " + args[i];
                }
                callExpr += ")";
            } else {
                std::string safeFuncName = sanitizeIdentifier(funcName);
                callExpr = safeFuncName + "(";
                for (size_t i = 0; i < args.size(); ++i) {
                    if (i > 0)
                        callExpr += ", ";
                    callExpr += args[i];
                }
                callExpr += ")";
            }

            // 戻り値の格納
            bool skip_dest = false;
            if (data.destination && data.destination->projections.empty()) {
                if (!isLocalUsed(data.destination->local)) {
                    skip_dest = true;
                }
            }
            if (data.destination && !skip_dest) {
                std::string dest = emitPlace(*data.destination, func);
                emitter_.emitLine(dest + " = " + callExpr + ";");
            } else {
                emitter_.emitLine(callExpr + ";");
            }

            // 次のブロック
            if (func.basic_blocks.size() > 1) {
                emitter_.emitLine("__block = " + std::to_string(data.success) + ";");
                emitter_.emitLine("continue __dispatch;");
            }
            break;
        }

        case mir::MirTerminator::Unreachable:
            emitter_.emitLine("throw new Error('Unreachable code');");
            break;
    }
}

// 線形フロー用の基本ブロック出力（switch/dispatchなし）
void JSCodeGen::emitLinearBlock(const mir::BasicBlock& block, const mir::MirFunction& func,
                                [[maybe_unused]] const mir::MirProgram& program) {
    // 文を直接出力
    for (const auto& stmt : block.statements) {
        if (stmt) {
            emitStatement(*stmt, func);
        }
    }

    // 終端命令を線形フロー用に出力
    if (block.terminator) {
        emitLinearTerminator(*block.terminator, func, program);
    }
}

// 線形フロー用の終端命令出力
void JSCodeGen::emitLinearTerminator(const mir::MirTerminator& term, const mir::MirFunction& func,
                                     [[maybe_unused]] const mir::MirProgram& program) {
    switch (term.kind) {
        case mir::MirTerminator::Return: {
            // 戻り値を返す
            if (func.return_local < func.locals.size()) {
                const auto& local = func.locals[func.return_local];
                if (local.type && local.type->kind == ast::TypeKind::Void) {
                    emitter_.emitLine("return;");
                } else {
                    auto it = inline_values_.find(func.return_local);
                    if (it != inline_values_.end()) {
                        emitter_.emitLine("return " + it->second + ";");
                    } else {
                        std::string retVar = getLocalVarName(func, func.return_local);
                        emitter_.emitLine("return " + retVar + ";");
                    }
                }
            } else {
                emitter_.emitLine("return;");
            }
            break;
        }

        case mir::MirTerminator::Goto:
            // 線形フローでは次のブロックに自然にフォールスルー
            // 何も出力しない
            break;

        case mir::MirTerminator::Call: {
            const auto& data = std::get<mir::MirTerminator::CallData>(term.data);

            // 関数名取得
            std::string funcName;
            if (data.func->kind == mir::MirOperand::FunctionRef) {
                funcName = std::get<std::string>(data.func->data);
            } else {
                funcName = emitOperand(*data.func, func);
            }

            // 引数
            std::vector<std::string> args;
            bool isFormatFunc = (funcName == "cm_println_format" || funcName == "cm_print_format");

            // 呼び出し先関数を取得
            const mir::MirFunction* calleeFunc = nullptr;
            auto it_func = function_map_.find(funcName);
            if (it_func != function_map_.end()) {
                calleeFunc = it_func->second;
                if (calleeFunc->is_extern &&
                    (calleeFunc->package_name == "js" || calleeFunc->package_name.empty())) {
                    funcName = mapExternJsName(calleeFunc->name);
                }
            }

            for (size_t i = 0; i < data.args.size(); ++i) {
                const auto& arg = data.args[i];
                if (arg) {
                    std::string argStr = emitOperand(*arg, func);

                    // 引数の型変換（Struct -> Interfaceの暗黙キャスト）
                    if (calleeFunc && i < calleeFunc->arg_locals.size()) {
                        mir::LocalId targetLocalId = calleeFunc->arg_locals[i];
                        if (targetLocalId < calleeFunc->locals.size()) {
                            const auto& targetLocal = calleeFunc->locals[targetLocalId];

                            bool isTargetInterface =
                                targetLocal.type &&
                                (targetLocal.type->kind == TypeKind::Interface ||
                                 (targetLocal.type->kind == TypeKind::Struct &&
                                  interface_names_.count(targetLocal.type->name)));

                            if (isTargetInterface) {
                                auto sourceType = getOperandType(*arg, func);
                                if (sourceType && sourceType->kind == TypeKind::Struct) {
                                    std::string vtableName =
                                        sanitizeIdentifier(sourceType->name) + "_" +
                                        sanitizeIdentifier(targetLocal.type->name) + "_vtable";
                                    argStr = "{ data: " + argStr + ", vtable: " + vtableName + " }";
                                }
                            }
                        }
                    }

                    // フォーマット関数の場合、char型引数を文字に変換
                    if (isFormatFunc && i >= 2) {
                        if (arg->kind == mir::MirOperand::Copy ||
                            arg->kind == mir::MirOperand::Move) {
                            const auto& place = std::get<mir::MirPlace>(arg->data);
                            if (place.local < func.locals.size()) {
                                const auto& local = func.locals[place.local];
                                if (local.type && local.type->kind == ast::TypeKind::Char) {
                                    argStr = "String.fromCharCode(" + argStr + ")";
                                }
                            }
                        }
                    }

                    args.push_back(argStr);
                }
            }

            // 組み込み関数のチェック
            std::string callExpr;
            if (isBuiltinFunction(funcName)) {
                callExpr = emitBuiltinCall(funcName, args);
            } else if (data.is_virtual && !args.empty()) {
                // 仮想ディスパッチ: receiver.vtable.method(receiver.data, ...)
                // args[0]がreceiverで、fat object {data, vtable}
                std::string receiver = args[0];
                std::string methodName;

                if (!data.method_name.empty()) {
                    methodName = data.method_name;
                } else {
                    // メソッド名を抽出（Interface__method形式から）
                    size_t sepPos = funcName.find("__");
                    methodName =
                        (sepPos != std::string::npos) ? funcName.substr(sepPos + 2) : funcName;
                    // サフィックスを除去（method_SType形式から）
                    size_t suffixPos = methodName.rfind("_S");
                    if (suffixPos != std::string::npos) {
                        methodName = methodName.substr(0, suffixPos);
                    }
                }

                // vtableから関数を取得し、dataを第一引数として呼び出す
                callExpr = receiver + ".vtable." + sanitizeIdentifier(methodName) + "(" + receiver +
                           ".data";
                for (size_t i = 1; i < args.size(); ++i) {
                    callExpr += ", " + args[i];
                }
                callExpr += ")";
            } else {
                std::string safeFuncName = sanitizeIdentifier(funcName);
                callExpr = safeFuncName + "(";
                for (size_t i = 0; i < args.size(); ++i) {
                    if (i > 0)
                        callExpr += ", ";
                    callExpr += args[i];
                }
                callExpr += ")";
            }

            // 戻り値の格納
            bool skip_dest = false;
            if (data.destination && data.destination->projections.empty()) {
                if (!isLocalUsed(data.destination->local)) {
                    skip_dest = true;
                }
            }
            if (data.destination && !skip_dest) {
                std::string dest = emitPlace(*data.destination, func);
                emitter_.emitLine(dest + " = " + callExpr + ";");
            } else {
                emitter_.emitLine(callExpr + ";");
            }
            // 線形フローでは次のブロックに自然にフォールスルー
            break;
        }

        case mir::MirTerminator::SwitchInt:
            // 線形フローにはSwitchIntは含まれないはず
            // フォールバックとしてエラーを投げる
            emitter_.emitLine("throw new Error('Unexpected SwitchInt in linear flow');");
            break;

        case mir::MirTerminator::Unreachable:
            emitter_.emitLine("throw new Error('Unreachable code');");
            break;
    }
}

}  // namespace cm::codegen::js
