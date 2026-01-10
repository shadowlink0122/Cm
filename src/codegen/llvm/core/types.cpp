/// @file llvm_types.cpp
/// @brief 型変換・定数変換処理

#include "mir_to_llvm.hpp"

#include <iostream>
#include <variant>

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
        case hir::TypeKind::Reference: {
            // LLVM 14+: opaque pointersを使用するが、元の型情報は保持する
            // ポインタ型はctx.getPtrType()を返すが、
            // 内部的には元の型情報（type->element_type）を保持している
            // store/load時に適切な型でキャストする
            return ctx.getPtrType();
        }
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
            std::string lookupName = type->name;

            // ジェネリック構造体の場合、型引数を考慮した名前を生成
            // 例: Node<int> -> Node__int
            if (!type->type_args.empty()) {
                for (const auto& typeArg : type->type_args) {
                    if (typeArg) {
                        lookupName += "__";
                        // 型名を正規化（Pointer<int>ならint*など）
                        if (typeArg->kind == hir::TypeKind::Struct) {
                            lookupName += typeArg->name;
                        } else {
                            // プリミティブ型の場合
                            switch (typeArg->kind) {
                                case hir::TypeKind::Int:
                                    lookupName += "int";
                                    break;
                                case hir::TypeKind::UInt:
                                    lookupName += "uint";
                                    break;
                                case hir::TypeKind::Long:
                                    lookupName += "long";
                                    break;
                                case hir::TypeKind::ULong:
                                    lookupName += "ulong";
                                    break;
                                case hir::TypeKind::Float:
                                    lookupName += "float";
                                    break;
                                case hir::TypeKind::Double:
                                    lookupName += "double";
                                    break;
                                case hir::TypeKind::Bool:
                                    lookupName += "bool";
                                    break;
                                case hir::TypeKind::Char:
                                    lookupName += "char";
                                    break;
                                case hir::TypeKind::String:
                                    lookupName += "string";
                                    break;
                                default:
                                    // その他の型は型名をそのまま使用
                                    if (!typeArg->name.empty()) {
                                        lookupName += typeArg->name;
                                    }
                                    break;
                            }
                        }
                    }
                }
            }

            auto it = structTypes.find(lookupName);
            if (it != structTypes.end()) {
                return it->second;
            }

            // フォールバック: 元の名前でも検索
            if (lookupName != type->name) {
                auto it2 = structTypes.find(type->name);
                if (it2 != structTypes.end()) {
                    return it2->second;
                }
            }

            // 見つからない場合は不透明型として扱う
            return llvm::StructType::create(ctx.getContext(), lookupName);
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
            (void)llvm::FunctionType::get(retType, paramTypes, false);  // 型チェックのみ
#if LLVM_VERSION_MAJOR >= 15
            // LLVM 15+: opaque pointerを使用（関数ポインタも単なるptr）
            return ctx.getPtrType();
#else
            // LLVM 14: typed pointer（関数ポインタ型）を返す
            auto funcType = llvm::FunctionType::get(retType, paramTypes, false);
            return llvm::PointerType::get(funcType, 0);
#endif
        }
        default:
            return ctx.getI32Type();
    }
}

// ポインタ型から指す先の型を取得
llvm::Type* MIRToLLVM::getPointeeType(const hir::TypePtr& ptrType) {
    if (!ptrType) {
        return ctx.getI32Type();
    }

    // ポインタ型または参照型の場合
    if (ptrType->kind == hir::TypeKind::Pointer || ptrType->kind == hir::TypeKind::Reference) {
        if (ptrType->element_type) {
            return convertType(ptrType->element_type);
        }
        // 要素型が不明な場合はi8として扱う
        return ctx.getI8Type();
    }

    // 関数型の場合（関数ポインタ）
    if (ptrType->kind == hir::TypeKind::Function) {
        // 関数型自体を返す（呼び出し時に使用）
        llvm::Type* retType =
            ptrType->return_type ? convertType(ptrType->return_type) : ctx.getVoidType();
        std::vector<llvm::Type*> paramTypes;
        for (const auto& paramType : ptrType->param_types) {
            paramTypes.push_back(convertType(paramType));
        }
        return llvm::FunctionType::get(retType, paramTypes, false);
    }

    // 配列型（動的配列/スライス）
    if (ptrType->kind == hir::TypeKind::Array && !ptrType->array_size.has_value()) {
        if (ptrType->element_type) {
            return convertType(ptrType->element_type);
        }
        return ctx.getI8Type();
    }

    // その他の型はそのまま変換
    return convertType(ptrType);
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
        // std::cerr << "[MIR2LLVM]             convertConstant: val=" << val << "\n";

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
