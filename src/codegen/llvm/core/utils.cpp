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
               name == "cm_slice_push_i64" || name == "cm_slice_push_f64" ||
               name == "cm_slice_push_ptr" || name == "cm_slice_push_slice") {
        // void cm_slice_push_*(i8* slice, value)
        llvm::Type* valType = ctx.getI32Type();
        if (name == "cm_slice_push_i8")
            valType = ctx.getI8Type();
        else if (name == "cm_slice_push_i64")
            valType = ctx.getI64Type();
        else if (name == "cm_slice_push_f64")
            valType = ctx.getF64Type();
        else if (name == "cm_slice_push_ptr" || name == "cm_slice_push_slice")
            valType = ctx.getPtrType();
        auto funcType =
            llvm::FunctionType::get(ctx.getVoidType(), {ctx.getPtrType(), valType}, false);
        auto func = module->getOrInsertFunction(name, funcType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "cm_slice_pop_i8" || name == "cm_slice_pop_i32" ||
               name == "cm_slice_pop_i64" || name == "cm_slice_pop_f64" ||
               name == "cm_slice_pop_ptr") {
        // value cm_slice_pop_*(i8* slice)
        llvm::Type* retType = ctx.getI32Type();
        if (name == "cm_slice_pop_i8")
            retType = ctx.getI8Type();
        else if (name == "cm_slice_pop_i64")
            retType = ctx.getI64Type();
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

    // currentProgramからFFI関数情報を取得
    if (currentProgram) {
        for (const auto& func : currentProgram->functions) {
            if (func && func->name == name && func->is_extern) {
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
