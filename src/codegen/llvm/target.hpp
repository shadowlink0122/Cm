#pragma once

#include "context.hpp"

#include <fstream>
#include <llvm/IR/InlineAsm.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Host.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>

namespace cm::codegen::llvm_backend {

/// ターゲットマネージャ
class TargetManager {
   private:
    TargetConfig config;
    llvm::TargetMachine* targetMachine = nullptr;
    bool initialized = false;

   public:
    /// コンストラクタ
    explicit TargetManager(BuildTarget target) {
        switch (target) {
            case BuildTarget::Baremetal:
                config = TargetConfig::getBaremetalARM();
                break;
            case BuildTarget::Native:
                config = TargetConfig::getNative();
                break;
            case BuildTarget::Wasm:
                config = TargetConfig::getWasm();
                break;
        }
    }

    /// カスタム設定
    explicit TargetManager(const TargetConfig& cfg) : config(cfg) {}

    /// 初期化
    void initialize() {
        if (initialized)
            return;

        // LLVM ターゲット初期化
        llvm::InitializeAllTargetInfos();
        llvm::InitializeAllTargets();
        llvm::InitializeAllTargetMCs();
        llvm::InitializeAllAsmParsers();
        llvm::InitializeAllAsmPrinters();

        // ターゲットマシン作成
        std::string error;
        auto target = llvm::TargetRegistry::lookupTarget(config.triple, error);
        if (!target) {
            throw std::runtime_error("Target not found: " + error);
        }

        llvm::TargetOptions options;
        options.MCOptions.AsmVerbose = true;

        // ベアメタルの場合の特殊設定
        if (config.noStd) {
            options.DisableIntegratedAS = false;
            options.MCOptions.ShowMCEncoding = false;
            options.MCOptions.MCUseDwarfDirectory = false;
        }

        // 最適化レベル設定
        llvm::CodeGenOpt::Level optLevel;
        switch (config.optLevel) {
            case 0:
                optLevel = llvm::CodeGenOpt::None;
                break;
            case 1:
                optLevel = llvm::CodeGenOpt::Less;
                break;
            case 2:
                optLevel = llvm::CodeGenOpt::Default;
                break;
            case 3:
                optLevel = llvm::CodeGenOpt::Aggressive;
                break;
            case -1:
                optLevel = llvm::CodeGenOpt::Default;
                break;  // サイズは後で
            default:
                optLevel = llvm::CodeGenOpt::Default;
        }

        targetMachine =
            target->createTargetMachine(config.triple, config.cpu, config.features, options,
                                        llvm::Reloc::PIC_, llvm::CodeModel::Small, optLevel);

        if (!targetMachine) {
            throw std::runtime_error("Failed to create target machine");
        }

        initialized = true;
    }

    /// モジュール設定
    void configureModule(llvm::Module& module) {
        module.setTargetTriple(config.triple);
        module.setDataLayout(targetMachine->createDataLayout());
    }

    /// オブジェクトファイル生成
    void emitObjectFile(llvm::Module& module, const std::string& filename) {
        std::error_code EC;
        llvm::raw_fd_ostream dest(filename, EC, llvm::sys::fs::OF_None);
        if (EC) {
            throw std::runtime_error("Cannot open file: " + filename);
        }

        llvm::legacy::PassManager pass;
        if (targetMachine->addPassesToEmitFile(pass, dest, nullptr, llvm::CGFT_ObjectFile)) {
            throw std::runtime_error("Target doesn't support object emission");
        }

        pass.run(module);
        dest.flush();
    }

    /// アセンブリ出力
    void emitAssembly(llvm::Module& module, const std::string& filename) {
        std::error_code EC;
        llvm::raw_fd_ostream dest(filename, EC, llvm::sys::fs::OF_None);
        if (EC) {
            throw std::runtime_error("Cannot open file: " + filename);
        }

        llvm::legacy::PassManager pass;
        if (targetMachine->addPassesToEmitFile(pass, dest, nullptr, llvm::CGFT_AssemblyFile)) {
            throw std::runtime_error("Target doesn't support assembly emission");
        }

        pass.run(module);
        dest.flush();
    }

    /// リンカスクリプト生成（ベアメタル用）
    void generateLinkerScript(const std::string& filename) {
        if (config.target != BuildTarget::Baremetal) {
            return;  // ベアメタル以外は不要
        }

        std::ofstream out(filename);
        out << R"(/* Cm Baremetal Linker Script */
MEMORY
{
    FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 256K
    RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 64K
}

ENTRY(_start)

SECTIONS
{
    .text :
    {
        KEEP(*(.vectors))
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
    } > FLASH

    .data :
    {
        _sdata = .;
        *(.data)
        *(.data.*)
        _edata = .;
    } > RAM AT> FLASH

    .bss :
    {
        _sbss = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        _ebss = .;
    } > RAM

    _estack = ORIGIN(RAM) + LENGTH(RAM);
}
)";
    }

