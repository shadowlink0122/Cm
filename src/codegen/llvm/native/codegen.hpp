#pragma once

#include "../../../common/debug/codegen.hpp"
#include "../../../mir/nodes.hpp"
#include "../core/context.hpp"
#include "../core/intrinsics.hpp"
#include "../core/mir_to_llvm.hpp"
#include "../optimizations/mir_pattern_detector.hpp"
#include "../optimizations/optimization_manager.hpp"
#include "../optimizations/pass_limiter.hpp"
#include "../optimizations/recursion_limiter.hpp"
#include "loop_detector.hpp"
#include "pass_debugger.hpp"
#include "target.hpp"

#include <filesystem>
#include <llvm/Analysis/CGSCCPassManager.h>
#include <llvm/Analysis/LoopAnalysisManager.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/StandardInstrumentations.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Transforms/Utils.h>

namespace cm::codegen::llvm_backend {

/// LLVM コード生成器
class LLVMCodeGen {
   public:
    /// 出力形式
    enum class OutputFormat {
        ObjectFile,  // .o
        Assembly,    // .s
        LLVMIR,      // .ll
        Bitcode,     // .bc
        Executable   // 実行可能ファイル（リンク済み）
    };

    /// コンパイルオプション
    struct Options {
        BuildTarget target = BuildTarget::Native;
        OutputFormat format = OutputFormat::ObjectFile;
        std::string outputFile = "output.o";
        int optimizationLevel = 3;  // デフォルトO3（最大最適化）- ポインタ型修正済み
        bool debugInfo = false;
        bool verbose = false;
        bool verifyIR = true;
        bool useCustomOptimizations = false;  // カスタム最適化は無効（ハング発生のため要調査）
        std::string customTriple = "";  // カスタムターゲット
        std::string linkerScript = "";  // ベアメタル用
    };

   private:
    Options options;
    std::unique_ptr<LLVMContext> context;
    std::unique_ptr<TargetManager> targetManager;
    std::unique_ptr<IntrinsicsManager> intrinsicsManager;
    std::unique_ptr<MIRToLLVM> converter;
    bool hasImports = false;  // インポートがあるかどうか（最適化制限用）

   public:
    /// コンストラクタ
    explicit LLVMCodeGen(const Options& opts) : options(opts) {}

    /// デフォルトコンストラクタ
    LLVMCodeGen() : options{} {}

    /// MIRプログラムをコンパイル
    void compile(const mir::MirProgram& program) {
        // // std::cerr << "[LLVM-CG] compile() start" << std::endl;
        cm::debug::codegen::log(cm::debug::codegen::Id::LLVMStart);

        // インポートの有無を記録（最適化時に使用）
        hasImports = !program.imports.empty();
        // // std::cerr << "[LLVM-CG] hasImports=" << hasImports
        //           << " (imports.size=" << program.imports.size() << ")" << std::endl;
        if (hasImports) {
            cm::debug::codegen::log(cm::debug::codegen::Id::LLVMInit,
                                    "Program has imports - optimization will be limited");
        }

        // 0. MIRレベルでのパターン検出と最適化レベル調整
        // // std::cerr << "[LLVM-CG] step 0: MIR pattern detection" << std::endl;
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
        // // std::cerr << "[LLVM-CG] step 1: initialize" << std::endl;
        initialize(program.filename);

        // 2. MIR → LLVM IR 変換
        // std::cerr << "[LLVM-CG] step 2: generateIR" << std::endl;
        generateIR(program);

        // 3. 検証
        // 不正なLLVM IRを検出して無限ループを防止
        // std::cerr << "[LLVM-CG] step 3: verifyModule" << std::endl;
        if (options.verifyIR) {
            verifyModule();
        }
        // std::cerr << "[LLVM-CG] step 3 done" << std::endl;

        // 3.5. 最適化前のパターン検出と調整
        // // std::cerr << "[LLVM-CG] step 3.5: pattern detection" << std::endl;
        if (options.optimizationLevel > 0) {
            // 問題のあるパターンを検出して最適化レベルを調整
            int adjusted_level = OptimizationPassLimiter::adjustOptimizationLevel(
                context->getModule(), options.optimizationLevel);

            if (adjusted_level != options.optimizationLevel) {
                if (cm::debug::g_debug_mode) {
                    std::cerr << "[LLVM] 最適化レベルを O" << options.optimizationLevel << " から O"
                              << adjusted_level << " に変更しました\n";
                }
                options.optimizationLevel = adjusted_level;
            }
        }

        // 4. 最適化
        // std::cerr << "[LLVM-CG] step 4: optimize" << std::endl;
        optimize();
        // std::cerr << "[LLVM-CG] step 4 done" << std::endl;

        // 5. 出力
        // std::cerr << "[LLVM-CG] step 5: emit" << std::endl;
        emit();
        // std::cerr << "[LLVM-CG] step 5 done" << std::endl;

        cm::debug::codegen::log(cm::debug::codegen::Id::LLVMEnd);
        // // std::cerr << "[LLVM-CG] compile() complete" << std::endl;
    }

