/// @file rvalue.cpp
/// @brief MIR右辺値（BinaryOp/Cast/Aggregate等）→ LLVM IR 変換

#include "internal/base/debug/codegen.hpp"
#include "internal/codegen/llvm/monitoring/compilation_guard.hpp"
#include "internal/syntax/ast/typedef.hpp"
#include "mir_to_llvm.hpp"

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

namespace cm::codegen::llvm_backend {

llvm::Value* MIRToLLVM::convertRvalue(const mir::MirRvalue& rvalue) {
    switch (rvalue.kind) {
        case mir::MirRvalue::Use: {
            auto& useData = std::get<mir::MirRvalue::UseData>(rvalue.data);
            if (useData.operand) {
                return convertOperand(*useData.operand);
            }
            return nullptr;
        }
        case mir::MirRvalue::BinaryOp: {
            auto& binop = std::get<mir::MirRvalue::BinaryOpData>(rvalue.data);
            // std::cerr << "[MIR2LLVM]         Converting BinaryOp, op=" << static_cast<int>(binop.op)
            // << "\n";

            auto lhs = convertOperand(*binop.lhs);
            if (!lhs) {
                return nullptr;
            }

            auto rhs = convertOperand(*binop.rhs);
            if (!rhs) {
                return nullptr;
            }

            auto result = convertBinaryOp(binop.op, lhs, rhs, binop.result_type,
                                          getOperandType(*binop.lhs), getOperandType(*binop.rhs));
            return result;
        }
        case mir::MirRvalue::UnaryOp: {
            auto& unop = std::get<mir::MirRvalue::UnaryOpData>(rvalue.data);
            auto operand = convertOperand(*unop.operand);
            return convertUnaryOp(unop.op, operand);
        }
        case mir::MirRvalue::FormatConvert: {
            auto& fmtData = std::get<mir::MirRvalue::FormatConvertData>(rvalue.data);
            auto value = convertOperand(*fmtData.operand);
            return convertFormatConvert(value, fmtData.format_spec);
        }
        case mir::MirRvalue::Ref: {
            // アドレス取得（&）: Placeのアドレス計算は convertPlaceToAddress に委譲する。
            // 旧実装はここに独自のプロジェクション走査を持っていたが、Fieldプロジェクション後に現在型を更新しないため、&h.vals[i] のような Field→Index 連鎖で誤ったGEP型により
            // 不正なアドレスを生成していた（共有実装は多段連鎖を正しく処理する）
            auto& refData = std::get<mir::MirRvalue::RefData>(rvalue.data);
            return convertPlaceToAddress(refData.place);
        }
        case mir::MirRvalue::Cast: {
            // 型変換
            auto& castData = std::get<mir::MirRvalue::CastData>(rvalue.data);
            if (!castData.operand) {
                return nullptr;
            }

            auto value = convertOperand(*castData.operand);
            if (!value) {
                return nullptr;
            }

            auto targetType = convertType(castData.target_type);
            if (!targetType) {
                return value;  // 変換できない場合はそのまま
            }

            auto sourceType = value->getType();

            // 同じ型なら変換不要
            // ただし、ポインタ同士（ptr == ptr）の場合でもunion alloca→string等の抽出が必要なケースがあるためスキップする
            if (sourceType == targetType) {
                bool isUnionAllocaExtract = false;
                if (sourceType->isPointerTy()) {
                    if (auto* allocaInst = llvm::dyn_cast<llvm::AllocaInst>(value)) {
                        auto* allocatedType = allocaInst->getAllocatedType();
                        if (auto* structTy = llvm::dyn_cast<llvm::StructType>(allocatedType)) {
                            if (structTy->getNumElements() == 2) {
                                auto* elem0 = structTy->getElementType(0);
                                auto* elem1 = structTy->getElementType(1);
                                if (elem0->isIntegerTy(32) && elem1->isArrayTy()) {
                                    isUnionAllocaExtract = true;
                                }
                            }
                        }
                    }
                }
                if (!isUnionAllocaExtract) {
                    return value;
                }
            }

            // float <-> double 変換
            if (sourceType->isFloatTy() && targetType->isDoubleTy()) {
                return builder->CreateFPExt(value, targetType, "fpext");
            }
            if (sourceType->isDoubleTy() && targetType->isFloatTy()) {
                return builder->CreateFPTrunc(value, targetType, "fptrunc");
            }

            // int <-> float/double 変換。符号なしソースはuitofp
            // （sitofp固定だとuint 4000000000 as doubleが-294967296になる）
            if (sourceType->isIntegerTy() && targetType->isFloatingPointTy()) {
                bool src_unsigned = false;
                if (auto src_hir = getOperandType(*castData.operand)) {
                    auto k = src_hir->kind;
                    src_unsigned = k == hir::TypeKind::UTiny || k == hir::TypeKind::UShort ||
                                   k == hir::TypeKind::UInt || k == hir::TypeKind::ULong ||
                                   k == hir::TypeKind::USize || k == hir::TypeKind::Bool ||
                                   k == hir::TypeKind::Char;
                }
                if (src_unsigned) {
                    return builder->CreateUIToFP(value, targetType, "uitofp");
                }
                return builder->CreateSIToFP(value, targetType, "sitofp");
            }
            if (sourceType->isFloatingPointTy() && targetType->isIntegerTy()) {
                // M9: 生のfptosiは範囲外がpoison（ターゲット依存でINT_MIN/トラップに分裂）のため、
                // 飽和intrinsicへ統一する（範囲外は型の最大/最小へclamp、NaNは0。全ターゲット共通）
                bool target_unsigned = false;
                if (castData.target_type) {
                    auto k = castData.target_type->kind;
                    target_unsigned = (k == hir::TypeKind::UTiny || k == hir::TypeKind::UShort ||
                                       k == hir::TypeKind::UInt || k == hir::TypeKind::ULong ||
                                       k == hir::TypeKind::USize || k == hir::TypeKind::Bool ||
                                       k == hir::TypeKind::Char);
                }
                llvm::Intrinsic::ID sat_id =
                    target_unsigned ? llvm::Intrinsic::fptoui_sat : llvm::Intrinsic::fptosi_sat;
                return builder->CreateIntrinsic(sat_id, {targetType, sourceType}, {value}, nullptr,
                                                "fptoint_sat");
            }

            // int サイズ変換
            // 拡張の符号はソース型のsignednessに従う（C言語と同じ規則）:
            // utiny 255 as int は 255（ゼロ拡張）であり -1（符号拡張）ではない
            if (sourceType->isIntegerTy() && targetType->isIntegerTy()) {
                auto srcBits = sourceType->getIntegerBitWidth();
                auto dstBits = targetType->getIntegerBitWidth();
                if (srcBits < dstBits) {
                    bool use_zext = false;
                    auto src_hir_type = getOperandType(*castData.operand);
                    if (src_hir_type) {
                        auto kind = src_hir_type->kind;
                        use_zext = (kind == hir::TypeKind::UTiny || kind == hir::TypeKind::UShort ||
                                    kind == hir::TypeKind::UInt || kind == hir::TypeKind::ULong ||
                                    kind == hir::TypeKind::USize || kind == hir::TypeKind::Bool ||
                                    kind == hir::TypeKind::Char);
                    } else if (castData.target_type) {
                        // ソース型が不明な場合はターゲット型で判定（従来動作）
                        auto kind = castData.target_type->kind;
                        use_zext = (kind == hir::TypeKind::UTiny || kind == hir::TypeKind::UShort ||
                                    kind == hir::TypeKind::UInt || kind == hir::TypeKind::ULong);
                    }
                    if (use_zext) {
                        return builder->CreateZExt(value, targetType, "zext");
                    }
                    return builder->CreateSExt(value, targetType, "sext");
                } else if (srcBits > dstBits) {
                    return builder->CreateTrunc(value, targetType, "trunc");
                }
            }

            // === タグ付きユニオン型への変換（int/long/bool/string/struct -> Union等） ===
            // targetTypeがStructType {i32, i8[N]}の場合
            // 注意: この検出はポインタ処理より前に配置する必要がある
            //       string型（ptr）がポインタ処理パスに吸収されないようにするため
            if (auto* structTy = llvm::dyn_cast<llvm::StructType>(targetType)) {
                // タグ付きユニオン構造体（{i32, i8[N]}）かどうか確認
                if (structTy->getNumElements() == 2) {
                    auto* elem0 = structTy->getElementType(0);
                    auto* elem1 = structTy->getElementType(1);
                    if (elem0->isIntegerTy(32) && elem1->isArrayTy()) {
                        // タグ付きユニオンへのキャスト
                        // 1. 一時allocaを作成
                        auto* alloca = builder->CreateAlloca(structTy, nullptr, "union_temp");

                        // ソースのHIR型を取得（構造体バリアントの判別に必要。
                        // 構造体値はLLVMレベルではポインタで届くためLLVM型では判別できない）
                        hir::TypePtr sourceHirType = nullptr;
                        if (castData.operand && (castData.operand->kind == mir::MirOperand::Copy ||
                                                 castData.operand->kind == mir::MirOperand::Move)) {
                            if (auto* place = std::get_if<mir::MirPlace>(&castData.operand->data)) {
                                if (place->projections.empty() && currentMIRFunction &&
                                    place->local < currentMIRFunction->locals.size()) {
                                    sourceHirType = currentMIRFunction->locals[place->local].type;
                                }
                            }
                        }
                        if (!sourceHirType && castData.operand) {
                            sourceHirType = castData.operand->type;
                        }

                        // 2.
                        // タグを設定（バリアントインデックスを計算）
                        // target_typeがtypedefエイリアスの場合はUnion本体へ解決してから参照する
                        hir::TypePtr resolvedTarget = resolveTypeAlias(castData.target_type);
                        if (!resolvedTarget) {
                            resolvedTarget = castData.target_type;
                        }
                        // typedefユニオンはUnionType::variantsに変種を保持するため両対応で取得
                        auto variantTypes = ast::union_variant_types(resolvedTarget);
                        int32_t tagValue = 0;
                        hir::TypePtr matchedVariant = nullptr;
                        if (!variantTypes.empty()) {
                            // まずHIR型（kind + 構造体名）で照合する
                            if (sourceHirType) {
                                for (size_t vi = 0; vi < variantTypes.size(); ++vi) {
                                    auto& varType = variantTypes[vi];
                                    if (!varType)
                                        continue;
                                    if (varType->kind == sourceHirType->kind &&
                                        (varType->kind != hir::TypeKind::Struct ||
                                         varType->name == sourceHirType->name)) {
                                        tagValue = static_cast<int32_t>(vi);
                                        matchedVariant = varType;
                                        break;
                                    }
                                }
                            }
                            // フォールバック: LLVM型で照合する
                            if (!matchedVariant) {
                                for (size_t vi = 0; vi < variantTypes.size(); ++vi) {
                                    auto& varType = variantTypes[vi];
                                    if (!varType)
                                        continue;
                                    auto* varLLVMType = convertType(varType);
                                    if (varLLVMType == sourceType) {
                                        tagValue = static_cast<int32_t>(vi);
                                        matchedVariant = varType;
                                        break;
                                    }
                                }
                            }
                        } else if (sourceType->isIntegerTy(64)) {
                            tagValue = 1;  // フォールバック: long = 1
                        }
                        auto* tagGEP = builder->CreateStructGEP(structTy, alloca, 0, "tag_ptr");
                        builder->CreateStore(llvm::ConstantInt::get(ctx.getI32Type(), tagValue),
                                             tagGEP);

                        // 3. ペイロードに値をストア（全型対応）
                        auto* payloadGEP =
                            builder->CreateStructGEP(structTy, alloca, 1, "payload_ptr");

                        bool structVariant =
                            matchedVariant && matchedVariant->kind == hir::TypeKind::Struct;
                        if (sourceType->isStructTy()) {
                            // 構造体型（値で届いた場合）: memcpyでペイロードにコピー
                            auto& dataLayout = module->getDataLayout();
                            auto payloadSize = dataLayout.getTypeAllocSize(sourceType);
                            auto* srcAlloca =
                                builder->CreateAlloca(sourceType, nullptr, "struct_tmp");
                            builder->CreateStore(value, srcAlloca);
                            builder->CreateMemCpy(payloadGEP, llvm::MaybeAlign(), srcAlloca,
                                                  llvm::MaybeAlign(), payloadSize);
                        } else if (structVariant && sourceType->isPointerTy()) {
                            // 構造体型（ポインタで届いた場合）: 指し先の構造体をコピー
                            auto& dataLayout = module->getDataLayout();
                            auto* variantTy = convertType(matchedVariant);
                            auto payloadSize = dataLayout.getTypeAllocSize(variantTy);
                            builder->CreateMemCpy(payloadGEP, llvm::MaybeAlign(), value,
                                                  llvm::MaybeAlign(), payloadSize);
                        } else {
                            // プリミティブ型（int/long/bool/float/double/ptr等）:
                            // bitcastしてストア
                            auto* payloadAsType = builder->CreateBitCast(
                                payloadGEP, llvm::PointerType::get(sourceType, 0),
                                "payload_as_type");
                            builder->CreateStore(value, payloadAsType);
                        }

                        // 4. 構造体全体をロードして返す
                        return builder->CreateLoad(structTy, alloca, "union_load");
                    }
                }
            }

            // ユニオン取り出し時の期待タグを計算する（判定できない場合は-1）
            // オペランドのユニオン型のバリアント一覧とターゲット型をHIRレベルで照合する
            auto computeExpectedUnionTag = [&]() -> int32_t {
                hir::TypePtr unionHir = nullptr;
                if (castData.operand) {
                    if (auto* place = std::get_if<mir::MirPlace>(&castData.operand->data)) {
                        if (place->projections.empty() && currentMIRFunction &&
                            place->local < currentMIRFunction->locals.size()) {
                            unionHir = currentMIRFunction->locals[place->local].type;
                        }
                    }
                    if (!unionHir) {
                        unionHir = castData.operand->type;
                    }
                }
                auto resolvedUnion = resolveTypeAlias(unionHir);
                auto variants = ast::union_variant_types(resolvedUnion ? resolvedUnion : unionHir);
                if (variants.empty()) {
                    return -1;
                }
                auto target = resolveTypeAlias(castData.target_type);
                if (!target) {
                    target = castData.target_type;
                }
                if (!target) {
                    return -1;
                }
                for (size_t vi = 0; vi < variants.size(); ++vi) {
                    auto& v = variants[vi];
                    if (!v) {
                        continue;
                    }
                    if (v->kind == target->kind &&
                        (v->kind != hir::TypeKind::Struct || v->name == target->name)) {
                        return static_cast<int32_t>(vi);
                    }
                }
                return -1;
            };

            // タグ検査を挿入する（不一致なら実行時パニック。従来は無検査でクラッシュしていた）
            auto emitUnionTagCheck = [&](llvm::StructType* unionTy, llvm::Value* unionPtr) {
                int32_t expectedTag = computeExpectedUnionTag();
                if (expectedTag < 0) {
                    return;
                }
                auto* tagPtr = builder->CreateStructGEP(unionTy, unionPtr, 0, "tag_check_ptr");
                auto* tagVal = builder->CreateLoad(ctx.getI32Type(), tagPtr, "tag_val");
                auto* expected = llvm::ConstantInt::get(ctx.getI32Type(), expectedTag);
                auto* mismatch = builder->CreateICmpNE(tagVal, expected, "union_tag.check");
                auto* func = builder->GetInsertBlock()->getParent();
                auto* failBB = llvm::BasicBlock::Create(ctx.getContext(), "union_tag.fail", func);
                auto* contBB = llvm::BasicBlock::Create(ctx.getContext(), "union_tag.cont", func);
                builder->CreateCondBr(mismatch, failBB, contBB);
                builder->SetInsertPoint(failBB);
                generatePanic("invalid union cast: active variant does not match target type");
                builder->SetInsertPoint(contBB);
            };

            // === ユニオン型の実行時型判別 (expr is Type) ===
            // タグを比較したboolを返す（ペイロードの取り出しは行わない）
            if (castData.check_only) {
                int32_t expectedTag = computeExpectedUnionTag();
                auto* expected =
                    llvm::ConstantInt::get(ctx.getI32Type(), expectedTag < 0 ? -1 : expectedTag);
                // ソースがタグ付きユニオン構造体（値渡し {i32, [N x i8]}）
                if (auto* structTy = llvm::dyn_cast<llvm::StructType>(sourceType)) {
                    if (structTy->getNumElements() == 2 &&
                        structTy->getElementType(0)->isIntegerTy(32) &&
                        structTy->getElementType(1)->isArrayTy()) {
                        auto* alloca = builder->CreateAlloca(structTy, nullptr, "union_is_temp");
                        builder->CreateStore(value, alloca);
                        auto* tagPtr = builder->CreateStructGEP(structTy, alloca, 0, "is_tag_ptr");
                        auto* tagVal = builder->CreateLoad(ctx.getI32Type(), tagPtr, "is_tag");
                        return builder->CreateICmpEQ(tagVal, expected, "union_is");
                    }
                }
                // ソースがユニオンalloca（ポインタで届いた場合）
                if (sourceType->isPointerTy()) {
                    if (auto* allocaInst = llvm::dyn_cast<llvm::AllocaInst>(value)) {
                        if (auto* sTy =
                                llvm::dyn_cast<llvm::StructType>(allocaInst->getAllocatedType())) {
                            if (sTy->getNumElements() == 2 &&
                                sTy->getElementType(0)->isIntegerTy(32) &&
                                sTy->getElementType(1)->isArrayTy()) {
                                auto* tagPtr =
                                    builder->CreateStructGEP(sTy, value, 0, "is_tag_ptr");
                                auto* tagVal =
                                    builder->CreateLoad(ctx.getI32Type(), tagPtr, "is_tag");
                                return builder->CreateICmpEQ(tagVal, expected, "union_is");
                            }
                        }
                    }
                }
                // 判定不能（型チェッカーがユニオン以外を拒否するため通常到達しない）
                return llvm::ConstantInt::getFalse(ctx.getContext());
            }

            // === タグ付きユニオン型からの変換（Union -> int/long/bool/string/struct等） ===
            // sourceTypeがStructType {i32, i8[N]}の場合
            if (auto* structTy = llvm::dyn_cast<llvm::StructType>(sourceType)) {
                // タグ付きユニオン構造体（{i32, i8[N]}）かどうか確認
                if (structTy->getNumElements() == 2) {
                    auto* elem0 = structTy->getElementType(0);
                    auto* elem1 = structTy->getElementType(1);
                    if (elem0->isIntegerTy(32) && elem1->isArrayTy()) {
                        // タグ付きユニオンからのキャスト
                        // valueは集約型なので、一時allocaにストアしてからアクセス
                        auto* alloca =
                            builder->CreateAlloca(structTy, nullptr, "union_extract_temp");
                        builder->CreateStore(value, alloca);

                        // タグ検査（不一致は実行時パニック）
                        emitUnionTagCheck(structTy, alloca);

                        // ペイロードポインタを取得
                        auto* payloadGEP =
                            builder->CreateStructGEP(structTy, alloca, 1, "payload_extract_ptr");

                        // ターゲット型に応じてロード（全型対応）
                        if (targetType->isStructTy()) {
                            // 構造体型: memcpyで読み出し
                            auto* destAlloca =
                                builder->CreateAlloca(targetType, nullptr, "struct_extract_tmp");
                            auto& dataLayout = module->getDataLayout();
                            auto copySize = dataLayout.getTypeAllocSize(targetType);
                            builder->CreateMemCpy(destAlloca, llvm::MaybeAlign(), payloadGEP,
                                                  llvm::MaybeAlign(), copySize);
                            return builder->CreateLoad(targetType, destAlloca, "struct_from_union");
                        } else {
                            // プリミティブ型（int/long/bool/float/double/ptr等）:
                            // bitcastしてロード
                            auto* payloadAsType = builder->CreateBitCast(
                                payloadGEP, llvm::PointerType::get(targetType, 0),
                                "payload_as_target");
                            return builder->CreateLoad(targetType, payloadAsType, "val_from_union");
                        }
                    }
                }
            }

            // LLVM 14+: opaque pointersではポインタ間のBitCast不要
            if (sourceType->isPointerTy() && targetType->isPointerTy()) {
                // opaque pointersでは全てのポインタは ptr 型なので通常変換不要
                // ただし、union alloca(ptr)→string(ptr)の抽出が必要なケースは除く
                bool isUnionAllocaExtract = false;
                if (auto* allocaInst = llvm::dyn_cast<llvm::AllocaInst>(value)) {
                    auto* allocatedType = allocaInst->getAllocatedType();
                    if (auto* sTy = llvm::dyn_cast<llvm::StructType>(allocatedType)) {
                        if (sTy->getNumElements() == 2 && sTy->getElementType(0)->isIntegerTy(32) &&
                            sTy->getElementType(1)->isArrayTy()) {
                            isUnionAllocaExtract = true;
                        }
                    }
                }
                if (!isUnionAllocaExtract) {
                    return value;
                }
            }

            // int <-> ポインタ変換
            if (sourceType->isIntegerTy() && targetType->isPointerTy()) {
                return builder->CreateIntToPtr(value, targetType, "inttoptr");
            }
            if (sourceType->isPointerTy()) {
                // ポインタがタグ付きユニオン構造体を指しているか確認（Union as T）
                if (auto* allocaInst = llvm::dyn_cast<llvm::AllocaInst>(value)) {
                    auto* allocatedType = allocaInst->getAllocatedType();
                    if (auto* structTy = llvm::dyn_cast<llvm::StructType>(allocatedType)) {
                        if (structTy->getNumElements() == 2) {
                            auto* elem0 = structTy->getElementType(0);
                            auto* elem1 = structTy->getElementType(1);
                            if (elem0->isIntegerTy(32) && elem1->isArrayTy()) {
                                // タグ付きユニオン構造体からの抽出（全型対応）
                                // タグ検査（不一致は実行時パニック）
                                emitUnionTagCheck(structTy, value);

                                auto* payloadGEP = builder->CreateStructGEP(structTy, value, 1,
                                                                            "union_payload_ptr");
                                if (targetType->isStructTy()) {
                                    // 構造体型: memcpyで読み出し
                                    auto* destAlloca = builder->CreateAlloca(
                                        targetType, nullptr, "struct_from_union_ptr");
                                    auto& dataLayout = module->getDataLayout();
                                    auto copySize = dataLayout.getTypeAllocSize(targetType);
                                    builder->CreateMemCpy(destAlloca, llvm::MaybeAlign(),
                                                          payloadGEP, llvm::MaybeAlign(), copySize);
                                    return builder->CreateLoad(targetType, destAlloca,
                                                               "struct_from_union_ptr_load");
                                } else {
                                    // プリミティブ型: bitcastしてロード
                                    auto* payloadAsType = builder->CreateBitCast(
                                        payloadGEP, llvm::PointerType::get(targetType, 0),
                                        "payload_as_target_ptr");
                                    return builder->CreateLoad(targetType, payloadAsType,
                                                               "val_from_union_ptr");
                                }
                            }
                        }
                    }
                }
                // タグ付きユニオンではない場合
                if (targetType->isIntegerTy()) {
                    return builder->CreatePtrToInt(value, targetType, "ptrtoint");
                }
            }

            return value;
        }
        case mir::MirRvalue::Aggregate: {
            // 集約型（構造体、配列、タプル）の構築
            auto& aggData = std::get<mir::MirRvalue::AggregateData>(rvalue.data);

            if (aggData.kind.type == mir::AggregateKind::Type::Struct) {
                // 構造体型を取得
                std::string structName = aggData.kind.name;

                // 型情報から型引数を取得してマングリング
                if (aggData.kind.ty && !aggData.kind.ty->type_args.empty()) {
                    for (const auto& typeArg : aggData.kind.ty->type_args) {
                        if (typeArg) {
                            structName += "__";
                            switch (typeArg->kind) {
                                case hir::TypeKind::Int:
                                    structName += "int";
                                    break;
                                case hir::TypeKind::UInt:
                                    structName += "uint";
                                    break;
                                case hir::TypeKind::Long:
                                    structName += "long";
                                    break;
                                case hir::TypeKind::ULong:
                                    structName += "ulong";
                                    break;
                                case hir::TypeKind::Float:
                                    structName += "float";
                                    break;
                                case hir::TypeKind::Double:
                                    structName += "double";
                                    break;
                                case hir::TypeKind::Bool:
                                    structName += "bool";
                                    break;
                                case hir::TypeKind::Char:
                                    structName += "char";
                                    break;
                                case hir::TypeKind::Struct:
                                    structName += typeArg->name;
                                    break;
                                default:
                                    structName += "unknown";
                                    break;
                            }
                        }
                    }
                }

                auto it = structTypes.find(structName);
                if (it == structTypes.end()) {
                    // マングリングなしでも試す
                    it = structTypes.find(aggData.kind.name);
                }

                if (it == structTypes.end() || !it->second) {
                    // 構造体型が見つからない
                    return nullptr;
                }

                auto* structType = it->second;

                // 一時変数を作成してフィールドを初期化
                auto* alloca = builder->CreateAlloca(structType, nullptr, "agg_temp");

                // 各フィールドを初期化
                for (size_t i = 0; i < aggData.operands.size(); ++i) {
                    if (aggData.operands[i]) {
                        auto* fieldValue = convertOperand(*aggData.operands[i]);
                        if (fieldValue) {
                            auto* gep =
                                builder->CreateStructGEP(structType, alloca, i, "agg_field");
                            builder->CreateStore(fieldValue, gep);
                        }
                    }
                }

                // 構造体値をロードして返す
                return builder->CreateLoad(structType, alloca, "agg_load");
            } else if (aggData.kind.type == mir::AggregateKind::Type::Array) {
                // 配列の処理
                if (aggData.operands.empty() || !aggData.operands[0]) {
                    return nullptr;
                }

                // 最初の要素から型を推定
                auto* firstElem = convertOperand(*aggData.operands[0]);
                if (!firstElem) {
                    return nullptr;
                }

                auto* elemType = firstElem->getType();
                auto* arrayType = llvm::ArrayType::get(elemType, aggData.operands.size());
                auto* alloca = builder->CreateAlloca(arrayType, nullptr, "arr_temp");

                // 各要素を初期化
                for (size_t i = 0; i < aggData.operands.size(); ++i) {
                    if (aggData.operands[i]) {
                        auto* elemValue = convertOperand(*aggData.operands[i]);
                        if (elemValue) {
                            auto* gep =
                                builder->CreateGEP(arrayType, alloca,
                                                   {llvm::ConstantInt::get(ctx.getI32Type(), 0),
                                                    llvm::ConstantInt::get(ctx.getI32Type(), i)},
                                                   "arr_elem");
                            builder->CreateStore(elemValue, gep);
                        }
                    }
                }

                return builder->CreateLoad(arrayType, alloca, "arr_load");
            }

            return nullptr;
        }
        default:
            return nullptr;
    }
}

// フォーマット変換
llvm::Value* MIRToLLVM::convertFormatConvert(llvm::Value* value, const std::string& format_spec) {
    if (!value)
        return nullptr;

    auto valueType = value->getType();
    llvm::FunctionCallee formatFunc;

    // 文字列型はi8*
    auto stringType = ctx.getPtrType();

    // フォーマット指定子に基づいて適切なフォーマット関数を選択
    if (format_spec == "x") {
        // 16進数（小文字）
        formatFunc = module->getOrInsertFunction(
            "cm_format_int_hex", llvm::FunctionType::get(stringType, {ctx.getI64Type()}, false));
        if (valueType->isIntegerTy() && valueType->getIntegerBitWidth() < 64) {
            value = builder->CreateSExt(value, ctx.getI64Type());
        }
        return builder->CreateCall(formatFunc, {value});
    } else if (format_spec == "X") {
        // 16進数（大文字）
        formatFunc = module->getOrInsertFunction(
            "cm_format_int_HEX", llvm::FunctionType::get(stringType, {ctx.getI64Type()}, false));
        if (valueType->isIntegerTy() && valueType->getIntegerBitWidth() < 64) {
            value = builder->CreateSExt(value, ctx.getI64Type());
        }
        return builder->CreateCall(formatFunc, {value});
    } else if (format_spec == "b") {
        // 2進数
        formatFunc = module->getOrInsertFunction(
            "cm_format_int_binary", llvm::FunctionType::get(stringType, {ctx.getI64Type()}, false));
        if (valueType->isIntegerTy() && valueType->getIntegerBitWidth() < 64) {
            value = builder->CreateSExt(value, ctx.getI64Type());
        }
        return builder->CreateCall(formatFunc, {value});
    } else if (format_spec == "o") {
        // 8進数
        formatFunc = module->getOrInsertFunction(
            "cm_format_int_octal", llvm::FunctionType::get(stringType, {ctx.getI64Type()}, false));
        if (valueType->isIntegerTy() && valueType->getIntegerBitWidth() < 64) {
            value = builder->CreateSExt(value, ctx.getI64Type());
        }
        return builder->CreateCall(formatFunc, {value});
    } else if (format_spec.find('.') != std::string::npos) {
        // 浮動小数点の精度指定
        int precision = 2;  // デフォルト
        try {
            precision = std::stoi(format_spec.substr(1));
        } catch (...) {}

        formatFunc = module->getOrInsertFunction(
            "cm_format_double_precision",
            llvm::FunctionType::get(stringType, {ctx.getF64Type(), ctx.getI32Type()}, false));

        if (!valueType->isDoubleTy()) {
            if (valueType->isFloatTy()) {
                value = builder->CreateFPExt(value, ctx.getF64Type());
            } else if (valueType->isIntegerTy()) {
                value = builder->CreateSIToFP(value, ctx.getF64Type());
            }
        }
        auto precisionVal = llvm::ConstantInt::get(ctx.getI32Type(), precision);
        return builder->CreateCall(formatFunc, {value, precisionVal});
    } else {
        // デフォルト：toString相当
        if (valueType->isDoubleTy() || valueType->isFloatTy()) {
            formatFunc = module->getOrInsertFunction(
                "cm_format_double", llvm::FunctionType::get(stringType, {ctx.getF64Type()}, false));
            if (valueType->isFloatTy()) {
                value = builder->CreateFPExt(value, ctx.getF64Type());
            }
            return builder->CreateCall(formatFunc, {value});
        } else if (valueType->isIntegerTy()) {
            formatFunc = module->getOrInsertFunction(
                "cm_format_int", llvm::FunctionType::get(stringType, {ctx.getI32Type()}, false));
            if (valueType->getIntegerBitWidth() > 32) {
                value = builder->CreateTrunc(value, ctx.getI32Type());
            } else if (valueType->getIntegerBitWidth() < 32) {
                value = builder->CreateSExt(value, ctx.getI32Type());
            }
            return builder->CreateCall(formatFunc, {value});
        } else {
            // その他（文字列など）はそのまま返す
            return value;
        }
    }
}

// オペランド変換

}  // namespace cm::codegen::llvm_backend
