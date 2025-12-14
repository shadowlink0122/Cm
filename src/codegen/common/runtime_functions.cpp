/// @file runtime_functions.cpp
/// @brief ランタイム関数宣言の実装

#include "runtime_functions.hpp"

namespace cm::codegen::common {

RuntimeFunctions::RuntimeFunctions(llvm::Module& mod, llvm::LLVMContext& context)
    : module(mod), ctx(context) {
    voidTy = llvm::Type::getVoidTy(ctx);
    i8Ty = llvm::Type::getInt8Ty(ctx);
    i32Ty = llvm::Type::getInt32Ty(ctx);
    i64Ty = llvm::Type::getInt64Ty(ctx);
    f64Ty = llvm::Type::getDoubleTy(ctx);
    ptrTy = llvm::PointerType::get(ctx, 0);
}

llvm::Function* RuntimeFunctions::declare(const std::string& name, llvm::Type* retTy,
                                          std::vector<llvm::Type*> argTys, bool isVarArg) {
    auto it = cache.find(name);
    if (it != cache.end()) {
        return it->second;
    }

    auto funcType = llvm::FunctionType::get(retTy, argTys, isVarArg);
    auto func = llvm::cast<llvm::Function>(module.getOrInsertFunction(name, funcType).getCallee());
    cache[name] = func;
    return func;
}

llvm::Function* RuntimeFunctions::get(const std::string& name) {
    // print系
    if (name == "cm_print_string")
        return getPrintString();
    if (name == "cm_println_string")
        return getPrintlnString();
    if (name == "cm_print_int")
        return getPrintInt();
    if (name == "cm_println_int")
        return getPrintlnInt();
    if (name == "cm_print_double")
        return getPrintDouble();
    if (name == "cm_println_double")
        return getPrintlnDouble();
    if (name == "cm_print_bool")
        return getPrintBool();
    if (name == "cm_println_bool")
        return getPrintlnBool();
    if (name == "cm_print_char")
        return getPrintChar();
    if (name == "cm_println_char")
        return getPrintlnChar();

    // 型変換
    if (name == "cm_int_to_string")
        return getIntToString();
    if (name == "cm_uint_to_string")
        return getUIntToString();
    if (name == "cm_char_to_string")
        return getCharToString();
    if (name == "cm_bool_to_string")
        return getBoolToString();
    if (name == "cm_double_to_string")
        return getDoubleToString();

    // 文字列操作
    if (name == "cm_string_concat")
        return getStringConcat();
    if (name == "cm_format_string")
        return getFormatString();
    if (name == "cm_format_replace")
        return getFormatReplace();
    if (name == "cm_format_replace_int")
        return getFormatReplaceInt();
    if (name == "cm_format_replace_double")
        return getFormatReplaceDouble();
    if (name == "cm_format_replace_string")
        return getFormatReplaceString();

    // パニック
    if (name == "__cm_panic")
        return getPanic();

    // 汎用: void()として宣言
    return declare(name, voidTy, {});
}

// Print functions
llvm::Function* RuntimeFunctions::getPrintString() {
    return declare("cm_print_string", voidTy, {ptrTy});
}

llvm::Function* RuntimeFunctions::getPrintlnString() {
    return declare("cm_println_string", voidTy, {ptrTy});
}

llvm::Function* RuntimeFunctions::getPrintInt() {
    return declare("cm_print_int", voidTy, {i32Ty});
}

llvm::Function* RuntimeFunctions::getPrintlnInt() {
    return declare("cm_println_int", voidTy, {i32Ty});
}

llvm::Function* RuntimeFunctions::getPrintDouble() {
    return declare("cm_print_double", voidTy, {f64Ty});
}

llvm::Function* RuntimeFunctions::getPrintlnDouble() {
    return declare("cm_println_double", voidTy, {f64Ty});
}

llvm::Function* RuntimeFunctions::getPrintBool() {
    return declare("cm_print_bool", voidTy, {i8Ty});
}

llvm::Function* RuntimeFunctions::getPrintlnBool() {
    return declare("cm_println_bool", voidTy, {i8Ty});
}

llvm::Function* RuntimeFunctions::getPrintChar() {
    return declare("cm_print_char", voidTy, {i8Ty});
}

llvm::Function* RuntimeFunctions::getPrintlnChar() {
    return declare("cm_println_char", voidTy, {i8Ty});
}

// Type conversion functions
llvm::Function* RuntimeFunctions::getIntToString() {
    return declare("cm_int_to_string", ptrTy, {i32Ty});
}

llvm::Function* RuntimeFunctions::getUIntToString() {
    return declare("cm_uint_to_string", ptrTy, {i32Ty});
}

llvm::Function* RuntimeFunctions::getCharToString() {
    return declare("cm_char_to_string", ptrTy, {i8Ty});
}

llvm::Function* RuntimeFunctions::getBoolToString() {
    return declare("cm_bool_to_string", ptrTy, {i8Ty});
}

llvm::Function* RuntimeFunctions::getDoubleToString() {
    return declare("cm_double_to_string", ptrTy, {f64Ty});
}

// String functions
llvm::Function* RuntimeFunctions::getStringConcat() {
    return declare("cm_string_concat", ptrTy, {ptrTy, ptrTy});
}

llvm::Function* RuntimeFunctions::getFormatString() {
    return declare("cm_format_string", ptrTy, {ptrTy, i32Ty}, true);
}

llvm::Function* RuntimeFunctions::getFormatReplace() {
    return declare("cm_format_replace", ptrTy, {ptrTy, ptrTy});
}

llvm::Function* RuntimeFunctions::getFormatReplaceInt() {
    return declare("cm_format_replace_int", ptrTy, {ptrTy, i32Ty});
}

llvm::Function* RuntimeFunctions::getFormatReplaceDouble() {
    return declare("cm_format_replace_double", ptrTy, {ptrTy, f64Ty});
}

llvm::Function* RuntimeFunctions::getFormatReplaceString() {
    return declare("cm_format_replace_string", ptrTy, {ptrTy, ptrTy});
}

// Panic
llvm::Function* RuntimeFunctions::getPanic() {
    return declare("__cm_panic", voidTy, {ptrTy});
}

}  // namespace cm::codegen::common
