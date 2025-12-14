/// @file type_converter.hpp
/// @brief HIR型からLLVM型への変換インターフェース
///
/// native/wasmで共通で使用する型変換ロジック

#pragma once

#include "../../hir/hir_nodes.hpp"

#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Type.h>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace cm::codegen::common {

/// 型変換の基底クラス
/// native/wasmで同じ変換ロジックを共有する
class TypeConverter {
   protected:
    llvm::LLVMContext& llvmCtx;

    // 構造体型キャッシュ
    std::unordered_map<std::string, llvm::StructType*> structTypes;

    // インターフェース名のセット
    std::unordered_set<std::string> interfaceNames;

    // インターフェース型キャッシュ (fat pointer)
    std::unordered_map<std::string, llvm::StructType*> interfaceTypes;

    // 基本型キャッシュ
    llvm::Type* voidTy = nullptr;
    llvm::Type* boolTy = nullptr;
    llvm::Type* i8Ty = nullptr;
    llvm::Type* i16Ty = nullptr;
    llvm::Type* i32Ty = nullptr;
    llvm::Type* i64Ty = nullptr;
    llvm::Type* f32Ty = nullptr;
    llvm::Type* f64Ty = nullptr;
    llvm::Type* ptrTy = nullptr;

   public:
    explicit TypeConverter(llvm::LLVMContext& ctx) : llvmCtx(ctx) { initBasicTypes(); }

    virtual ~TypeConverter() = default;

    /// HIR型をLLVM型に変換
    virtual llvm::Type* convert(const hir::TypePtr& type);

    /// 構造体型を登録
    void registerStructType(const std::string& name, llvm::StructType* type) {
        structTypes[name] = type;
    }

    /// インターフェース名を登録
    void registerInterface(const std::string& name) { interfaceNames.insert(name); }

    /// インターフェース型かどうか
    bool isInterfaceType(const std::string& name) const { return interfaceNames.count(name) > 0; }

    /// インターフェース用のfat pointer型を取得
    llvm::StructType* getInterfaceFatPtrType(const std::string& interfaceName);

    // 基本型アクセサ
    llvm::Type* getVoidType() const { return voidTy; }
    llvm::Type* getBoolType() const { return boolTy; }
    llvm::Type* getI8Type() const { return i8Ty; }
    llvm::Type* getI16Type() const { return i16Ty; }
    llvm::Type* getI32Type() const { return i32Ty; }
    llvm::Type* getI64Type() const { return i64Ty; }
    llvm::Type* getF32Type() const { return f32Ty; }
    llvm::Type* getF64Type() const { return f64Ty; }
    llvm::Type* getPtrType() const { return ptrTy; }

   private:
    void initBasicTypes();
};

}  // namespace cm::codegen::common