    /// LLVM IR を文字列として取得（デバッグ用）
    std::string getIRString() const {
        std::string str;
        llvm::raw_string_ostream os(str);
        context->getModule().print(os, nullptr);
        return str;
    }

   private:
    /// 初期化
    void initialize(const std::string& moduleName) {
        cm::debug::codegen::log(cm::debug::codegen::Id::LLVMInit);

        // ターゲット設定
        TargetConfig config;
        if (!options.customTriple.empty()) {
            config.triple = options.customTriple;
            config.target = BuildTarget::Native;  // カスタム
        } else {
            switch (options.target) {
                case BuildTarget::Baremetal:
                    config = TargetConfig::getBaremetalARM();
                    break;
                case BuildTarget::Wasm:
                    config = TargetConfig::getWasm();
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
        // 注意: declareAll()は呼び出さない
        // 組み込み関数は使用時にオンデマンドで宣言される

        // 変換器
        converter = std::make_unique<MIRToLLVM>(*context);

        // ベアメタルの場合、スタートアップコード生成
        if (config.target == BuildTarget::Baremetal) {
            targetManager->generateStartupCode(context->getModule());
        }
    }

    /// IR生成
    void generateIR(const mir::MirProgram& program) {
        cm::debug::codegen::log(cm::debug::codegen::Id::LLVMIRGen, "Generating LLVM IR from MIR");

        // MIR → LLVM IR
        converter->convert(program);

        if (options.verbose) {
            llvm::errs() << "=== Generated LLVM IR ===\n";
            context->getModule().print(llvm::errs(), nullptr);
            llvm::errs() << "========================\n";
        }
    }

    /// モジュール検証
    void verifyModule() {
        cm::debug::codegen::log(cm::debug::codegen::Id::LLVMVerify);

        std::string errors;
        llvm::raw_string_ostream os(errors);

        if (llvm::verifyModule(context->getModule(), &os)) {
            // デバッグ: IRをダンプ
            context->getModule().print(llvm::errs(), nullptr);
            cm::debug::codegen::log(cm::debug::codegen::Id::LLVMError,
                                    "Module verification failed: " + errors,
                                    cm::debug::Level::Error);
            throw std::runtime_error("LLVM module verification failed:\n" + errors);
        }

        cm::debug::codegen::log(cm::debug::codegen::Id::LLVMVerifyOK);
    }

    /// 最適化
    void optimize() {
        if (options.optimizationLevel == 0) {
            return;  // 最適化なし
        }

        // インポートがある場合でも、まず再帰とパターンを検証
        // 再帰とループの事前検証を実行
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
                return;  // 最適化を完全にスキップ
            }
        }

        // NOTE: 以前はimport使用時にO1制限を行っていたが、
        // RecursionLimiterとOptimizationPassLimiterによる事前検証で
        // 無限ループの根本原因が解消されたため、O2/O3を完全有効化

        cm::debug::codegen::log(cm::debug::codegen::Id::LLVMOptimize,
                                "Level " + std::to_string(options.optimizationLevel));

        // カスタム最適化を使用する場合
        if (options.useCustomOptimizations) {
            using namespace cm::codegen::llvm_backend::optimizations;

            // 最適化レベルをマップ
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

            // カスタム最適化マネージャーの設定
            auto config = createConfigFromLevel(customLevel);

            // ターゲット固有の調整
            if (context->getTargetConfig().target == BuildTarget::Wasm) {
                config = createConfigForTarget("wasm32");
            } else if (context->getTargetConfig().target == BuildTarget::Baremetal) {
                config.level = OptimizationManager::OptLevel::Os;
                config.enableVectorization = false;  // ベアメタルではベクトル化を無効
            }

            config.printStatistics = options.verbose;

            // 最適化実行
            OptimizationManager optManager(config);
            optManager.optimizeModule(context->getModule());

            if (options.verbose) {
                llvm::errs() << "\n[Custom Optimizations Complete]\n";
            }
        }

        // PassBuilder設定（TargetMachineでCPU固有のベクトル化を有効化）
        llvm::TargetMachine* TM = targetManager ? targetManager->getTargetMachine() : nullptr;
        llvm::PassBuilder passBuilder(TM);

        // 解析マネージャ
        llvm::LoopAnalysisManager LAM;
        llvm::FunctionAnalysisManager FAM;
        llvm::CGSCCAnalysisManager CGAM;
        llvm::ModuleAnalysisManager MAM;

        // デフォルトパイプライン登録
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
                break;  // サイズ
            default:
                optLevel = llvm::OptimizationLevel::O2;
        }