    /// スタートアップコード生成（ベアメタル用）
    void generateStartupCode(llvm::Module& module) {
        if (config.target != BuildTarget::Baremetal) {
            return;
        }

        auto& ctx = module.getContext();
        auto& builder = *new llvm::IRBuilder<>(ctx);

        // _start関数
        auto startType = llvm::FunctionType::get(llvm::Type::getVoidTy(ctx), false);
        auto startFunc =
            llvm::Function::Create(startType, llvm::Function::ExternalLinkage, "_start", &module);

        auto bb = llvm::BasicBlock::Create(ctx, "entry", startFunc);
        builder.SetInsertPoint(bb);

        // スタックポインタ設定
        auto spType = llvm::Type::getInt32PtrTy(ctx);
        auto sp = module.getOrInsertGlobal("_estack", spType);
        // ARM: MSPレジスタに設定（インラインアセンブリ）
        auto asmType = llvm::FunctionType::get(llvm::Type::getVoidTy(ctx), {spType}, false);
        auto setMSP = llvm::InlineAsm::get(asmType, "msr msp, $0", "r", true);
        builder.CreateCall(setMSP, {sp});

        // データセクション初期化
        generateDataInit(module, builder);

        // BSSセクションゼロクリア
        generateBssInit(module, builder);

        // main呼び出し
        auto mainFunc = module.getFunction("main");
        if (mainFunc) {
            builder.CreateCall(mainFunc);
        }

        // 無限ループ（リターンしない）
        auto loopBB = llvm::BasicBlock::Create(ctx, "hang", startFunc);
        builder.CreateBr(loopBB);
        builder.SetInsertPoint(loopBB);
        builder.CreateBr(loopBB);

        delete &builder;
    }

   private:
    /// データセクション初期化
    void generateDataInit(llvm::Module& module, llvm::IRBuilder<>& builder) {
        auto& ctx = module.getContext();

        // extern symbols
        auto i8PtrTy = llvm::Type::getInt8PtrTy(ctx);
        auto sdata = module.getOrInsertGlobal("_sdata", i8PtrTy);
        auto edata = module.getOrInsertGlobal("_edata", i8PtrTy);
        auto sidata = module.getOrInsertGlobal("_sidata", i8PtrTy);

        // memcpy(_sdata, _sidata, _edata - _sdata)
        auto memcpy = module.getOrInsertFunction("memcpy", i8PtrTy, i8PtrTy, i8PtrTy,
                                                 llvm::Type::getInt32Ty(ctx));

        auto sdataPtr = builder.CreateLoad(i8PtrTy, sdata);
        auto edataPtr = builder.CreateLoad(i8PtrTy, edata);
        auto sidataPtr = builder.CreateLoad(i8PtrTy, sidata);

        auto size = builder.CreatePtrDiff(i8PtrTy, edataPtr, sdataPtr);
        builder.CreateCall(memcpy, {sdataPtr, sidataPtr, size});
    }

    /// BSSセクション初期化
    void generateBssInit(llvm::Module& module, llvm::IRBuilder<>& builder) {
        auto& ctx = module.getContext();

        // extern symbols
        auto i8PtrTy = llvm::Type::getInt8PtrTy(ctx);
        auto sbss = module.getOrInsertGlobal("_sbss", i8PtrTy);
        auto ebss = module.getOrInsertGlobal("_ebss", i8PtrTy);

        // memset(_sbss, 0, _ebss - _sbss)
        auto memset = module.getOrInsertFunction(
            "memset", i8PtrTy, i8PtrTy, llvm::Type::getInt8Ty(ctx), llvm::Type::getInt32Ty(ctx));

        auto sbssPtr = builder.CreateLoad(i8PtrTy, sbss);
        auto ebssPtr = builder.CreateLoad(i8PtrTy, ebss);

        auto size = builder.CreatePtrDiff(i8PtrTy, ebssPtr, sbssPtr);
        auto zero = llvm::ConstantInt::get(llvm::Type::getInt8Ty(ctx), 0);
        builder.CreateCall(memset, {sbssPtr, zero, size});
    }
};

/// TargetConfig::getNative() 実装
inline TargetConfig TargetConfig::getNative() {
    auto triple = llvm::sys::getDefaultTargetTriple();
    auto cpu = llvm::sys::getHostCPUName().str();

    TargetConfig config;
    config.target = BuildTarget::Native;
    config.triple = triple;
    config.cpu = cpu;
    config.features = "";  // 自動検出
    config.noStd = false;
    config.noMain = false;
    config.optLevel = 2;

    // データレイアウトは後で設定
    return config;
}

}  // namespace cm::codegen::llvm_backend