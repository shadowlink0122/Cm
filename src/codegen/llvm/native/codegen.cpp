// LLVMコード生成器の実装
#include "codegen.hpp"

#include "../optimizations/mir_pattern_detector.hpp"
#include "../optimizations/optimization_manager.hpp"
#include "../optimizations/pass_limiter.hpp"
#include "../optimizations/recursion_limiter.hpp"
#include "pass_debugger.hpp"

namespace cm::codegen::llvm_backend {

// MIRプログラムをコンパイル
void LLVMCodeGen::compile(const mir::MirProgram& program) {
    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMStart);

    // インポートの有無を記録（最適化時に使用）
    hasImports = !program.imports.empty();
    if (hasImports) {
        cm::debug::codegen::log(cm::debug::codegen::Id::LLVMInit,
                                "Program has imports - optimization will be limited");
    }

    // 0. MIRレベルでのパターン検出と最適化レベル調整
    int adjusted_level =
        MIRPatternDetector::adjustOptimizationLevel(program, options.optimizationLevel);
    if (adjusted_level != options.optimizationLevel) {
        if (cm::debug::g_debug_mode) {
            std::cerr << "[MIR] 最適化レベルを O" << options.optimizationLevel << " から O"
                      << adjusted_level << " に変更しました（MIRパターン検出による）\n";
        }
        options.optimizationLevel = adjusted_level;
    }

    // 1. 初期化
    initialize(program.filename);

    // 2. MIR → LLVM IR 変換
    generateIR(program);

    // 3. 検証
    if (options.verifyIR) {
        verifyModule();
    }

    // 3.5. 最適化前のパターン検出と調整
    if (options.optimizationLevel > 0) {
        int adjusted_level2 = OptimizationPassLimiter::adjustOptimizationLevel(
            context->getModule(), options.optimizationLevel);

        if (adjusted_level2 != options.optimizationLevel) {
            if (cm::debug::g_debug_mode) {
                std::cerr << "[LLVM] 最適化レベルを O" << options.optimizationLevel << " から O"
                          << adjusted_level2 << " に変更しました\n";
            }
            options.optimizationLevel = adjusted_level2;
        }
    }

    // 4. 最適化
    optimize();

    // 5. 出力
    emit();

    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMEnd);
}

// LLVM IR を文字列として取得（デバッグ用）
std::string LLVMCodeGen::getIRString() const {
    std::string str;
    llvm::raw_string_ostream os(str);
    context->getModule().print(os, nullptr);
    return str;
}

// 初期化
void LLVMCodeGen::initialize(const std::string& moduleName) {
    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMInit);

    // ターゲット設定
    TargetConfig config;
    if (!options.customTriple.empty()) {
        config.triple = options.customTriple;
        config.target = BuildTarget::Native;
    } else {
        switch (options.target) {
            case BuildTarget::Baremetal:
                config = TargetConfig::getBaremetalARM();
                break;
            case BuildTarget::BaremetalX86:
                config = TargetConfig::getBaremetalX86();
                break;
            case BuildTarget::Wasm:
                config = TargetConfig::getWasm();
                break;
            case BuildTarget::BaremetalUEFI:
                config = TargetConfig::getBaremetalUEFI();
                break;
            default:
                config = TargetConfig::getNative();
        }
    }
    config.debugInfo = options.debugInfo;
    config.optLevel = options.optimizationLevel;

    // コンテキスト作成
    context = std::make_unique<LLVMContext>(moduleName, config);

    // ターゲットマネージャ
    targetManager = std::make_unique<TargetManager>(config);
    targetManager->initialize();
    targetManager->configureModule(context->getModule());

    // 組み込み関数マネージャ（宣言は遅延実行）
    intrinsicsManager = std::make_unique<IntrinsicsManager>(&context->getModule(),
                                                            &context->getContext(), config);

    // 変換器
    converter = std::make_unique<MIRToLLVM>(*context);

    // ベアメタルの場合、スタートアップコード生成
    if (config.target == BuildTarget::Baremetal) {
        targetManager->generateStartupCode(context->getModule());
    }
}

