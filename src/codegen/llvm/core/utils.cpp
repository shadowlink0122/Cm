/// @file llvm_utils.cpp
/// @brief ユーティリティ関数（外部関数宣言、パニック生成、型情報取得）

#include "mir_to_llvm.hpp"

#include <iostream>

namespace cm::codegen::llvm_backend {

// 外部関数宣言
llvm::Function* MIRToLLVM::declareExternalFunction(const std::string& name) {
    if (name == "__print__" || name == "__println__") {
        auto printfType = llvm::FunctionType::get(ctx.getI32Type(), {ctx.getPtrType()}, true);
        auto func = module->getOrInsertFunction("printf", printfType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "printf") {
        // 可変長引数のprintf
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
    } else if (name == "cm_println_float" || name == "cm_print_float") {
        auto funcType = llvm::FunctionType::get(ctx.getVoidType(), {ctx.getF32Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "cm_println_uint" || name == "cm_print_uint") {
        auto funcType = llvm::FunctionType::get(ctx.getVoidType(), {ctx.getI32Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "cm_println_long" || name == "cm_print_long") {
        auto funcType = llvm::FunctionType::get(ctx.getVoidType(), {ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "cm_println_ulong" || name == "cm_print_ulong") {
        auto funcType = llvm::FunctionType::get(ctx.getVoidType(), {ctx.getI64Type()}, false);
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
    // Debug/Display用フォーマット関数（型→文字列変換）
    else if (name == "cm_format_int") {
        auto funcType = llvm::FunctionType::get(ctx.getPtrType(), {ctx.getI32Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "cm_format_uint") {
        auto funcType = llvm::FunctionType::get(ctx.getPtrType(), {ctx.getI32Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "cm_format_double") {
        auto funcType = llvm::FunctionType::get(ctx.getPtrType(), {ctx.getF64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "cm_format_bool") {
        auto funcType = llvm::FunctionType::get(ctx.getPtrType(), {ctx.getI8Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "cm_format_char") {
        auto funcType = llvm::FunctionType::get(ctx.getPtrType(), {ctx.getI8Type()}, false);
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
    // 文字列比較関数（no_std対応の自前実装）
    else if (name == "cm_strcmp") {
        auto funcType =
            llvm::FunctionType::get(ctx.getI32Type(), {ctx.getPtrType(), ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "cm_strncmp") {
        auto funcType = llvm::FunctionType::get(
            ctx.getI32Type(), {ctx.getPtrType(), ctx.getPtrType(), ctx.getI64Type()}, false);
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
    // 文字列first/last
    else if (name == "__builtin_string_first" || name == "__builtin_string_last") {
        auto funcType = llvm::FunctionType::get(ctx.getI8Type(), {ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // スライス操作関数
    else if (name == "cm_slice_new") {
        // i8* cm_slice_new(i64 elem_size, i64 initial_capacity)
        auto funcType =
            llvm::FunctionType::get(ctx.getPtrType(), {ctx.getI64Type(), ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "cm_slice_len" || name == "cm_slice_cap") {
        // i64 cm_slice_len(i8* slice)
        auto funcType = llvm::FunctionType::get(ctx.getI64Type(), {ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "cm_slice_push_i8" || name == "cm_slice_push_i32" ||
               name == "cm_slice_push_i64" || name == "cm_slice_push_f32" ||
               name == "cm_slice_push_f64" || name == "cm_slice_push_ptr" ||
               name == "cm_slice_push_slice") {
        // void cm_slice_push_*(i8* slice, value)
        llvm::Type* valType = ctx.getI32Type();
        if (name == "cm_slice_push_i8")
            valType = ctx.getI8Type();
        else if (name == "cm_slice_push_i64")
            valType = ctx.getI64Type();
        else if (name == "cm_slice_push_f32")
            valType = ctx.getF32Type();
        else if (name == "cm_slice_push_f64")
            valType = ctx.getF64Type();
        else if (name == "cm_slice_push_ptr" || name == "cm_slice_push_slice")
            valType = ctx.getPtrType();
        else if (name == "cm_slice_push_blob")
            valType = ctx.getPtrType();  // blobデータへのポインタ
        auto funcType =
            llvm::FunctionType::get(ctx.getVoidType(), {ctx.getPtrType(), valType}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "cm_slice_pop_i8" || name == "cm_slice_pop_i32" ||
               name == "cm_slice_pop_i64" || name == "cm_slice_pop_f32" ||
               name == "cm_slice_pop_f64" || name == "cm_slice_pop_ptr") {
        // value cm_slice_pop_*(i8* slice)
        llvm::Type* retType = ctx.getI32Type();
        if (name == "cm_slice_pop_i8")
            retType = ctx.getI8Type();
        else if (name == "cm_slice_pop_i64")
            retType = ctx.getI64Type();
        else if (name == "cm_slice_pop_f32")
            retType = ctx.getF32Type();
        else if (name == "cm_slice_pop_f64")
            retType = ctx.getF64Type();
        else if (name == "cm_slice_pop_ptr")
            retType = ctx.getPtrType();
        auto funcType = llvm::FunctionType::get(retType, {ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "cm_slice_delete") {
        // void cm_slice_delete(i8* slice, i64 index)
        auto funcType =
            llvm::FunctionType::get(ctx.getVoidType(), {ctx.getPtrType(), ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "cm_slice_clear") {
        // void cm_slice_clear(i8* slice)
        auto funcType = llvm::FunctionType::get(ctx.getVoidType(), {ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "__builtin_slice_get_i8" || name == "cm_slice_get_i8") {
        // i8 cm_slice_get_i8(i8* slice, i64 index)
        auto funcType =
            llvm::FunctionType::get(ctx.getI8Type(), {ctx.getPtrType(), ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "__builtin_slice_get_i32" || name == "cm_slice_get_i32") {
        // i32 cm_slice_get_i32(i8* slice, i64 index)
        auto funcType =
            llvm::FunctionType::get(ctx.getI32Type(), {ctx.getPtrType(), ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "__builtin_slice_get_i64" || name == "cm_slice_get_i64") {
        auto funcType =
            llvm::FunctionType::get(ctx.getI64Type(), {ctx.getPtrType(), ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "__builtin_slice_get_f32" || name == "cm_slice_get_f32") {
        auto funcType =
            llvm::FunctionType::get(ctx.getF32Type(), {ctx.getPtrType(), ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "__builtin_slice_get_f64" || name == "cm_slice_get_f64") {
        auto funcType =
            llvm::FunctionType::get(ctx.getF64Type(), {ctx.getPtrType(), ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "__builtin_slice_get_ptr" || name == "cm_slice_get_ptr") {
        auto funcType =
            llvm::FunctionType::get(ctx.getPtrType(), {ctx.getPtrType(), ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // 汎用スライス要素アクセス（多次元配列用）
    else if (name == "cm_slice_get_element_ptr") {
        // void* cm_slice_get_element_ptr(void* slice, i64 index)
        auto funcType =
            llvm::FunctionType::get(ctx.getPtrType(), {ctx.getPtrType(), ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "cm_slice_get_subslice") {
        // void* cm_slice_get_subslice(void* slice, i64 index)
        auto funcType =
            llvm::FunctionType::get(ctx.getPtrType(), {ctx.getPtrType(), ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "cm_slice_first_ptr" || name == "cm_slice_last_ptr") {
        // void* cm_slice_first_ptr(void* slice) / cm_slice_last_ptr(void* slice)
        auto funcType = llvm::FunctionType::get(ctx.getPtrType(), {ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "cm_slice_elem_size") {
        // i64 cm_slice_elem_size(void* slice)
        auto funcType = llvm::FunctionType::get(ctx.getI64Type(), {ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "cm_slice_reverse" || name == "cm_slice_sort") {
        // void* cm_slice_reverse(void* slice) / cm_slice_sort(void* slice)
        auto funcType = llvm::FunctionType::get(ctx.getPtrType(), {ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "cm_array_to_slice") {
        // void* cm_array_to_slice(void* array, i64 len, i64 elem_size)
        auto funcType = llvm::FunctionType::get(
            ctx.getPtrType(), {ctx.getPtrType(), ctx.getI64Type(), ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "cm_slice_subslice") {
        // void* cm_slice_subslice(void* slice, i64 start, i64 end)
        auto funcType = llvm::FunctionType::get(
            ctx.getPtrType(), {ctx.getPtrType(), ctx.getI64Type(), ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "cm_array_equal") {
        // bool cm_array_equal(void* lhs, void* rhs, i64 lhs_len, i64 rhs_len, i64 elem_size)
        auto funcType =
            llvm::FunctionType::get(ctx.getBoolType(),
                                    {ctx.getPtrType(), ctx.getPtrType(), ctx.getI64Type(),
                                     ctx.getI64Type(), ctx.getI64Type()},
                                    false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "cm_slice_equal") {
        // bool cm_slice_equal(void* lhs, void* rhs)
        auto funcType =
            llvm::FunctionType::get(ctx.getBoolType(), {ctx.getPtrType(), ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // スライス first/last
    else if (name == "cm_slice_first_i32") {
        auto funcType = llvm::FunctionType::get(ctx.getI32Type(), {ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "cm_slice_first_i64") {
        auto funcType = llvm::FunctionType::get(ctx.getI64Type(), {ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "cm_slice_last_i32") {
        auto funcType = llvm::FunctionType::get(ctx.getI32Type(), {ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "cm_slice_last_i64") {
        auto funcType = llvm::FunctionType::get(ctx.getI64Type(), {ctx.getPtrType()}, false);
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
            ctx.getI32Type(), {ctx.getPtrType(), ctx.getI64Type(), ctx.getI32Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // 配列 indexOf (i64版)
    else if (name == "__builtin_array_indexOf_i64") {
        auto funcType = llvm::FunctionType::get(
            ctx.getI32Type(), {ctx.getPtrType(), ctx.getI64Type(), ctx.getI64Type()}, false);
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
            ctx.getI32Type(), {ctx.getPtrType(), ctx.getI64Type(), ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // 配列 sortBy (コールバック付き)
    else if (name == "__builtin_array_sortBy_i32" || name == "__builtin_array_sortBy") {
        auto funcType = llvm::FunctionType::get(
            ctx.getPtrType(), {ctx.getPtrType(), ctx.getI64Type(), ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "__builtin_array_sortBy_i64") {
        auto funcType = llvm::FunctionType::get(
            ctx.getPtrType(), {ctx.getPtrType(), ctx.getI64Type(), ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // 配列 first (i32版)
    else if (name == "__builtin_array_first_i32" || name == "__builtin_array_first") {
        auto funcType =
            llvm::FunctionType::get(ctx.getI32Type(), {ctx.getPtrType(), ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // 配列 first (i64版)
    else if (name == "__builtin_array_first_i64") {
        auto funcType =
            llvm::FunctionType::get(ctx.getI64Type(), {ctx.getPtrType(), ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // 配列 last (i32版)
    else if (name == "__builtin_array_last_i32" || name == "__builtin_array_last") {
        auto funcType =
            llvm::FunctionType::get(ctx.getI32Type(), {ctx.getPtrType(), ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // 配列 last (i64版)
    else if (name == "__builtin_array_last_i64") {
        auto funcType =
            llvm::FunctionType::get(ctx.getI64Type(), {ctx.getPtrType(), ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // 配列 find (i32版、コールバック付き)
    else if (name == "__builtin_array_find_i32" || name == "__builtin_array_find") {
        auto funcType = llvm::FunctionType::get(
            ctx.getI32Type(), {ctx.getPtrType(), ctx.getI64Type(), ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // 配列 find (i64版、コールバック付き)
    else if (name == "__builtin_array_find_i64") {
        auto funcType = llvm::FunctionType::get(
            ctx.getI64Type(), {ctx.getPtrType(), ctx.getI64Type(), ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // 配列 reverse - 逆順の配列を返す（ポインタとサイズ）
    else if (name == "__builtin_array_reverse" || name == "__builtin_array_reverse_i32") {
        auto funcType =
            llvm::FunctionType::get(ctx.getPtrType(), {ctx.getPtrType(), ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // 配列 reverse (i64版)
    else if (name == "__builtin_array_reverse_i64") {
        auto funcType =
            llvm::FunctionType::get(ctx.getPtrType(), {ctx.getPtrType(), ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // 配列 sort - ソート済み配列を返す（ポインタとサイズ）
    else if (name == "__builtin_array_sort" || name == "__builtin_array_sort_i32") {
        auto funcType =
            llvm::FunctionType::get(ctx.getPtrType(), {ctx.getPtrType(), ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // 配列 sort (i64版)
    else if (name == "__builtin_array_sort_i64") {
        auto funcType =
            llvm::FunctionType::get(ctx.getPtrType(), {ctx.getPtrType(), ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // 配列 sortBy - カスタムコンパレータでソート（ポインタ、サイズ、コンパレータ）
    else if (name == "__builtin_array_sortBy" || name == "__builtin_array_sortBy_i32") {
        auto funcType = llvm::FunctionType::get(
            ctx.getPtrType(), {ctx.getPtrType(), ctx.getI64Type(), ctx.getPtrType()}, false);
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
    } else if (name == "__builtin_array_reduce_i64") {
        auto funcType = llvm::FunctionType::get(
            ctx.getI64Type(),
            {ctx.getPtrType(), ctx.getI64Type(), ctx.getPtrType(), ctx.getI64Type()}, false);
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
    // 配列 map (コールバック付き) - 戻り値はポインタ（新しい配列）
    else if (name == "__builtin_array_map" || name == "__builtin_array_map_i32") {
        auto funcType = llvm::FunctionType::get(
            ctx.getPtrType(), {ctx.getPtrType(), ctx.getI64Type(), ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // 配列 map (クロージャ版) - 戻り値はポインタ（新しい配列）
    else if (name == "__builtin_array_map_closure" || name == "__builtin_array_map_i32_closure") {
        auto funcType = llvm::FunctionType::get(
            ctx.getPtrType(),
            {ctx.getPtrType(), ctx.getI64Type(), ctx.getPtrType(), ctx.getI32Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // 配列 filter (コールバック付き) - 戻り値はポインタ（新しい配列）
    else if (name == "__builtin_array_filter" || name == "__builtin_array_filter_i32") {
        auto funcType = llvm::FunctionType::get(
            ctx.getPtrType(), {ctx.getPtrType(), ctx.getI64Type(), ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // 配列 filter (クロージャ版) - 戻り値はポインタ（新しい配列）
    else if (name == "__builtin_array_filter_closure" ||
             name == "__builtin_array_filter_i32_closure") {
        auto funcType = llvm::FunctionType::get(
            ctx.getPtrType(),
            {ctx.getPtrType(), ctx.getI64Type(), ctx.getPtrType(), ctx.getI32Type()}, false);
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

    // ============================================================
    // TCP ネットワーク関数 (net_runtime.cpp)
    // ============================================================
    // int64_t cm_tcp_listen(int32_t port)
    else if (name == "cm_tcp_listen") {
        auto funcType = llvm::FunctionType::get(ctx.getI64Type(), {ctx.getI32Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int64_t cm_tcp_accept(int64_t server_fd)
    else if (name == "cm_tcp_accept") {
        auto funcType = llvm::FunctionType::get(ctx.getI64Type(), {ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int64_t cm_tcp_connect(int64_t host_ptr, int32_t port)
    else if (name == "cm_tcp_connect") {
        auto funcType =
            llvm::FunctionType::get(ctx.getI64Type(), {ctx.getI64Type(), ctx.getI32Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int32_t cm_tcp_read(int64_t fd, int64_t buf_ptr, int32_t size)
    // int32_t cm_tcp_write(int64_t fd, int64_t buf_ptr, int32_t size)
    else if (name == "cm_tcp_read" || name == "cm_tcp_write") {
        auto funcType = llvm::FunctionType::get(
            ctx.getI32Type(), {ctx.getI64Type(), ctx.getI64Type(), ctx.getI32Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // void cm_tcp_close(int64_t fd)
    else if (name == "cm_tcp_close") {
        auto funcType = llvm::FunctionType::get(ctx.getVoidType(), {ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int32_t cm_tcp_set_nonblocking(int64_t fd)
    else if (name == "cm_tcp_set_nonblocking") {
        auto funcType = llvm::FunctionType::get(ctx.getI32Type(), {ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int64_t cm_tcp_poll_create()
    else if (name == "cm_tcp_poll_create") {
        auto funcType = llvm::FunctionType::get(ctx.getI64Type(), false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int32_t cm_tcp_poll_add(int64_t poll_handle, int64_t fd, int32_t events)
    else if (name == "cm_tcp_poll_add") {
        auto funcType = llvm::FunctionType::get(
            ctx.getI32Type(), {ctx.getI64Type(), ctx.getI64Type(), ctx.getI32Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int32_t cm_tcp_poll_remove(int64_t poll_handle, int64_t fd)
    else if (name == "cm_tcp_poll_remove") {
        auto funcType =
            llvm::FunctionType::get(ctx.getI32Type(), {ctx.getI64Type(), ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int32_t cm_tcp_poll_wait(int64_t poll_handle, int32_t timeout_ms)
    else if (name == "cm_tcp_poll_wait") {
        auto funcType =
            llvm::FunctionType::get(ctx.getI32Type(), {ctx.getI64Type(), ctx.getI32Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int64_t cm_tcp_poll_get_fd(int64_t poll_handle, int32_t index)
    else if (name == "cm_tcp_poll_get_fd") {
        auto funcType =
            llvm::FunctionType::get(ctx.getI64Type(), {ctx.getI64Type(), ctx.getI32Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int32_t cm_tcp_poll_get_events(int64_t poll_handle, int32_t index)
    else if (name == "cm_tcp_poll_get_events") {
        auto funcType =
            llvm::FunctionType::get(ctx.getI32Type(), {ctx.getI64Type(), ctx.getI32Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // void cm_tcp_poll_destroy(int64_t poll_handle)
    else if (name == "cm_tcp_poll_destroy") {
        auto funcType = llvm::FunctionType::get(ctx.getVoidType(), {ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }

    // ============================================================
    // UDP ネットワーク関数 (net_runtime.cpp)
    // ============================================================
    // int64_t cm_udp_create()
    else if (name == "cm_udp_create") {
        auto funcType = llvm::FunctionType::get(ctx.getI64Type(), false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int32_t cm_udp_bind(int64_t fd, int32_t port)
    else if (name == "cm_udp_bind") {
        auto funcType =
            llvm::FunctionType::get(ctx.getI32Type(), {ctx.getI64Type(), ctx.getI32Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int32_t cm_udp_sendto(int64_t fd, int64_t host_ptr, int32_t port, int64_t buf_ptr, int32_t
    // size)
    else if (name == "cm_udp_sendto") {
        auto funcType =
            llvm::FunctionType::get(ctx.getI32Type(),
                                    {ctx.getI64Type(), ctx.getI64Type(), ctx.getI32Type(),
                                     ctx.getI64Type(), ctx.getI32Type()},
                                    false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int32_t cm_udp_recvfrom(int64_t fd, int64_t buf_ptr, int32_t size)
    else if (name == "cm_udp_recvfrom") {
        auto funcType = llvm::FunctionType::get(
            ctx.getI32Type(), {ctx.getI64Type(), ctx.getI64Type(), ctx.getI32Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // void cm_udp_close(int64_t fd)
    else if (name == "cm_udp_close") {
        auto funcType = llvm::FunctionType::get(ctx.getVoidType(), {ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int32_t cm_udp_set_broadcast(int64_t fd)
    else if (name == "cm_udp_set_broadcast") {
        auto funcType = llvm::FunctionType::get(ctx.getI32Type(), {ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }

    // ============================================================
    // DNS / Socket ユーティリティ (net_runtime.cpp)
    // ============================================================
    // char* cm_dns_resolve(const char* hostname)
    else if (name == "cm_dns_resolve") {
        auto funcType = llvm::FunctionType::get(ctx.getPtrType(), {ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int32_t cm_socket_set_timeout(int64_t fd, int32_t timeout_ms)
    // int32_t cm_socket_set_reuse_addr(int64_t fd) 等のソケットオプション
    else if (name == "cm_socket_set_timeout" || name == "cm_socket_set_recv_buffer" ||
             name == "cm_socket_set_send_buffer") {
        auto funcType =
            llvm::FunctionType::get(ctx.getI32Type(), {ctx.getI64Type(), ctx.getI32Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "cm_socket_set_reuse_addr" || name == "cm_socket_set_nodelay" ||
               name == "cm_socket_set_keepalive") {
        auto funcType = llvm::FunctionType::get(ctx.getI32Type(), {ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }

    // ============================================================
    // Atomic操作 (sync_runtime.cpp) — cm_プレフィクス版 + レガシー版
    // ============================================================
    // i32: load/store/fetch_add/fetch_sub
    else if (name == "cm_atomic_load_i32" || name == "atomic_load_i32") {
        auto funcType = llvm::FunctionType::get(ctx.getI32Type(), {ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "cm_atomic_store_i32" || name == "atomic_store_i32") {
        auto funcType =
            llvm::FunctionType::get(ctx.getVoidType(), {ctx.getPtrType(), ctx.getI32Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "cm_atomic_fetch_add_i32" || name == "cm_atomic_fetch_sub_i32" ||
               name == "atomic_fetch_add_i32" || name == "atomic_fetch_sub_i32") {
        auto funcType =
            llvm::FunctionType::get(ctx.getI32Type(), {ctx.getPtrType(), ctx.getI32Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // i32: compare_exchange(ptr, ptr, i32) → int
    else if (name == "cm_atomic_compare_exchange_i32" || name == "atomic_compare_exchange_i32") {
        auto funcType = llvm::FunctionType::get(
            ctx.getI32Type(), {ctx.getPtrType(), ctx.getPtrType(), ctx.getI32Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // i64: load/store/fetch_add/fetch_sub
    else if (name == "cm_atomic_load_i64" || name == "atomic_load_i64") {
        auto funcType = llvm::FunctionType::get(ctx.getI64Type(), {ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "cm_atomic_store_i64" || name == "atomic_store_i64") {
        auto funcType =
            llvm::FunctionType::get(ctx.getVoidType(), {ctx.getPtrType(), ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "cm_atomic_fetch_add_i64" || name == "cm_atomic_fetch_sub_i64" ||
               name == "atomic_fetch_add_i64" || name == "atomic_fetch_sub_i64") {
        auto funcType =
            llvm::FunctionType::get(ctx.getI64Type(), {ctx.getPtrType(), ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // i64: compare_exchange(ptr, i64, i64) → int
    else if (name == "cm_atomic_compare_exchange_i64" || name == "atomic_compare_exchange_i64") {
        auto funcType = llvm::FunctionType::get(
            ctx.getI32Type(), {ctx.getPtrType(), ctx.getPtrType(), ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }

    // ============================================================
    // Channel (channel_runtime.cpp)
    // ============================================================
    // int64_t cm_channel_create(int32_t capacity)
    else if (name == "cm_channel_create") {
        auto funcType = llvm::FunctionType::get(ctx.getI64Type(), {ctx.getI32Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int32_t cm_channel_send(int64_t handle, int64_t value)
    // int32_t cm_channel_try_send(int64_t handle, int64_t value)
    else if (name == "cm_channel_send" || name == "cm_channel_try_send") {
        auto funcType =
            llvm::FunctionType::get(ctx.getI32Type(), {ctx.getI64Type(), ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int32_t cm_channel_recv(int64_t handle, int64_t* value)
    // int32_t cm_channel_try_recv(int64_t handle, int64_t* value)
    else if (name == "cm_channel_recv" || name == "cm_channel_try_recv") {
        auto funcType =
            llvm::FunctionType::get(ctx.getI32Type(), {ctx.getI64Type(), ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // void cm_channel_close(int64_t handle) / void cm_channel_destroy(int64_t handle)
    else if (name == "cm_channel_close" || name == "cm_channel_destroy") {
        auto funcType = llvm::FunctionType::get(ctx.getVoidType(), {ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int32_t cm_channel_len(int64_t handle) / int32_t cm_channel_is_closed(int64_t handle)
    else if (name == "cm_channel_len" || name == "cm_channel_is_closed") {
        auto funcType = llvm::FunctionType::get(ctx.getI32Type(), {ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }

    // ============================================================
    // Thread (thread_runtime.cpp)
    // ============================================================
    // uint64_t cm_thread_create(void* fn, void* arg)
    // uint64_t cm_thread_spawn_with_arg(void* fn, void* arg)
    else if (name == "cm_thread_create" || name == "cm_thread_spawn_with_arg") {
        auto funcType =
            llvm::FunctionType::get(ctx.getI64Type(), {ctx.getPtrType(), ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int cm_thread_join(uint64_t thread_id, void** retval)
    else if (name == "cm_thread_join") {
        auto funcType =
            llvm::FunctionType::get(ctx.getI32Type(), {ctx.getI64Type(), ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // void cm_thread_detach(uint64_t thread_id)
    else if (name == "cm_thread_detach") {
        auto funcType = llvm::FunctionType::get(ctx.getVoidType(), {ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // uint64_t cm_thread_self()
    else if (name == "cm_thread_self") {
        auto funcType = llvm::FunctionType::get(ctx.getI64Type(), false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // void cm_thread_sleep_us(uint64_t microseconds)
    else if (name == "cm_thread_sleep_us") {
        auto funcType = llvm::FunctionType::get(ctx.getVoidType(), {ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // void cm_thread_join_all(uint64_t* handles, int32_t count)
    else if (name == "cm_thread_join_all") {
        auto funcType =
            llvm::FunctionType::get(ctx.getVoidType(), {ctx.getPtrType(), ctx.getI32Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }

    // ============================================================
    // HTTP クライアント (http_runtime.cpp)
    // ============================================================
    // int64_t cm_http_request_create()
    else if (name == "cm_http_request_create") {
        auto funcType = llvm::FunctionType::get(ctx.getI64Type(), false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // void cm_http_request_set_method(int64_t handle, int32_t method)
    else if (name == "cm_http_request_set_method") {
        auto funcType =
            llvm::FunctionType::get(ctx.getVoidType(), {ctx.getI64Type(), ctx.getI32Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // void cm_http_request_set_url(int64_t handle, const char* host, int32_t port, const char*
    // path)
    else if (name == "cm_http_request_set_url") {
        auto funcType = llvm::FunctionType::get(
            ctx.getVoidType(),
            {ctx.getI64Type(), ctx.getPtrType(), ctx.getI32Type(), ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // void cm_http_request_set_header(int64_t handle, const char* key, const char* value)
    else if (name == "cm_http_request_set_header") {
        auto funcType = llvm::FunctionType::get(
            ctx.getVoidType(), {ctx.getI64Type(), ctx.getPtrType(), ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // void cm_http_request_set_body(int64_t handle, const char* body)
    else if (name == "cm_http_request_set_body") {
        auto funcType =
            llvm::FunctionType::get(ctx.getVoidType(), {ctx.getI64Type(), ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // void cm_http_request_destroy(int64_t handle)
    // void cm_http_response_destroy(int64_t handle)
    // void cm_http_server_req_destroy(int64_t handle)
    else if (name == "cm_http_request_destroy" || name == "cm_http_response_destroy" ||
             name == "cm_http_server_req_destroy") {
        auto funcType = llvm::FunctionType::get(ctx.getVoidType(), {ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int64_t cm_http_execute(int64_t req_handle)
    else if (name == "cm_http_execute") {
        auto funcType = llvm::FunctionType::get(ctx.getI64Type(), {ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int32_t cm_http_response_status(int64_t handle)
    // int32_t cm_http_response_is_error(int64_t handle)
    else if (name == "cm_http_response_status" || name == "cm_http_response_is_error") {
        auto funcType = llvm::FunctionType::get(ctx.getI32Type(), {ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // const char* cm_http_response_body(int64_t handle)
    else if (name == "cm_http_response_body") {
        auto funcType = llvm::FunctionType::get(ctx.getPtrType(), {ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // const char* cm_http_response_header(int64_t handle, const char* key)
    else if (name == "cm_http_response_header") {
        auto funcType =
            llvm::FunctionType::get(ctx.getPtrType(), {ctx.getI64Type(), ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int64_t cm_http_get(const char* host, int32_t port, const char* path)
    // int64_t cm_http_delete(const char* host, int32_t port, const char* path)
    else if (name == "cm_http_get" || name == "cm_http_delete") {
        auto funcType = llvm::FunctionType::get(
            ctx.getI64Type(), {ctx.getPtrType(), ctx.getI32Type(), ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int64_t cm_http_post(const char* host, int32_t port, const char* path, const char* body)
    // int64_t cm_http_put(const char* host, int32_t port, const char* path, const char* body)
    else if (name == "cm_http_post" || name == "cm_http_put") {
        auto funcType = llvm::FunctionType::get(
            ctx.getI64Type(),
            {ctx.getPtrType(), ctx.getI32Type(), ctx.getPtrType(), ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }

    // ============================================================
    // HTTP サーバー (http_runtime.cpp)
    // ============================================================
    // int64_t cm_http_server_create(int32_t port)
    else if (name == "cm_http_server_create") {
        auto funcType = llvm::FunctionType::get(ctx.getI64Type(), {ctx.getI32Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // void cm_http_server_close(int64_t server_fd)
    else if (name == "cm_http_server_close") {
        auto funcType = llvm::FunctionType::get(ctx.getVoidType(), {ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int64_t cm_http_server_accept(int64_t server_fd)
    else if (name == "cm_http_server_accept") {
        auto funcType = llvm::FunctionType::get(ctx.getI64Type(), {ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // void cm_http_server_respond(int64_t handle, int32_t status, const char* body)
    else if (name == "cm_http_server_respond") {
        auto funcType = llvm::FunctionType::get(
            ctx.getVoidType(), {ctx.getI64Type(), ctx.getI32Type(), ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // const char* cm_http_server_req_method/path/body(int64_t handle)
    else if (name == "cm_http_server_req_method" || name == "cm_http_server_req_path" ||
             name == "cm_http_server_req_body") {
        auto funcType = llvm::FunctionType::get(ctx.getPtrType(), {ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // const char* cm_http_server_req_header(int64_t handle, const char* key)
    else if (name == "cm_http_server_req_header") {
        auto funcType =
            llvm::FunctionType::get(ctx.getPtrType(), {ctx.getI64Type(), ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // const char* cm_http_error_message(int64_t handle)
    // const char* cm_http_response_content_type(int64_t handle)
    // const char* cm_http_response_location(int64_t handle)
    else if (name == "cm_http_error_message" || name == "cm_http_response_content_type" ||
             name == "cm_http_response_location") {
        auto funcType = llvm::FunctionType::get(ctx.getPtrType(), {ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int cm_http_response_is_redirect(int64_t handle)
    else if (name == "cm_http_response_is_redirect") {
        auto funcType = llvm::FunctionType::get(ctx.getI32Type(), {ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int64_t cm_http_test_server_start(int32_t port, int32_t max_requests)
    else if (name == "cm_http_test_server_start") {
        auto funcType =
            llvm::FunctionType::get(ctx.getI64Type(), {ctx.getI32Type(), ctx.getI32Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // URL解析関数
    // int64_t cm_http_parse_url(const char* url)
    else if (name == "cm_http_parse_url") {
        auto funcType = llvm::FunctionType::get(ctx.getI64Type(), {ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // const char* cm_http_parsed_scheme/host/path(int64_t handle)
    else if (name == "cm_http_parsed_scheme" || name == "cm_http_parsed_host" ||
             name == "cm_http_parsed_path") {
        auto funcType = llvm::FunctionType::get(ctx.getPtrType(), {ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // int cm_http_parsed_port(int64_t handle)
    else if (name == "cm_http_parsed_port") {
        auto funcType = llvm::FunctionType::get(ctx.getI32Type(), {ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // void cm_http_parsed_url_destroy(int64_t handle)
    else if (name == "cm_http_parsed_url_destroy") {
        auto funcType = llvm::FunctionType::get(ctx.getVoidType(), {ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // リクエストオプション: void cm_http_request_set_*(int64_t handle, ...)
    else if (name == "cm_http_request_set_timeout" ||
             name == "cm_http_request_set_follow_redirects" ||
             name == "cm_http_request_set_max_redirects") {
        auto funcType =
            llvm::FunctionType::get(ctx.getVoidType(), {ctx.getI64Type(), ctx.getI32Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // void cm_http_request_set_basic_auth(int64_t handle, const char* user, const char* pass)
    else if (name == "cm_http_request_set_basic_auth") {
        auto funcType = llvm::FunctionType::get(
            ctx.getVoidType(), {ctx.getI64Type(), ctx.getPtrType(), ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // void cm_http_request_set_bearer_auth/content_type(int64_t handle, const char* value)
    else if (name == "cm_http_request_set_bearer_auth" ||
             name == "cm_http_request_set_content_type") {
        auto funcType =
            llvm::FunctionType::get(ctx.getVoidType(), {ctx.getI64Type(), ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }
    // void cm_http_request_set_json(int64_t handle)
    else if (name == "cm_http_request_set_json") {
        auto funcType = llvm::FunctionType::get(ctx.getVoidType(), {ctx.getI64Type()}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }

    // currentProgramから関数情報を取得（extern関数だけでなく、全ての関数を検索）
    // これにより、モノモーフィック化されたメソッド（Container__int__get等）も正しいシグネチャで宣言される
    if (currentProgram) {
        for (const auto& func : currentProgram->functions) {
            if (func && func->name == name) {
                // 戻り値型
                llvm::Type* returnType = ctx.getVoidType();
                if (func->return_local < func->locals.size()) {
                    auto& returnLocal = func->locals[func->return_local];
                    if (returnLocal.type && returnLocal.type->kind != hir::TypeKind::Void) {
                        returnType = convertType(returnLocal.type);
                    }
                }

                // パラメータ型
                std::vector<llvm::Type*> paramTypes;
                for (const auto& arg_local : func->arg_locals) {
                    if (arg_local < func->locals.size()) {
                        auto& local = func->locals[arg_local];
                        if (local.type) {
                            paramTypes.push_back(convertType(local.type));
                        }
                    }
                }

                // 関数型（可変長引数を考慮）
                auto funcType = llvm::FunctionType::get(returnType, paramTypes, func->is_variadic);
                auto result = module->getOrInsertFunction(name, funcType);
                return llvm::cast<llvm::Function>(result.getCallee());
            }
        }
    }

    // モジュール分割コンパイル時: allModuleFunctionsから検索
    if (!allModuleFunctions.empty()) {
        for (const auto* func : allModuleFunctions) {
            if (func && func->name == name) {
                // 戻り値型
                llvm::Type* returnType = ctx.getVoidType();
                if (func->return_local < func->locals.size()) {
                    auto& returnLocal = func->locals[func->return_local];
                    if (returnLocal.type && returnLocal.type->kind != hir::TypeKind::Void) {
                        returnType = convertType(returnLocal.type);
                    }
                }

                // パラメータ型
                std::vector<llvm::Type*> paramTypes;
                for (const auto& arg_local : func->arg_locals) {
                    if (arg_local < func->locals.size()) {
                        auto& local = func->locals[arg_local];
                        if (local.type) {
                            paramTypes.push_back(convertType(local.type));
                        }
                    }
                }

                // 関数型（可変長引数を考慮）
                auto funcType = llvm::FunctionType::get(returnType, paramTypes, func->is_variadic);
                auto result = module->getOrInsertFunction(name, funcType);
                return llvm::cast<llvm::Function>(result.getCallee());
            }
        }
    }

    // 最終フォールバック: void() として宣言（本来ここには到達しないはず）
    std::cerr << "[WARN] declareExternalFunction: unknown function '" << name
              << "' - using void() signature\n";
    auto funcType = llvm::FunctionType::get(ctx.getVoidType(), false);
    auto func = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, name, module);
    return func;
}

// 組み込み関数呼び出し（将来の実装用）
llvm::Value* MIRToLLVM::callIntrinsic([[maybe_unused]] const std::string& name,
                                      [[maybe_unused]] llvm::ArrayRef<llvm::Value*> args) {
    return nullptr;
}

// NOTE: freeHeapAllocatedLocals() はベアメタル対応のため削除
// すべての配列はスタックに割り当てられるため、明示的な解放は不要

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
