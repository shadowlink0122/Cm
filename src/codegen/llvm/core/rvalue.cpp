/// @file rvalue.cpp
/// @brief MIR右辺値（BinaryOp/Cast/Aggregate等）→ LLVM IR 変換

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
            // std::cerr << "[MIR2LLVM]         Converting BinaryOp, op=" <<
            // static_cast<int>(binop.op)
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
            // アドレス取得（&）
            auto& refData = std::get<mir::MirRvalue::RefData>(rvalue.data);
            auto local = refData.place.local;

            if (locals.find(local) == locals.end() || !locals[local]) {
                return nullptr;
            }

            llvm::Value* basePtr = locals[local];

            // プロジェクションがある場合
            if (!refData.place.projections.empty()) {
                auto& localInfo = currentMIRFunction->locals[local];
                hir::TypePtr currentType = localInfo.type;
                llvm::Value* addr = basePtr;

                // Projectionシーケンスを順次処理
                for (size_t pi = 0; pi < refData.place.projections.size(); ++pi) {
                    const auto& proj = refData.place.projections[pi];

                    if (proj.kind == mir::ProjectionKind::Deref) {
                        // ポインタをロード
                        if (currentType && currentType->kind == hir::TypeKind::Pointer) {
                            addr = builder->CreateLoad(ctx.getPtrType(), addr, "deref_load");
                            currentType = currentType->element_type;
                        }
                    } else if (proj.kind == mir::ProjectionKind::Index) {
                        // ポインタ型の場合、Indexの前に暗黙のDerefが必要
                        // MIR生成で Deref が省略されるケース（p[0]等）に対応
                        if (currentType && currentType->kind == hir::TypeKind::Pointer) {
                            addr = builder->CreateLoad(ctx.getPtrType(), addr, "implicit_deref");
                            currentType = currentType->element_type;
                        }

                        // インデックスアクセス
                        llvm::Value* indexVal = nullptr;
                        if (locals.find(proj.index_local) != locals.end() &&
                            locals[proj.index_local]) {
                            auto& idxLocal = currentMIRFunction->locals[proj.index_local];
                            auto idxType = convertType(idxLocal.type);
                            indexVal = builder->CreateLoad(idxType, locals[proj.index_local]);
                            // i64に拡張
                            if (indexVal->getType()->isIntegerTy() &&
                                indexVal->getType()->getIntegerBitWidth() < 64) {
                                indexVal = builder->CreateSExt(indexVal, ctx.getI64Type());
                            }
                        } else {
                            indexVal = llvm::ConstantInt::get(ctx.getI64Type(), 0);
                        }

                        // Array型の場合と、Pointer要素へのアクセス（Deref後）で異なるGEPを生成
                        if (currentType && currentType->kind == hir::TypeKind::Array) {
                            // Array型: {0, idx}の2インデックスGEP
                            auto elemType = convertType(currentType->element_type);
                            auto arraySize = currentType->array_size.value_or(0);
                            auto arrayType = llvm::ArrayType::get(elemType, arraySize);
                            addr = builder->CreateGEP(
                                arrayType, addr,
                                {llvm::ConstantInt::get(ctx.getI64Type(), 0), indexVal},
                                "arr_elem_ptr");
                            // 型を要素型に更新
                            currentType = currentType->element_type;
                        } else {
                            // Pointer要素へのアクセス（Deref後）: {idx}のみのGEP
                            llvm::Type* elemType = ctx.getI32Type();  // デフォルト
                            if (currentType) {
                                elemType = convertType(currentType);
                            }
                            addr = builder->CreateGEP(elemType, addr, indexVal, "idx_elem_ptr");
                        }
                    } else if (proj.kind == mir::ProjectionKind::Field) {
                        // 構造体フィールドへのアドレス
                        auto& localInfo = currentMIRFunction->locals[local];
                        hir::TypePtr structType = localInfo.type;

                        // 既にGEPで移動している場合、現在の型を追跡
                        // フィールドアクセスでは元のローカル変数の型から辿る
                        if (structType && structType->kind == hir::TypeKind::Struct) {
                            // ジェネリック構造体の場合、型引数を考慮した名前を生成
                            std::string structLookupName = structType->name;
                            if (!structType->type_args.empty()) {
                                for (const auto& typeArg : structType->type_args) {
                                    if (typeArg) {
                                        structLookupName += "__";
                                        if (typeArg->kind == hir::TypeKind::Struct) {
                                            // ネストジェネリックの場合、再帰的にマングリング
                                            std::string nestedName = typeArg->name;
                                            // type_argsがある場合（例:
                                            // Vector<int>）、再帰的に処理
                                            if (!typeArg->type_args.empty()) {
                                                for (const auto& nestedArg : typeArg->type_args) {
                                                    if (nestedArg) {
                                                        nestedName += "__";
                                                        switch (nestedArg->kind) {
                                                            case hir::TypeKind::Int:
                                                                nestedName += "int";
                                                                break;
                                                            case hir::TypeKind::UInt:
                                                                nestedName += "uint";
                                                                break;
                                                            case hir::TypeKind::Long:
                                                                nestedName += "long";
                                                                break;
                                                            case hir::TypeKind::ULong:
                                                                nestedName += "ulong";
                                                                break;
                                                            case hir::TypeKind::Float:
                                                                nestedName += "float";
                                                                break;
                                                            case hir::TypeKind::Double:
                                                                nestedName += "double";
                                                                break;
                                                            case hir::TypeKind::Bool:
                                                                nestedName += "bool";
                                                                break;
                                                            case hir::TypeKind::Char:
                                                                nestedName += "char";
                                                                break;
                                                            case hir::TypeKind::String:
                                                                nestedName += "string";
                                                                break;
                                                            case hir::TypeKind::Struct:
                                                                nestedName += nestedArg->name;
                                                                break;
                                                            default:
                                                                if (!nestedArg->name.empty()) {
                                                                    nestedName += nestedArg->name;
                                                                }
                                                                break;
                                                        }
                                                    }
                                                }
                                            }
                                            structLookupName += nestedName;
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
                            auto it = structTypes.find(structLookupName);
                            if (it != structTypes.end()) {
                                // LLVM 14: typed pointers require bitcast
#if LLVM_VERSION_MAJOR < 15
                                auto structPtrType = llvm::PointerType::get(it->second, 0);
                                if (basePtr->getType() != structPtrType) {
                                    basePtr = builder->CreateBitCast(basePtr, structPtrType,
                                                                     "struct_ptr_cast");
                                }
#endif
                                std::vector<llvm::Value*> indices;
                                indices.push_back(llvm::ConstantInt::get(ctx.getI32Type(), 0));
                                indices.push_back(
                                    llvm::ConstantInt::get(ctx.getI32Type(), proj.field_id));
                                addr = builder->CreateGEP(it->second, addr, indices, "field_ptr");
                            }
                        }
                    }
                }
                // Projectionシーケンス処理後はaddrを返す
                return addr;
            }

            return basePtr;
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
            // ただし、ポインタ同士（ptr == ptr）の場合でもunion alloca→string等の
            // 抽出が必要なケースがあるためスキップする
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

            // int <-> float/double 変換
            if (sourceType->isIntegerTy() && targetType->isFloatingPointTy()) {
                return builder->CreateSIToFP(value, targetType, "sitofp");
            }
            if (sourceType->isFloatingPointTy() && targetType->isIntegerTy()) {
                return builder->CreateFPToSI(value, targetType, "fptosi");
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

                        // 2.
                        // タグを設定（castData.target_type->type_argsからバリアントインデックスを計算）
                        int32_t tagValue = 0;
                        if (castData.target_type && !castData.target_type->type_args.empty()) {
                            // ソース型のLLVM型とtype_argsのHIR型を照合してタグ値を決定
                            for (size_t vi = 0; vi < castData.target_type->type_args.size(); ++vi) {
                                auto& varType = castData.target_type->type_args[vi];
                                if (!varType)
                                    continue;
                                auto* varLLVMType = convertType(varType);
                                if (varLLVMType == sourceType) {
                                    tagValue = static_cast<int32_t>(vi);
                                    break;
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

                        if (sourceType->isStructTy()) {
                            // 構造体型: memcpyでペイロードにコピー
                            auto& dataLayout = module->getDataLayout();
                            auto payloadSize = dataLayout.getTypeAllocSize(sourceType);
                            auto* srcAlloca =
                                builder->CreateAlloca(sourceType, nullptr, "struct_tmp");
                            builder->CreateStore(value, srcAlloca);
                            builder->CreateMemCpy(payloadGEP, llvm::MaybeAlign(), srcAlloca,
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