// IR生成
void LLVMCodeGen::generateIR(const mir::MirProgram& program) {
    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMIRGen, "Generating LLVM IR from MIR");
    converter->convert(program);
    if (options.verbose) {
        llvm::errs() << "=== Generated LLVM IR ===\n";
        context->getModule().print(llvm::errs(), nullptr);
        llvm::errs() << "========================\n";
    }
}

// モジュール検証
void LLVMCodeGen::verifyModule() {
    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMVerify);
    std::string errors;
    llvm::raw_string_ostream os(errors);
    if (llvm::verifyModule(context->getModule(), &os)) {
        context->getModule().print(llvm::errs(), nullptr);
        cm::debug::codegen::log(cm::debug::codegen::Id::LLVMError,
                                "Module verification failed: " + errors,
                                cm::debug::Level::Error);
        throw std::runtime_error("LLVM module verification failed:\n" + errors);
    }
    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMVerifyOK);
}

// 最適化
void LLVMCodeGen::optimize() {
    if (options.optimizationLevel == 0) {
        return;
    }

    // 再帰とループの事前検証
    RecursionLimiter::preprocessModule(context->getModule(), options.optimizationLevel);

    // パターンベースの最適化レベル調整
    int adjustedLevel = OptimizationPassLimiter::adjustOptimizationLevel(
        context->getModule(), options.optimizationLevel);
    if (adjustedLevel != options.optimizationLevel) {
        cm::debug::codegen::log(cm::debug::codegen::Id::LLVMOptimize,
                                "Optimization level adjusted from O" +
                                    std::to_string(options.optimizationLevel) + " to O" +
                                    std::to_string(adjustedLevel));
        options.optimizationLevel = adjustedLevel;

        if (adjustedLevel == 0) {
            cm::debug::codegen::log(cm::debug::codegen::Id::LLVMOptimize,
                                    "Skipping optimization due to complexity patterns");
            return;
        }
    }

    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMOptimize,
                            "Level " + std::to_string(options.optimizationLevel));

    // カスタム最適化を使用する場合
    if (options.useCustomOptimizations) {
        using namespace cm::codegen::llvm_backend::optimizations;

        OptimizationManager::OptLevel customLevel;
        switch (options.optimizationLevel) {
            case 1:
                customLevel = OptimizationManager::OptLevel::O1;
                break;
            case 2:
                customLevel = OptimizationManager::OptLevel::O2;
                break;
            case 3:
                customLevel = OptimizationManager::OptLevel::O3;
                break;
            case -1:
                customLevel = OptimizationManager::OptLevel::Oz;
                break;
            default:
                customLevel = OptimizationManager::OptLevel::O2;
        }

        auto config = createConfigFromLevel(customLevel);

        if (context->getTargetConfig().target == BuildTarget::Wasm) {
            config = createConfigForTarget("wasm32");
        } else if (context->getTargetConfig().target == BuildTarget::Baremetal) {
            config.level = OptimizationManager::OptLevel::Os;
            config.enableVectorization = false;
        }

        config.printStatistics = options.verbose;

        OptimizationManager optManager(config);
        optManager.optimizeModule(context->getModule());

        if (options.verbose) {
            llvm::errs() << "\n[Custom Optimizations Complete]\n";
        }
    }

    // PassBuilder設定
    llvm::TargetMachine* TM = targetManager ? targetManager->getTargetMachine() : nullptr;
    llvm::PassBuilder passBuilder(TM);

    // 解析マネージャ
    llvm::LoopAnalysisManager LAM;
    llvm::FunctionAnalysisManager FAM;
    llvm::CGSCCAnalysisManager CGAM;
    llvm::ModuleAnalysisManager MAM;

    passBuilder.registerModuleAnalyses(MAM);
    passBuilder.registerCGSCCAnalyses(CGAM);
    passBuilder.registerFunctionAnalyses(FAM);
    passBuilder.registerLoopAnalyses(LAM);
    passBuilder.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    // 最適化レベル
    llvm::OptimizationLevel optLevel;
    switch (options.optimizationLevel) {
        case 1:
            optLevel = llvm::OptimizationLevel::O1;
            break;
        case 2:
            optLevel = llvm::OptimizationLevel::O2;
            break;
        case 3:
            optLevel = llvm::OptimizationLevel::O3;
            break;
        case -1:
            optLevel = llvm::OptimizationLevel::Oz;
            break;
        default:
            optLevel = llvm::OptimizationLevel::O2;
    }

    // verboseモードでのパステスト
    if (options.verbose && (options.optimizationLevel >= 2)) {
        llvm::errs() << "[PASS_DEBUG] Running individual pass debugging for O"
                     << options.optimizationLevel << "\n";
        auto results = PassDebugger::runPassesWithTimeout(context->getModule(), passBuilder,
                                                          optLevel, 5000);
        cm::codegen::llvm_backend::PassDebugger::printResults(results);

        bool hasTimeout = false;
        for (const auto& result : results) {
            if (result.timeout) {
                hasTimeout = true;
                llvm::errs() << "[PASS_DEBUG] Detected timeout in pass: " << result.passName
                             << "\n";
                llvm::errs() << "[PASS_DEBUG] Falling back to O1 optimization\n";
                break;
            }
        }

        if (hasTimeout) {
            optLevel = llvm::OptimizationLevel::O1;
        }
    }

    // モジュールパスマネージャ
    llvm::ModulePassManager MPM;

    // ターゲット別の最適化
    if (context->getTargetConfig().target == BuildTarget::Wasm) {
        MPM = passBuilder.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::Oz);
    } else if (context->getTargetConfig().target == BuildTarget::Baremetal) {
        MPM = passBuilder.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::Os);
    } else if (context->getTargetConfig().target == BuildTarget::BaremetalUEFI) {
        MPM = passBuilder.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2);
    } else {
        MPM = passBuilder.buildPerModuleDefaultPipeline(optLevel);
    }

    MPM.run(context->getModule(), MAM);

    if (options.verbose) {
        llvm::errs() << "=== Optimized LLVM IR ===\n";
        context->getModule().print(llvm::errs(), nullptr);
        llvm::errs() << "========================\n";
    }

    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMOptimizeEnd);
}

