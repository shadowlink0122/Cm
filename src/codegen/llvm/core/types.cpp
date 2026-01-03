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
        case hir::TypeKind::ISize:
        case hir::TypeKind::USize:
            // ポインタサイズ整数（64ビット想定）
            return ctx.getI64Type();
        case hir::TypeKind::Float:
        case hir::TypeKind::UFloat:
            return ctx.getF32Type();
        case hir::TypeKind::Double:
        case hir::TypeKind::UDouble:
            return ctx.getF64Type();
        case hir::TypeKind::String:
        case hir::TypeKind::CString:
            return ctx.getPtrType();
        case hir::TypeKind::Pointer:
        case hir::TypeKind::Reference:
            // LLVM 14: すべてのポインタはi8*として統一
            // （typed pointers互換性のため、store/load時にbitcastを使用）
            return ctx.getPtrType();
        case hir::TypeKind::Array: {
            // 動的配列（スライス）の場合はポインタ型を返す
            if (!type->array_size.has_value()) {
                return ctx.getPtrType();
            }
            // 静的配列の場合は配列型を返す
            auto elemType = convertType(type->element_type);
            size_t size = type->array_size.value();
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
        case hir::TypeKind::Function: {
            // 関数ポインタ型
            llvm::Type* retType =
                type->return_type ? convertType(type->return_type) : ctx.getVoidType();
            std::vector<llvm::Type*> paramTypes;
            for (const auto& paramType : type->param_types) {
                paramTypes.push_back(convertType(paramType));
            }
            // 関数型は作成するが、LLVM 14+のopaque pointerでは使用しない
            llvm::FunctionType::get(retType, paramTypes, false);
            // LLVM 14+: opaque pointerを使用（関数ポインタも単なるptr）
            return ctx.getPtrType();
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
        int64_t val = std::get<int64_t>(constant.value);

        // 型情報がある場合、適切な型で生成
        if (constant.type) {
            switch (constant.type->kind) {
                case hir::TypeKind::Pointer:
                case hir::TypeKind::Reference:
                case hir::TypeKind::String:
                case hir::TypeKind::CString:
                    // ポインタ型の場合は null pointer
                    return llvm::ConstantPointerNull::get(
                        llvm::PointerType::get(ctx.getContext(), 0));
                case hir::TypeKind::Array:
                    // 動的配列（スライス）の場合もポインタ
                    if (!constant.type->array_size.has_value()) {
                        return llvm::ConstantPointerNull::get(
                            llvm::PointerType::get(ctx.getContext(), 0));
                    }
                    break;
                case hir::TypeKind::Long:
                case hir::TypeKind::ULong:
                case hir::TypeKind::ISize:
                case hir::TypeKind::USize:
                    return llvm::ConstantInt::get(ctx.getI64Type(), val);
                case hir::TypeKind::Short:
                case hir::TypeKind::UShort:
                    return llvm::ConstantInt::get(ctx.getI16Type(), val);
                case hir::TypeKind::Tiny:
                case hir::TypeKind::UTiny:
                case hir::TypeKind::Char:
                    return llvm::ConstantInt::get(ctx.getI8Type(), val);
                default:
                    break;
            }
        }
        return llvm::ConstantInt::get(ctx.getI32Type(), val);
    } else if (std::holds_alternative<double>(constant.value)) {
        // 型情報がある場合、適切な浮動小数点型で生成
        if (constant.type && (constant.type->kind == hir::TypeKind::Float ||
                              constant.type->kind == hir::TypeKind::UFloat)) {
            return llvm::ConstantFP::get(ctx.getF32Type(), std::get<double>(constant.value));
        }
        return llvm::ConstantFP::get(ctx.getF64Type(), std::get<double>(constant.value));
    } else if (std::holds_alternative<std::string>(constant.value)) {
        // 文字列リテラル
        auto& str = std::get<std::string>(constant.value);
        return builder->CreateGlobalStringPtr(str, "str");
    } else {
        // nullまたは未知の型
        if (constant.type) {
            if (constant.type->kind == hir::TypeKind::Pointer ||
                constant.type->kind == hir::TypeKind::Reference ||
                constant.type->kind == hir::TypeKind::String ||
                constant.type->kind == hir::TypeKind::CString ||
                (constant.type->kind == hir::TypeKind::Array &&
                 !constant.type->array_size.has_value())) {
                // LLVM 14+: opaque pointerを使用
                return llvm::ConstantPointerNull::get(
                    llvm::cast<llvm::PointerType>(ctx.getPtrType()));
            }
        }
        return llvm::Constant::getNullValue(ctx.getI32Type());
    }
}

}  // namespace cm::codegen::llvm_backend
