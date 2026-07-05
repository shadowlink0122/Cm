/// @file statement.cpp
/// @brief MIR文（Assign/Asm等）→ LLVM IR 変換

#include "../../../common/debug/codegen.hpp"
#include "../monitoring/compilation_guard.hpp"
#include "mir_to_llvm.hpp"

#include <llvm/IR/InlineAsm.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace cm::codegen::llvm_backend {

void MIRToLLVM::convertStatement(const mir::MirStatement& stmt) {
    // 無限ループ検出用のカウンタ
    static thread_local std::unordered_map<const mir::MirStatement*, int> statementProcessCount;
    static thread_local const mir::MirFunction* lastFunction = nullptr;

    // 新しい関数に入った場合はカウンタをリセット
    if (currentMIRFunction != lastFunction) {
        statementProcessCount.clear();
        lastFunction = currentMIRFunction;
    }

    // 同じステートメントが過度に処理されている場合はエラー
    auto& count = statementProcessCount[&stmt];
    count++;
    if (count > 100) {
        // std::cerr << "[MIR2LLVM] ERROR: Infinite loop detected! Statement at address " <<
        // &stmt
        // << " processed " << count << " times\n";
        if (currentMIRFunction) {
            // std::cerr << "[MIR2LLVM] Function: " << currentMIRFunction->name << "\n";
        }
        throw std::runtime_error("Infinite loop detected in convertStatement");
    }

    if (cm::debug::g_debug_mode && currentMIRFunction && currentMIRFunction->name == "main") {
        debug_msg("MIR",
                  "Processing statement kind: " + std::to_string(static_cast<int>(stmt.kind)));
    }
    switch (stmt.kind) {
        case mir::MirStatement::Assign: {
            auto& assign = std::get<mir::MirStatement::AssignData>(stmt.data);

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
                        if (currentMIRFunction &&
                            srcPlace.local < currentMIRFunction->locals.size()) {
                            auto& srcLocal = currentMIRFunction->locals[srcPlace.local];

                            if (srcLocal.type && srcLocal.type->name.find("__TaggedUnion_") == 0) {
                                isSrcTaggedUnionPayload = true;
                            }
                        }
                    }

                    // ターゲットが構造体型か確認
                    bool isTargetStruct = false;
                    hir::TypePtr targetType = nullptr;
                    if (currentMIRFunction &&
                        assign.place.local < currentMIRFunction->locals.size()) {
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
                                break;
                            }
                        } else if (targetType) {
                            // 非構造体ペイロード（string/ptr/int等）
                            // ペイロードバイト配列からターゲット型でロード
                            auto srcAddr = convertPlaceToAddress(srcPlace);
                            if (srcAddr) {
                                auto llvmTargetType = convertType(targetType);
                                auto loadVal =
                                    builder->CreateLoad(llvmTargetType, srcAddr, "payload_load");

                                auto destAddr = locals[assign.place.local];
                                if (destAddr && allocatedLocals.count(assign.place.local) > 0) {
                                    // allocaモード: load→store
                                    builder->CreateStore(loadVal, destAddr);
                                } else {
                                    // SSAモード（string等allocaなし）: 直接値を設定
                                    locals[assign.place.local] = loadVal;
                                }
                                break;
                            }
                        }
                    }
                }
            }

            auto rvalue = convertRvalue(*assign.rvalue);

            if (rvalue) {
                // 関数参照の特別処理
                // bool isFunctionValue = false;
                if (llvm::isa<llvm::Function>(rvalue)) {
                    // Function*の場合、直接localsに格納（allocaせずにSSA形式で扱う）
                    locals[assign.place.local] = rvalue;
                    // 確認: 実際に格納されたか
                    if (cm::debug::g_debug_mode && currentMIRFunction &&
                        currentMIRFunction->name == "main") {
                        auto func = llvm::cast<llvm::Function>(rvalue);
                        debug_msg("MIR", "Stored function '" + func->getName().str() +
                                             "' to local " + std::to_string(assign.place.local) +
                                             " (locals.size=" + std::to_string(locals.size()) +
                                             ")");
                        // local 2の状態を確認
                        if (locals.size() > 2 && locals[2]) {
                            debug_msg("MIR", "Local 2 is now: " +
                                                 std::string(llvm::isa<llvm::Function>(locals[2])
                                                                 ? "Function"
                                                                 : "Other"));
                        }
                    }
                    break;
                }

                // projectionsがある場合（構造体フィールドなど）は常にstoreが必要
                bool hasProjections = !assign.place.projections.empty();

                // allocaされた変数かどうかをチェック
                bool isAllocated = allocatedLocals.count(assign.place.local) > 0;

                // 関数ポインタのSSA形式での代入も処理
                if (!hasProjections && !isAllocated) {
                    // SSA形式の変数への代入（関数ポインタ等）
                    locals[assign.place.local] = rvalue;
                    break;
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
                                                                    structLookupName +=
                                                                        typeArg->name;
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
                                            currentType =
                                                structIt->second->fields[proj.field_id].type;
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
                            if (sourceType->isPointerTy() && targetType->isStructTy() &&
                                isRvalueAlloca) {
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
                                auto castedPtr =
                                    builder->CreateBitCast(rvalue, targetPtrType, "prim_cast");
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
                            else if (sourceType->isFloatingPointTy() &&
                                     targetType->isFloatingPointTy()) {
                                if (sourceType->isDoubleTy() && targetType->isFloatTy()) {
                                    // double -> float
                                    rvalue = builder->CreateFPTrunc(rvalue, targetType, "fptrunc");
                                } else if (sourceType->isFloatTy() && targetType->isDoubleTy()) {
                                    // float -> double
                                    rvalue = builder->CreateFPExt(rvalue, targetType, "fpext");
                                }
                            }
                            // LLVM 14+: opaque pointersではポインタ間のBitCastは不要
                            // すべてのポインタは単に ptr 型
                            else if (sourceType->isPointerTy() && targetType->isPointerTy()) {
                                // opaque pointersでは何もしない
                                // rvalueはそのまま使用可能
                            }
                        }

                        // LLVM 14+対応: ポインタ型の場合は、すべてopaque pointerとして扱う
                        // 型検証を追加して安全にstore
                        if (addr && rvalue) {
                            // Deref時: アドレスをターゲット型のポインタにbitcast
                            // LLVM 14 typed pointers
                            // modeでは、store先のポインタ型とrvalueの型が一致する必要がある
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
                                                    for (const auto& typeArg :
                                                         targetType->type_args) {
                                                        if (typeArg) {
                                                            structLookupName += "__";
                                                            if (typeArg->kind ==
                                                                hir::TypeKind::Struct) {
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
                                                                        structLookupName +=
                                                                            "double";
                                                                        break;
                                                                    case hir::TypeKind::Bool:
                                                                        structLookupName += "bool";
                                                                        break;
                                                                    case hir::TypeKind::Char:
                                                                        structLookupName += "char";
                                                                        break;
                                                                    case hir::TypeKind::String:
                                                                        structLookupName +=
                                                                            "string";
                                                                        break;
                                                                    default:
                                                                        if (!typeArg->name
                                                                                 .empty()) {
                                                                            structLookupName +=
                                                                                typeArg->name;
                                                                        }
                                                                        break;
                                                                }
                                                            }
                                                        }
                                                    }
                                                }
                                                auto structDefIt =
                                                    structDefs.find(structLookupName);
                                                if (structDefIt != structDefs.end() &&
                                                    proj.field_id <
                                                        structDefIt->second->fields.size()) {
                                                    targetType =
                                                        structDefIt->second->fields[proj.field_id]
                                                            .type;
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
                                    rvalue = builder->CreateBitCast(rvalue, ctx.getPtrType(),
                                                                    "ptr_cast");
                                }
                            }

                            // Tagged Unionペイロードへの書き込みを検出
                            // field[1]への書き込みで、ターゲットがi8配列の場合はmemcpyを使用
                            bool isTaggedUnionPayload = false;
                            bool isFieldProj = !assign.place.projections.empty() &&
                                               assign.place.projections.back().kind ==
                                                   mir::ProjectionKind::Field &&
                                               assign.place.projections.back().field_id == 1;

                            if (isFieldProj) {
                                // 親がTagged Union型か確認
                                if (currentMIRFunction &&
                                    assign.place.local < currentMIRFunction->locals.size()) {
                                    auto& local = currentMIRFunction->locals[assign.place.local];
                                    if (local.type &&
                                        local.type->name.find("__TaggedUnion_") == 0) {
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
                                    auto tempAlloca = builder->CreateAlloca(rvalue->getType(),
                                                                            nullptr, "tmp_payload");
                                    builder->CreateStore(rvalue, tempAlloca);
                                    srcPtr = tempAlloca;
                                }

                                // ペイロードサイズを取得
                                auto dataLayout = module->getDataLayout();
                                auto payloadSize = dataLayout.getTypeAllocSize(structType);

                                // memcpy: dest=addr, src=srcPtr, size=payloadSize
                                builder->CreateMemCpy(addr, llvm::MaybeAlign(), srcPtr,
                                                      llvm::MaybeAlign(), payloadSize);
                            } else {
                                // 通常のStore操作を実行

                                // Tagged Unionペイロードへの書き込み:
                                // ペイロードフィールドはi8[N]配列。プリミティブ値(i32等)の場合、
                                // 配列全体がストアされず上位バイトにゴミが残る。
                                // → ストア前にペイロード領域をゼロクリアして安全性を確保
                                if (isTaggedUnionPayload && addr) {
                                    // ペイロードi8配列のサイズを取得
                                    // addrはGEPでfield[1]を指すポインタ
                                    uint64_t payloadSize = 8;  // デフォルト8バイト
                                    if (currentMIRFunction &&
                                        assign.place.local < currentMIRFunction->locals.size()) {
                                        auto& local =
                                            currentMIRFunction->locals[assign.place.local];
                                        if (local.type) {
                                            auto llvmStructType = convertType(local.type);
                                            if (llvmStructType->isStructTy()) {
                                                auto structTy =
                                                    llvm::cast<llvm::StructType>(llvmStructType);
                                                if (structTy->getNumElements() >= 2) {
                                                    auto payloadFieldType =
                                                        structTy->getElementType(1);
                                                    auto dataLayout = module->getDataLayout();
                                                    payloadSize = dataLayout.getTypeAllocSize(
                                                        payloadFieldType);
                                                }
                                            }
                                        }
                                    }
                                    // ペイロード領域をゼロクリア
                                    builder->CreateMemSet(
                                        addr, llvm::ConstantInt::get(ctx.getI8Type(), 0),
                                        payloadSize, llvm::MaybeAlign());

                                    // ペイロード値のビット幅がペイロード領域より小さい場合、
                                    // ゼロ拡張して全バイトを定義済みにする
                                    // 例: Result<ulong, long>::Ok(0) で i32(0) → i64(0) に拡張
                                    // これによりLLVM最適化がmemset+storeをconstant phiに
                                    // 畳み込む際にundefinedバイトが生成されない
                                    if (rvalue->getType()->isIntegerTy() && payloadSize > 0) {
                                        unsigned valueBits =
                                            rvalue->getType()->getIntegerBitWidth();
                                        unsigned payloadBits =
                                            static_cast<unsigned>(payloadSize * 8);
                                        if (valueBits < payloadBits) {
                                            rvalue = builder->CreateZExt(
                                                rvalue,
                                                llvm::IntegerType::get(ctx.getContext(),
                                                                       payloadBits),
                                                "payload_zext");
                                        }
                                    }
                                }

                                // BUG修正(v0.14.2): asm入出力で参照される変数のみvolatileにする
                                auto* storeInst = builder->CreateStore(rvalue, addr);
                                if (isAllocated &&
                                    asmReferencedLocals.count(assign.place.local) > 0) {
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
            break;
        }
        case mir::MirStatement::Asm: {
            // インラインアセンブリ
            auto& asmData = std::get<mir::MirStatement::AsmData>(stmt.data);
            debug_msg("llvm_asm", "Emitting inline asm: " + asmData.code +
                                      " operands=" + std::to_string(asmData.operands.size()));

            // ASMコードをそのまま使用（レジスタの自動リマップは行わない）
            // 理由: %rdi/%rsi等はABI引数参照だけでなく汎用スクラッチレジスタとしても
            // 使用されるため、一律リマップは意図しない動作を引き起こす
            // ABI引数を参照する場合は ${r:varname} 構文を使用すること
            std::string asmCodeRemapped = asmData.code;

            if (asmData.operands.empty()) {
                // オペランドなし: シンプルなasm
                auto* asmFuncTy = llvm::FunctionType::get(ctx.getVoidType(), false);
                // Bug#7修正: 全ASMにhasSideEffects=trueとフラグクロバーを設定
                // must { __asm__("hlt"); } がループから脱出する問題を防止
                // LLVMがASM周辺の制御フローを不正に最適化しないようにする
                std::string constraints = "~{memory},~{dirflag},~{fpsr},~{flags}";
                auto* inlineAsm = llvm::InlineAsm::get(asmFuncTy, asmCodeRemapped, constraints,
                                                       true  // hasSideEffects: 常にtrue
                );
                // Bug#7修正: must { __asm__() } の場合のみコンパイラバリアを挿入
                // LLVMの制御フロー最適化（hlt後のコードを到達不能と判断）を阻止
                // ハードウェアfence (mfence) ではなくコンパイラバリアを使用
                // （UEFIベアメタル環境ではmfenceがGPFを引き起こす可能性があるため）
                if (asmData.is_must) {
                    auto* barrierTy = llvm::FunctionType::get(ctx.getVoidType(), false);
                    auto* barrier = llvm::InlineAsm::get(
                        barrierTy, "", "~{memory},~{dirflag},~{fpsr},~{flags}", true);
                    builder->CreateCall(barrierTy, barrier);
                }
                builder->CreateCall(asmFuncTy, inlineAsm);
                if (asmData.is_must) {
                    auto* barrierTy = llvm::FunctionType::get(ctx.getVoidType(), false);
                    auto* barrier = llvm::InlineAsm::get(
                        barrierTy, "", "~{memory},~{dirflag},~{fpsr},~{flags}", true);
                    builder->CreateCall(barrierTy, barrier);
                }
            } else {
                // オペランド付き: 制約文字列とオペランドを生成
                // 出力/入出力オペランドを分類
                // LLVMの制約形式: 出力制約,tied入力,純粋入力の順
                std::vector<llvm::Type*> inputTypes;
                std::vector<llvm::Value*> inputValues;
                std::vector<llvm::Value*> outputPtrs;      // 出力先ポインタ（=r用）
                std::vector<llvm::Type*> outputTypes;      // 出力オペランドの型（=r用）
                std::vector<mir::LocalId> outputLocalIds;  // 出力ローカルIDも記録
                std::string constraints;
                int outputCount = 0;  // =r の数
                int inputCount = 0;  // 入力オペランドの数（将来の拡張/デバッグ用）
                (void)inputCount;  // 現時点では読み取り不要だが、インクリメントは維持

                // AArch64ターゲット判定とオペランド型記録
                // LLVMのAArch64バックエンドがi32に対してxレジスタを割り当てる場合があるため、
                // :w修飾子を付与して32bitレジスタ(w)を強制する
                std::string asmTriple = module->getTargetTriple();
                bool isAArch64Target = (asmTriple.find("aarch64") != std::string::npos ||
                                        asmTriple.find("arm64") != std::string::npos);
                std::map<size_t, llvm::Type*> operandElemTypes;  // オペランドindex→LLVM型
                std::map<size_t, bool> operandIsMemory;          // メモリ制約かどうか

                // =m制約用: メモリ出力はポインタを入力として渡す
                std::vector<llvm::Value*> memOutputPtrs;  // =m用のポインタ（入力として渡す）
                std::vector<llvm::Type*> memOutputTypes;  // =m用の要素型（elementtype属性用）
                std::vector<mir::LocalId> memOutputLocalIds;
                std::vector<std::string> memOutputConstraints;

                // m入力制約用: ポインタを渡し、elementtype属性が必要
                std::vector<size_t> memInputIndices;  // pureInputValues内でのm制約インデックス
                std::vector<llvm::Type*> memInputTypes;  // m入力の要素型（elementtype属性用）
                // +m tied入力用: 同様にelementtype属性が必要
                std::vector<size_t> memTiedInputIndices;  // tiedInputValues内での+m制約インデックス
                std::vector<llvm::Type*> memTiedInputTypes;  // +m入力の要素型

                // 2パス方式で入力値を収集
                // 第1パス: +rのtied入力を収集（出力順）
                // 第2パス: 純粋な入力(r,m)を収集
                std::vector<llvm::Value*> tiedInputValues;
                std::vector<llvm::Type*> tiedInputTypes;
                std::vector<llvm::Value*> pureInputValues;
                std::vector<llvm::Type*> pureInputTypes;

                for (size_t i = 0; i < asmData.operands.size(); ++i) {
                    auto& operand = asmData.operands[i];

                    // 定数オペランドの場合（macro/const）
                    if (operand.is_constant) {
                        // i,n制約: 定数値をConstantIntとして生成
                        llvm::Value* constVal =
                            llvm::ConstantInt::get(ctx.getI64Type(), operand.const_value);
                        pureInputValues.push_back(constVal);
                        pureInputTypes.push_back(constVal->getType());
                        debug_msg("llvm_asm", "[ASM] const operand: " + operand.constraint + " = " +
                                                  std::to_string(operand.const_value));
                        continue;
                    }

                    // ローカル変数を取得（localsはstd::map）
                    llvm::Value* localPtr = nullptr;
                    if (locals.count(operand.local_id) > 0 && locals[operand.local_id]) {
                        localPtr = locals[operand.local_id];
                    }

                    if (!localPtr)
                        continue;

                    // ローカル変数の実際の型を取得
                    llvm::Type* elemType = ctx.getI32Type();  // デフォルトint型
                    if (auto* allocaInst = llvm::dyn_cast<llvm::AllocaInst>(localPtr)) {
                        // allocaから実際の型を取得
                        elemType = allocaInst->getAllocatedType();
                    } else if (currentMIRFunction &&
                               operand.local_id < currentMIRFunction->locals.size()) {
                        // SSA値（ConstantInt等）の場合、MIR型情報から型を推定
                        auto& localInfo = currentMIRFunction->locals[operand.local_id];
                        if (localInfo.type) {
                            auto llvmType = convertType(localInfo.type);
                            if (llvmType && !llvmType->isVoidTy()) {
                                elemType = llvmType;
                            }
                        }
                    }
                    // AArch64用: オペランドの型とメモリ制約を記録
                    operandElemTypes[i] = elemType;
                    operandIsMemory[i] = (operand.constraint.find('m') != std::string::npos);
                    bool hasOutput = operand.constraint[0] == '=' || operand.constraint[0] == '+';
                    bool isPureInput = operand.constraint[0] != '=' && operand.constraint[0] != '+';
                    bool isTiedInput = operand.constraint[0] == '+';

                    // ローカル変数から値をロード
                    auto loadValue = [&]() -> llvm::Value* {
                        if (llvm::isa<llvm::PointerType>(localPtr->getType()) ||
                            llvm::isa<llvm::AllocaInst>(localPtr)) {
                            auto* loadInst = builder->CreateLoad(elemType, localPtr);
                            if (auto* li = llvm::dyn_cast<llvm::LoadInst>(loadInst)) {
                                li->setVolatile(true);
                            }
                            return loadInst;
                        }
                        return localPtr;
                    };

                    if (isTiedInput) {
                        // +r, +m: tied入力 - 先に入力値を収集
                        bool isMemoryTied = (operand.constraint.find('m') != std::string::npos);
                        if (isMemoryTied) {
                            // +m: ポインタを渡す（メモリ出力と同様）
                            // tiedInputValues内でのインデックスを記録（後でelementtype追加用）
                            memTiedInputIndices.push_back(tiedInputValues.size());
                            memTiedInputTypes.push_back(elemType);  // 要素型を記録
                            tiedInputValues.push_back(localPtr);
                            tiedInputTypes.push_back(localPtr->getType());
                        } else {
                            // +r: 値をロードして渡す
                            auto* val = loadValue();
                            tiedInputValues.push_back(val);
                            tiedInputTypes.push_back(val->getType());
                        }
                    } else if (isPureInput) {
                        // 制約の種類によって値の渡し方を変える
                        std::string constraintType = operand.constraint;
                        bool isMemoryConstraint = (constraintType.find('m') != std::string::npos);
                        bool isImmediateConstraint =
                            (constraintType.find('i') != std::string::npos ||
                             constraintType.find('n') != std::string::npos);
                        bool isGeneralConstraint = (constraintType.find('g') != std::string::npos);

                        if (isMemoryConstraint) {
                            // m制約: ポインタ（アドレス）を渡す
                            // pureInputValues内でのインデックスを記録（後でelementtype追加用）
                            memInputIndices.push_back(pureInputValues.size());
                            memInputTypes.push_back(elemType);  // 要素型を記録
                            pureInputValues.push_back(localPtr);
                            pureInputTypes.push_back(localPtr->getType());
                        } else if (isImmediateConstraint) {
                            // i,n制約: 即値（値をロードして渡す、LLVMが定数最適化）
                            auto* val = loadValue();
                            pureInputValues.push_back(val);
                            pureInputTypes.push_back(val->getType());
                        } else if (isGeneralConstraint) {
                            // g制約: 汎用オペランド（レジスタ、メモリ、即値のいずれか）
                            // 値をロードして渡す（LLVMが最適な方法を選択）
                            auto* val = loadValue();
                            pureInputValues.push_back(val);
                            pureInputTypes.push_back(val->getType());
                        } else {
                            // r制約: 値をロードして渡す
                            auto* val = loadValue();
                            pureInputValues.push_back(val);
                            pureInputTypes.push_back(val->getType());
                        }
                    }

                    if (hasOutput) {
                        // 制約の種類をチェック: =m (メモリ出力) か =r (レジスタ出力) か
                        std::string constraintType = operand.constraint;
                        bool isMemOutput = (constraintType.find('m') != std::string::npos);

                        if (isMemOutput) {
                            // =m 制約: メモリ出力
                            // LLVMではメモリ出力はポインタを入力として渡す（void型）
                            // 出力オペランドの場合、ストア可能なポインタが必要
                            if (!llvm::isa<llvm::AllocaInst>(localPtr) &&
                                !llvm::isa<llvm::GlobalVariable>(localPtr) &&
                                !llvm::isa<llvm::GetElementPtrInst>(localPtr)) {
                                auto* alloca =
                                    builder->CreateAlloca(elemType, nullptr, "asm_mem_out");
                                localPtr = alloca;
                                allocatedLocals.insert(operand.local_id);
                            }
                            // メモリ出力として記録（後で入力に追加）
                            memOutputPtrs.push_back(localPtr);
                            memOutputTypes.push_back(elemType);  // 要素型を記録
                            memOutputLocalIds.push_back(operand.local_id);
                            // 制約文字列を記録（=m → *m として変換）
                            if (operand.constraint[0] == '=') {
                                memOutputConstraints.push_back("=*m");
                            } else {  // +m
                                memOutputConstraints.push_back("+*m");
                            }
                        } else {
                            // =r 制約: レジスタ出力（従来通り）
                            if (!llvm::isa<llvm::AllocaInst>(localPtr) &&
                                !llvm::isa<llvm::GlobalVariable>(localPtr) &&
                                !llvm::isa<llvm::GetElementPtrInst>(localPtr)) {
                                auto* alloca =
                                    builder->CreateAlloca(elemType, nullptr, "asm_out_tmp");
                                if (isTiedInput && !tiedInputValues.empty()) {
                                    auto* initStore =
                                        builder->CreateStore(tiedInputValues.back(), alloca);
                                    initStore->setVolatile(true);
                                }
                                localPtr = alloca;
                                // localsマップを更新して、後続のcopy操作が
                                // allocaからvolatile loadで値を読み取れるようにする
                                locals[operand.local_id] = alloca;
                                allocatedLocals.insert(operand.local_id);
                            }
                            outputPtrs.push_back(localPtr);
                            outputTypes.push_back(elemType);  // 出力の型を記録
                            outputLocalIds.push_back(operand.local_id);
                            outputCount++;
                        }
                    }

                    if (!constraints.empty())
                        constraints += ",";
                    constraints += operand.constraint;
                }

                // 入力値をLLVM順序で結合: 純粋入力 → tied入力 → メモリ出力
                // 制約文字列の順序と一致させる（r,... → 0,1,... → *m,...）
                for (size_t i = 0; i < pureInputValues.size(); ++i) {
                    inputValues.push_back(pureInputValues[i]);
                    inputTypes.push_back(pureInputTypes[i]);
                    inputCount++;
                }
                for (size_t i = 0; i < tiedInputValues.size(); ++i) {
                    inputValues.push_back(tiedInputValues[i]);
                    inputTypes.push_back(tiedInputTypes[i]);
                    inputCount++;
                }
                // メモリ出力ポインタを入力として追加（=m, +m制約用）
                for (size_t i = 0; i < memOutputPtrs.size(); ++i) {
                    inputValues.push_back(memOutputPtrs[i]);
                    inputTypes.push_back(memOutputPtrs[i]->getType());
                    inputCount++;
                }

                // 制約文字列をLLVM形式に変換
                // LLVMの制約形式: 出力制約,入力制約の順
                // オペランド番号はこの順序に対応するため、再マッピングが必要
                // +rは「=r」（出力）と「0」（入力をtie）に分解する
                std::string outputConstraints;
                std::string inputConstraints;

                // オペランド番号の再マッピング表（元の番号→LLVM番号）
                std::map<size_t, size_t> operandRemap;
                size_t llvmOutputIdx = 0;  // 出力オペランドのLLVMインデックス
                size_t llvmInputIdx = 0;  // 入力オペランドを数える（出力の後に来る）

                // まず出力オペランドを処理（=r のみ、=m は除外）
                for (size_t i = 0; i < asmData.operands.size(); ++i) {
                    const auto& operand = asmData.operands[i];

                    if (operand.constraint[0] == '+' || operand.constraint[0] == '=') {
                        // メモリ出力(=m, +m)は出力制約に含めない（入力として処理）
                        bool isMemOutput = (operand.constraint.find('m') != std::string::npos);
                        if (isMemOutput) {
                            // メモリ出力はスキップ（後で入力として追加）
                            continue;
                        }

                        // =r または +r: 出力オペランド
                        operandRemap[i] = llvmOutputIdx;
                        llvmOutputIdx++;

                        if (operand.constraint[0] == '+') {
                            if (!outputConstraints.empty())
                                outputConstraints += ",";
                            outputConstraints += "=" + operand.constraint.substr(1);
                        } else {
                            if (!outputConstraints.empty())
                                outputConstraints += ",";
                            outputConstraints += operand.constraint;
                        }
                    }
                }

                // 次に入力オペランドを処理
                // inputValuesの構築順序と一致させるため:
                // 1. 純粋入力（r, m等）
                // 2. tied入力（+r）
                // 3. メモリ出力（=m, +m）

                // 1. 純粋入力を先に処理
                for (size_t i = 0; i < asmData.operands.size(); ++i) {
                    const auto& operand = asmData.operands[i];

                    // 純粋な入力（r, m等）のみ処理
                    if (operand.constraint[0] != '+' && operand.constraint[0] != '=') {
                        operandRemap[i] = llvmOutputIdx + llvmInputIdx;
                        llvmInputIdx++;
                        if (!inputConstraints.empty())
                            inputConstraints += ",";
                        // m制約は*m（間接メモリ）に変換
                        if (operand.constraint.find('m') != std::string::npos) {
                            inputConstraints += "*m";
                        } else {
                            inputConstraints += operand.constraint;
                        }
                    }
                }

                // 2. tied入力（+r）を処理
                for (size_t i = 0; i < asmData.operands.size(); ++i) {
                    const auto& operand = asmData.operands[i];

                    if (operand.constraint[0] == '+') {
                        // +r: 入力としてtied（出力番号を参照）
                        // ただし+mはスキップ（メモリ出力として別処理）
                        if (operand.constraint.find('m') == std::string::npos) {
                            if (!inputConstraints.empty())
                                inputConstraints += ",";
                            inputConstraints += std::to_string(operandRemap[i]);
                        }
                    }
                }

                // 3. メモリ出力の制約を入力制約に追加（=*m形式）
                // メモリ出力のオペランド番号も再マッピング
                size_t memOutputStartIdx = llvmOutputIdx + llvmInputIdx;
                for (size_t i = 0; i < asmData.operands.size(); ++i) {
                    const auto& operand = asmData.operands[i];
                    if ((operand.constraint[0] == '=' || operand.constraint[0] == '+') &&
                        operand.constraint.find('m') != std::string::npos) {
                        // メモリ出力のオペランド番号を設定
                        operandRemap[i] = memOutputStartIdx++;
                        if (!inputConstraints.empty())
                            inputConstraints += ",";
                        // =m → "*m" (indirect memory), +m も同様
                        inputConstraints += "*m";
                    }
                }

                // 最終制約: 出力,入力の順
                std::string llvmConstraints = outputConstraints;
                if (!inputConstraints.empty()) {
                    if (!llvmConstraints.empty()) {
                        llvmConstraints += ",";
                    }
                    llvmConstraints += inputConstraints;
                }

                // clobberリストを追加（~{memory}, ~{eax}等）
                // is_must=trueの場合はデフォルトで~{memory}を追加
                if (asmData.is_must && !llvmConstraints.empty()) {
                    // clobbers配列に~{memory}がなければ追加
                    bool hasMemoryClobber = false;
                    for (const auto& clob : asmData.clobbers) {
                        if (clob == "memory" || clob == "~{memory}") {
                            hasMemoryClobber = true;
                            break;
                        }
                    }
                    if (!hasMemoryClobber) {
                        llvmConstraints += ",~{memory}";
                    }
                }
                // 明示的なclobbersを追加
                for (const auto& clob : asmData.clobbers) {
                    if (!llvmConstraints.empty()) {
                        llvmConstraints += ",";
                    }
                    // ~{...}形式でなければ追加
                    if (clob.substr(0, 2) == "~{") {
                        llvmConstraints += clob;
                    } else {
                        llvmConstraints += "~{" + clob + "}";
                    }
                }

                // ハードコードレジスタの自動クロバー検出
                // ASMコード内の %reg パターンを検出し、入出力オペランドでないものを
                // 自動的にクロバーとして追加する
                // （LLVMのインライン展開時にレジスタの値が不正に再利用されることを防止）
                {
                    // x86-64のレジスタ名一覧（LLVM形式）
                    // 64bit → LLVM名のマッピング
                    static const std::vector<std::pair<std::string, std::string>> regPatterns = {
                        // 64ビットレジスタ
                        {"%rax", "rax"},
                        {"%rbx", "rbx"},
                        {"%rcx", "rcx"},
                        {"%rdx", "rdx"},
                        {"%rsi", "rsi"},
                        {"%rdi", "rdi"},
                        {"%rbp", "rbp"},
                        {"%r8", "r8"},
                        {"%r9", "r9"},
                        {"%r10", "r10"},
                        {"%r11", "r11"},
                        {"%r12", "r12"},
                        {"%r13", "r13"},
                        {"%r14", "r14"},
                        {"%r15", "r15"},
                        // 32ビットレジスタ（対応する64ビットをクロバー）
                        {"%eax", "rax"},
                        {"%ebx", "rbx"},
                        {"%ecx", "rcx"},
                        {"%edx", "rdx"},
                        {"%esi", "rsi"},
                        {"%edi", "rdi"},
                        // 16ビットレジスタ
                        {"%ax", "rax"},
                        {"%bx", "rbx"},
                        {"%cx", "rcx"},
                        {"%dx", "rdx"},
                        // 8ビットレジスタ
                        {"%al", "rax"},
                        {"%bl", "rbx"},
                        {"%cl", "rcx"},
                        {"%dl", "rdx"},
                        {"%ah", "rax"},
                        {"%bh", "rbx"},
                        {"%ch", "rcx"},
                        {"%dh", "rdx"},
                    };

                    std::set<std::string> detectedClobbers;
                    std::string asmCode = asmCodeRemapped;

                    for (const auto& [pattern, llvmName] : regPatterns) {
                        size_t searchPos = 0;
                        while ((searchPos = asmCode.find(pattern, searchPos)) !=
                               std::string::npos) {
                            // レジスタ名の直後が英数字やアンダースコアでないことを確認
                            // （%r12の検出で%r12bを誤検出しないように）
                            size_t afterPos = searchPos + pattern.size();
                            bool isFullMatch = true;
                            if (afterPos < asmCode.size()) {
                                char nextChar = asmCode[afterPos];
                                // %r8, %r9等の短いパターンと%r8d等の区別
                                if (std::isalnum(nextChar) || nextChar == '_') {
                                    // ただし%eax等→%eaxl等は普通ないので、
                                    // 32bit以上のパターンは次の文字がレジスタ拡張子でなければOK
                                    if (pattern.size() >= 4) {
                                        // %rax, %eax等: 後ろの文字は通常ない
                                        isFullMatch = false;
                                    } else {
                                        // %r8, %al等: %r8d のような拡張をチェック
                                        isFullMatch = false;
                                    }
                                }
                            }
                            if (isFullMatch) {
                                detectedClobbers.insert(llvmName);
                            }
                            searchPos += pattern.size();
                        }
                    }

                    // オペランドとして使用されているレジスタは除外しない
                    // （LLVMは入出力オペランドと重複するクロバーを自動的に無視する）
                    // 既に追加済みのクロバーとの重複チェック
                    for (const auto& reg : detectedClobbers) {
                        std::string clobStr = "~{" + reg + "}";
                        if (llvmConstraints.find(clobStr) == std::string::npos) {
                            if (!llvmConstraints.empty()) {
                                llvmConstraints += ",";
                            }
                            llvmConstraints += clobStr;
                        }
                    }
                }

                // asmコード内のオペランド番号を更新
                // 2段階方式: まず一時プレースホルダーに置換、次に最終番号に置換
                // これにより$0→$1と$1→$0のような交差置換が正しく処理される
                std::string remappedCode = asmCodeRemapped;

                // 第1段階: $N を一時プレースホルダー __TMP_N__ に置換
                // ただし $$N（即値エスケープ）はスキップする
                for (int i = static_cast<int>(asmData.operands.size()) - 1; i >= 0; --i) {
                    std::string oldPattern = "$" + std::to_string(i);
                    std::string tempPattern = "__TMP_" + std::to_string(i) + "__";
                    size_t pos = 0;
                    while ((pos = remappedCode.find(oldPattern, pos)) != std::string::npos) {
                        // $$N（即値エスケープ）の場合はスキップ
                        if (pos > 0 && remappedCode[pos - 1] == '$') {
                            pos++;
                            continue;
                        }
                        size_t afterNum = pos + oldPattern.length();
                        if (afterNum < remappedCode.length() &&
                            std::isdigit(remappedCode[afterNum])) {
                            pos++;
                            continue;
                        }
                        remappedCode.replace(pos, oldPattern.length(), tempPattern);
                        pos += tempPattern.length();
                    }
                }

                // 第2段階: __TMP_N__ を最終的な$REMAP[N]に置換
                // AArch64ターゲットの場合、i32以下の型のレジスタオペランドに:w修飾子を付与
                // これによりLLVMがxレジスタではなくwレジスタ(32bit)を使用する
                for (size_t i = 0; i < asmData.operands.size(); ++i) {
                    std::string tempPattern = "__TMP_" + std::to_string(i) + "__";
                    std::string newPattern;
                    // AArch64 + i32以下 + 非メモリ制約の場合に :w 修飾子を付与
                    bool needsWModifier = false;
                    if (isAArch64Target && !operandIsMemory[i]) {
                        auto typeIt = operandElemTypes.find(i);
                        if (typeIt != operandElemTypes.end()) {
                            llvm::Type* opType = typeIt->second;
                            if (opType->isIntegerTy() && opType->getIntegerBitWidth() <= 32) {
                                needsWModifier = true;
                            }
                        }
                    }
                    if (needsWModifier) {
                        newPattern = "${" + std::to_string(operandRemap[i]) + ":w}";
                    } else {
                        newPattern = "$" + std::to_string(operandRemap[i]);
                    }
                    size_t pos = 0;
                    while ((pos = remappedCode.find(tempPattern, pos)) != std::string::npos) {
                        remappedCode.replace(pos, tempPattern.length(), newPattern);
                        pos += newPattern.length();
                    }
                }

                constraints = llvmConstraints;  // 変換後の制約を使用

                if (outputCount > 0) {
                    // 出力がある場合: 戻り値型を設定
                    llvm::Type* retType;
                    if (outputCount == 1) {
                        // 単一出力: 直接その型を返す
                        retType = !outputTypes.empty() ? outputTypes[0] : ctx.getI32Type();
                    } else {
                        // 複数出力: 構造体型を返す
                        retType = llvm::StructType::get(ctx.getContext(), outputTypes);
                    }

                    auto* asmFuncTy = llvm::FunctionType::get(retType, inputTypes, false);
                    // 出力オペランドがある場合は常にsideeffect=trueにして最適化を抑制
                    auto* inlineAsm = llvm::InlineAsm::get(asmFuncTy, remappedCode, constraints,
                                                           true /* hasSideEffects */);
                    auto* result = builder->CreateCall(asmFuncTy, inlineAsm, inputValues);

                    // 出力結果を各ローカル変数にstoreする
                    for (size_t i = 0; i < outputLocalIds.size() && i < outputPtrs.size(); ++i) {
                        auto local_id = outputLocalIds[i];
                        auto* outputPtr = outputPtrs[i];

                        if (!outputPtr)
                            continue;

                        llvm::Value* outputValue;
                        if (outputCount == 1) {
                            // 単一出力: 結果をそのまま使用
                            outputValue = result;
                        } else {
                            // 複数出力: 構造体からextractvalueで取り出す
                            outputValue =
                                builder->CreateExtractValue(result, {static_cast<unsigned>(i)});
                        }

                        // 出力ポインタがある場合はvolatile store
                        if (llvm::isa<llvm::PointerType>(outputPtr->getType())) {
                            auto* storeInst = builder->CreateStore(outputValue, outputPtr);
                            storeInst->setVolatile(true);
                            // allocatedLocalsに追加してvolatile loadを強制
                            allocatedLocals.insert(local_id);
                        } else {
                            // 直接SSA値として格納（fallback）
                            locals[local_id] = outputValue;
                        }
                    }

                    // m入力制約にelementtype属性を追加（*m間接メモリ用）
                    if (!memInputIndices.empty()) {
                        if (auto* callInst = llvm::dyn_cast<llvm::CallInst>(result)) {
                            size_t pureInputStartArgIdx = tiedInputValues.size();
                            for (size_t i = 0; i < memInputIndices.size(); ++i) {
                                size_t pureIdx = memInputIndices[i];
                                size_t argIdx = pureInputStartArgIdx + pureIdx;
                                // オペランドの実際の型を使用
                                llvm::Type* memElemType = (i < memInputTypes.size())
                                                              ? memInputTypes[i]
                                                              : ctx.getI32Type();
                                auto elemTypeAttr = llvm::Attribute::get(
                                    ctx.getContext(), llvm::Attribute::ElementType, memElemType);
                                callInst->addParamAttr(argIdx, elemTypeAttr);
                            }
                        }
                    }
                } else if (!inputValues.empty()) {
                    // 入力のみ: 戻り値なし
                    auto* asmFuncTy = llvm::FunctionType::get(ctx.getVoidType(), inputTypes, false);
                    auto* inlineAsm =
                        llvm::InlineAsm::get(asmFuncTy, remappedCode, constraints, asmData.is_must);
                    auto* callInst = builder->CreateCall(asmFuncTy, inlineAsm, inputValues);

                    // メモリ出力ポインタ（*m制約）にelementtype属性を追加
                    // LLVM 17ではindirect memory制約にはelementtype属性が必要
                    if (!memOutputPtrs.empty()) {
                        // メモリ出力のインデックス計算: tied入力 + pure入力 + memOutputIdx
                        size_t memOutputStartArgIdx =
                            tiedInputValues.size() + pureInputValues.size();
                        for (size_t i = 0; i < memOutputPtrs.size(); ++i) {
                            size_t argIdx = memOutputStartArgIdx + i;
                            // オペランドの実際の型を使用
                            llvm::Type* memElemType =
                                (i < memOutputTypes.size()) ? memOutputTypes[i] : ctx.getI32Type();
                            auto elemTypeAttr = llvm::Attribute::get(
                                ctx.getContext(), llvm::Attribute::ElementType, memElemType);
                            callInst->addParamAttr(argIdx, elemTypeAttr);
                        }

                        // メモリ出力のローカル変数をallocatedLocalsに追加
                        for (const auto& local_id : memOutputLocalIds) {
                            allocatedLocals.insert(local_id);
                        }
                    }

                    // m入力制約にもelementtype属性を追加（*m間接メモリ用）
                    if (!memInputIndices.empty()) {
                        // memInputIndicesはpureInputValues内でのインデックス
                        // 実際のCallInst引数インデックス: tied入力 + pureInputIndex
                        size_t pureInputStartArgIdx = tiedInputValues.size();
                        for (size_t i = 0; i < memInputIndices.size(); ++i) {
                            size_t pureIdx = memInputIndices[i];
                            size_t argIdx = pureInputStartArgIdx + pureIdx;
                            // オペランドの実際の型を使用
                            llvm::Type* memElemType =
                                (i < memInputTypes.size()) ? memInputTypes[i] : ctx.getI32Type();
                            auto elemTypeAttr = llvm::Attribute::get(
                                ctx.getContext(), llvm::Attribute::ElementType, memElemType);
                            callInst->addParamAttr(argIdx, elemTypeAttr);
                        }
                    }

                    // +m tied入力にもelementtype属性を追加
                    if (!memTiedInputIndices.empty()) {
                        // memTiedInputIndicesはtiedInputValues内でのインデックス
                        // 実際のCallInst引数インデックス: tiedInputIndex （先頭から）
                        for (size_t i = 0; i < memTiedInputIndices.size(); ++i) {
                            size_t tiedIdx = memTiedInputIndices[i];
                            // オペランドの実際の型を使用
                            llvm::Type* memElemType = (i < memTiedInputTypes.size())
                                                          ? memTiedInputTypes[i]
                                                          : ctx.getI32Type();
                            auto elemTypeAttr = llvm::Attribute::get(
                                ctx.getContext(), llvm::Attribute::ElementType, memElemType);
                            callInst->addParamAttr(tiedIdx, elemTypeAttr);
                        }
                    }
                }
            }
            break;
        }
        case mir::MirStatement::StorageLive:
        case mir::MirStatement::StorageDead:
        case mir::MirStatement::Nop:
            // これらは無視
            break;
    }

    // 関数終了のデバッグ
    count--;
    // std::cerr << "[MIR2LLVM]         convertStatement EXITING (depth=" << count
    // << ", kind=" << static_cast<int>(stmt.kind) << ")\n";
}

}  // namespace cm::codegen::llvm_backend