// 出力
void LLVMCodeGen::emit() {
    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMEmit, options.outputFile);

    switch (options.format) {
        case OutputFormat::ObjectFile:
            emitObjectFile();
            break;
        case OutputFormat::Assembly:
            emitAssembly();
            break;
        case OutputFormat::LLVMIR:
            emitLLVMIR();
            break;
        case OutputFormat::Bitcode:
            emitBitcode();
            break;
        case OutputFormat::Executable:
            emitExecutable();
            break;
    }

    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMEmitEnd,
                            "Output: " + options.outputFile);
}

// オブジェクトファイル出力
void LLVMCodeGen::emitObjectFile() {
    targetManager->emitObjectFile(context->getModule(), options.outputFile);

    if (context->getTargetConfig().target == BuildTarget::Baremetal) {
        std::string ldScript = options.linkerScript.empty() ? "link.ld" : options.linkerScript;
        targetManager->generateLinkerScript(ldScript);
        cm::debug::codegen::log(cm::debug::codegen::Id::LLVMLinkerScript, ldScript);
    }
}

// アセンブリ出力
void LLVMCodeGen::emitAssembly() {
    targetManager->emitAssembly(context->getModule(), options.outputFile);
}

// LLVM IR出力（テキスト）
void LLVMCodeGen::emitLLVMIR() {
    std::error_code EC;
    llvm::raw_fd_ostream out(options.outputFile, EC);
    if (EC) {
        throw std::runtime_error("Cannot open file: " + options.outputFile);
    }
    context->getModule().print(out, nullptr);
}

// ビットコード出力
void LLVMCodeGen::emitBitcode() {
    std::error_code EC;
    llvm::raw_fd_ostream out(options.outputFile, EC);
    if (EC) {
        throw std::runtime_error("Cannot open file: " + options.outputFile);
    }
    llvm::WriteBitcodeToFile(context->getModule(), out);
}

