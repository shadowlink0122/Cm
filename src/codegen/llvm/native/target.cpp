// ターゲットマネージャ実装
#include "target.hpp"

namespace cm::codegen::llvm_backend {

// コンストラクタ（BuildTarget指定）
TargetManager::TargetManager(BuildTarget target) {
    switch (target) {
        case BuildTarget::Baremetal:
            config = TargetConfig::getBaremetalARM();
            break;
        case BuildTarget::BaremetalX86:
            config = TargetConfig::getBaremetalX86();
            break;
        case BuildTarget::Native:
            config = TargetConfig::getNative();
            break;
        case BuildTarget::Wasm:
            config = TargetConfig::getWasm();
            break;
        case BuildTarget::BaremetalUEFI:
            config = TargetConfig::getBaremetalUEFI();
            break;
    }
}

// 初期化
void TargetManager::initialize() {
    if (initialized)
        return;

    // LLVM Fatal Error Handlerを設定
    static bool errorHandlerInstalled = false;
    if (!errorHandlerInstalled) {
        llvm::install_fatal_error_handler(llvmFatalErrorHandler, nullptr);
        errorHandlerInstalled = true;
    }

    // ネイティブターゲットの初期化
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();

    // WebAssemblyターゲットの初期化
    LLVMInitializeWebAssemblyTargetInfo();
    LLVMInitializeWebAssemblyTarget();
    LLVMInitializeWebAssemblyTargetMC();
    LLVMInitializeWebAssemblyAsmParser();
    LLVMInitializeWebAssemblyAsmPrinter();

    // X86ターゲットの初期化
    LLVMInitializeX86TargetInfo();
    LLVMInitializeX86Target();
    LLVMInitializeX86TargetMC();
    LLVMInitializeX86AsmParser();
    LLVMInitializeX86AsmPrinter();

    // ARMターゲットの初期化
    LLVMInitializeARMTargetInfo();
    LLVMInitializeARMTarget();
    LLVMInitializeARMTargetMC();
    LLVMInitializeARMAsmParser();
    LLVMInitializeARMAsmPrinter();

    // ターゲットマシン作成
    std::string error;
    auto target = llvm::TargetRegistry::lookupTarget(config.triple, error);
    if (!target) {
        throw std::runtime_error("Target not found: " + error);
    }

    llvm::TargetOptions options;
    options.MCOptions.AsmVerbose = true;

    if (config.noStd) {
        options.DisableIntegratedAS = false;
        options.MCOptions.ShowMCEncoding = false;
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
            break;
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

// モジュール設定
void TargetManager::configureModule(llvm::Module& module) {
    module.setTargetTriple(config.triple);
    module.setDataLayout(targetMachine->createDataLayout());
}

// オブジェクトファイル生成
void TargetManager::emitObjectFile(llvm::Module& module, const std::string& filename) {
    if (!SafeCodeGenerator::checkComplexity(module)) {
        std::cerr << "[CODEGEN] Warning: Module complexity is high, proceeding with caution\n";
    }

    try {
        SafeCodeGenerator::emitObjectFileSafe(module, targetMachine, filename,
                                              std::chrono::seconds(30));
    } catch (const std::exception& e) {
        std::string error_msg = "Failed to generate object file: ";
        error_msg += e.what();
        if (error_msg.find("timeout") != std::string::npos) {
            error_msg += "\nHint: Try reducing optimization level (use -O1 or -O0)";
        }
        throw std::runtime_error(error_msg);
    }
}

// アセンブリ出力
void TargetManager::emitAssembly(llvm::Module& module, const std::string& filename) {
    if (!SafeCodeGenerator::checkComplexity(module)) {
        std::cerr << "[CODEGEN] Warning: Module complexity is high, proceeding with caution\n";
    }

    try {
        SafeCodeGenerator::emitAssemblySafe(module, targetMachine, filename,
                                            std::chrono::seconds(30));
    } catch (const std::exception& e) {
        std::string error_msg = "Failed to generate assembly: ";
        error_msg += e.what();
        if (error_msg.find("timeout") != std::string::npos) {
            error_msg += "\nHint: Try reducing optimization level (use -O1 or -O0)";
        }
        throw std::runtime_error(error_msg);
    }
}

// リンカスクリプト生成（ベアメタル用）
void TargetManager::generateLinkerScript(const std::string& filename) {
    if (config.target != BuildTarget::Baremetal) {
        return;
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

// スタートアップコード生成（ベアメタル用）
void TargetManager::generateStartupCode(llvm::Module& module) {
    if (config.target != BuildTarget::Baremetal) {
        return;
    }

    auto& ctx = module.getContext();
    auto& builder = *new llvm::IRBuilder<>(ctx);

    auto startType = llvm::FunctionType::get(llvm::Type::getVoidTy(ctx), false);
    auto startFunc =
        llvm::Function::Create(startType, llvm::Function::ExternalLinkage, "_start", &module);

    auto bb = llvm::BasicBlock::Create(ctx, "entry", startFunc);
    builder.SetInsertPoint(bb);

    auto spType = llvm::PointerType::get(ctx, 0);
    auto sp = module.getOrInsertGlobal("_estack", spType);
    auto asmType = llvm::FunctionType::get(llvm::Type::getVoidTy(ctx), {spType}, false);
    auto setMSP = llvm::InlineAsm::get(asmType, "msr msp, $0", "r", true);
    builder.CreateCall(setMSP, {sp});

    generateDataInit(module, builder);
    generateBssInit(module, builder);

    auto mainFunc = module.getFunction("main");
    if (mainFunc) {
        builder.CreateCall(mainFunc);
    }

    auto loopBB = llvm::BasicBlock::Create(ctx, "hang", startFunc);
    builder.CreateBr(loopBB);
    builder.SetInsertPoint(loopBB);
    builder.CreateBr(loopBB);

    delete &builder;
}

// データセクション初期化
void TargetManager::generateDataInit(llvm::Module& module, llvm::IRBuilder<>& builder) {
    auto& ctx = module.getContext();
    auto ptrTy = llvm::PointerType::get(ctx, 0);
    auto sdata = module.getOrInsertGlobal("_sdata", ptrTy);
    auto edata = module.getOrInsertGlobal("_edata", ptrTy);
    auto sidata = module.getOrInsertGlobal("_sidata", ptrTy);

    auto memcpy =
        module.getOrInsertFunction("memcpy", ptrTy, ptrTy, ptrTy, llvm::Type::getInt32Ty(ctx));

    auto sdataPtr = builder.CreateLoad(ptrTy, sdata, "sdata_ptr");
    auto edataPtr = builder.CreateLoad(ptrTy, edata, "edata_ptr");
    auto sidataPtr = builder.CreateLoad(ptrTy, sidata, "sidata_ptr");

    auto size = builder.CreatePtrDiff(ptrTy, edataPtr, sdataPtr);
    builder.CreateCall(memcpy, {sdataPtr, sidataPtr, size});
}

// BSSセクション初期化
void TargetManager::generateBssInit(llvm::Module& module, llvm::IRBuilder<>& builder) {
    auto& ctx = module.getContext();
    auto ptrTy = llvm::PointerType::get(ctx, 0);
    auto sbss = module.getOrInsertGlobal("_sbss", ptrTy);
    auto ebss = module.getOrInsertGlobal("_ebss", ptrTy);

    auto memset = module.getOrInsertFunction("memset", ptrTy, ptrTy, llvm::Type::getInt8Ty(ctx),
                                             llvm::Type::getInt32Ty(ctx));

    auto sbssPtr = builder.CreateLoad(ptrTy, sbss);
    auto ebssPtr = builder.CreateLoad(ptrTy, ebss);

    auto size = builder.CreatePtrDiff(ptrTy, ebssPtr, sbssPtr);
    auto zero = llvm::ConstantInt::get(llvm::Type::getInt8Ty(ctx), 0);
    builder.CreateCall(memset, {sbssPtr, zero, size});
}

// TargetConfig::getNative() 実装
TargetConfig TargetConfig::getNative() {
    auto hostTriple = llvm::sys::getDefaultTargetTriple();
    auto hostCpu = llvm::sys::getHostCPUName().str();

#ifdef CM_DEFAULT_TARGET_ARCH
    std::string targetArch = CM_DEFAULT_TARGET_ARCH;

    bool isArm64 = (targetArch == "arm64" || targetArch == "aarch64");
    bool isX86 = (targetArch == "x86_64");

    std::string triple = hostTriple;
    std::string cpu = hostCpu;

    if (isArm64 && hostTriple.find("x86_64") != std::string::npos) {
        auto pos = triple.find("x86_64");
        if (pos != std::string::npos) {
            triple.replace(pos, 6, "aarch64");
        }
        cpu = "generic";
    } else if (isX86 && (hostTriple.find("arm64") != std::string::npos ||
                         hostTriple.find("aarch64") != std::string::npos)) {
        auto pos = triple.find("arm64");
        if (pos != std::string::npos) {
            triple.replace(pos, 5, "x86_64");
        } else {
            pos = triple.find("aarch64");
            if (pos != std::string::npos) {
                triple.replace(pos, 7, "x86_64");
            }
        }
        cpu = "generic";
    }
#else
    std::string triple = hostTriple;
    std::string cpu = hostCpu;
#endif

    llvm::StringMap<bool> features;
    llvm::sys::getHostCPUFeatures(features);
    std::string featureStr;
    if (cpu != "generic") {
        for (const auto& feature : features) {
            if (feature.second) {
                if (!featureStr.empty())
                    featureStr += ",";
                featureStr += "+" + feature.first().str();
            }
        }
    }

    TargetConfig config;
    config.target = BuildTarget::Native;
    config.triple = triple;
    config.cpu = cpu;
    config.features = featureStr;
    config.noStd = false;
    config.noMain = false;
    config.optLevel = 2;

    return config;
}

}  // namespace cm::codegen::llvm_backend