#pragma once

#include <llvm/Config/llvm-config.h>  // LLVM_VERSION_MAJOR を定義（最初にインクルード）

// LLVM_VERSION_MAJORが定義されていることを確認
#ifndef LLVM_VERSION_MAJOR
#error "LLVM_VERSION_MAJOR is not defined. llvm-config.h may not be included correctly."
#endif

#include "../core/context.hpp"
#include "safe_codegen.hpp"

#include <fstream>
#include <llvm/IR/InlineAsm.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/MC/TargetRegistry.h>
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

// LLVM 17以降でMCUseDwarfDirectoryがenum型に変更
#if LLVM_VERSION_MAJOR >= 17
            options.MCOptions.MCUseDwarfDirectory = llvm::MCTargetOptions::DisableDwarfDirectory;
#else
            options.MCOptions.MCUseDwarfDirectory = false;
#endif
        }

        // 最適化レベル設定
#if LLVM_VERSION_MAJOR >= 18
        llvm::CodeGenOptLevel optLevel;
#else
        llvm::CodeGenOpt::Level optLevel;
#endif
        switch (config.optLevel) {
            case 0:
#if LLVM_VERSION_MAJOR >= 18
                optLevel = llvm::CodeGenOptLevel::None;
#else
                optLevel = llvm::CodeGenOpt::None;
#endif
                break;
            case 1:
#if LLVM_VERSION_MAJOR >= 18
                optLevel = llvm::CodeGenOptLevel::Less;
#else
                optLevel = llvm::CodeGenOpt::Less;
#endif
                break;
            case 2:
#if LLVM_VERSION_MAJOR >= 18
                optLevel = llvm::CodeGenOptLevel::Default;
#else
                optLevel = llvm::CodeGenOpt::Default;
#endif
                break;
            case 3:
#if LLVM_VERSION_MAJOR >= 18
                optLevel = llvm::CodeGenOptLevel::Aggressive;
#else
                optLevel = llvm::CodeGenOpt::Aggressive;
#endif
                break;
            case -1:
#if LLVM_VERSION_MAJOR >= 18
                optLevel = llvm::CodeGenOptLevel::Default;
#else
                optLevel = llvm::CodeGenOpt::Default;
#endif
                break;  // サイズは後で
            default:
#if LLVM_VERSION_MAJOR >= 18
                optLevel = llvm::CodeGenOptLevel::Default;
#else
                optLevel = llvm::CodeGenOpt::Default;
#endif
        }

        targetMachine =
            target->createTargetMachine(config.triple, config.cpu, config.features, options,
                                        llvm::Reloc::PIC_, llvm::CodeModel::Small, optLevel);

        if (!targetMachine) {
            throw std::runtime_error("Failed to create target machine");
        }

        initialized = true;
    }

    /// TargetMachine取得（PassBuilder用）
    llvm::TargetMachine* getTargetMachine() const { return targetMachine; }

    /// モジュール設定
    void configureModule(llvm::Module& module) {
        module.setTargetTriple(config.triple);
        module.setDataLayout(targetMachine->createDataLayout());
    }

    /// オブジェクトファイル生成（安全版）
    void emitObjectFile(llvm::Module& module, const std::string& filename) {
        // 複雑度チェック
        if (!SafeCodeGenerator::checkComplexity(module)) {
            std::cerr << "[CODEGEN] Warning: Module complexity is high, proceeding with caution\n";
        }

        // タイムアウト付きで安全にコード生成
        try {
            SafeCodeGenerator::emitObjectFileSafe(module, targetMachine, filename,
                                                  std::chrono::seconds(30));
        } catch (const std::exception& e) {
            // エラーメッセージを改善
            std::string error_msg = "Failed to generate object file: ";
            error_msg += e.what();
            if (error_msg.find("timeout") != std::string::npos) {
                error_msg += "\nHint: Try reducing optimization level (use -O1 or -O0)";
            }
            throw std::runtime_error(error_msg);
        }
    }

    /// アセンブリ出力（安全版）
    void emitAssembly(llvm::Module& module, const std::string& filename) {
        // 複雑度チェック
        if (!SafeCodeGenerator::checkComplexity(module)) {
            std::cerr << "[CODEGEN] Warning: Module complexity is high, proceeding with caution\n";
        }

        // タイムアウト付きで安全にコード生成
        try {
            SafeCodeGenerator::emitAssemblySafe(module, targetMachine, filename,
                                                std::chrono::seconds(30));
        } catch (const std::exception& e) {
            // エラーメッセージを改善
            std::string error_msg = "Failed to generate assembly: ";
            error_msg += e.what();
            if (error_msg.find("timeout") != std::string::npos) {
                error_msg += "\nHint: Try reducing optimization level (use -O1 or -O0)";
            }
            throw std::runtime_error(error_msg);
        }
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
        // LLVM 14+: opaque pointers を使用
        auto spType = llvm::PointerType::get(ctx, 0);
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
        // LLVM 14+: opaque pointers を使用
        auto ptrTy = llvm::PointerType::get(ctx, 0);
        auto sdata = module.getOrInsertGlobal("_sdata", ptrTy);
        auto edata = module.getOrInsertGlobal("_edata", ptrTy);
        auto sidata = module.getOrInsertGlobal("_sidata", ptrTy);

        // memcpy(_sdata, _sidata, _edata - _sdata)
        auto memcpy =
            module.getOrInsertFunction("memcpy", ptrTy, ptrTy, ptrTy, llvm::Type::getInt32Ty(ctx));

        // グローバル変数（ポインタへのポインタ）からポインタをロード
        auto sdataPtr = builder.CreateLoad(ptrTy, sdata, "sdata_ptr");
        auto edataPtr = builder.CreateLoad(ptrTy, edata, "edata_ptr");
        auto sidataPtr = builder.CreateLoad(ptrTy, sidata, "sidata_ptr");

        auto size = builder.CreatePtrDiff(ptrTy, edataPtr, sdataPtr);
        builder.CreateCall(memcpy, {sdataPtr, sidataPtr, size});
    }

    /// BSSセクション初期化
    void generateBssInit(llvm::Module& module, llvm::IRBuilder<>& builder) {
        auto& ctx = module.getContext();

        // extern symbols
        // LLVM 14+: opaque pointers を使用
        auto ptrTy = llvm::PointerType::get(ctx, 0);
        auto sbss = module.getOrInsertGlobal("_sbss", ptrTy);
        auto ebss = module.getOrInsertGlobal("_ebss", ptrTy);

        // memset(_sbss, 0, _ebss - _sbss)
        auto memset = module.getOrInsertFunction("memset", ptrTy, ptrTy, llvm::Type::getInt8Ty(ctx),
                                                 llvm::Type::getInt32Ty(ctx));

        auto sbssPtr = builder.CreateLoad(ptrTy, sbss);
        auto ebssPtr = builder.CreateLoad(ptrTy, ebss);

        auto size = builder.CreatePtrDiff(ptrTy, ebssPtr, sbssPtr);
        auto zero = llvm::ConstantInt::get(llvm::Type::getInt8Ty(ctx), 0);
        builder.CreateCall(memset, {sbssPtr, zero, size});
    }
};

/// TargetConfig::getNative() 実装
inline TargetConfig TargetConfig::getNative() {
    auto triple = llvm::sys::getDefaultTargetTriple();
    auto cpu = llvm::sys::getHostCPUName().str();

    // CPUの機能フラグを取得（SSE, AVX, NEON等）してSIMDベクトル化を有効にする
    llvm::StringMap<bool> features;
    llvm::sys::getHostCPUFeatures(features);
    std::string featureStr;
    for (const auto& feature : features) {
        if (feature.second) {  // 機能が有効な場合
            if (!featureStr.empty())
                featureStr += ",";
            featureStr += "+" + feature.first().str();
        }
    }

    TargetConfig config;
    config.target = BuildTarget::Native;
    config.triple = triple;
    config.cpu = cpu;
    config.features = featureStr;  // CPU機能を有効化
    config.noStd = false;
    config.noMain = false;
    config.optLevel = 2;

    // データレイアウトは後で設定
    return config;
}

}  // namespace cm::codegen::llvm_backend