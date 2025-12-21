/// @file llvm_utils.cpp
/// @brief ユーティリティ関数（外部関数宣言、パニック生成、型情報取得）

#include "mir_to_llvm.hpp"

namespace cm::codegen::llvm_backend {

// 外部関数宣言
llvm::Function* MIRToLLVM::declareExternalFunction(const std::string& name) {
    if (name == "__print__" || name == "__println__") {
        auto printfType = llvm::FunctionType::get(ctx.getI32Type(), {ctx.getPtrType()}, true);
        auto func = module->getOrInsertFunction("printf", printfType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "puts") {
        auto putsType = llvm::FunctionType::get(ctx.getI32Type(), {ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction("puts", putsType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // ランタイム関数の宣言
    else if (name == "cm_println_int" || name == "cm_print_int") {
        auto funcType = llvm::FunctionType::get(ctx.getVoidType(), {ctx.getI32Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "cm_println_string" || name == "cm_print_string") {
        auto funcType = llvm::FunctionType::get(ctx.getVoidType(), {ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "cm_println_double" || name == "cm_print_double") {
        auto funcType = llvm::FunctionType::get(ctx.getVoidType(), {ctx.getF64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "cm_println_uint" || name == "cm_print_uint") {
        auto funcType = llvm::FunctionType::get(ctx.getVoidType(), {ctx.getI32Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "cm_println_bool" || name == "cm_print_bool") {
        auto funcType = llvm::FunctionType::get(ctx.getVoidType(), {ctx.getI8Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "cm_println_char" || name == "cm_print_char") {
        auto funcType = llvm::FunctionType::get(ctx.getVoidType(), {ctx.getI8Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // 型変換関数
    else if (name == "cm_int_to_string" || name == "cm_uint_to_string") {
        auto funcType = llvm::FunctionType::get(ctx.getPtrType(), {ctx.getI32Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "cm_char_to_string") {
        auto funcType = llvm::FunctionType::get(ctx.getPtrType(), {ctx.getI8Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "cm_bool_to_string") {
        auto funcType = llvm::FunctionType::get(ctx.getPtrType(), {ctx.getI8Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "cm_double_to_string") {
        auto funcType = llvm::FunctionType::get(ctx.getPtrType(), {ctx.getF64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // 文字列操作関数
    else if (name == "cm_string_concat") {
        auto funcType =
            llvm::FunctionType::get(ctx.getPtrType(), {ctx.getPtrType(), ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // 文字列長取得関数
    else if (name == "__builtin_string_len" || name == "cm_strlen") {
        auto funcType = llvm::FunctionType::get(ctx.getI64Type(), {ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // 文字列charAt
    else if (name == "__builtin_string_charAt") {
        auto funcType =
            llvm::FunctionType::get(ctx.getI8Type(), {ctx.getPtrType(), ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // 文字列substring
    else if (name == "__builtin_string_substring") {
        auto funcType = llvm::FunctionType::get(
            ctx.getPtrType(), {ctx.getPtrType(), ctx.getI64Type(), ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // 文字列indexOf
    else if (name == "__builtin_string_indexOf") {
        auto funcType =
            llvm::FunctionType::get(ctx.getI64Type(), {ctx.getPtrType(), ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // 文字列toUpperCase/toLowerCase/trim
    else if (name == "__builtin_string_toUpperCase" || name == "__builtin_string_toLowerCase" ||
             name == "__builtin_string_trim") {
        auto funcType = llvm::FunctionType::get(ctx.getPtrType(), {ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // 文字列startsWith/endsWith/includes
    else if (name == "__builtin_string_startsWith" || name == "__builtin_string_endsWith" ||
             name == "__builtin_string_includes") {
        auto funcType =
            llvm::FunctionType::get(ctx.getBoolType(), {ctx.getPtrType(), ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // 文字列repeat
    else if (name == "__builtin_string_repeat") {
        auto funcType =
            llvm::FunctionType::get(ctx.getPtrType(), {ctx.getPtrType(), ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // 文字列replace
    else if (name == "__builtin_string_replace") {
        auto funcType = llvm::FunctionType::get(
            ctx.getPtrType(), {ctx.getPtrType(), ctx.getPtrType(), ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // 配列スライス
    else if (name == "__builtin_array_slice") {
        // void* __builtin_array_slice(void* arr, i64 elem_size, i64 arr_len, i64 start, i64 end,
        // i64* out_len)
        auto funcType =
            llvm::FunctionType::get(ctx.getPtrType(),
                                    {ctx.getPtrType(), ctx.getI64Type(), ctx.getI64Type(),
                                     ctx.getI64Type(), ctx.getI64Type(), ctx.getPtrType()},
                                    false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // 配列 indexOf (i32版)
    else if (name == "__builtin_array_indexOf_i32" || name == "__builtin_array_indexOf") {
        auto funcType = llvm::FunctionType::get(
            ctx.getI64Type(), {ctx.getPtrType(), ctx.getI64Type(), ctx.getI32Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // 配列 indexOf (i64版)
    else if (name == "__builtin_array_indexOf_i64") {
        auto funcType = llvm::FunctionType::get(
            ctx.getI64Type(), {ctx.getPtrType(), ctx.getI64Type(), ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // 配列 includes (i32版)
    else if (name == "__builtin_array_includes_i32" || name == "__builtin_array_includes") {
        auto funcType = llvm::FunctionType::get(
            ctx.getI8Type(), {ctx.getPtrType(), ctx.getI64Type(), ctx.getI32Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // 配列 includes (i64版)
    else if (name == "__builtin_array_includes_i64") {
        auto funcType = llvm::FunctionType::get(
            ctx.getI8Type(), {ctx.getPtrType(), ctx.getI64Type(), ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // 配列 some (コールバック付き)
    else if (name == "__builtin_array_some_i32" || name == "__builtin_array_some") {
        auto funcType = llvm::FunctionType::get(
            ctx.getI8Type(), {ctx.getPtrType(), ctx.getI64Type(), ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // 配列 every (コールバック付き)
    else if (name == "__builtin_array_every_i32" || name == "__builtin_array_every") {
        auto funcType = llvm::FunctionType::get(
            ctx.getI8Type(), {ctx.getPtrType(), ctx.getI64Type(), ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // 配列 findIndex (コールバック付き)
    else if (name == "__builtin_array_findIndex_i32" || name == "__builtin_array_findIndex") {
        auto funcType = llvm::FunctionType::get(
            ctx.getI64Type(), {ctx.getPtrType(), ctx.getI64Type(), ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // 配列 reduce (コールバック付き)
    else if (name == "__builtin_array_reduce_i32" || name == "__builtin_array_reduce") {
        auto funcType = llvm::FunctionType::get(
            ctx.getI32Type(),
            {ctx.getPtrType(), ctx.getI64Type(), ctx.getPtrType(), ctx.getI32Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // 配列 forEach (コールバック付き)
    else if (name == "__builtin_array_forEach_i32" || name == "__builtin_array_forEach") {
        auto funcType = llvm::FunctionType::get(
            ctx.getVoidType(), {ctx.getPtrType(), ctx.getI64Type(), ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // フォーマット出力関数（可変長引数）
    else if (name == "cm_println_format" || name == "cm_print_format") {
        auto funcType =
            llvm::FunctionType::get(ctx.getVoidType(), {ctx.getPtrType(), ctx.getI32Type()}, true);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "cm_format_string") {
        auto funcType =
            llvm::FunctionType::get(ctx.getPtrType(), {ctx.getPtrType(), ctx.getI32Type()}, true);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }

    // その他の関数は void() として宣言
    auto funcType = llvm::FunctionType::get(ctx.getVoidType(), false);
    auto func = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, name, module);
    return func;
}

// 組み込み関数呼び出し（将来の実装用）
llvm::Value* MIRToLLVM::callIntrinsic([[maybe_unused]] const std::string& name,
                                      [[maybe_unused]] llvm::ArrayRef<llvm::Value*> args) {
    return nullptr;
}

// パニック生成
void MIRToLLVM::generatePanic(const std::string& message) {
    auto msgStr = builder->CreateGlobalStringPtr(message, "panic_msg");
    auto putsFunc = declareExternalFunction("puts");
    builder->CreateCall(putsFunc, {msgStr});

    auto exitType = llvm::FunctionType::get(ctx.getVoidType(), {ctx.getI32Type()}, false);
    auto exitFunc = module->getOrInsertFunction("exit", exitType);
    builder->CreateCall(exitFunc, {llvm::ConstantInt::get(ctx.getI32Type(), 1)});
    builder->CreateUnreachable();
}

// MIRオペランドからHIR型情報を取得
hir::TypePtr MIRToLLVM::getOperandType(const mir::MirOperand& operand) {
    switch (operand.kind) {
        case mir::MirOperand::Constant: {
            auto& constant = std::get<mir::MirConstant>(operand.data);
            return constant.type;
        }
        case mir::MirOperand::Copy:
        case mir::MirOperand::Move: {
            auto& place = std::get<mir::MirPlace>(operand.data);
            if (currentMIRFunction && place.local < currentMIRFunction->locals.size()) {
                return currentMIRFunction->locals[place.local].type;
            }
            return nullptr;
        }
        default:
            return nullptr;
    }
}

}  // namespace cm::codegen::llvm_backend