        // O2/O3でverboseモードが有効な場合、個別パステストを実行
        if (options.verbose && (options.optimizationLevel >= 2)) {
            llvm::errs() << "[PASS_DEBUG] Running individual pass debugging for O"
                         << options.optimizationLevel << "\n";

            auto results = PassDebugger::runPassesWithTimeout(context->getModule(), passBuilder,
                                                              optLevel, 5000);
            cm::codegen::llvm_backend::PassDebugger::printResults(results);

            // タイムアウトしたパスがある場合は、O1に下げて実行
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
            // WebAssembly用の最適化
            MPM =
                passBuilder.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::Oz  // サイズ優先
                );
        } else if (context->getTargetConfig().target == BuildTarget::Baremetal) {
            // ベアメタル用の最適化
            MPM =
                passBuilder.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::Os  // サイズ優先
                );
        } else {
            // ネイティブ用の最適化
            MPM = passBuilder.buildPerModuleDefaultPipeline(optLevel);
        }

        // 最適化実行
        MPM.run(context->getModule(), MAM);

        if (options.verbose) {
            llvm::errs() << "=== Optimized LLVM IR ===\n";
            context->getModule().print(llvm::errs(), nullptr);
            llvm::errs() << "========================\n";
        }

        cm::debug::codegen::log(cm::debug::codegen::Id::LLVMOptimizeEnd);
    }

    /// 出力
    void emit() {
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

    /// オブジェクトファイル出力
    void emitObjectFile() {
        targetManager->emitObjectFile(context->getModule(), options.outputFile);

        // ベアメタルの場合、リンカスクリプトも生成
        if (context->getTargetConfig().target == BuildTarget::Baremetal) {
            std::string ldScript = options.linkerScript.empty() ? "link.ld" : options.linkerScript;
            targetManager->generateLinkerScript(ldScript);

            cm::debug::codegen::log(cm::debug::codegen::Id::LLVMLinkerScript, ldScript);
        }
    }

    /// アセンブリ出力
    void emitAssembly() { targetManager->emitAssembly(context->getModule(), options.outputFile); }

    /// LLVM IR出力（テキスト）
    void emitLLVMIR() {
        std::error_code EC;
        llvm::raw_fd_ostream out(options.outputFile, EC);
        if (EC) {
            throw std::runtime_error("Cannot open file: " + options.outputFile);
        }

        context->getModule().print(out, nullptr);
    }

    /// ビットコード出力
    void emitBitcode() {
        std::error_code EC;
        llvm::raw_fd_ostream out(options.outputFile, EC);
        if (EC) {
            throw std::runtime_error("Cannot open file: " + options.outputFile);
        }

        llvm::WriteBitcodeToFile(context->getModule(), out);
    }

    /// 実行可能ファイル生成（リンク）
    void emitExecutable() {
        // まずオブジェクトファイル生成
        std::string objFile = options.outputFile + ".o";
        targetManager->emitObjectFile(context->getModule(), objFile);

        // GPU関数の使用を検出
        bool needsGPU = checkForGPUUsage();
        bool needsNet = checkForNetUsage();
        bool needsSync = checkForSyncUsage();
        bool needsThread = checkForThreadUsage();
        bool needsHTTP = checkForHTTPUsage();
        // pthread依存: sync または thread が必要
        bool needsPthread = needsSync || needsThread;
        // C++ランタイム依存: C++で書かれたstdランタイムが必要
        bool needsCppRuntime = needsGPU || needsNet || needsSync || needsThread || needsHTTP;

        // リンカ呼び出し
        std::string linkCmd;

        if (context->getTargetConfig().target == BuildTarget::Baremetal) {
            // ベアメタル：arm-none-eabi-ld使用
            linkCmd = "arm-none-eabi-ld -T link.ld " + objFile + " -o " + options.outputFile;
        } else if (context->getTargetConfig().target == BuildTarget::Wasm) {
            // WebAssembly：wasm-ld使用
            std::string runtimePath = findRuntimeLibrary();
            linkCmd = "wasm-ld --entry=_start --allow-undefined " + objFile + " " + runtimePath +
                      " -o " + options.outputFile;
        } else {
            // ネイティブ：システムリンカ使用
            std::string runtimePath = findRuntimeLibrary();

#ifdef __APPLE__
            // macOSではシステムのclangを使用してリンク
            linkCmd = "/usr/bin/clang++ -mmacosx-version-min=15.0 -Wl,-dead_strip ";
            if (context->getTargetConfig().noStd) {
                linkCmd += "-nostdlib ";
            }
            linkCmd += objFile + " " + runtimePath;

            // GPU使用時: GPUランタイム + Metal/Foundationフレームワークをリンク
            if (needsGPU) {
                std::string gpuRuntimePath = findGPURuntimeLibrary();
                if (!gpuRuntimePath.empty()) {
                    linkCmd += " " + gpuRuntimePath;
                    linkCmd += " -framework Metal -framework Foundation";
                }
            }

            // Net使用時: ネットワークランタイムをリンク
            if (needsNet) {
                std::string netRuntimePath = findStdRuntimeLibrary("net");
                if (!netRuntimePath.empty()) {
                    linkCmd += " " + netRuntimePath;
                }
            }

            // Sync使用時: 同期ランタイムをリンク
            if (needsSync) {
                std::string syncRuntimePath = findStdRuntimeLibrary("sync");
                if (!syncRuntimePath.empty()) {
                    linkCmd += " " + syncRuntimePath;
                }
            }

            // Thread使用時: スレッドランタイムをリンク
            if (needsThread) {
                std::string threadRuntimePath = findStdRuntimeLibrary("thread");
                if (!threadRuntimePath.empty()) {
                    linkCmd += " " + threadRuntimePath;
                }
            }

            // HTTP使用時: HTTPランタイム + OpenSSLをリンク
            if (needsHTTP) {
                std::string httpRuntimePath = findStdRuntimeLibrary("http");
                if (!httpRuntimePath.empty()) {
                    linkCmd += " " + httpRuntimePath;
                }
                // OpenSSL (libssl/libcrypto) をリンク
                // Homebrewのパスを検出
                std::string opensslPrefix;
                FILE* pipe = popen("brew --prefix openssl 2>/dev/null", "r");
                if (pipe) {
                    char buffer[256];
                    if (fgets(buffer, sizeof(buffer), pipe)) {
                        opensslPrefix = buffer;
                        // 末尾改行を除去
                        while (!opensslPrefix.empty() && opensslPrefix.back() == '\n')
                            opensslPrefix.pop_back();
                    }
                    pclose(pipe);
                }
                if (!opensslPrefix.empty()) {
                    linkCmd += " -L" + opensslPrefix + "/lib";
                }
                linkCmd += " -lssl -lcrypto";
            }

            // C++ランタイムが必要な場合にリンク
            if (needsCppRuntime) {
                linkCmd += " -lc++";
            }

            // pthread依存ライブラリ
            if (needsPthread) {
                linkCmd += " -lpthread";
            }

            linkCmd += " -o " + options.outputFile;
#else
            // Linuxでもclangを使用
            linkCmd = "clang -Wl,--gc-sections ";
            if (context->getTargetConfig().noStd) {
                linkCmd += "-nostdlib ";
            }
            linkCmd += objFile + " " + runtimePath;

            // stdランタイムのリンク（Linux）
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

   private:
    /// ランタイムライブラリのパスを検索
    std::string findRuntimeLibrary() {
        // WASMターゲットの場合は専用ランタイムを使用
        if (context->getTargetConfig().target == BuildTarget::Wasm) {
#ifdef CM_RUNTIME_WASM_PATH
            if (std::filesystem::exists(CM_RUNTIME_WASM_PATH)) {
                return CM_RUNTIME_WASM_PATH;
            }
#endif
            // フォールバック: build/lib/cm_runtime_wasm.o
            if (std::filesystem::exists("build/lib/cm_runtime_wasm.o")) {
                return "build/lib/cm_runtime_wasm.o";
            }
        }

// 1. コンパイル時に埋め込まれたパス（CMake定義）
#ifdef CM_RUNTIME_PATH
        if (std::filesystem::exists(CM_RUNTIME_PATH)) {
            return CM_RUNTIME_PATH;
        }
#endif

        // 2. cm実行ファイルと同じディレクトリ
        // （インストール時に使用）

        // 3. build/lib/ ディレクトリ
        std::vector<std::string> searchPaths = {
            "build/lib/cm_runtime.o",
            "./build/lib/cm_runtime.o",
            "../build/lib/cm_runtime.o",
            // 開発時の互換性のために.tmpも検索
            ".tmp/cm_runtime.o",
        };

        for (const auto& path : searchPaths) {
            if (std::filesystem::exists(path)) {
                return path;
            }
        }

        // 4. 見つからない場合はオンザフライでコンパイル
        return compileRuntimeOnDemand();
    }

    /// ランタイムをオンデマンドでコンパイル
    std::string compileRuntimeOnDemand() {
        // ランタイムソースの可能な場所
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

        // build/lib/にコンパイル
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

    /// GPU関数の使用を検出
    bool checkForGPUUsage() const {
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

    /// Net関数の使用を検出（cm_tcp_*, cm_udp_*, cm_dns_*, cm_socket_*）
    bool checkForNetUsage() const {
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

    /// Sync関数の使用を検出（cm_mutex_*, cm_rwlock_*, cm_atomic_*, cm_channel_*, cm_once_*,
    /// atomic_*）
    bool checkForSyncUsage() const {
        for (const auto& func : context->getModule()) {
            if (func.isDeclaration()) {
                std::string name = func.getName().str();
                if (name.find("cm_mutex_") == 0 || name.find("cm_rwlock_") == 0 ||
                    name.find("cm_atomic_") == 0 || name.find("cm_channel_") == 0 ||
                    name.find("cm_once_") == 0 ||
                    // レガシー名（cm_プレフィクスなし）
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

    /// Thread関数の使用を検出（cm_thread_*）
    bool checkForThreadUsage() const {
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

    /// HTTP関数の使用を検出（cm_http_*）
    bool checkForHTTPUsage() const {
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

    /// GPUランタイムライブラリのパスを検索
    std::string findGPURuntimeLibrary() {
#ifdef CM_GPU_RUNTIME_PATH
        if (std::filesystem::exists(CM_GPU_RUNTIME_PATH)) {
            return CM_GPU_RUNTIME_PATH;
        }
#endif
        // フォールバック
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

    /// stdランタイムライブラリの汎用検索（net, sync, thread, http）
    std::string findStdRuntimeLibrary(const std::string& name) {
        // 1. CMake定義パスをチェック
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

        // 2. フォールバック: build/lib/ を検索
        // syncはアーカイブ(.a)、それ以外は.o
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

    /// インポートされた外部関数があるかチェック
    bool checkForImports() const {
        // モジュール内の全関数をチェック
        for (const auto& func : context->getModule()) {
            // 宣言のみ（ボディがない）関数は外部関数
            if (func.isDeclaration()) {
                // システム関数（printf, malloc等）以外は外部インポート
                std::string name = func.getName().str();
                if (name != "printf" && name != "puts" && name != "malloc" && name != "free" &&
                    name != "memcpy" && name != "memset" && name != "__cm_panic" &&
                    name != "__cm_alloc" && name != "__cm_dealloc" &&
                    name.find("llvm.") != 0) {  // llvm組み込み関数を除外
                    // インポートされた外部関数を発見
                    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMOptimize,
                                            "Found imported function: " + name);
                    return true;
                }
            }
        }
        return false;
    }
};

}  // namespace cm::codegen::llvm_backend