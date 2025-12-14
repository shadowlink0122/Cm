/// @file interface.cpp
/// @brief インターフェース/vtable関連のLLVMコード生成

#include "../../../common/debug/codegen.hpp"
#include "mir_to_llvm.hpp"

namespace cm::codegen::llvm_backend {

// インターフェース用のfat pointer型を取得
llvm::StructType* MIRToLLVM::getInterfaceFatPtrType(const std::string& interfaceName) {
    auto it = interfaceTypes.find(interfaceName);
    if (it != interfaceTypes.end()) {
        return it->second;
    }

    // fat pointer = {i8* data, i8** vtable}
    std::vector<llvm::Type*> fatPtrFields = {
        ctx.getPtrType(),  // data pointer
        ctx.getPtrType()   // vtable pointer
    };

    auto fatPtrType =
        llvm::StructType::create(ctx.getContext(), fatPtrFields, interfaceName + "_fat_ptr");
    interfaceTypes[interfaceName] = fatPtrType;
    return fatPtrType;
}

// vtable生成
void MIRToLLVM::generateVTables(const mir::MirProgram& program) {
    for (const auto& vtable : program.vtables) {
        if (!vtable)
            continue;

        std::string vtableName = vtable->type_name + "_" + vtable->interface_name + "_vtable";

        // vtableは関数ポインタの配列
        std::vector<llvm::Constant*> vtableEntries;

        for (const auto& entry : vtable->entries) {
            // 実装関数を検索
            auto funcIt = functions.find(entry.impl_function_name);
            if (funcIt != functions.end()) {
                vtableEntries.push_back(funcIt->second);
            } else {
                // 関数がまだ宣言されていない場合は、後で解決するためにnullを入れる
                // これは関数宣言順序の問題を避けるため
                vtableEntries.push_back(llvm::Constant::getNullValue(ctx.getPtrType()));
            }
        }

        // vtable配列型を作成
        auto vtableArrayType = llvm::ArrayType::get(ctx.getPtrType(), vtableEntries.size());
        auto vtableArray = llvm::ConstantArray::get(vtableArrayType, vtableEntries);

        // グローバル変数として定義
        auto vtableGlobal =
            new llvm::GlobalVariable(*module, vtableArrayType,
                                     true,  // isConstant
                                     llvm::GlobalValue::PrivateLinkage, vtableArray, vtableName);

        vtableGlobals[vtable->type_name + "_" + vtable->interface_name] = vtableGlobal;
    }
}

// インターフェースメソッド呼び出し生成
llvm::Value* MIRToLLVM::generateInterfaceMethodCall(const std::string& interfaceName,
                                                    const std::string& methodName,
                                                    llvm::Value* receiver,
                                                    llvm::ArrayRef<llvm::Value*> args) {
    // receiverはfat pointer型
    [[maybe_unused]] auto fatPtrType = getInterfaceFatPtrType(interfaceName);

    // dataポインタを取得
    auto dataPtr = builder->CreateExtractValue(receiver, {0}, "data_ptr");

    // vtableポインタを取得
    auto vtablePtr = builder->CreateExtractValue(receiver, {1}, "vtable_ptr");

    // インターフェース定義からメソッドインデックスを検索
    int methodIndex = -1;
    if (currentProgram) {
        for (const auto& iface : currentProgram->interfaces) {
            if (iface && iface->name == interfaceName) {
                for (size_t i = 0; i < iface->methods.size(); ++i) {
                    if (iface->methods[i].name == methodName) {
                        methodIndex = static_cast<int>(i);
                        break;
                    }
                }
                break;
            }
        }
    }

    if (methodIndex < 0) {
        cm::debug::codegen::log(
            cm::debug::codegen::Id::LLVMError,
            "Method not found in interface: " + interfaceName + "::" + methodName,
            cm::debug::Level::Error);
        return nullptr;
    }

    // vtableから関数ポインタを取得
    auto vtableArrayType = llvm::ArrayType::get(ctx.getPtrType(), 1);  // 仮のサイズ
    auto funcPtrPtr = builder->CreateGEP(vtableArrayType, vtablePtr,
                                         {llvm::ConstantInt::get(ctx.getI32Type(), 0),
                                          llvm::ConstantInt::get(ctx.getI32Type(), methodIndex)},
                                         "func_ptr_ptr");
    auto funcPtr = builder->CreateLoad(ctx.getPtrType(), funcPtrPtr, "func_ptr");

    // 関数を呼び出す（最初の引数はdataPtr）
    std::vector<llvm::Value*> callArgs;
    callArgs.push_back(dataPtr);
    callArgs.insert(callArgs.end(), args.begin(), args.end());

    // 関数型を構築（void戻り値、最初の引数はi8*）
    std::vector<llvm::Type*> paramTypes = {ctx.getPtrType()};
    for (auto arg : args) {
        paramTypes.push_back(arg->getType());
    }
    auto funcType = llvm::FunctionType::get(ctx.getVoidType(), paramTypes, false);

    return builder->CreateCall(funcType, funcPtr, callArgs);
}

}  // namespace cm::codegen::llvm_backend
