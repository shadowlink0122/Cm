#pragma once

#include "../../../common/debug/codegen.hpp"
#include "../../../mir/mir_nodes.hpp"
#include "../core/context.hpp"
#include "../core/intrinsics.hpp"
#include "../core/mir_to_llvm.hpp"
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
        int optimizationLevel = 2;  // 0-3, -1=size
        bool debugInfo = false;
        bool verbose = false;
        bool verifyIR = true;
        std::string customTriple = "";  // カスタムターゲット
        std::string linkerScript = "";  // ベアメタル用
    };

   private:
    Options options;
    std::unique_ptr<LLVMContext> context;
    std::unique_ptr<TargetManager> targetManager;
    std::unique_ptr<IntrinsicsManager> intrinsicsManager;
    std::unique_ptr<MIRToLLVM> converter;

   public:
    /// コンストラクタ
    explicit LLVMCodeGen(const Options& opts) : options(opts) {}

    /// デフォルトコンストラクタ
    LLVMCodeGen() : options{} {}

    /// MIRプログラムをコンパイル
    void compile(const mir::MirProgram& program) {
        cm::debug::codegen::log(cm::debug::codegen::Id::LLVMStart);

        // 1. 初期化
        initialize(program.filename);

        // 2. MIR → LLVM IR 変換
        generateIR(program);

        // 3. 検証
        if (options.verifyIR) {
            verifyModule();
        }

        // 4. 最適化
        optimize();

        // 5. 出力
        emit();

        cm::debug::codegen::log(cm::debug::codegen::Id::LLVMEnd);
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

        cm::debug::codegen::log(cm::debug::codegen::Id::LLVMOptimize,
                                "Level " + std::to_string(options.optimizationLevel));

        // PassBuilder設定
        llvm::PassBuilder passBuilder;

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

        // リンカ呼び出し
        std::string linkCmd;

        if (context->getTargetConfig().target == BuildTarget::Baremetal) {
            // ベアメタル：arm-none-eabi-ld使用
            linkCmd = "arm-none-eabi-ld -T link.ld " + objFile + " -o " + options.outputFile;
        } else if (context->getTargetConfig().target == BuildTarget::Wasm) {
            // WebAssembly：wasm-ld使用
            // WASMランタイムライブラリのパスを検索
            std::string runtimePath = findRuntimeLibrary();
            // WASI用：_startはランタイムから提供される
            linkCmd = "wasm-ld --entry=_start " + objFile + " " + runtimePath + " -o " +
                      options.outputFile;
        } else {
            // ネイティブ：システムリンカ使用

            // Cmランタイムライブラリのパスを検索
            std::string runtimePath = findRuntimeLibrary();

#ifdef __APPLE__
            // macOSではclangを使用してリンク
            linkCmd = "clang ";
            if (context->getTargetConfig().noStd) {
                linkCmd += "-nostdlib ";
            }
            linkCmd += objFile + " " + runtimePath + " -o " + options.outputFile;
#else
            linkCmd = "ld ";
            if (context->getTargetConfig().noStd) {
                linkCmd += "-nostdlib ";
            } else {
                linkCmd += "-lc ";  // libc
            }
            linkCmd += objFile + " " + runtimePath + " -o " + options.outputFile;
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
};

}  // namespace cm::codegen::llvm_backend