// 実行可能ファイル生成（リンク）
void LLVMCodeGen::emitExecutable() {
    // まずオブジェクトファイル生成
    std::string objFile = options.outputFile + ".o";
    targetManager->emitObjectFile(context->getModule(), objFile);

    // 使用ライブラリの検出
    bool needsGPU = checkForGPUUsage();
    bool needsNet = checkForNetUsage();
    bool needsSync = checkForSyncUsage();
    bool needsThread = checkForThreadUsage();
    bool needsHTTP = checkForHTTPUsage();
    bool needsPthread = needsSync || needsThread;
    bool needsCppRuntime = needsGPU || needsNet || needsSync || needsThread || needsHTTP;

    // リンカ呼び出し
    std::string linkCmd;

    if (context->getTargetConfig().target == BuildTarget::Baremetal) {
        linkCmd = "arm-none-eabi-ld -T link.ld " + objFile + " -o " + options.outputFile;
    } else if (context->getTargetConfig().target == BuildTarget::BaremetalUEFI) {
        linkCmd =
            "lld-link /subsystem:efi_application /entry:efi_main /out:" + options.outputFile +
            " " + objFile;
    } else if (context->getTargetConfig().target == BuildTarget::Wasm) {
        std::string runtimePath = findRuntimeLibrary();
        linkCmd = "wasm-ld --entry=_start --allow-undefined " + objFile + " " + runtimePath +
                  " -o " + options.outputFile;
    } else {
        // ネイティブ：システムリンカ使用
        std::string runtimePath = findRuntimeLibrary();

#ifdef __APPLE__
        linkCmd = "/usr/bin/clang++ -mmacosx-version-min=15.0 -Wl,-dead_strip ";
#ifdef CM_DEFAULT_TARGET_ARCH
        linkCmd += "-arch " + std::string(CM_DEFAULT_TARGET_ARCH) + " ";
#endif
        if (context->getTargetConfig().noStd) {
            linkCmd += "-nostdlib ";
        }
        linkCmd += objFile + " " + runtimePath;

        if (needsGPU) {
            std::string gpuRuntimePath = findGPURuntimeLibrary();
            if (!gpuRuntimePath.empty()) {
                linkCmd += " " + gpuRuntimePath;
                linkCmd += " -framework Metal -framework Foundation";
            }
        }

        if (needsNet) {
            std::string netRuntimePath = findStdRuntimeLibrary("net");
            if (!netRuntimePath.empty()) {
                linkCmd += " " + netRuntimePath;
            }
        }

        if (needsSync) {
            std::string syncRuntimePath = findStdRuntimeLibrary("sync");
            if (!syncRuntimePath.empty()) {
                linkCmd += " " + syncRuntimePath;
            }
        }

        if (needsThread) {
            std::string threadRuntimePath = findStdRuntimeLibrary("thread");
            if (!threadRuntimePath.empty()) {
                linkCmd += " " + threadRuntimePath;
            }
        }

        if (needsHTTP) {
            std::string httpRuntimePath = findStdRuntimeLibrary("http");
            if (!httpRuntimePath.empty()) {
                linkCmd += " " + httpRuntimePath;
            }
            std::string opensslPrefix;
#ifdef CM_DEFAULT_TARGET_ARCH
            std::string targetArch = CM_DEFAULT_TARGET_ARCH;
#else
            std::string targetArch = "arm64";
#endif
            if (targetArch == "arm64") {
                if (std::filesystem::exists("/opt/homebrew/opt/openssl@3/lib")) {
                    opensslPrefix = "/opt/homebrew/opt/openssl@3";
                }
            } else {
                if (std::filesystem::exists("/usr/local/opt/openssl@3/lib")) {
                    opensslPrefix = "/usr/local/opt/openssl@3";
                }
            }
            if (opensslPrefix.empty()) {
                FILE* pipe = popen("brew --prefix openssl@3 2>/dev/null", "r");
                if (pipe) {
                    char buffer[256];
                    if (fgets(buffer, sizeof(buffer), pipe)) {
                        opensslPrefix = buffer;
                        while (!opensslPrefix.empty() && opensslPrefix.back() == '\n')
                            opensslPrefix.pop_back();
                    }
                    pclose(pipe);
                }
            }
            if (!opensslPrefix.empty()) {
                linkCmd += " -L" + opensslPrefix + "/lib";
            }
            linkCmd += " -lssl -lcrypto";
        }

        if (needsCppRuntime) {
            linkCmd += " -lc++";
        }

        if (needsPthread) {
            linkCmd += " -lpthread";
        }

        linkCmd += " -o " + options.outputFile;
#else
        linkCmd = "clang -Wl,--gc-sections ";
        if (context->getTargetConfig().noStd) {
            linkCmd += "-nostdlib ";
        }
        linkCmd += objFile + " " + runtimePath;

        if (needsNet) {
            std::string path = findStdRuntimeLibrary("net");
            if (!path.empty())
                linkCmd += " " + path;
        }
        if (needsSync) {
            std::string path = findStdRuntimeLibrary("sync");
            if (!path.empty())
                linkCmd += " " + path;
        }
        if (needsThread) {
            std::string path = findStdRuntimeLibrary("thread");
            if (!path.empty())
                linkCmd += " " + path;
        }
        if (needsHTTP) {
            std::string path = findStdRuntimeLibrary("http");
            if (!path.empty())
                linkCmd += " " + path;
            linkCmd += " -lssl -lcrypto";
        }
        if (needsCppRuntime)
            linkCmd += " -lstdc++";
        if (needsPthread)
            linkCmd += " -lpthread";

        linkCmd += " -o " + options.outputFile;
#endif
    }

    // リンカ実行
    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMLink, linkCmd);
    int result = std::system(linkCmd.c_str());
    if (result != 0) {
        throw std::runtime_error("Linking failed");
    }

    // 一時ファイル削除
    std::remove(objFile.c_str());
}

