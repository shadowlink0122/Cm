/// @file call.cpp
/// @brief Callターミネータ変換の入口（関数名解決とpanic/print/組み込み呼び出しの特別処理）

#include "internal/codegen/llvm/core/mir_to_llvm.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace cm::codegen::llvm_backend {

/// Callターミネータの変換本体（分離元のswitch脱出用breakはreturnに置換済み）
void MIRToLLVM::convertCallTerminator(const mir::MirTerminator::CallData& callData) {
    // 関数名を取得
    std::string funcName;
    bool isIndirectCall = false;          // 関数ポインタ変数からの呼び出し
    llvm::Value* funcPtrValue = nullptr;  // 関数ポインタ値

    if (callData.func->kind == mir::MirOperand::Constant) {
        auto& constant = std::get<mir::MirConstant>(callData.func->data);
        if (auto* name = std::get_if<std::string>(&constant.value)) {
            funcName = *name;
        }
    } else if (callData.func->kind == mir::MirOperand::FunctionRef) {
        funcName = std::get<std::string>(callData.func->data);
    } else if (callData.func->kind == mir::MirOperand::Copy ||
               callData.func->kind == mir::MirOperand::Move) {
        // 関数ポインタ変数からの呼び出し
        isIndirectCall = true;
        funcPtrValue = convertOperand(*callData.func);

        // convertOperandがFunction*を返した場合、それを関数ポインタとして扱う
        if (funcPtrValue && llvm::isa<llvm::Function>(funcPtrValue)) {
            // Function*が直接返された場合は、直接呼び出しとして扱う
            auto func = llvm::cast<llvm::Function>(funcPtrValue);
            funcName = func->getName().str();
            isIndirectCall = false;
            funcPtrValue = nullptr;
        }
    }

    // panic(msg): void __cm_panic(const char*) へ正規化して呼び出す（呼び出し式の型（T等）から誤ったシグネチャで宣言されると
    // wasmでsignature mismatchになる。panicは戻らないため戻り値は使われない）
    if (funcName == "panic") {
        auto panicType = llvm::FunctionType::get(
            ctx.getVoidType(), {llvm::PointerType::get(ctx.getContext(), 0)}, false);
        auto panicFunc = module->getOrInsertFunction("__cm_panic", panicType);
        llvm::Value* msgArg = nullptr;
        if (!callData.args.empty() && callData.args[0]) {
            msgArg = convertOperand(*callData.args[0]);
        }
        if (!msgArg) {
            msgArg = builder->CreateGlobalStringPtr("panic", "panic_msg");
        }
        builder->CreateCall(panicFunc, {msgArg});
        builder->CreateUnreachable();
        return;
    }

    // Print/Format系の特別処理（ヘルパー関数を使用）
    // ============================================================
    if (funcName == "cm_println_format" || funcName == "cm_print_format") {
        bool isNewline = funcName.find("println") != std::string::npos;
        generatePrintFormatCall(callData, isNewline);
        builder->CreateBr(blocks[callData.success]);
        return;
    }

    if (funcName == "cm_format_string") {
        generateFormatStringCall(callData);
        builder->CreateBr(blocks[callData.success]);
        return;
    }

    if (funcName == "__print__" || funcName == "__println__" || funcName == "std::io::print" ||
        funcName == "std::io::println") {
        bool isNewline = funcName.find("println") != std::string::npos;
        generatePrintCall(callData, isNewline);
        if (blocks.find(callData.success) == blocks.end()) {
            std::cerr << "[CODEGEN] CRITICAL: Success block bb" << callData.success
                      << " not found! Creating unreachable.\n"
                      << std::flush;
            builder->CreateUnreachable();
            return;
        }
        builder->CreateBr(blocks[callData.success]);
        return;
    }

    // ============================================================
    // Tagged Union Variant Constructor (v0.13.0)
    // ============================================================
    // Color::RGB(255, 128, 64) のようなvariant constructor呼び出しを検出
    // "::" を含み、通常の関数として登録されていない場合、variant constructorとして処理
    // ただし、モジュール関数（m::calculate等）は通常の関数として処理する
    if (funcName.find("::") != std::string::npos && functions.count(funcName) == 0) {
        // まずMIR関数リストを検索（モジュール関数の可能性）
        bool isMirFunction = false;
        for (const auto& func : currentProgram->functions) {
            if (func->name == funcName) {
                isMirFunction = true;
                break;
            }
        }

        // MIR関数として存在する場合は、通常の関数呼び出しとして続行（このブロックを抜けて、後続の通常関数呼び出し処理へ）
        if (!isMirFunction) {
            size_t colonPos = funcName.find("::");
            std::string enumName = funcName.substr(0, colonPos);
            std::string variantName = funcName.substr(colonPos + 2);

            // v0.13.0: 暫定実装 - variant constructorを検出
            // enum_info_キャッシュが実装されるまでは、未知の関数呼び出しを
            // タグ値として処理し、警告を抑制する
            // TODO: MIRToLLVMにenum_info_マップを追加

            // 現時点では単純にタグ値0を返す（シンプルなenumの場合最初の値）
            // 引数がある場合も無視（Associated dataは後で対応）
            llvm::Value* tagValue = llvm::ConstantInt::get(ctx.getI32Type(), 0);

            if (callData.destination) {
                auto destLocal = callData.destination->local;
                if (allocatedLocals.count(destLocal) > 0 && locals[destLocal]) {
                    builder->CreateStore(tagValue, locals[destLocal]);
                } else {
                    locals[destLocal] = tagValue;
                }
            }

            if (callData.success != mir::INVALID_BLOCK) {
                builder->CreateBr(blocks[callData.success]);
            }
            return;
        }
    }

    // ============================================================
    // 配列スライス呼び出し
    // ============================================================
    if (funcName == "__builtin_array_slice") {
        // 引数: arr, elem_size, arr_len, start, end
        // ランタイム関数: void* __builtin_array_slice(void* arr, i64 elem_size, i64 arr_len, i64 start, i64 end, i64* out_len)

        std::vector<llvm::Value*> args;
        for (const auto& arg : callData.args) {
            args.push_back(convertOperand(*arg));
        }

        // 最初の引数（配列）をポインタに変換
        llvm::Value* arrPtr = args[0];
        if (arrPtr->getType()->isArrayTy()) {
            // 配列をallocaに格納してポインタを取得
            auto arrAlloca = builder->CreateAlloca(arrPtr->getType(), nullptr, "arr_tmp");
            builder->CreateStore(arrPtr, arrAlloca);
            arrPtr = builder->CreateBitCast(arrAlloca, ctx.getPtrType(), "arr_ptr");
        } else if (!arrPtr->getType()->isPointerTy()) {
            arrPtr = builder->CreateIntToPtr(arrPtr, ctx.getPtrType(), "arr_ptr");
        }

        // out_len用のallocaを作成
        auto outLenAlloca = builder->CreateAlloca(ctx.getI64Type(), nullptr, "out_len");
        builder->CreateStore(llvm::ConstantInt::get(ctx.getI64Type(), 0), outLenAlloca);

        // ランタイム関数を取得
        auto sliceFunc = declareExternalFunction("__builtin_array_slice");

        // 引数を整数型に変換（必要な場合）
        std::vector<llvm::Value*> callArgs;
        callArgs.push_back(arrPtr);  // void* arr
        for (size_t i = 1; i < args.size() && i <= 4; ++i) {
            auto arg = args[i];
            if (arg->getType() != ctx.getI64Type()) {
                if (arg->getType()->isIntegerTy()) {
                    arg = builder->CreateSExt(arg, ctx.getI64Type(), "sext");
                }
            }
            callArgs.push_back(arg);
        }
        // out_lenポインタをi8*にキャスト（LLVM 14互換性のため）
        auto outLenCast = builder->CreateBitCast(outLenAlloca, ctx.getPtrType(), "out_len_cast");
        callArgs.push_back(outLenCast);  // i8* out_len

        auto result = builder->CreateCall(sliceFunc, callArgs, "slice_result");

        if (callData.destination) {
            auto destLocal = callData.destination->local;
            if (allocatedLocals.count(destLocal) > 0 && locals[destLocal]) {
                builder->CreateStore(result, locals[destLocal]);
            } else {
                locals[destLocal] = result;
            }
        }

        if (callData.success != mir::INVALID_BLOCK) {
            builder->CreateBr(blocks[callData.success]);
        }
        return;
    }

    // ============================================================
    // 配列 map/filter 呼び出し
    // ============================================================
    if (funcName.find("__builtin_array_") == 0 &&
        (funcName.find("map") != std::string::npos ||
         funcName.find("filter") != std::string::npos ||
         funcName.find("some") != std::string::npos ||
         funcName.find("every") != std::string::npos ||
         funcName.find("findIndex") != std::string::npos ||
         funcName.find("reduce") != std::string::npos ||
         funcName.find("forEach") != std::string::npos)) {
        std::vector<llvm::Value*> args;
        for (const auto& arg : callData.args) {
            args.push_back(convertOperand(*arg));
        }

        // 引数の型を適切に変換
        // 最初の引数（配列）をポインタに変換
        if (!args.empty()) {
            llvm::Value* arrPtr = args[0];

            // LoadInstの場合、ロード元のポインタを使用
            if (auto* loadInst = llvm::dyn_cast<llvm::LoadInst>(arrPtr)) {
                auto ptrOperand = loadInst->getPointerOperand();
                if (auto* allocaInst = llvm::dyn_cast<llvm::AllocaInst>(ptrOperand)) {
                    auto allocatedType = allocaInst->getAllocatedType();
                    // 配列型のallocaの場合、最初の要素へのポインタを取得
                    if (allocatedType->isArrayTy()) {
                        arrPtr = builder->CreateGEP(allocatedType, ptrOperand,
                                                    {llvm::ConstantInt::get(ctx.getI32Type(), 0),
                                                     llvm::ConstantInt::get(ctx.getI32Type(), 0)},
                                                    "array_elem_ptr");
                        // i8*にビットキャスト
                        arrPtr = builder->CreateBitCast(arrPtr, ctx.getPtrType(), "arr_cast");
                    } else if (allocatedType->isPointerTy()) {
                        // ポインタ型のallocaの場合、ロードした値（ポインタ）を使用
                        // arrPtr = loadInst (既にロード済み)
                    }
                }
            } else if (arrPtr->getType()->isPointerTy()) {
                // ポインタ型の場合、配列ポインタかチェック
                if (auto* allocaInst = llvm::dyn_cast<llvm::AllocaInst>(arrPtr)) {
                    auto allocatedType = allocaInst->getAllocatedType();
                    if (allocatedType->isArrayTy()) {
                        // 配列型のallocaの場合、最初の要素へのポインタを取得
                        arrPtr = builder->CreateGEP(allocatedType, arrPtr,
                                                    {llvm::ConstantInt::get(ctx.getI32Type(), 0),
                                                     llvm::ConstantInt::get(ctx.getI32Type(), 0)},
                                                    "array_ptr");
                    }
                }
                // i8*にキャスト
                if (arrPtr->getType() != ctx.getPtrType()) {
                    arrPtr = builder->CreateBitCast(arrPtr, ctx.getPtrType(), "arr_cast");
                }
            } else if (arrPtr->getType()->isArrayTy()) {
                // 配列値の場合、allocaに格納してポインタを取得
                auto arrAlloca = builder->CreateAlloca(arrPtr->getType(), nullptr, "arr_tmp");
                builder->CreateStore(arrPtr, arrAlloca);
                // 最初の要素へのポインタを取得
                arrPtr = builder->CreateGEP(arrPtr->getType(), arrAlloca,
                                            {llvm::ConstantInt::get(ctx.getI32Type(), 0),
                                             llvm::ConstantInt::get(ctx.getI32Type(), 0)},
                                            "array_ptr");
                arrPtr = builder->CreateBitCast(arrPtr, ctx.getPtrType(), "arr_cast");
            }
            args[0] = arrPtr;
        }

        // 2番目の引数（配列長）をi64に変換
        if (args.size() >= 2) {
            auto lengthArg = args[1];
            if (lengthArg->getType() != ctx.getI64Type()) {
                if (lengthArg->getType()->isIntegerTy()) {
                    // 整数型の場合、i64に拡張
                    if (lengthArg->getType()->getIntegerBitWidth() < 64) {
                        lengthArg = builder->CreateSExt(lengthArg, ctx.getI64Type(), "length_i64");
                    } else {
                        lengthArg = builder->CreateTrunc(lengthArg, ctx.getI64Type(), "length_i64");
                    }
                    args[1] = lengthArg;
                }
            }
        }

        // 関数ポインタ引数（3番目）も適切にキャスト
        if (args.size() >= 3) {
            auto funcPtr = args[2];
            if (funcPtr->getType() != ctx.getPtrType()) {
                funcPtr = builder->CreateBitCast(funcPtr, ctx.getPtrType(), "func_cast");
                args[2] = funcPtr;
            }
        }

        // acc64混合幅reduceの初期値（4番目）はi64へ拡張する（i32リテラル初期値との型不一致対策）
        if (funcName.find("reduce_i32_acc64") != std::string::npos && args.size() >= 4) {
            auto initArg = args[3];
            if (initArg->getType()->isIntegerTy() &&
                initArg->getType()->getIntegerBitWidth() < 64) {
                args[3] = builder->CreateSExt(initArg, ctx.getI64Type(), "reduce_init_i64");
            }
        }

        // 高階クロージャ呼び出し（C6）: 可変個のキャプチャ引数を環境ポインタ+サンクへ正規化する
        // （map/filter/reduce/forEach/some/every/findIndexの_closure変種すべて）
        if (funcName.rfind("__builtin_array_", 0) == 0 && funcName.size() > 8 &&
            funcName.compare(funcName.size() - 8, 8, "_closure") == 0) {
            normalizeHofClosureArgs(callData, funcName, args);
        }

        // 関数を呼び出す
        auto func = declareExternalFunction(funcName);
        auto result = builder->CreateCall(func, args);

        if (callData.destination) {
            auto destLocal = callData.destination->local;
            if (allocatedLocals.count(destLocal) > 0 && locals[destLocal]) {
                builder->CreateStore(result, locals[destLocal]);
            } else {
                locals[destLocal] = result;
            }
        }

        if (callData.success != mir::INVALID_BLOCK) {
            builder->CreateBr(blocks[callData.success]);
        }
        return;
    }

    // ============================================================
    // 固定配列比較呼び出し
    // ============================================================
    if (funcName == "cm_array_equal") {
        // 引数: lhs, rhs, lhs_len, rhs_len, elem_size
        // ランタイム関数: bool cm_array_equal(void* lhs, void* rhs, i64 lhs_len, i64 rhs_len, i64 elem_size)

        std::vector<llvm::Value*> args;
        for (const auto& arg : callData.args) {
            args.push_back(convertOperand(*arg));
        }

        // lhsとrhsをポインタに変換
        auto convertToPtr = [&](llvm::Value* val) -> llvm::Value* {
            if (val->getType()->isArrayTy()) {
                auto arrAlloca = builder->CreateAlloca(val->getType(), nullptr, "arr_tmp");
                builder->CreateStore(val, arrAlloca);
                return builder->CreateBitCast(arrAlloca, ctx.getPtrType(), "arr_ptr");
            } else if (!val->getType()->isPointerTy()) {
                return builder->CreateIntToPtr(val, ctx.getPtrType(), "arr_ptr");
            }
            return val;
        };

        llvm::Value* lhsPtr = convertToPtr(args[0]);
        llvm::Value* rhsPtr = convertToPtr(args[1]);

        // 残りの引数を整数型に変換
        std::vector<llvm::Value*> callArgs;
        callArgs.push_back(lhsPtr);
        callArgs.push_back(rhsPtr);
        for (size_t i = 2; i < args.size(); ++i) {
            auto arg = args[i];
            if (arg->getType() != ctx.getI64Type()) {
                if (arg->getType()->isIntegerTy()) {
                    arg = builder->CreateSExt(arg, ctx.getI64Type(), "sext");
                }
            }
            callArgs.push_back(arg);
        }

        auto equalFunc = declareExternalFunction("cm_array_equal");
        auto result = builder->CreateCall(equalFunc, callArgs, "array_eq_result");

        if (callData.destination) {
            auto destLocal = callData.destination->local;
            // bool戻り値（i1）をメモリ格納用（i8）に変換
            llvm::Value* resultToStore = result;
            if (result->getType()->isIntegerTy(1)) {
                resultToStore = builder->CreateZExt(result, ctx.getI8Type(), "bool_ext");
            }
            if (allocatedLocals.count(destLocal) > 0 && locals[destLocal]) {
                builder->CreateStore(resultToStore, locals[destLocal]);
            } else {
                locals[destLocal] = resultToStore;
            }
        }

        if (callData.success != mir::INVALID_BLOCK) {
            builder->CreateBr(blocks[callData.success]);
        }
        return;
    }

    // ============================================================
    // 通常の関数呼び出し
    // ============================================================

    std::vector<llvm::Value*> args;
    for (const auto& arg : callData.args) {
        args.push_back(convertOperand(*arg));
    }

    // インターフェース/プリミティブimplのメソッド呼び出しディスパッチ（処理された場合はここで完了）
    if (generateMethodCallDispatch(callData, args)) {
        return;
    }

    // 通常の直接/間接関数呼び出し
    generateRegularCall(callData, funcName, isIndirectCall, funcPtrValue, args);
}

}  // namespace cm::codegen::llvm_backend
