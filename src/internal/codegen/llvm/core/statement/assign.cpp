/// @file assign.cpp
/// @brief MIR Assign文 → LLVM IR 変換（convertStatementのAssignケースを分離）

#include "internal/base/debug/codegen.hpp"
#include "internal/codegen/llvm/core/mir_to_llvm.hpp"
#include "internal/codegen/llvm/monitoring/compilation_guard.hpp"

#include <iostream>
#include <llvm/IR/InlineAsm.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cm::codegen::llvm_backend {

/// Assign文の変換本体（分離元のswitch脱出用breakはreturnに置換済み）
void MIRToLLVM::convertAssignStatement(const mir::MirStatement::AssignData& assign) {
    // interface型へのcoercion（動的ディスパッチ用 fat pointer 構築）
    // - Shape sh = sq;      : dest=interface値、src=具象構造体
    // - Shape* p = &sq;     : dest=interfaceポインタ、src=具象構造体へのRef
    if (assign.place.projections.empty() && currentMIRFunction &&
        assign.place.local < currentMIRFunction->locals.size()) {
        const auto& destType = currentMIRFunction->locals[assign.place.local].type;

        // Case A: interface値への具象構造体の代入
        if (destType && destType->kind == hir::TypeKind::Struct &&
            isInterfaceType(destType->name) && assign.rvalue->kind == mir::MirRvalue::Use) {
            auto& useData = std::get<mir::MirRvalue::UseData>(assign.rvalue->data);
            if (useData.operand && (useData.operand->kind == mir::MirOperand::Copy ||
                                    useData.operand->kind == mir::MirOperand::Move)) {
                auto& srcPlace = std::get<mir::MirPlace>(useData.operand->data);
                if (srcPlace.projections.empty() &&
                    srcPlace.local < currentMIRFunction->locals.size()) {
                    const auto& srcType = currentMIRFunction->locals[srcPlace.local].type;
                    if (srcType && srcType->kind == hir::TypeKind::Struct &&
                        !isInterfaceType(srcType->name)) {
                        // 具象構造体のallocaをdataポインタとしてfat pointerを構築
                        auto srcAddr = locals[srcPlace.local];
                        if (srcAddr) {
                            auto fat =
                                createInterfaceFatPtr(srcAddr, srcType->name, destType->name);
                            if (allocatedLocals.count(assign.place.local) > 0 &&
                                locals[assign.place.local]) {
                                builder->CreateStore(fat, locals[assign.place.local]);
                            } else {
                                // SSA形式のローカルにはfat値を直接束縛する
                                locals[assign.place.local] = fat;
                            }
                            return;
                        }
                    }
                }
            }
        }

        // Case B: interfaceポインタへの具象構造体アドレスの代入
        if (destType && destType->kind == hir::TypeKind::Pointer && destType->element_type &&
            destType->element_type->kind == hir::TypeKind::Struct &&
            isInterfaceType(destType->element_type->name)) {
            // Ref rvalue（&sq）またはUse copy（ポインタ変数経由）でソースが具象構造体を指す場合
            const std::string& ifaceName = destType->element_type->name;
            llvm::Value* dataPtr = nullptr;
            std::string concreteName;

            if (assign.rvalue->kind == mir::MirRvalue::Ref) {
                // &sq を直接代入するケース
                auto& refData = std::get<mir::MirRvalue::RefData>(assign.rvalue->data);
                const auto& refPlace = refData.place;
                if (refPlace.projections.empty() &&
                    refPlace.local < currentMIRFunction->locals.size()) {
                    const auto& srcType = currentMIRFunction->locals[refPlace.local].type;
                    if (srcType && srcType->kind == hir::TypeKind::Struct &&
                        !isInterfaceType(srcType->name) && locals[refPlace.local]) {
                        dataPtr = locals[refPlace.local];
                        concreteName = srcType->name;
                    }
                }
            } else if (assign.rvalue->kind == mir::MirRvalue::Use) {
                // 具象構造体ポインタ変数のコピー（&sq が一旦 *Sq テンポラリを経由するケース）
                auto& useData = std::get<mir::MirRvalue::UseData>(assign.rvalue->data);
                if (useData.operand && (useData.operand->kind == mir::MirOperand::Copy ||
                                        useData.operand->kind == mir::MirOperand::Move)) {
                    auto& srcPlace = std::get<mir::MirPlace>(useData.operand->data);
                    if (srcPlace.projections.empty() &&
                        srcPlace.local < currentMIRFunction->locals.size()) {
                        const auto& srcType = currentMIRFunction->locals[srcPlace.local].type;
                        if (srcType && srcType->kind == hir::TypeKind::Pointer &&
                            srcType->element_type &&
                            srcType->element_type->kind == hir::TypeKind::Struct &&
                            !isInterfaceType(srcType->element_type->name) &&
                            locals[srcPlace.local]) {
                            // ポインタ値をロードしてdataポインタとする
                            dataPtr = builder->CreateLoad(ctx.getPtrType(), locals[srcPlace.local],
                                                          "iface_src");
                            concreteName = srcType->element_type->name;
                        }
                    }
                }
            }

            if (dataPtr && !concreteName.empty()) {
                // Shape* はfat pointer値そのもの:
                // dataフィールドが実装オブジェクトを直接指す
                auto fat = createInterfaceFatPtr(dataPtr, concreteName, ifaceName);
                if (allocatedLocals.count(assign.place.local) > 0 && locals[assign.place.local]) {
                    builder->CreateStore(fat, locals[assign.place.local]);
                } else {
                    // SSA形式のローカルにはfat値を直接束縛する
                    locals[assign.place.local] = fat;
                }
                return;
            }

            // &sh（interface値のアドレス取得）: 同じオブジェクトを指す
            // fat pointerのコピーとして扱う
            if (assign.rvalue->kind == mir::MirRvalue::Ref) {
                auto& refData2 = std::get<mir::MirRvalue::RefData>(assign.rvalue->data);
                const auto& rp = refData2.place;
                if (rp.projections.empty() && rp.local < currentMIRFunction->locals.size()) {
                    const auto& st = currentMIRFunction->locals[rp.local].type;
                    if (st && st->kind == hir::TypeKind::Struct && isInterfaceType(st->name) &&
                        locals[rp.local]) {
                        auto fatType = getInterfaceFatPtrType(st->name);
                        llvm::Value* fatVal = locals[rp.local];
                        if (fatVal->getType()->isPointerTy()) {
                            fatVal = builder->CreateLoad(fatType, fatVal, "iface_copy");
                        }
                        if (allocatedLocals.count(assign.place.local) > 0 &&
                            locals[assign.place.local]) {
                            builder->CreateStore(fatVal, locals[assign.place.local]);
                        } else {
                            locals[assign.place.local] = fatVal;
                        }
                        return;
                    }
                }
            }
        }
    }

    // Tagged Unionペイロード読み込みの特別処理
    // rvalueがUse/Copyで、ソースがTagged Unionのfield[1]かつターゲットが構造体の場合
    // memcpyを使用して直接コピー
    if (assign.rvalue->kind == mir::MirRvalue::Use) {
        auto& useData = std::get<mir::MirRvalue::UseData>(assign.rvalue->data);
        if (useData.operand && (useData.operand->kind == mir::MirOperand::Copy ||
                                useData.operand->kind == mir::MirOperand::Move)) {
            auto& srcPlace = std::get<mir::MirPlace>(useData.operand->data);

            // ソースがTagged Unionのfield[1]か確認
            bool isSrcTaggedUnionPayload = false;
            if (!srcPlace.projections.empty() &&
                srcPlace.projections.back().kind == mir::ProjectionKind::Field &&
                srcPlace.projections.back().field_id == 1) {
                if (currentMIRFunction && srcPlace.local < currentMIRFunction->locals.size()) {
                    auto& srcLocal = currentMIRFunction->locals[srcPlace.local];

                    if (srcLocal.type && srcLocal.type->name.find("__TaggedUnion_") == 0) {
                        isSrcTaggedUnionPayload = true;
                    }
                }
            }

            // ターゲットが構造体型か確認
            bool isTargetStruct = false;
            hir::TypePtr targetType = nullptr;
            if (currentMIRFunction && assign.place.local < currentMIRFunction->locals.size()) {
                targetType = currentMIRFunction->locals[assign.place.local].type;
                if (targetType && targetType->kind == hir::TypeKind::Struct) {
                    isTargetStruct = true;
                }
            }

            // Tagged Unionペイロードからの値コピー
            if (isSrcTaggedUnionPayload) {
                if (isTargetStruct) {
                    // 構造体ペイロード → memcpyで直接コピー
                    auto srcAddr = convertPlaceToAddress(srcPlace);

                    // ターゲットアドレス（構造体ローカル）
                    auto destAddr = locals[assign.place.local];
                    if (!destAddr && allocatedLocals.count(assign.place.local) > 0) {
                        destAddr = locals[assign.place.local];
                    }

                    if (srcAddr && destAddr) {
                        // 構造体サイズを取得
                        auto llvmTargetType = convertType(targetType);
                        auto dataLayout = module->getDataLayout();
                        auto structSize = dataLayout.getTypeAllocSize(llvmTargetType);

                        // memcpy: dest=構造体ローカル, src=i8配列, size=構造体サイズ
                        builder->CreateMemCpy(destAddr, llvm::MaybeAlign(), srcAddr,
                                              llvm::MaybeAlign(), structSize);
                        return;
                    }
                } else if (targetType) {
                    // 非構造体ペイロード（string/ptr/int等）
                    // ペイロードバイト配列からターゲット型でロード
                    auto srcAddr = convertPlaceToAddress(srcPlace);
                    if (srcAddr) {
                        auto llvmTargetType = convertType(targetType);
                        auto loadVal = builder->CreateLoad(llvmTargetType, srcAddr, "payload_load");

                        auto destAddr = locals[assign.place.local];
                        if (destAddr && allocatedLocals.count(assign.place.local) > 0) {
                            // allocaモード: load→store
                            builder->CreateStore(loadVal, destAddr);
                        } else {
                            // SSAモード（string等allocaなし）: 直接値を設定
                            locals[assign.place.local] = loadVal;
                        }
                        return;
                    }
                }
            }
        }
    }

    auto rvalue = convertRvalue(*assign.rvalue);

    if (rvalue) {
        // 関数参照の特別処理
        // 投影がある場合（構造体の関数型フィールドへの代入 ops.apply = f 等）はこのショートカットを使わず、
        // 通常のstore経路で関数ポインタをフィールドへ書き込む（ここでlocalsを上書きすると構造体ローカルのスロット自体が関数値に化けて後続のフィールドアクセスが壊れる）
        if (llvm::isa<llvm::Function>(rvalue) && assign.place.projections.empty()) {
            // Function*の場合、直接localsに格納（allocaせずにSSA形式で扱う）
            locals[assign.place.local] = rvalue;
            // 確認: 実際に格納されたか
            if (cm::debug::debug_mode() && currentMIRFunction &&
                currentMIRFunction->name == "main") {
                auto func = llvm::cast<llvm::Function>(rvalue);
                debug_msg("MIR", "Stored function '" + func->getName().str() + "' to local " +
                                     std::to_string(assign.place.local) +
                                     " (locals.size=" + std::to_string(locals.size()) + ")");
                // local 2の状態を確認
                if (locals.size() > 2 && locals[2]) {
                    debug_msg("MIR",
                              "Local 2 is now: " + std::string(llvm::isa<llvm::Function>(locals[2])
                                                                   ? "Function"
                                                                   : "Other"));
                }
            }
            return;
        }

        // projectionsがある場合（構造体フィールドなど）は常にstoreが必要
        bool hasProjections = !assign.place.projections.empty();

        // allocaされた変数かどうかをチェック
        bool isAllocated = allocatedLocals.count(assign.place.local) > 0;

        // 関数ポインタのSSA形式での代入も処理
        if (!hasProjections && !isAllocated) {
            // SSA形式の変数への代入（関数ポインタ等）
            locals[assign.place.local] = rvalue;
            return;
        }

        if (hasProjections || isAllocated) {
            // Placeに値を格納
            auto addr = convertPlaceToAddress(assign.place);

            if (addr) {
                // ターゲット型を取得（allocaまたはGEPの場合）
                llvm::Type* targetType = nullptr;
                if (auto alloca = llvm::dyn_cast<llvm::AllocaInst>(addr)) {
                    targetType = alloca->getAllocatedType();
                } else if (auto gep = llvm::dyn_cast<llvm::GetElementPtrInst>(addr)) {
                    targetType = gep->getResultElementType();
                }

                // Derefプロジェクションがある場合、MIRの型情報から要素型を取得
                bool hasDeref = false;
                hir::TypePtr derefTargetType = nullptr;
                for (const auto& proj : assign.place.projections) {
                    if (proj.kind == mir::ProjectionKind::Deref) {
                        hasDeref = true;
                        break;
                    }
                }

                if (hasDeref && currentMIRFunction &&
                    assign.place.local < currentMIRFunction->locals.size()) {
                    auto& local = currentMIRFunction->locals[assign.place.local];
                    // プロジェクションチェーンを辿って最終的なターゲット型を取得
                    hir::TypePtr currentType = local.type;
                    for (const auto& proj : assign.place.projections) {
                        if (!currentType)
                            break;
                        if (proj.kind == mir::ProjectionKind::Deref) {
                            if (currentType->kind == hir::TypeKind::Pointer &&
                                currentType->element_type) {
                                currentType = currentType->element_type;
                            }
                        } else if (proj.kind == mir::ProjectionKind::Field) {
                            if (currentType->kind == hir::TypeKind::Struct) {
                                // ジェネリック構造体の場合、型引数を考慮した名前を生成
                                std::string structLookupName = currentType->name;
                                if (!currentType->type_args.empty()) {
                                    for (const auto& typeArg : currentType->type_args) {
                                        if (typeArg) {
                                            structLookupName += "__";
                                            if (typeArg->kind == hir::TypeKind::Struct) {
                                                structLookupName += typeArg->name;
                                            } else {
                                                switch (typeArg->kind) {
                                                    case hir::TypeKind::Int:
                                                        structLookupName += "int";
                                                        break;
                                                    case hir::TypeKind::UInt:
                                                        structLookupName += "uint";
                                                        break;
                                                    case hir::TypeKind::Long:
                                                        structLookupName += "long";
                                                        break;
                                                    case hir::TypeKind::ULong:
                                                        structLookupName += "ulong";
                                                        break;
                                                    case hir::TypeKind::Float:
                                                        structLookupName += "float";
                                                        break;
                                                    case hir::TypeKind::Double:
                                                        structLookupName += "double";
                                                        break;
                                                    case hir::TypeKind::Bool:
                                                        structLookupName += "bool";
                                                        break;
                                                    case hir::TypeKind::Char:
                                                        structLookupName += "char";
                                                        break;
                                                    case hir::TypeKind::String:
                                                        structLookupName += "string";
                                                        break;
                                                    default:
                                                        if (!typeArg->name.empty()) {
                                                            structLookupName += typeArg->name;
                                                        }
                                                        break;
                                                }
                                            }
                                        }
                                    }
                                }
                                auto structIt = structDefs.find(structLookupName);
                                if (structIt != structDefs.end() &&
                                    proj.field_id < structIt->second->fields.size()) {
                                    currentType = structIt->second->fields[proj.field_id].type;
                                }
                            }
                        }
                    }
                    // 最終的な型（フィールドの型など）を使用
                    if (currentType) {
                        targetType = convertType(currentType);
                    }
                }

                auto sourceType = rvalue->getType();

                if (targetType) {
                    // sourceがポインタで、targetが構造体の場合（構造体のコピー）
                    // 重要: rvalueがalloca（スタック上のアドレス）の場合のみloadする
                    // rvalueがポインタ値（nullポインタを含む）の場合はloadしてはいけない
                    bool isRvalueAlloca = llvm::isa<llvm::AllocaInst>(rvalue);
                    if (sourceType->isPointerTy() && targetType->isStructTy() && isRvalueAlloca) {
                        // C14: しきい値超の集約はload/storeの第一級集約コピーにせずmemcpyで転写する
                        // （第一級集約コピーはO2のSROA/instcombineが全要素をSSA展開し、
                        // int[16384]フィールドで24秒/6.4GBに達する二次爆発の原因だった）
                        const auto& dataLayout = module->getDataLayout();
                        const uint64_t copySize = dataLayout.getTypeAllocSize(targetType);
                        constexpr uint64_t kAggregateMemcpyThreshold = 128;
                        if (copySize >= kAggregateMemcpyThreshold && addr != rvalue) {
                            builder->CreateMemCpy(addr, llvm::MaybeAlign(), rvalue,
                                                  llvm::MaybeAlign(), copySize);
                            return;
                        }
                        // ポインタからロードして構造体値を取得
                        rvalue = builder->CreateLoad(targetType, rvalue, "struct_load");
                        sourceType = rvalue->getType();
                    }
                    // sourceがポインタで、targetがプリミティブ整数型の場合
                    // (プリミティブ型implメソッドのselfコピー: i8* -> i32)
                    else if (sourceType->isPointerTy() && targetType->isIntegerTy() &&
                             targetType->getIntegerBitWidth() >= 8) {
                        // i8*をtargetType*にキャストしてload
                        auto targetPtrType = llvm::PointerType::get(targetType, 0);
                        auto castedPtr = builder->CreateBitCast(rvalue, targetPtrType, "prim_cast");
                        rvalue = builder->CreateLoad(targetType, castedPtr, "prim_load");
                        sourceType = rvalue->getType();
                    }
                    // sourceがポインタで、targetが浮動小数点型の場合
                    else if (sourceType->isPointerTy() && targetType->isFloatingPointTy()) {
                        auto targetPtrType = llvm::PointerType::get(targetType, 0);
                        auto castedPtr =
                            builder->CreateBitCast(rvalue, targetPtrType, "float_cast");
                        rvalue = builder->CreateLoad(targetType, castedPtr, "float_load");
                        sourceType = rvalue->getType();
                    }

                    // i1からi8への変換が必要な場合（bool値の格納）
                    if (sourceType->isIntegerTy(1) && targetType->isIntegerTy(8)) {
                        rvalue = builder->CreateZExt(rvalue, ctx.getI8Type(), "bool_ext");
                    }
                    // i1からの拡張は常にゼロ拡張
                    else if (sourceType->isIntegerTy(1) && targetType->isIntegerTy()) {
                        rvalue = builder->CreateZExt(rvalue, targetType, "bool_zext");
                    }
                    // 整数型間の変換
                    else if (sourceType->isIntegerTy() && targetType->isIntegerTy()) {
                        auto sourceBits = sourceType->getIntegerBitWidth();
                        auto targetBits = targetType->getIntegerBitWidth();
                        if (sourceBits > targetBits) {
                            // 縮小変換 (例: i32 -> i8, i32 -> i16)
                            rvalue = builder->CreateTrunc(rvalue, targetType, "trunc");
                        } else if (sourceBits < targetBits) {
                            // 拡大変換 (例: i8 -> i32)
                            rvalue = builder->CreateSExt(rvalue, targetType, "sext");
                        }
                    }
                    // 浮動小数点型間の変換
                    else if (sourceType->isFloatingPointTy() && targetType->isFloatingPointTy()) {
                        if (sourceType->isDoubleTy() && targetType->isFloatTy()) {
                            // double -> float
                            rvalue = builder->CreateFPTrunc(rvalue, targetType, "fptrunc");
                        } else if (sourceType->isFloatTy() && targetType->isDoubleTy()) {
                            // float -> double
                            rvalue = builder->CreateFPExt(rvalue, targetType, "fpext");
                        }
                    }
                    // LLVM 14+: opaque pointersではポインタ間のBitCastは不要すべてのポインタは単に ptr 型
                    else if (sourceType->isPointerTy() && targetType->isPointerTy()) {
                        // opaque pointersでは何もしない
                        // rvalueはそのまま使用可能
                    }
                }

                // LLVM 14+対応: ポインタ型の場合は、すべてopaque pointerとして扱う
                // 型検証を追加して安全にstore
                if (addr && rvalue) {
                    // Deref時: アドレスをターゲット型のポインタにbitcast
                    // LLVM 14 typed pointers modeでは、store先のポインタ型とrvalueの型が一致する必要がある
                    if (hasDeref && targetType && !targetType->isPointerTy()) {
                        auto targetPtrType = llvm::PointerType::get(targetType, 0);
                        if (addr->getType() != targetPtrType) {
                            addr = builder->CreateBitCast(addr, targetPtrType, "typed_ptr");
                        }
                    }
                    // LLVM 14+: opaque pointerでは型情報をMIRから取得する必要がある
                    // 正しい型でストアするため、必要に応じてキャストを行う

                    // MIRの型情報からstore先の型を取得
                    hir::TypePtr targetType = nullptr;

                    // Placeから型情報を取得
                    if (assign.place.local < currentMIRFunction->locals.size()) {
                        auto& local = currentMIRFunction->locals[assign.place.local];
                        targetType = local.type;

                        // プロジェクションがある場合、最終的な型を取得
                        for (const auto& proj : assign.place.projections) {
                            if (!targetType)
                                break;

                            switch (proj.kind) {
                                case mir::ProjectionKind::Field:
                                    if (targetType->kind == hir::TypeKind::Struct) {
                                        // ジェネリック構造体の場合、型引数を考慮した名前を生成
                                        std::string structLookupName = targetType->name;
                                        if (!targetType->type_args.empty()) {
                                            for (const auto& typeArg : targetType->type_args) {
                                                if (typeArg) {
                                                    structLookupName += "__";
                                                    if (typeArg->kind == hir::TypeKind::Struct) {
                                                        structLookupName += typeArg->name;
                                                    } else {
                                                        switch (typeArg->kind) {
                                                            case hir::TypeKind::Int:
                                                                structLookupName += "int";
                                                                break;
                                                            case hir::TypeKind::UInt:
                                                                structLookupName += "uint";
                                                                break;
                                                            case hir::TypeKind::Long:
                                                                structLookupName += "long";
                                                                break;
                                                            case hir::TypeKind::ULong:
                                                                structLookupName += "ulong";
                                                                break;
                                                            case hir::TypeKind::Float:
                                                                structLookupName += "float";
                                                                break;
                                                            case hir::TypeKind::Double:
                                                                structLookupName += "double";
                                                                break;
                                                            case hir::TypeKind::Bool:
                                                                structLookupName += "bool";
                                                                break;
                                                            case hir::TypeKind::Char:
                                                                structLookupName += "char";
                                                                break;
                                                            case hir::TypeKind::String:
                                                                structLookupName += "string";
                                                                break;
                                                            default:
                                                                if (!typeArg->name.empty()) {
                                                                    structLookupName +=
                                                                        typeArg->name;
                                                                }
                                                                break;
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                        auto structDefIt = structDefs.find(structLookupName);
                                        if (structDefIt != structDefs.end() &&
                                            proj.field_id < structDefIt->second->fields.size()) {
                                            targetType =
                                                structDefIt->second->fields[proj.field_id].type;
                                        }
                                    }
                                    break;
                                case mir::ProjectionKind::Deref:
                                    targetType = targetType->element_type;
                                    break;
                                case mir::ProjectionKind::Index:
                                    targetType = targetType->element_type;
                                    break;
                            }
                        }
                    }

                    // ポインタ型同士の場合、型が異なればキャストを行う
                    if (targetType && targetType->kind == hir::TypeKind::Pointer &&
                        rvalue->getType()->isPointerTy()) {
                        // allocaされたポインタ変数にポインタ値を格納する場合
                        // addrは i8** 型、rvalueは具体的なポインタ型（例：i32*）
                        // rvalueを i8* にビットキャストしてから格納
                        if (rvalue->getType() != ctx.getPtrType()) {
                            rvalue = builder->CreateBitCast(rvalue, ctx.getPtrType(), "ptr_cast");
                        }
                    }

                    // Tagged Unionペイロードへの書き込みを検出
                    // field[1]への書き込みで、ターゲットがi8配列の場合はmemcpyを使用
                    bool isTaggedUnionPayload = false;
                    bool isFieldProj =
                        !assign.place.projections.empty() &&
                        assign.place.projections.back().kind == mir::ProjectionKind::Field &&
                        assign.place.projections.back().field_id == 1;

                    if (isFieldProj) {
                        // 親がTagged Union型か確認
                        if (currentMIRFunction &&
                            assign.place.local < currentMIRFunction->locals.size()) {
                            auto& local = currentMIRFunction->locals[assign.place.local];
                            if (local.type && local.type->name.find("__TaggedUnion_") == 0) {
                                isTaggedUnionPayload = true;
                            }
                        }
                    }

                    // rvalueが構造体値、またはallocaポインタで構造体を指す場合
                    bool isStructPayload = rvalue->getType()->isStructTy();
                    llvm::Type* structType = nullptr;
                    if (!isStructPayload && rvalue->getType()->isPointerTy()) {
                        if (auto* allocaInst = llvm::dyn_cast<llvm::AllocaInst>(rvalue)) {
                            if (allocaInst->getAllocatedType()->isStructTy()) {
                                isStructPayload = true;
                                structType = allocaInst->getAllocatedType();
                            }
                        }
                    }
                    if (!structType && isStructPayload) {
                        structType = rvalue->getType();
                    }

                    // Index projectionがあるか確認（配列要素への代入）
                    bool hasIndexProjection = false;
                    for (const auto& proj : assign.place.projections) {
                        if (proj.kind == mir::ProjectionKind::Index) {
                            hasIndexProjection = true;
                            break;
                        }
                    }

                    // 構造体代入でmemcpyを使用する条件:
                    // 1. Tagged Unionペイロードへの書き込み
                    bool needsStructCopy =
                        (isTaggedUnionPayload && isStructPayload && structType) ||
                        (hasIndexProjection && isStructPayload && structType);

                    if (needsStructCopy) {
                        // 構造体ペイロードの場合: memcpyを使用
                        llvm::Value* srcPtr = rvalue;
                        if (!rvalue->getType()->isPointerTy()) {
                            // 一時的なallocaを作成してstoreし、そのポインタを使用
                            auto tempAlloca =
                                builder->CreateAlloca(rvalue->getType(), nullptr, "tmp_payload");
                            builder->CreateStore(rvalue, tempAlloca);
                            srcPtr = tempAlloca;
                        }

                        // ペイロードサイズを取得
                        auto dataLayout = module->getDataLayout();
                        auto payloadSize = dataLayout.getTypeAllocSize(structType);

                        // memcpy: dest=addr, src=srcPtr, size=payloadSize
                        builder->CreateMemCpy(addr, llvm::MaybeAlign(), srcPtr, llvm::MaybeAlign(),
                                              payloadSize);
                    } else {
                        // 通常のStore操作を実行

                        // Tagged Unionペイロードへの書き込み:
                        // ペイロードフィールドはi8[N]配列。プリミティブ値(i32等)の場合、配列全体がストアされず上位バイトにゴミが残る。
                        // → ストア前にペイロード領域をゼロクリアして安全性を確保
                        if (isTaggedUnionPayload && addr) {
                            // ペイロードi8配列のサイズを取得
                            // addrはGEPでfield[1]を指すポインタ
                            uint64_t payloadSize = 8;  // デフォルト8バイト
                            if (currentMIRFunction &&
                                assign.place.local < currentMIRFunction->locals.size()) {
                                auto& local = currentMIRFunction->locals[assign.place.local];
                                if (local.type) {
                                    auto llvmStructType = convertType(local.type);
                                    if (llvmStructType->isStructTy()) {
                                        auto structTy =
                                            llvm::cast<llvm::StructType>(llvmStructType);
                                        if (structTy->getNumElements() >= 2) {
                                            auto payloadFieldType = structTy->getElementType(1);
                                            auto dataLayout = module->getDataLayout();
                                            payloadSize =
                                                dataLayout.getTypeAllocSize(payloadFieldType);
                                        }
                                    }
                                }
                            }
                            // ペイロード領域をゼロクリア
                            builder->CreateMemSet(addr, llvm::ConstantInt::get(ctx.getI8Type(), 0),
                                                  payloadSize, llvm::MaybeAlign());

                            // ペイロード値のビット幅がペイロード領域より小さい場合、ゼロ拡張して全バイトを定義済みにする
                            // 例: Result<ulong, long>::Ok(0) で i32(0) → i64(0) に拡張
                            // これによりLLVM最適化がmemset+storeをconstant phiに畳み込む際にundefinedバイトが生成されない
                            if (rvalue->getType()->isIntegerTy() && payloadSize > 0) {
                                unsigned valueBits = rvalue->getType()->getIntegerBitWidth();
                                unsigned payloadBits = static_cast<unsigned>(payloadSize * 8);
                                if (valueBits < payloadBits) {
                                    rvalue = builder->CreateZExt(
                                        rvalue,
                                        llvm::IntegerType::get(ctx.getContext(), payloadBits),
                                        "payload_zext");
                                }
                            }
                        }

                        // BUG修正(v0.14.2): asm入出力で参照される変数のみvolatileにする
                        auto* storeInst = builder->CreateStore(rvalue, addr);
                        if (isAllocated && asmReferencedLocals.count(assign.place.local) > 0) {
                            storeInst->setVolatile(true);
                        }
                    }
                }
            } else {
                // addr取得失敗: フォールバックとしてlocalsに直接格納（SSA形式）
                locals[assign.place.local] = rvalue;
            }
        } else {
            // SSA形式：allocaがない変数に直接値を格納
            locals[assign.place.local] = rvalue;
        }
    }
}

}  // namespace cm::codegen::llvm_backend