// ランタイムライブラリのパスを検索
std::string LLVMCodeGen::findRuntimeLibrary() {
    if (context->getTargetConfig().target == BuildTarget::Wasm) {
#ifdef CM_RUNTIME_WASM_PATH
        if (std::filesystem::exists(CM_RUNTIME_WASM_PATH)) {
            return CM_RUNTIME_WASM_PATH;
        }
#endif
        if (std::filesystem::exists("build/lib/cm_runtime_wasm.o")) {
            return "build/lib/cm_runtime_wasm.o";
        }
        return compileWasmRuntimeOnDemand();
    }

#ifdef CM_RUNTIME_PATH
    if (std::filesystem::exists(CM_RUNTIME_PATH)) {
        return CM_RUNTIME_PATH;
    }
#endif

    std::vector<std::string> searchPaths = {
        "build/lib/cm_runtime.o",
        "./build/lib/cm_runtime.o",
        "../build/lib/cm_runtime.o",
        ".tmp/cm_runtime.o",
    };

    for (const auto& path : searchPaths) {
        if (std::filesystem::exists(path)) {
            return path;
        }
    }

    return compileRuntimeOnDemand();
}

// ランタイムをオンデマンドでコンパイル
std::string LLVMCodeGen::compileRuntimeOnDemand() {
    std::vector<std::string> sourcePaths = {
        "src/codegen/llvm/native/runtime.c",
        "./src/codegen/llvm/native/runtime.c",
        "../src/codegen/llvm/native/runtime.c",
    };

    std::string runtimeSource;
    for (const auto& path : sourcePaths) {
        if (std::filesystem::exists(path)) {
            runtimeSource = path;
            break;
        }
    }

    if (runtimeSource.empty()) {
        throw std::runtime_error(
            "Cannot find Cm runtime library. "
            "Please rebuild the compiler with 'cmake --build build'");
    }

    std::filesystem::create_directories("build/lib");
    std::string outputPath = "build/lib/cm_runtime.o";

    std::string compileCmd = "clang -c " + runtimeSource + " -o " + outputPath + " -O2";
    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMInit,
                            "Compiling runtime: " + compileCmd);

    int result = std::system(compileCmd.c_str());
    if (result != 0) {
        throw std::runtime_error("Failed to compile Cm runtime library");
    }

    return outputPath;
}

