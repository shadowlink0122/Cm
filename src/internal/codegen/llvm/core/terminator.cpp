/// @file terminator.cpp
/// @brief ターミネータ変換処理
/// Print/Format処理はprint_codegen.cppに分離

#include "mir_to_llvm.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace cm::codegen::llvm_backend {

void MIRToLLVM::convertTerminator(const mir::MirTerminator& term) {
    // // debug_msg("MIR2LLVM", "convertTerminator");
    switch (term.kind) {
        case mir::MirTerminator::Goto: {
            auto& gotoData = std::get<mir::MirTerminator::GotoData>(term.data);
            auto target = blocks[gotoData.target];
            builder->CreateBr(target);
            break;
        }
        case mir::MirTerminator::SwitchInt: {
            auto& switchData = std::get<mir::MirTerminator::SwitchIntData>(term.data);
            auto discr = convertOperand(*switchData.discriminant);
            auto defaultBB = blocks[switchData.otherwise];
            auto switchInst = builder->CreateSwitch(discr, defaultBB, switchData.targets.size());

            for (const auto& [value, target] : switchData.targets) {
                auto discrType = discr->getType();
                auto caseVal = llvm::ConstantInt::get(discrType, value);
                switchInst->addCase(llvm::cast<llvm::ConstantInt>(caseVal), blocks[target]);
            }
            break;
        }
        case mir::MirTerminator::Return: {
            // NOTE: ベアメタル対応 - スタック配列は関数終了時に自動解放

            // sret関数はretval allocaの内容を先頭引数の呼び出し元バッファへmemcpyしてret voidする（C14 Phase 4。
            // 第一級集約returnを排し、SROAの全要素展開によるO2二次爆発を防ぐ）
            if (currentMIRFunction && needsSretReturn(*currentMIRFunction)) {
                auto retVal = locals[currentMIRFunction->return_local];
                auto sretPtr = currentFunction->getArg(0);
                if (retVal && llvm::isa<llvm::AllocaInst>(retVal)) {
                    auto allocaInst = llvm::cast<llvm::AllocaInst>(retVal);
                    auto dataLayout = module->getDataLayout();
                    auto allocSize = dataLayout.getTypeAllocSize(allocaInst->getAllocatedType());
                    builder->CreateMemCpy(sretPtr, llvm::MaybeAlign(), retVal, llvm::MaybeAlign(),
                                          allocSize);
                }
                builder->CreateRetVoid();
                break;
            }

            if (currentMIRFunction->name == "main") {
                // main関数は常にi32を返す
                if (currentMIRFunction->return_local < currentMIRFunction->locals.size()) {
                    auto retVal = locals[currentMIRFunction->return_local];
                    if (retVal) {
                        if (llvm::isa<llvm::AllocaInst>(retVal)) {
                            auto allocaInst = llvm::cast<llvm::AllocaInst>(retVal);
                            auto allocatedType = allocaInst->getAllocatedType();
                            retVal = builder->CreateLoad(allocatedType, retVal, "retval");
                        }
                        builder->CreateRet(retVal);
                    } else {
                        builder->CreateRet(llvm::ConstantInt::get(ctx.getI32Type(), 0));
                    }
                } else {
                    builder->CreateRet(llvm::ConstantInt::get(ctx.getI32Type(), 0));
                }
            } else {
                auto& returnLocal = currentMIRFunction->locals[currentMIRFunction->return_local];
                bool isVoidReturn =
                    returnLocal.type && returnLocal.type->kind == hir::TypeKind::Void;

                if (isVoidReturn) {
                    builder->CreateRetVoid();
                } else if (currentMIRFunction->return_local < currentMIRFunction->locals.size()) {
                    auto retVal = locals[currentMIRFunction->return_local];
                    if (retVal) {
                        if (llvm::isa<llvm::AllocaInst>(retVal)) {
                            auto allocaInst = llvm::cast<llvm::AllocaInst>(retVal);
                            auto allocatedType = allocaInst->getAllocatedType();
                            retVal = builder->CreateLoad(allocatedType, retVal, "retval");
                        }
                        builder->CreateRet(retVal);
                    } else {
                        builder->CreateRetVoid();
                    }
                } else {
                    builder->CreateRetVoid();
                }
            }
            break;
        }
        case mir::MirTerminator::Unreachable: {
            builder->CreateUnreachable();
            break;
        }
        case mir::MirTerminator::Call: {
            // Callターミネータの変換本体はterminator/call.cppに分離
            convertCallTerminator(std::get<mir::MirTerminator::CallData>(term.data));
            break;
        }
    }
}

}  // namespace cm::codegen::llvm_backend
