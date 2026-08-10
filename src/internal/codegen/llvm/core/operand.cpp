/// @file operand.cpp
/// @brief MIRオペランド/Place → LLVM IR 変換

#include "internal/base/debug/codegen.hpp"
#include "internal/codegen/llvm/monitoring/compilation_guard.hpp"
#include "internal/syntax/ast/typekey.hpp"
#include "mir_to_llvm.hpp"

#include <iostream>
#include <llvm/IR/InlineAsm.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>
#include <map>
#include <memory>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cm::codegen::llvm_backend {

llvm::Value* MIRToLLVM::convertOperand(const mir::MirOperand& operand) {
    // 再帰深度の追跡と制限
    static thread_local int recursion_depth = 0;
    static thread_local std::unordered_set<const mir::MirOperand*> processing;

    // 最大再帰深度の制限
    const int MAX_RECURSION_DEPTH = 100;

    // 循環参照の検出
    if (processing.count(&operand) > 0) {
        // std::cerr << "[MIR2LLVM]        Operand kind: " << static_cast<int>(operand.kind) <<
        // "\n";
        if (operand.kind == mir::MirOperand::Copy || operand.kind == mir::MirOperand::Move) {
            // auto& place = std::get<mir::MirPlace>(operand.data);
            // std::cerr << "[MIR2LLVM]        Place local: " << place.local << "\n";
        }
        return llvm::UndefValue::get(ctx.getI64Type());
    }

    if (recursion_depth >= MAX_RECURSION_DEPTH) {
        // std::cerr << "[MIR2LLVM]        Current depth: " << recursion_depth << "\n";
        // std::cerr << "[MIR2LLVM]        Operand kind: " << static_cast<int>(operand.kind) <<
        // "\n";
        return llvm::UndefValue::get(ctx.getI64Type());
    }

    // RAII for tracking recursion depth and processing set
    struct RecursionGuard {
        int& depth;
        std::unordered_set<const mir::MirOperand*>& set;
        const mir::MirOperand* op;
        RecursionGuard(int& d, std::unordered_set<const mir::MirOperand*>& s,
                       const mir::MirOperand* o)
            : depth(d), set(s), op(o) {
            depth++;
            set.insert(op);
        }
        ~RecursionGuard() {
            depth--;
            set.erase(op);
        }
    };

    RecursionGuard guard(recursion_depth, processing, &operand);

    // // debug_msg("MIR2LLVM", "convertOperand called");

    switch (operand.kind) {
        case mir::MirOperand::Copy:
        case mir::MirOperand::Move: {
            auto& place = std::get<mir::MirPlace>(operand.data);
            // // debug_msg("MIR2LLVM", "Place operand");

            // プロジェクションがある場合（フィールドアクセスなど）
            if (!place.projections.empty()) {
                auto addr = convertPlaceToAddress(place);
                if (addr) {
                    // フィールドの型を取得してロード
                    llvm::Type* fieldType = nullptr;

                    // プロジェクションチェーンを辿って最終的なフィールド型を取得
                    hir::TypePtr currentType = nullptr;
                    if (currentMIRFunction && place.local < currentMIRFunction->locals.size()) {
                        currentType = currentMIRFunction->locals[place.local].type;
                    }

                    for (const auto& proj : place.projections) {
                        if (proj.kind == mir::ProjectionKind::Field && currentType) {
                            // Generic型もStructとして扱う（モノモーフィック化後の型）
                            if (currentType->kind == hir::TypeKind::Struct ||
                                currentType->kind == hir::TypeKind::Generic) {
                                std::string lookupName = currentType->name;

                                // Tagged Union構造体の特別処理
                                // __TaggedUnion_* 構造体は {i32 tag, i8[N] payload} 形式
                                if (lookupName.find("__TaggedUnion_") == 0) {
                                    if (proj.field_id == 0) {
                                        // field[0] = タグ (常にi32)
                                        currentType = hir::make_int();
                                    } else if (proj.field_id == 1) {
                                        // field[1] = ペイロード: 型引数から最大サイズの型を推論
                                        // デフォルトはi64（ポインタサイズ）で安全側に倒す
                                        currentType = hir::make_long();

                                        // ベースローカルのtype_argsが利用可能なら正確な型を使用
                                        if (currentMIRFunction &&
                                            place.local < currentMIRFunction->locals.size()) {
                                            auto& baseType =
                                                currentMIRFunction->locals[place.local].type;
                                            if (baseType && !baseType->type_args.empty()) {
                                                // type_argsの中で最大サイズの型をペイロード型として使用
                                                bool has64bit = false;
                                                hir::TypePtr bestType = nullptr;
                                                for (const auto& arg : baseType->type_args) {
                                                    if (!arg)
                                                        continue;
                                                    auto k = arg->kind;
                                                    if (k == hir::TypeKind::Long ||
                                                        k == hir::TypeKind::ULong ||
                                                        k == hir::TypeKind::Double ||
                                                        k == hir::TypeKind::UDouble ||
                                                        k == hir::TypeKind::Pointer ||
                                                        k == hir::TypeKind::Reference ||
                                                        k == hir::TypeKind::String ||
                                                        k == hir::TypeKind::CString ||
                                                        k == hir::TypeKind::ISize ||
                                                        k == hir::TypeKind::USize) {
                                                        has64bit = true;
                                                        if (!bestType)
                                                            bestType = arg;
                                                    } else if (!has64bit && !bestType) {
                                                        bestType = arg;
                                                    }
                                                }
                                                if (has64bit) {
                                                    currentType = hir::make_long();
                                                } else if (bestType) {
                                                    currentType = bestType;
                                                }
                                            }
                                        }
                                    }
                                } else {
                                    auto structIt = structDefs.find(lookupName);

                                    // フォールバック: 関数名から構造体名を推論
                                    // Container__int__get → Container__int
                                    if (structIt == structDefs.end() && currentMIRFunction) {
                                        const auto& funcName = currentMIRFunction->name;
                                        size_t lastDunder = funcName.rfind("__");
                                        if (lastDunder != std::string::npos && lastDunder > 0) {
                                            std::string inferredStruct =
                                                funcName.substr(0, lastDunder);
                                            structIt = structDefs.find(inferredStruct);
                                            if (structIt != structDefs.end()) {
                                                lookupName = inferredStruct;
                                            }
                                        }
                                    }

                                    if (structIt != structDefs.end()) {
                                        auto& fields = structIt->second->fields;
                                        if (proj.field_id < fields.size()) {
                                            currentType = fields[proj.field_id].type;

                                            // フィールド型がジェネリックパラメータ(T等)の場合、マングリング名から具体型に置換
                                            // 例: Box__intのvalue: T -> int
                                            if (currentType && currentType->name.length() <= 2 &&
                                                !currentType->name.empty()) {
                                                // ベースローカルの型名からマングリング名を抽出
                                                auto& baseLocal =
                                                    currentMIRFunction->locals[place.local].type;
                                                std::string mangledName;
                                                if (baseLocal) {
                                                    if (!baseLocal->type_args.empty() &&
                                                        baseLocal->type_args[0]) {
                                                        // type_argsがある場合は直接使用
                                                        currentType = baseLocal->type_args[0];
                                                    } else if (cm::ast::typekey::is_encoded_key(
                                                                   baseLocal->name)) {
                                                        // $エンコード名はtypekeyの可逆復号で第1型引数を得る（$移行用・フラット規約下では不活性）
                                                        auto decoded =
                                                            cm::ast::typekey::decode_type_args(
                                                                baseLocal->name);
                                                        if (!decoded.empty() && decoded[0]) {
                                                            currentType = decoded[0];
                                                        }
                                                    } else if (baseLocal->name.find("__") !=
                                                               std::string::npos) {
                                                        // マングリング名から型引数を抽出
                                                        // (Box__int
                                                        // -> int)
                                                        mangledName = baseLocal->name;
                                                        size_t pos = mangledName.find("__");
                                                        std::string typeArg =
                                                            mangledName.substr(pos + 2);
                                                        size_t nextPos = typeArg.find("__");
                                                        if (nextPos != std::string::npos) {
                                                            typeArg = typeArg.substr(0, nextPos);
                                                        }
                                                        if (!typeArg.empty()) {
                                                            if (typeArg == "int")
                                                                currentType = hir::make_int();
                                                            else if (typeArg == "uint")
                                                                currentType = hir::make_uint();
                                                            else if (typeArg == "long")
                                                                currentType = hir::make_long();
                                                            else if (typeArg == "ulong")
                                                                currentType = hir::make_ulong();
                                                            else if (typeArg == "float")
                                                                currentType = hir::make_float();
                                                            else if (typeArg == "double")
                                                                currentType = hir::make_double();
                                                            else if (typeArg == "bool")
                                                                currentType = hir::make_bool();
                                                            else if (typeArg == "string")
                                                                currentType = hir::make_string();
                                                            else {
                                                                auto st =
                                                                    std::make_shared<hir::Type>(
                                                                        hir::TypeKind::Struct);
                                                                st->name = typeArg;
                                                                currentType = st;
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        } else if (proj.kind == mir::ProjectionKind::Index && currentType) {
                            // 配列型またはポインタ型経由のインデックスアクセス
                            if (currentType->kind == hir::TypeKind::Array &&
                                currentType->element_type) {
                                currentType = currentType->element_type;
                            } else if (currentType->kind == hir::TypeKind::Pointer &&
                                       currentType->element_type) {
                                // ポインタ型経由のインデックスアクセス (ptr[i])
                                currentType = currentType->element_type;
                            }
                        } else if (proj.kind == mir::ProjectionKind::Deref && currentType) {
                            if (currentType->kind == hir::TypeKind::Pointer &&
                                currentType->element_type) {
                                currentType = currentType->element_type;
                            }
                            // プリミティブ型借用selfの場合: MIRでは元の型で記録されているが
                            // Deref後も同じ型を維持（ポインタからプリミティブ値をロード）
                            // currentType がすでにプリミティブ型なら変更不要
                        }
                    }

                    // 最終的な型を使用
                    if (currentType) {
                        fieldType = convertType(currentType);
                    }

                    if (!fieldType) {
                        // フォールバック: i32として扱う
                        if (cm::debug::debug_mode()) {
                            std::cerr << "[DEBUG] fieldType fallback to i32 in "
                                      << (currentMIRFunction ? currentMIRFunction->name : "?")
                                      << " local=" << place.local
                                      << " projections=" << place.projections.size();
                            if (currentType) {
                                std::cerr << " currentType=" << currentType->name
                                          << " kind=" << static_cast<int>(currentType->kind);
                            } else {
                                std::cerr << " currentType=null";
                            }
                            std::cerr << "\n";
                        }
                        fieldType = ctx.getI32Type();
                    }

                    // プリミティブ型借用selfの特別処理:
                    // 最後のプロジェクションがDerefで、addrがArgumentの場合
                    // これはプリミティブ型の借用selfへのアクセス（*self）
                    // 元のローカル型からロード型を決定
                    if (!place.projections.empty()) {
                        const auto& lastProj = place.projections.back();
                        if (lastProj.kind == mir::ProjectionKind::Deref &&
                            llvm::isa<llvm::Argument>(addr)) {
                            // 借用selfへのDeref: 元のMIRローカル型を確認
                            if (currentMIRFunction &&
                                place.local < currentMIRFunction->locals.size()) {
                                auto& localInfo = currentMIRFunction->locals[place.local];
                                if (localInfo.type) {
                                    // Pointer型の場合はelement_typeを、そうでなければ元の型を使用
                                    hir::TypePtr elemType = localInfo.type;
                                    if (localInfo.type->kind == hir::TypeKind::Pointer &&
                                        localInfo.type->element_type) {
                                        elemType = localInfo.type->element_type;
                                    }
                                    // プリミティブ型ならその型でロード
                                    if (elemType->kind == hir::TypeKind::Int ||
                                        elemType->kind == hir::TypeKind::UInt ||
                                        elemType->kind == hir::TypeKind::Long ||
                                        elemType->kind == hir::TypeKind::ULong ||
                                        elemType->kind == hir::TypeKind::Float ||
                                        elemType->kind == hir::TypeKind::Double ||
                                        elemType->kind == hir::TypeKind::Bool ||
                                        elemType->kind == hir::TypeKind::Char) {
                                        fieldType = convertType(elemType);
                                    }
                                }
                            }
                        }
                    }

                    // LLVM 14 typed pointers:
                    // 構造体型をロードする場合、アドレスを正しい型にキャスト
#if LLVM_VERSION_MAJOR < 15
                    if (fieldType->isStructTy()) {
                        auto expectedPtrType = llvm::PointerType::get(fieldType, 0);
                        if (addr->getType() != expectedPtrType) {
                            addr =
                                builder->CreateBitCast(addr, expectedPtrType, "ptr_to_struct_cast");
                        }
                    }
#endif
                    return builder->CreateLoad(fieldType, addr, "field_load");
                }
                return nullptr;
            }

            // 通常のローカル変数
            auto local = place.local;
            auto val = locals[local];

            // デバッグ: main関数でのコピー操作を確認
            if (cm::debug::debug_mode() && currentMIRFunction &&
                currentMIRFunction->name == "main" && local <= 2) {
                if (val) {
                    if (llvm::isa<llvm::Function>(val)) {
                        auto func = llvm::cast<llvm::Function>(val);
                        debug_msg("MIR", "Copying function '" + func->getName().str() +
                                             "' from local " + std::to_string(local));
                    } else {
                        debug_msg("MIR", "Copying non-function from local " +
                                             std::to_string(local) + " (type: " +
                                             std::to_string(val->getType()->getTypeID()) + ")");
                    }
                } else {
                    debug_msg("MIR",
                              "Local " + std::to_string(local) + " is null when trying to copy!");
                }
            }

            if (val && llvm::isa<llvm::AllocaInst>(val)) {
                // アロケーションの場合
                auto allocaInst = llvm::cast<llvm::AllocaInst>(val);
                auto allocatedType = allocaInst->getAllocatedType();

                // 構造体型の場合はポインタをそのまま返す（値渡しではなくポインタ渡し）
                if (allocatedType->isStructTy()) {
                    return val;
                }

                // プリミティブ借用selfの場合: allocatedTypeがポインタでもMIR型がprimitiveなら
                // ポインタをロードした後、そのポインタからプリミティブ値をロード
                // 注意: impl メソッド内でのみ適用（通常のポインタ変数を壊さないため）
                bool isPrimitiveImplMethod =
                    currentMIRFunction &&
                    (currentMIRFunction->name.find("__") != std::string::npos);

                if (isPrimitiveImplMethod && allocatedType->isPointerTy() && currentMIRFunction &&
                    local < currentMIRFunction->locals.size()) {
                    auto& localInfo = currentMIRFunction->locals[local];
                    // selfローカル（最初の引数ローカル）かどうかをチェック
                    bool isSelfLocal = !currentMIRFunction->arg_locals.empty() &&
                                       local == currentMIRFunction->arg_locals[0];
                    // localNameが"self"の場合もチェック
                    if (!isSelfLocal && localInfo.name == "self") {
                        isSelfLocal = true;
                    }

                    if (isSelfLocal) {
                        // プリミティブ型のimplメソッドか判定
                        // 関数名が int__xxx, long__xxx 等の形式
                        const std::string& funcName = currentMIRFunction->name;
                        size_t dunderPos = funcName.find("__");
                        if (dunderPos != std::string::npos) {
                            std::string typeName = funcName.substr(0, dunderPos);
                            bool isPrimitiveType = typeName == "int" || typeName == "uint" ||
                                                   typeName == "long" || typeName == "ulong" ||
                                                   typeName == "short" || typeName == "ushort" ||
                                                   typeName == "float" || typeName == "double" ||
                                                   typeName == "bool" || typeName == "char";

                            if (isPrimitiveType) {
                                // まずポインタをロード
                                auto ptrVal =
                                    builder->CreateLoad(allocatedType, val, "self_ptr_load");
                                // 次にプリミティブ型を決定してロード
                                llvm::Type* elemType = nullptr;
                                if (typeName == "int" || typeName == "uint") {
                                    elemType = ctx.getI32Type();
                                } else if (typeName == "long" || typeName == "ulong") {
                                    elemType = ctx.getI64Type();
                                } else if (typeName == "short" || typeName == "ushort") {
                                    elemType = ctx.getI16Type();
                                } else if (typeName == "float") {
                                    elemType = ctx.getF32Type();
                                } else if (typeName == "double") {
                                    elemType = ctx.getF64Type();
                                } else if (typeName == "bool" || typeName == "char") {
                                    elemType = ctx.getI8Type();
                                }
                                if (elemType) {
                                    return builder->CreateLoad(elemType, ptrVal,
                                                               "borrowed_self_prim_load");
                                }
                            }
                        }
                    }
                }

                // スカラー型の場合はロード
                // BUG修正(v0.14.2): asm入出力で参照される変数のみvolatile loadで最適化を防止
                auto* loadInst = builder->CreateLoad(allocatedType, val, "load");
                if (allocatedLocals.count(local) > 0 && asmReferencedLocals.count(local) > 0) {
                    // asm参照変数: volatile loadで最適化を抑制
                    if (auto* li = llvm::dyn_cast<llvm::LoadInst>(loadInst)) {
                        li->setVolatile(true);
                    }
                }
                return loadInst;
            }
            // static変数（GlobalVariable）の場合もロードが必要
            if (val && llvm::isa<llvm::GlobalVariable>(val)) {
                auto globalVar = llvm::cast<llvm::GlobalVariable>(val);
                auto valueType = globalVar->getValueType();
                return builder->CreateLoad(valueType, val, "static_load");
            }
            // プリミティブ型借用selfの処理:
            // 引数（Argument）でポインタ型で、MIRローカル型がプリミティブの場合
            // ポインタから値をロードして返す
            if (val && llvm::isa<llvm::Argument>(val) && val->getType()->isPointerTy()) {
                if (currentMIRFunction && local < currentMIRFunction->locals.size()) {
                    auto& localInfo = currentMIRFunction->locals[local];
                    if (localInfo.type) {
                        hir::TypePtr elemType = localInfo.type;
                        // Pointer<Primitive>型の場合はelement_typeを取得
                        if (localInfo.type->kind == hir::TypeKind::Pointer &&
                            localInfo.type->element_type) {
                            elemType = localInfo.type->element_type;
                        }
                        // Pointer型だがelement_typeがない場合（借用selfのMIR表現）
                        // 関数の戻り値型を使用してプリミティブ型を判定
                        else if (localInfo.type->kind == hir::TypeKind::Pointer &&
                                 !localInfo.type->element_type) {
                            // 関数の戻り値型を取得（return_localの型を使用）
                            if (currentMIRFunction && currentMIRFunction->return_local <
                                                          currentMIRFunction->locals.size()) {
                                auto& retLocal =
                                    currentMIRFunction->locals[currentMIRFunction->return_local];
                                if (retLocal.type &&
                                    retLocal.type->kind != hir::TypeKind::Pointer) {
                                    elemType = retLocal.type;
                                }
                            }
                        }
                        auto typeKind = elemType->kind;

                        // ローカル変数自体がポインタ型の場合はロードをスキップ
                        // ポインタを格納するlocal_2のような変数はポインタ値をそのまま格納すべき
                        if (localInfo.type->kind == hir::TypeKind::Pointer) {
                            // ポインタ型のローカル変数へはポインタ値をそのまま返す
                            return val;
                        }

                        // プリミティブ型または構造体型の場合はロード
                        bool isPrimitive =
                            typeKind == hir::TypeKind::Int || typeKind == hir::TypeKind::UInt ||
                            typeKind == hir::TypeKind::Long || typeKind == hir::TypeKind::ULong ||
                            typeKind == hir::TypeKind::Short || typeKind == hir::TypeKind::UShort ||
                            typeKind == hir::TypeKind::Float || typeKind == hir::TypeKind::Double ||
                            typeKind == hir::TypeKind::Bool || typeKind == hir::TypeKind::Char;
                        // 構造体型/ジェネリック型もロードが必要
                        bool isStruct =
                            typeKind == hir::TypeKind::Struct || typeKind == hir::TypeKind::Generic;
                        if (isPrimitive || isStruct) {
                            auto loadType = convertType(elemType);
                            return builder->CreateLoad(
                                loadType, val,
                                isPrimitive ? "borrowed_prim_load" : "borrowed_struct_load");
                        }
                    }
                }
            }
            return val;
        }
        case mir::MirOperand::Constant: {
            auto& constant = std::get<mir::MirConstant>(operand.data);
            return convertConstant(constant);
        }
        case mir::MirOperand::FunctionRef: {
            // 関数参照: 関数ポインタとして使える値を返す
            const std::string& funcName = std::get<std::string>(operand.data);
            // まずfunctionsマップから検索
            auto it = functions.find(funcName);
            if (it != functions.end() && it->second) {
                return it->second;
            }
            // 見つからない場合はモジュールから検索
            auto func = module->getFunction(funcName);
            if (func) {
                // 関数を値として使うために、関数のアドレスをポインタとして返す
                // LLVM 14+では、Function*自体が値として使える
                return func;
            }
            // 関数が見つからない場合はnull
            cm::debug::codegen::log(cm::debug::codegen::Id::LLVMError,
                                    "Function not found for FunctionRef: " + funcName,
                                    cm::debug::Level::Warn);
            return nullptr;
        }
        default:
            return nullptr;
    }
}

// Place変換（アドレス取得）
llvm::Value* MIRToLLVM::convertPlaceToAddress(const mir::MirPlace& place) {
    auto addr = locals[place.local];

    // 現在の型を追跡（ネストしたフィールドアクセス用）
    hir::TypePtr currentType = nullptr;
    if (currentMIRFunction && place.local < currentMIRFunction->locals.size()) {
        currentType = currentMIRFunction->locals[place.local].type;
    }

    // 投影処理
    for (size_t projIdx = 0; projIdx < place.projections.size(); ++projIdx) {
        const auto& proj = place.projections[projIdx];
        switch (proj.kind) {
            case mir::ProjectionKind::Field: {
                // 構造体フィールドアクセス
                if (!addr) {
                    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMError,
                                            "Field projection on null address",
                                            cm::debug::Level::Error);
                    return nullptr;
                }

                // 現在の型から構造体型を取得
                llvm::Type* structType = nullptr;
                std::string structName;

                // Generic型の場合も構造体として扱う（モノモーフィック化後の型）
                // ポインタ型の場合はelement_typeを使用
                hir::TypePtr targetStructType = currentType;
                if (currentType && currentType->kind == hir::TypeKind::Pointer &&
                    currentType->element_type) {
                    targetStructType = currentType->element_type;
                }
                if (targetStructType && (targetStructType->kind == hir::TypeKind::Struct ||
                                         targetStructType->kind == hir::TypeKind::Generic)) {
                    structName = targetStructType->name;

                    // 受け手構造体の参照キーはモノモーフ化のキー産生と同じ正準関数で構築する（mono-flat-name-elimination②）。
                    // 従来は手組みのフラット連結でネスト・特殊化引数の乖離があり、フィールド投影の構造体解決が外れていた
                    if (!targetStructType->type_args.empty() &&
                        structName.find("__") == std::string::npos &&
                        structName.find('$') == std::string::npos) {
                        std::string base = structName;
                        auto lt = base.find('<');
                        if (lt != std::string::npos) {
                            base = base.substr(0, lt);
                        }
                        structName = cm::ast::typekey::struct_key_from_tree(
                            base, targetStructType->type_args);
                    }

                    auto it = structTypes.find(structName);
                    if (it != structTypes.end()) {
                        structType = it->second;
                    }
                }
                // フォールバック: 関数名から構造体名を推論
                // Container__int__get → Container__int
                if (!structType && currentMIRFunction) {
                    const auto& funcName = currentMIRFunction->name;
                    // 最後の__を見つけて、それより前の部分を構造体名として使う
                    size_t lastDunder = funcName.rfind("__");
                    if (lastDunder != std::string::npos && lastDunder > 0) {
                        std::string inferredStruct = funcName.substr(0, lastDunder);
                        auto it = structTypes.find(inferredStruct);
                        if (it != structTypes.end()) {
                            structType = it->second;
                            structName = inferredStruct;
                        }
                    }
                }

                if (!structType) {
                    // 型情報が取得できない場合は、addrの型から推測
                    if (auto allocaInst = llvm::dyn_cast<llvm::AllocaInst>(addr)) {
                        structType = allocaInst->getAllocatedType();
                    } else if (auto loadInst = llvm::dyn_cast<llvm::LoadInst>(addr)) {
                        (void)loadInst;  // 未使用警告を抑制
                        // LoadInst（デリファレンス後）の場合
                        // Deref後は currentType が構造体型になっているはず
                        if (currentType && currentType->kind == hir::TypeKind::Struct) {
                            auto it = structTypes.find(currentType->name);
                            if (it != structTypes.end()) {
                                structType = it->second;
                            }
                        }
                    } else if (auto gepInst = llvm::dyn_cast<llvm::GetElementPtrInst>(addr)) {
                        // GEPの結果型から構造体型を取得
                        // opaque pointerモードでは getSourceElementType と indices から推測
                        auto srcElemType = gepInst->getSourceElementType();
                        if (srcElemType && srcElemType->isArrayTy()) {
                            // 配列からのGEPの場合、要素型を取得
                            structType = srcElemType->getArrayElementType();
                        } else if (srcElemType && srcElemType->isStructTy()) {
                            // 構造体からのGEPの場合、最後のインデックスでフィールド型を取得
                            auto structTy = llvm::cast<llvm::StructType>(srcElemType);
                            // フィールド型を取得（最後のインデックスを使用）
                            if (gepInst->getNumIndices() >= 2) {
                                if (auto constIdx = llvm::dyn_cast<llvm::ConstantInt>(
                                        gepInst->getOperand(gepInst->getNumIndices()))) {
                                    auto fieldIdx = constIdx->getZExtValue();
                                    if (fieldIdx < structTy->getNumElements()) {
                                        structType = structTy->getElementType(fieldIdx);
                                    }
                                }
                            }
                        } else {
                            structType = srcElemType;
                        }
                    }
                }

                if (!structType || !structType->isStructTy()) {
                    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMError,
                                            "Cannot determine struct type for field access",
                                            cm::debug::Level::Error);
                    return nullptr;
                }

                // LLVM 14: typed pointers require matching pointer types for GEP
                // If addr is i8* but we need StructType*, bitcast first
#if LLVM_VERSION_MAJOR < 15
                auto structPtrType = llvm::PointerType::get(structType, 0);
                if (addr->getType() != structPtrType) {
                    addr = builder->CreateBitCast(addr, structPtrType, "struct_ptr_cast");
                }
#endif

                std::vector<llvm::Value*> indices;
                indices.push_back(llvm::ConstantInt::get(ctx.getI32Type(), 0));  // 構造体ベース
                indices.push_back(llvm::ConstantInt::get(ctx.getI32Type(),
                                                         proj.field_id));  // フィールドインデックス

                addr = builder->CreateGEP(structType, addr, indices, "field_ptr");

                // 次のプロジェクションのために型を更新
                // structNameは関数名から推論された可能性があるので、それを使用
                if (!structName.empty()) {
                    auto struct_it = structDefs.find(structName);
                    if (struct_it != structDefs.end() &&
                        proj.field_id < struct_it->second->fields.size()) {
                        currentType = struct_it->second->fields[proj.field_id].type;
                    }
                }
                break;
            }
            case mir::ProjectionKind::Index: {
                // 配列インデックスアクセス（多次元配列フラット化対応）
                if (!addr) {
                    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMError,
                                            "Index projection on null address",
                                            cm::debug::Level::Error);
                    return nullptr;
                }

                // スライス型（可変長Array）へのIndexプロジェクション: CmSliceヘッダ経由で要素アドレスを計算する（B4）。
                // 従来は固定長配列と同じフラットGEPに落ち、ヘッダポインタのスロットを要素列として誤読していた。
                // 要素が内側スライス（多次元）の場合はインライン格納表現が異なるため従来経路に委ねる
                {
                    const bool cur_is_slice =
                        currentType && currentType->kind == hir::TypeKind::Array &&
                        !currentType->array_size.has_value() &&
                        (currentType->dimensions.empty() || currentType->dimensions[0] == 0);
                    // 内側「スライス」要素のみインラインヘッダ格納として従来経路へ委ねる。
                    // 固定長配列要素はN×要素ストライドのインラインblobであり、ヘッダ経由のGEPで正しく届く（Y6）
                    const bool elem_is_inline_slice =
                        cur_is_slice && currentType->element_type &&
                        currentType->element_type->kind == hir::TypeKind::Array &&
                        !currentType->element_type->array_size.has_value();
                    if (cur_is_slice && !elem_is_inline_slice) {
                        // 添字値を取得（alloca格納の場合はロードしてi64へ拡張）
                        llvm::Value* sliceIndexVal = nullptr;
                        auto slice_idx_it = locals.find(proj.index_local);
                        if (slice_idx_it != locals.end()) {
                            sliceIndexVal = slice_idx_it->second;
                            if (allocatedLocals.count(proj.index_local)) {
                                llvm::Type* idxType = ctx.getI64Type();
                                if (currentMIRFunction &&
                                    proj.index_local < currentMIRFunction->locals.size()) {
                                    auto& idxLocal = currentMIRFunction->locals[proj.index_local];
                                    idxType = convertType(idxLocal.type);
                                }
                                sliceIndexVal =
                                    builder->CreateLoad(idxType, sliceIndexVal, "idx_load");
                                if (idxType->isIntegerTy(32)) {
                                    sliceIndexVal = builder->CreateSExt(
                                        sliceIndexVal, ctx.getI64Type(), "idx_ext");
                                }
                            }
                        }
                        if (!sliceIndexVal) {
                            cm::debug::codegen::log(cm::debug::codegen::Id::LLVMError,
                                                    "Cannot get index value for slice access",
                                                    cm::debug::Level::Error);
                            return nullptr;
                        }

                        // addrはCmSlice*を格納するスロット（alloca/フィールドGEP）を指すためまずヘッダポインタをロードする。
                        // Deref直後等で既にヘッダポインタ値そのものの場合は再ロードしない
                        llvm::Value* hdrPtr = addr;
                        if (!llvm::isa<llvm::LoadInst>(addr)) {
                            hdrPtr = builder->CreateLoad(ctx.getPtrType(), addr, "slice_hdr");
                        }

                        // CmSliceの先頭フィールドdataを読み、要素型ストライドでGEPする
                        llvm::Value* dataPtr =
                            builder->CreateLoad(ctx.getPtrType(), hdrPtr, "slice_data");
                        llvm::Type* sliceElemType = currentType->element_type
                                                        ? convertType(currentType->element_type)
                                                        : ctx.getI32Type();
                        addr = builder->CreateGEP(sliceElemType, dataPtr, sliceIndexVal,
                                                  "slice_elem_ptr");
                        currentType = currentType->element_type;
                        break;
                    }
                }

                // ポインタ型の場合は単純なポインタ演算（フラット化不要）
                // Deref後のLoadInst結果（ポインタ値）へのインデックスアクセスも含む
                bool isPointerIndexing =
                    (currentType && currentType->kind == hir::TypeKind::Pointer);

                // Deref後（currentTypeはポインタの要素型）でもaddrがLoadInst結果の場合はポインタへのインデックスアクセスとして扱う
                if (!isPointerIndexing && addr && llvm::isa<llvm::LoadInst>(addr)) {
                    // LoadInstの結果（ポインタ値）へのインデックスアクセス
                    isPointerIndexing = true;
                }

                // Deref後にaddrがArgument（ポインタ引数、needsLoad=falseでDerefスキップ）の場合
                // currentTypeはelement_type（int等）だがaddrはポインタ値 → ポインタIndexing
                if (!isPointerIndexing && addr && llvm::isa<llvm::Argument>(addr) && projIdx > 0 &&
                    place.projections[projIdx - 1].kind == mir::ProjectionKind::Deref) {
                    isPointerIndexing = true;
                }

                if (isPointerIndexing) {
                    // インデックス値を取得
                    llvm::Value* indexVal = nullptr;
                    auto idx_it = locals.find(proj.index_local);
                    if (idx_it != locals.end()) {
                        indexVal = idx_it->second;
                        if (allocatedLocals.count(proj.index_local)) {
                            llvm::Type* idxType = ctx.getI64Type();
                            if (currentMIRFunction &&
                                proj.index_local < currentMIRFunction->locals.size()) {
                                auto& idxLocal = currentMIRFunction->locals[proj.index_local];
                                idxType = convertType(idxLocal.type);
                            }
                            indexVal = builder->CreateLoad(idxType, indexVal, "idx_load");
                            if (idxType->isIntegerTy(32)) {
                                indexVal =
                                    builder->CreateSExt(indexVal, ctx.getI64Type(), "idx_ext");
                            }
                        }
                    }

                    if (!indexVal) {
                        cm::debug::codegen::log(cm::debug::codegen::Id::LLVMError,
                                                "Cannot get index value for pointer access",
                                                cm::debug::Level::Error);
                        return nullptr;
                    }

                    // ポインタが指す要素の型を取得
                    llvm::Type* elemType = ctx.getI32Type();  // デフォルト
                    if (currentType && currentType->kind == hir::TypeKind::Pointer &&
                        currentType->element_type) {
                        // 通常のポインタ型: element_typeを使用
                        elemType = convertType(currentType->element_type);
                    } else if (currentType && llvm::isa<llvm::LoadInst>(addr)) {
                        // Deref後（LoadInst結果へのインデックスアクセス）。
                        // pointeeが固定長配列の場合、Indexは「配列内の要素」への添字であり、
                        // 配列型ストライド（N×要素）でGEPすると要素ずれになる（Y6: rows[0][1]=vが隣要素へ書かれていた）。
                        // 要素型ストライドでGEPする
                        if (currentType->kind == hir::TypeKind::Array &&
                            currentType->array_size.has_value() && currentType->element_type) {
                            elemType = convertType(currentType->element_type);
                        } else {
                            // currentType自体が要素型
                            elemType = convertType(currentType);
                        }
                    }

                    // addrがポインタ変数を格納している場合、まずポインタ値をロード
                    // これはField
                    // projection後（構造体のポインタフィールドへのGEP）にも適用される
                    llvm::Value* ptrVal = addr;
                    bool needsLoad = false;

                    if (auto allocaInst = llvm::dyn_cast<llvm::AllocaInst>(addr)) {
                        auto allocType = allocaInst->getAllocatedType();
                        // allocaがポインタ型を格納している場合
                        if (allocType->isPointerTy() || allocType == ctx.getPtrType()) {
                            needsLoad = true;
                        }
                    } else if (auto gepInst = llvm::dyn_cast<llvm::GetElementPtrInst>(addr)) {
                        // GEP結果がポインタフィールドへのアドレスの場合
                        // currentTypeがPointerなら、このアドレスからポインタ値をロード
                        (void)gepInst;  // 未使用警告を抑制
                        // currentType->kind == Pointer はポインタ型フィールドを示す
                        needsLoad = true;
                    } else if (llvm::isa<llvm::LoadInst>(addr)) {
                        // LoadInst結果（Deref後）はすでにロード済みなので再ロード不要
                        needsLoad = false;
                    } else if (llvm::isa<llvm::Argument>(addr)) {
                        // ポインタ引数（Deref後）はすでにポインタ値なので再ロード不要
                        needsLoad = false;
                    } else {
                        // その他の場合（currentTypeがポインタ型なら）
                        needsLoad = true;
                    }

                    if (needsLoad) {
                        ptrVal = builder->CreateLoad(ctx.getPtrType(), addr, "ptr_load");
                    }

                    // ポインタ + オフセット
                    addr = builder->CreateGEP(elemType, ptrVal, indexVal, "ptr_elem");

                    // 型情報を更新
                    currentType = currentType->element_type;
                    break;
                }

                // 連続するIndexプロジェクションを収集（多次元配列のフラット化）
                std::vector<llvm::Value*> indexValues;
                std::vector<uint64_t> dimensions;
                hir::TypePtr arrayTypeInfo = currentType;

                // 現在のインデックスを取得
                llvm::Value* indexVal = nullptr;
                auto idx_it = locals.find(proj.index_local);
                if (idx_it != locals.end()) {
                    indexVal = idx_it->second;
                    if (allocatedLocals.count(proj.index_local)) {
                        llvm::Type* idxType = ctx.getI64Type();
                        if (currentMIRFunction &&
                            proj.index_local < currentMIRFunction->locals.size()) {
                            auto& idxLocal = currentMIRFunction->locals[proj.index_local];
                            idxType = convertType(idxLocal.type);
                        }
                        indexVal = builder->CreateLoad(idxType, indexVal, "idx_load");
                        if (idxType->isIntegerTy(32)) {
                            indexVal = builder->CreateSExt(indexVal, ctx.getI64Type(), "idx_ext");
                        }
                    }
                }

                if (!indexVal) {
                    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMError,
                                            "Cannot get index value for array access",
                                            cm::debug::Level::Error);
                    return nullptr;
                }

                indexValues.push_back(indexVal);

                // 配列の次元サイズを収集
                if (arrayTypeInfo && arrayTypeInfo->kind == hir::TypeKind::Array) {
                    if (arrayTypeInfo->array_size.has_value()) {
                        dimensions.push_back(*arrayTypeInfo->array_size);
                    }
                    arrayTypeInfo = arrayTypeInfo->element_type;
                }

                // 連続するIndexプロジェクションを先読み
                size_t consumedProjections = 0;
                for (size_t nextIdx = projIdx + 1; nextIdx < place.projections.size(); ++nextIdx) {
                    const auto& nextProj = place.projections[nextIdx];
                    if (nextProj.kind != mir::ProjectionKind::Index)
                        break;
                    if (!arrayTypeInfo || arrayTypeInfo->kind != hir::TypeKind::Array)
                        break;

                    // 次のインデックス値を取得
                    llvm::Value* nextIndexVal = nullptr;
                    auto next_idx_it = locals.find(nextProj.index_local);
                    if (next_idx_it != locals.end()) {
                        nextIndexVal = next_idx_it->second;
                        if (allocatedLocals.count(nextProj.index_local)) {
                            llvm::Type* idxType = ctx.getI64Type();
                            if (currentMIRFunction &&
                                nextProj.index_local < currentMIRFunction->locals.size()) {
                                auto& idxLocal = currentMIRFunction->locals[nextProj.index_local];
                                idxType = convertType(idxLocal.type);
                            }
                            nextIndexVal = builder->CreateLoad(idxType, nextIndexVal, "idx_load");
                            if (idxType->isIntegerTy(32)) {
                                nextIndexVal =
                                    builder->CreateSExt(nextIndexVal, ctx.getI64Type(), "idx_ext");
                            }
                        }
                    }

                    if (!nextIndexVal)
                        break;

                    indexValues.push_back(nextIndexVal);
                    if (arrayTypeInfo->array_size.has_value()) {
                        dimensions.push_back(*arrayTypeInfo->array_size);
                    }
                    arrayTypeInfo = arrayTypeInfo->element_type;
                    consumedProjections++;
                }

                // 線形インデックスを計算: linear = i0*D1*D2*... + i1*D2*D3*... + ... + iN
                llvm::Value* linearIndex = nullptr;
                if (indexValues.size() > 1 && dimensions.size() == indexValues.size()) {
                    // 多次元配列の場合、線形インデックスを計算
                    linearIndex = llvm::ConstantInt::get(ctx.getI64Type(), 0);

                    for (size_t i = 0; i < indexValues.size(); ++i) {
                        llvm::Value* idx = indexValues[i];

                        // 後続の全次元のサイズを乗算
                        uint64_t stride = 1;
                        for (size_t j = i + 1; j < dimensions.size(); ++j) {
                            stride *= dimensions[j];
                        }

                        if (stride > 1) {
                            llvm::Value* strideVal =
                                llvm::ConstantInt::get(ctx.getI64Type(), stride);
                            idx = builder->CreateMul(idx, strideVal, "stride_mul");
                        }

                        linearIndex = builder->CreateAdd(linearIndex, idx, "linear_add");
                    }
                } else {
                    // 1次元配列の場合
                    linearIndex = indexValues[0];
                }

                // 消費したプロジェクションをスキップ
                projIdx += consumedProjections;

                // 最終的な要素型を取得
                llvm::Type* elemType = nullptr;
                if (arrayTypeInfo) {
                    elemType = convertType(arrayTypeInfo);
                } else if (currentType && currentType->kind == hir::TypeKind::Array) {
                    // フォールバック: フラット化後の要素型を計算
                    hir::TypePtr elemTypeInfo = currentType;
                    for (size_t i = 0; i < indexValues.size() && elemTypeInfo; ++i) {
                        if (elemTypeInfo->kind == hir::TypeKind::Array) {
                            elemTypeInfo = elemTypeInfo->element_type;
                        }
                    }
                    if (elemTypeInfo) {
                        elemType = convertType(elemTypeInfo);
                    }
                }

                if (!elemType) {
                    // 最終フォールバック: allocaから型を推測
                    if (auto allocaInst = llvm::dyn_cast<llvm::AllocaInst>(addr)) {
                        auto allocType = allocaInst->getAllocatedType();
                        if (allocType->isArrayTy()) {
                            elemType = allocType;
                            while (elemType->isArrayTy()) {
                                elemType = elemType->getArrayElementType();
                            }
                        }
                    }
                }

                if (!elemType) {
                    elemType = ctx.getI32Type();  // デフォルト
                }

                // Clang準拠: 多次元GEPを生成
                // フラット化せず、配列の配列として複数インデックスでアクセス
                // gep inbounds [300 x [300 x i32]], ptr %arr, i64 0, i64 %i, i64 %j
                if (auto allocaInst = llvm::dyn_cast<llvm::AllocaInst>(addr)) {
                    auto allocType = allocaInst->getAllocatedType();
                    if (allocType->isArrayTy() && indexValues.size() > 0) {
                        // 多次元GEP用インデックスを構築
                        std::vector<llvm::Value*> gepIndices;
                        // 最初のインデックスは常に0（ポインタから配列への変換）
                        gepIndices.push_back(llvm::ConstantInt::get(ctx.getI64Type(), 0));
                        // 各次元のインデックスを追加
                        for (auto* idx : indexValues) {
                            gepIndices.push_back(idx);
                        }
                        // 多次元GEPを生成
                        addr = builder->CreateInBoundsGEP(allocType, addr, gepIndices, "elem_ptr");
                        currentType = arrayTypeInfo;
                        break;
                    }
                }

                // フォールバック: 多次元GEPが使えない場合はフラット化
                llvm::Value* basePtr = addr;
                if (auto allocaInst = llvm::dyn_cast<llvm::AllocaInst>(addr)) {
                    auto allocType = allocaInst->getAllocatedType();
                    if (allocType->isArrayTy()) {
                        // 多次元配列の場合、要素型へのポインタに変換
                        std::vector<llvm::Value*> zeroIndices;
                        zeroIndices.push_back(llvm::ConstantInt::get(ctx.getI64Type(), 0));
                        llvm::Type* currentArrayType = allocType;
                        while (currentArrayType->isArrayTy()) {
                            zeroIndices.push_back(llvm::ConstantInt::get(ctx.getI64Type(), 0));
                            currentArrayType = currentArrayType->getArrayElementType();
                        }
                        basePtr = builder->CreateGEP(allocType, addr, zeroIndices, "flat_base");
                    }
                }
                addr = builder->CreateGEP(elemType, basePtr, linearIndex, "flat_elem_ptr");

                // 型情報を更新
                currentType = arrayTypeInfo;
                break;
            }
            case mir::ProjectionKind::Deref: {
                // interfaceポインタ（fat pointer値）のDerefは恒等:
                // fat自体が実装オブジェクトへの参照なのでロード不要
                if (currentType && currentType->kind == hir::TypeKind::Pointer &&
                    currentType->element_type &&
                    currentType->element_type->kind == hir::TypeKind::Struct &&
                    isInterfaceType(currentType->element_type->name)) {
                    currentType = currentType->element_type;
                    // SSA形式（fat値がそのまま束縛されている）場合は一時スピルしてアドレス化する
                    if (addr && !addr->getType()->isPointerTy()) {
                        auto fatTy = getInterfaceFatPtrType(currentType->name);
                        auto spill = builder->CreateAlloca(fatTy, nullptr, "iface_spill");
                        builder->CreateStore(addr, spill);
                        addr = spill;
                    }
                    break;
                }

                // デリファレンス：ポインタ変数から実際のポインタ値をロード
                // LLVM 14+では CreateLoad の第1引数はロードする値の型（pointee type）を指定
                // ポインタからポインタをロードするため、ポインタ型そのものを指定

                // 重要: 借用selfの場合、addrは既にポインタ値（関数引数として渡された）
                // allocaに格納されていない場合は、追加のロードは不要
                bool needsLoad = true;

                // addrがllvm::Argumentの場合は直接ポインタ値として使用（引数はlocals[arg_local] = &argで直接格納されている）
                if (llvm::isa<llvm::Argument>(addr)) {
                    needsLoad = false;
                    // addrはすでにポインタ値なのでそのまま使用
                }

                if (needsLoad) {
                    llvm::Type* ptrType = ctx.getPtrType();  // ptr型（opaque pointer）
                    addr = builder->CreateLoad(ptrType, addr);
                }

                // 次のプロジェクションのために型を更新
                if (currentType && currentType->kind == hir::TypeKind::Pointer &&
                    currentType->element_type) {
                    currentType = currentType->element_type;
                }
                break;
            }
        }
    }

    // プロジェクションがある場合はGEPの結果をそのまま返す
    if (!place.projections.empty() && addr) {
        return addr;
    }

    // allocaインストラクションの場合はそのまま返す
    if (addr && llvm::isa<llvm::AllocaInst>(addr)) {
        return addr;
    }

    // ポインタ型の場合（関数引数など）はそのまま返す
    if (addr && addr->getType()->isPointerTy()) {
        return addr;
    }

    // それ以外はnullptr（SSA形式で直接値を使う）
    return nullptr;
}

// インターフェース関連の実装は interface.cpp に移動

}  // namespace cm::codegen::llvm_backend