// WASMランタイムをオンデマンドでコンパイル
std::string LLVMCodeGen::compileWasmRuntimeOnDemand() {
    std::vector<std::string> sourcePaths = {
        "src/codegen/llvm/wasm/runtime_wasm.c",
        "./src/codegen/llvm/wasm/runtime_wasm.c",
        "../src/codegen/llvm/wasm/runtime_wasm.c",
    };

    std::string runtimeSource;
    for (const auto& path : sourcePaths) {
        if (std::filesystem::exists(path)) {
            runtimeSource = path;
            break;
        }
    }

    if (runtimeSource.empty()) {
        throw std::runtime_error(
            "Cannot find Cm WASM runtime source. "
            "Please rebuild the compiler with 'cmake --build build'");
    }

    std::vector<std::string> clangPaths = {
        "/opt/homebrew/opt/llvm@17/bin/clang",
        "/opt/homebrew/opt/llvm/bin/clang",
        "/usr/local/opt/llvm@17/bin/clang",
        "/usr/local/opt/llvm/bin/clang",
    };

    std::string wasmClang;
    for (const auto& path : clangPaths) {
        if (std::filesystem::exists(path)) {
            wasmClang = path;
            break;
        }
    }

    if (wasmClang.empty()) {
        throw std::runtime_error(
            "Cannot find WASM-capable clang. "
            "Please install LLVM with Homebrew: brew install llvm@17");
    }

    std::string sourceDir = std::filesystem::path(runtimeSource).parent_path().string();

    std::filesystem::create_directories("build/lib");
    std::string outputPath = "build/lib/cm_runtime_wasm.o";

    std::string compileCmd = wasmClang + " -c " + runtimeSource + " -o " + outputPath +
                             " --target=wasm32-wasi -O2 -ffunction-sections -fdata-sections"
                             " -nostdlib -D__wasi__ -I" +
                             sourceDir;
    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMInit,
                            "Compiling WASM runtime: " + compileCmd);

    int result = std::system(compileCmd.c_str());
    if (result != 0) {
        throw std::runtime_error("Failed to compile Cm WASM runtime library");
    }

    return outputPath;
}

// GPU関数の使用を検出
bool LLVMCodeGen::checkForGPUUsage() const {
    for (const auto& func : context->getModule()) {
        if (func.isDeclaration()) {
            std::string name = func.getName().str();
            if (name.find("gpu_") == 0) {
                cm::debug::codegen::log(cm::debug::codegen::Id::LLVMOptimize,
                                        "GPU function detected: " + name);
                return true;
            }
        }
    }
    return false;
}

// Net関数の使用を検出
bool LLVMCodeGen::checkForNetUsage() const {
    for (const auto& func : context->getModule()) {
        if (func.isDeclaration()) {
            std::string name = func.getName().str();
            if (name.find("cm_tcp_") == 0 || name.find("cm_udp_") == 0 ||
                name.find("cm_dns_") == 0 || name.find("cm_socket_") == 0) {
                cm::debug::codegen::log(cm::debug::codegen::Id::LLVMOptimize,
                                        "Net function detected: " + name);
                return true;
            }
        }
    }
    return false;
}

// Sync関数の使用を検出
bool LLVMCodeGen::checkForSyncUsage() const {
    for (const auto& func : context->getModule()) {
        if (func.isDeclaration()) {
            std::string name = func.getName().str();
            if (name.find("cm_mutex_") == 0 || name.find("cm_rwlock_") == 0 ||
                name.find("cm_atomic_") == 0 || name.find("cm_channel_") == 0 ||
                name.find("cm_once_") == 0 ||
                name.find("atomic_store_") == 0 || name.find("atomic_load_") == 0 ||
                name.find("atomic_fetch_") == 0 || name.find("atomic_compare_") == 0) {
                cm::debug::codegen::log(cm::debug::codegen::Id::LLVMOptimize,
                                        "Sync function detected: " + name);
                return true;
            }
        }
    }
    return false;
}

