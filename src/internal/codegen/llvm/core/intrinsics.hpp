#pragma once

#include "context.hpp"

#include <llvm/Config/llvm-config.h>  // LLVM_VERSION_MAJOR を定義（最初にインクルード）
#include <llvm/IR/Function.h>
#include <llvm/IR/Intrinsics.h>
#include <string>
#include <unordered_map>

namespace cm::codegen::llvm_backend {

/// 組み込み関数管理（各メソッドの実装は intrinsics.cpp）
class IntrinsicsManager {
   private:
    llvm::Module* module;
    llvm::LLVMContext* context;
    TargetConfig config;

    // キャッシュ
    std::unordered_map<std::string, llvm::Function*> intrinsics;

   public:
    IntrinsicsManager(llvm::Module* m, llvm::LLVMContext* ctx, const TargetConfig& cfg)
        : module(m), context(ctx), config(cfg) {}

    /// 全組み込み関数を宣言
    void declareAll();

    /// 組み込み関数取得
    llvm::Function* get(const std::string& name);

   private:
    /// メモリ操作組み込み
    void declareMemoryIntrinsics();

    /// 数学関数組み込み
    void declareMathIntrinsics();

    /// ビット操作組み込み
    void declareBitIntrinsics();

    /// アトミック操作組み込み
    void declareAtomicIntrinsics();

    /// プラットフォーム固有組み込み
    void declarePlatformSpecific();

    /// ベアメタル用組み込み
    void declareBaremetalIntrinsics();

    /// WebAssembly用組み込み
    void declareWasmIntrinsics();

    /// ネイティブ用組み込み
    void declareNativeIntrinsics();

    /// Linux システムコール
    void declareLinuxSyscalls();

    void declareWindowsIntrinsics();

    void declareDarwinIntrinsics();

    /// 数学関数ヘルパー
    void declareMathFunc(const std::string& name, llvm::Type* type);

    /// abs関数ヘルパー
    void declareAbsFunc(const std::string& name, llvm::Type* type);

    /// ビット操作ヘルパー
    void declareBitFunc(const std::string& name, llvm::Type* type);

    /// ビット操作ヘルパー（2引数）
    void declareBitFunc2(const std::string& name, llvm::Type* type);

    /// LLVM組み込み関数取得
    llvm::Function* getOrDeclareLLVMIntrinsic(const std::string& name);
};

}  // namespace cm::codegen::llvm_backend
