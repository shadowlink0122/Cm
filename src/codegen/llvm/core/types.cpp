/// @file llvm_types.cpp
/// @brief 型変換・定数変換処理

#include "mir_to_llvm.hpp"

namespace cm::codegen::llvm_backend {

// 型変換
llvm::Type* MIRToLLVM::convertType(const hir::TypePtr& type) {
    if (!type)
        return ctx.getI32Type();

    switch (type->kind) {
        case hir::TypeKind::Void:
            return ctx.getVoidType();
        case hir::TypeKind::Bool:
            // メモリ上ではi8として扱う（i1は演算時のみ）
            return ctx.getI8Type();
        case hir::TypeKind::Tiny:
        case hir::TypeKind::UTiny:
        case hir::TypeKind::Char:
            return ctx.getI8Type();
        case hir::TypeKind::Short:
        case hir::TypeKind::UShort:
            return ctx.getI16Type();
        case hir::TypeKind::Int:
        case hir::TypeKind::UInt:
            return ctx.getI32Type();
        case hir::TypeKind::Long:
        case hir::TypeKind::ULong:
            return ctx.getI64Type();
        case hir::TypeKind::Float:
            return ctx.getF32Type();
        case hir::TypeKind::Double:
            return ctx.getF64Type();
        case hir::TypeKind::String:
            return ctx.getPtrType();
        case hir::TypeKind::Pointer:
        case hir::TypeKind::Reference:
            return ctx.getPtrType();
        case hir::TypeKind::Array: {
            auto elemType = convertType(type->element_type);
            size_t size = type->array_size.value_or(0);
            return llvm::ArrayType::get(elemType, size);
        }
        case hir::TypeKind::Struct: {
            // まずインターフェース型かチェック
            if (isInterfaceType(type->name)) {
                // インターフェース型はfat pointerとして扱う
                auto it = interfaceTypes.find(type->name);
                if (it != interfaceTypes.end()) {
                    return it->second;
                }
                // まだ作成されていない場合は作成
                return getInterfaceFatPtrType(type->name);
            }

            // 構造体型を検索
            auto it = structTypes.find(type->name);
            if (it != structTypes.end()) {
                return it->second;
            }
            // 見つからない場合は不透明型として扱う
            return llvm::StructType::create(ctx.getContext(), type->name);
        }
        case hir::TypeKind::TypeAlias: {
            // typedefの実際の型がある場合は再帰的に変換
            if (type->element_type) {
                return convertType(type->element_type);
            }
            
            // まずインターフェース型かチェック
            if (isInterfaceType(type->name)) {
                auto it = interfaceTypes.find(type->name);
                if (it != interfaceTypes.end()) {
                    return it->second;
                }
                return getInterfaceFatPtrType(type->name);
            }

            // TypeAliasはMIRレベルで解決されているべき
            // 万が一解決されていない場合、structTypesを確認
            auto it = structTypes.find(type->name);
            if (it != structTypes.end()) {
                return it->second;
            }
            // フォールバック: intとして扱う
            return ctx.getI32Type();
        }
        default:
            return ctx.getI32Type();
    }
}

// 定数変換
llvm::Constant* MIRToLLVM::convertConstant(const mir::MirConstant& constant) {
    // std::variant を処理
    if (std::holds_alternative<bool>(constant.value)) {
        // bool定数はi8として生成（メモリ格納用）
        return llvm::ConstantInt::get(ctx.getI8Type(), std::get<bool>(constant.value));
    } else if (std::holds_alternative<char>(constant.value)) {
        // 文字リテラルはi8として生成
        return llvm::ConstantInt::get(ctx.getI8Type(), std::get<char>(constant.value));
    } else if (std::holds_alternative<int64_t>(constant.value)) {
        return llvm::ConstantInt::get(ctx.getI32Type(), std::get<int64_t>(constant.value));
    } else if (std::holds_alternative<double>(constant.value)) {
        return llvm::ConstantFP::get(ctx.getF64Type(), std::get<double>(constant.value));
    } else if (std::holds_alternative<std::string>(constant.value)) {
        // 文字列リテラル
        auto& str = std::get<std::string>(constant.value);
        return builder->CreateGlobalStringPtr(str, "str");
    } else {
        // nullまたは未知の型
        return llvm::Constant::getNullValue(ctx.getI32Type());
    }
}

}  // namespace cm::codegen::llvm_backend