// Thread関数の使用を検出
bool LLVMCodeGen::checkForThreadUsage() const {
    for (const auto& func : context->getModule()) {
        if (func.isDeclaration()) {
            std::string name = func.getName().str();
            if (name.find("cm_thread_") == 0) {
                cm::debug::codegen::log(cm::debug::codegen::Id::LLVMOptimize,
                                        "Thread function detected: " + name);
                return true;
            }
        }
    }
    return false;
}

// HTTP関数の使用を検出
bool LLVMCodeGen::checkForHTTPUsage() const {
    for (const auto& func : context->getModule()) {
        if (func.isDeclaration()) {
            std::string name = func.getName().str();
            if (name.find("cm_http_") == 0) {
                cm::debug::codegen::log(cm::debug::codegen::Id::LLVMOptimize,
                                        "HTTP function detected: " + name);
                return true;
            }
        }
    }
    return false;
}

// GPUランタイムライブラリのパスを検索
std::string LLVMCodeGen::findGPURuntimeLibrary() {
#ifdef CM_GPU_RUNTIME_PATH
    if (std::filesystem::exists(CM_GPU_RUNTIME_PATH)) {
        return CM_GPU_RUNTIME_PATH;
    }
#endif
    std::vector<std::string> searchPaths = {
        "build/lib/cm_gpu_runtime.o",
        "./build/lib/cm_gpu_runtime.o",
        "../build/lib/cm_gpu_runtime.o",
    };
    for (const auto& path : searchPaths) {
        if (std::filesystem::exists(path)) {
            return path;
        }
    }
    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMError, "GPU runtime library not found");
    return "";
}

// stdランタイムライブラリの汎用検索
std::string LLVMCodeGen::findStdRuntimeLibrary(const std::string& name) {
#ifdef CM_NET_RUNTIME_PATH
    if (name == "net" && std::filesystem::exists(CM_NET_RUNTIME_PATH)) {
        return CM_NET_RUNTIME_PATH;
    }
#endif
#ifdef CM_SYNC_RUNTIME_PATH
    if (name == "sync" && std::filesystem::exists(CM_SYNC_RUNTIME_PATH)) {
        return CM_SYNC_RUNTIME_PATH;
    }
#endif
#ifdef CM_THREAD_RUNTIME_PATH
    if (name == "thread" && std::filesystem::exists(CM_THREAD_RUNTIME_PATH)) {
        return CM_THREAD_RUNTIME_PATH;
    }
#endif
#ifdef CM_HTTP_RUNTIME_PATH
    if (name == "http" && std::filesystem::exists(CM_HTTP_RUNTIME_PATH)) {
        return CM_HTTP_RUNTIME_PATH;
    }
#endif

    std::string ext = (name == "sync") ? ".a" : ".o";
    std::string filename = "cm_" + name + "_runtime" + ext;

    std::vector<std::string> searchPaths = {
        "build/lib/" + filename,
        "./build/lib/" + filename,
        "../build/lib/" + filename,
    };
    for (const auto& path : searchPaths) {
        if (std::filesystem::exists(path)) {
            return path;
        }
    }

    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMError,
                            name + " runtime library not found");
    return "";
}

// インポートされた外部関数があるかチェック
bool LLVMCodeGen::checkForImports() const {
    for (const auto& func : context->getModule()) {
        if (func.isDeclaration()) {
            std::string name = func.getName().str();
            if (name != "printf" && name != "puts" && name != "malloc" && name != "free" &&
                name != "memcpy" && name != "memset" && name != "__cm_panic" &&
                name != "__cm_alloc" && name != "__cm_dealloc" &&
                name.find("llvm.") != 0) {
                cm::debug::codegen::log(cm::debug::codegen::Id::LLVMOptimize,
                                        "Found imported function: " + name);
                return true;
            }
        }
    }
    return false;
}

}  // namespace cm::codegen::llvm_backend