#pragma once

#include <llvm/Config/llvm-config.h>  // LLVM_VERSION_MAJOR を定義（最初にインクルード）

// LLVM_VERSION_MAJORが定義されていることを確認
#ifndef LLVM_VERSION_MAJOR
#error "LLVM_VERSION_MAJOR is not defined. llvm-config.h may not be included correctly."
#endif

#include "../core/context.hpp"
#include "safe_codegen.hpp"

#include <cstdlib>  // exit()
#include <fstream>
#include <llvm/IR/InlineAsm.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/ErrorHandling.h>  // install_fatal_error_handler
#include <llvm/Support/FileSystem.h>
#if LLVM_VERSION_MAJOR >= 16
#include <llvm/TargetParser/Host.h>
#else
#include <llvm/Support/Host.h>
#endif
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>

// WebAssemblyターゲット初期化関数のextern C宣言
extern "C" {
void LLVMInitializeWebAssemblyTargetInfo();
void LLVMInitializeWebAssemblyTarget();
void LLVMInitializeWebAssemblyTargetMC();
void LLVMInitializeWebAssemblyAsmParser();
void LLVMInitializeWebAssemblyAsmPrinter();
}

namespace cm::codegen::llvm_backend {

// LLVMのFatal Errorで即時終了（ハング防止）
inline void llvmFatalErrorHandler(void* /*user_data*/, const char* message, bool gen_crash_diag) {
    std::cerr << "\n[LLVM Fatal Error] " << message << std::endl;
    if (gen_crash_diag) {
        std::cerr << "[LLVM] Please report this bug.\n";
    }
    std::exit(1);
}

/// ターゲットマネージャ
class TargetManager {
   private:
    TargetConfig config;
    llvm::TargetMachine* targetMachine = nullptr;
    bool initialized = false;

   public:
    /// コンストラクタ
    explicit TargetManager(BuildTarget target);

    /// カスタム設定
    explicit TargetManager(const TargetConfig& cfg) : config(cfg) {}

    /// 初期化
    void initialize();

    /// TargetMachine取得（PassBuilder用）
    llvm::TargetMachine* getTargetMachine() const { return targetMachine; }

    /// モジュール設定
    void configureModule(llvm::Module& module);

    /// オブジェクトファイル生成（安全版）
    void emitObjectFile(llvm::Module& module, const std::string& filename);

    /// アセンブリ出力（安全版）
    void emitAssembly(llvm::Module& module, const std::string& filename);

    /// リンカスクリプト生成（ベアメタル用）
    void generateLinkerScript(const std::string& filename);

    /// スタートアップコード生成（ベアメタル用）
    void generateStartupCode(llvm::Module& module);

   private:
    /// データセクション初期化
    void generateDataInit(llvm::Module& module, llvm::IRBuilder<>& builder);

    /// BSSセクション初期化
    void generateBssInit(llvm::Module& module, llvm::IRBuilder<>& builder);
};

}  // namespace cm::codegen::llvm_backend