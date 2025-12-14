/// @file type_converter.cpp
/// @brief HIR型からLLVM型への変換実装

#include "type_converter.hpp"

namespace cm::codegen::common {

void TypeConverter::initBasicTypes() {
    voidTy = llvm::Type::getVoidTy(llvmCtx);
    boolTy = llvm::Type::getInt8Ty(llvmCtx);  // boolはi8として格納
    i8Ty = llvm::Type::getInt8Ty(llvmCtx);
    i16Ty = llvm::Type::getInt16Ty(llvmCtx);
    i32Ty = llvm::Type::getInt32Ty(llvmCtx);
    i64Ty = llvm::Type::getInt64Ty(llvmCtx);
    f32Ty = llvm::Type::getFloatTy(llvmCtx);
    f64Ty = llvm::Type::getDoubleTy(llvmCtx);
    ptrTy = llvm::PointerType::get(llvmCtx, 0);  // Opaque pointer
}

llvm::Type* TypeConverter::convert(const hir::TypePtr& type) {
    if (!type)
        return i32Ty;

    switch (type->kind) {
        case hir::TypeKind::Void:
            return voidTy;
        case hir::TypeKind::Bool:
            return boolTy;
        case hir::TypeKind::Tiny:
        case hir::TypeKind::UTiny:
        case hir::TypeKind::Char:
            return i8Ty;
        case hir::TypeKind::Short:
        case hir::TypeKind::UShort:
            return i16Ty;
        case hir::TypeKind::Int:
        case hir::TypeKind::UInt:
            return i32Ty;
        case hir::TypeKind::Long:
        case hir::TypeKind::ULong:
            return i64Ty;
        case hir::TypeKind::Float:
            return f32Ty;
        case hir::TypeKind::Double:
            return f64Ty;
        case hir::TypeKind::String:
            return ptrTy;
        case hir::TypeKind::Pointer:
        case hir::TypeKind::Reference:
            return ptrTy;
        case hir::TypeKind::Array: {
            auto elemType = convert(type->element_type);
            size_t size = type->array_size.value_or(0);
            return llvm::ArrayType::get(elemType, size);
        }
        case hir::TypeKind::Struct: {
            // インターフェース型かチェック
            if (isInterfaceType(type->name)) {
                auto it = interfaceTypes.find(type->name);
                if (it != interfaceTypes.end()) {
                    return it->second;
                }
                return getInterfaceFatPtrType(type->name);
            }

            // 構造体型を検索
            auto it = structTypes.find(type->name);
            if (it != structTypes.end()) {
                return it->second;
            }
            // 見つからない場合は不透明型として扱う
            return llvm::StructType::create(llvmCtx, type->name);
        }
        case hir::TypeKind::TypeAlias: {
            // インターフェース型かチェック
            if (isInterfaceType(type->name)) {
                auto it = interfaceTypes.find(type->name);
                if (it != interfaceTypes.end()) {
                    return it->second;
                }
                return getInterfaceFatPtrType(type->name);
            }

            // TypeAliasはMIRレベルで解決されているべき
            auto it = structTypes.find(type->name);
            if (it != structTypes.end()) {
                return it->second;
            }
            return i32Ty;  // フォールバック
        }
        default:
            return i32Ty;
    }
}

llvm::StructType* TypeConverter::getInterfaceFatPtrType(const std::string& interfaceName) {
    auto it = interfaceTypes.find(interfaceName);
    if (it != interfaceTypes.end()) {
        return it->second;
    }

    // fat pointer型を作成: {i8* data, i8** vtable}
    auto fatPtrType = llvm::StructType::create(llvmCtx, {ptrTy, ptrTy},  // {data_ptr, vtable_ptr}
                                               interfaceName + "_fat_ptr");
    interfaceTypes[interfaceName] = fatPtrType;
    return fatPtrType;
}

}  // namespace cm::codegen::common
