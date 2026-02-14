#pragma once

#include "../../../common/debug/codegen.hpp"
#include "../../../mir/nodes.hpp"
#include "../core/context.hpp"
#include "../core/intrinsics.hpp"
#include "../core/mir_to_llvm.hpp"
#include "../optimizations/mir_pattern_detector.hpp"
#include "target.hpp"

#include <cstdio>
#include <filesystem>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Verifier.h>
#include <llvm/MC/TargetRegistry.h>
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
        int optimizationLevel = 3;
        bool debugInfo = false;
        bool verbose = false;
        bool verifyIR = true;
        bool useCustomOptimizations = false;
        std::string customTriple = "";
        std::string linkerScript = "";
    };

   private:
    Options options;
    std::unique_ptr<LLVMContext> context;
    std::unique_ptr<TargetManager> targetManager;
    std::unique_ptr<IntrinsicsManager> intrinsicsManager;
    std::unique_ptr<MIRToLLVM> converter;
    bool hasImports = false;

   public:
    /// コンストラクタ
    explicit LLVMCodeGen(const Options& opts) : options(opts) {}

    /// デフォルトコンストラクタ
    LLVMCodeGen() : options{} {}

    /// MIRプログラムをコンパイル
    void compile(const mir::MirProgram& program);

    /// LLVM IR を文字列として取得（デバッグ用）
    std::string getIRString() const;

   private:
    /// 初期化
    void initialize(const std::string& moduleName);

    /// IR生成
    void generateIR(const mir::MirProgram& program);

    /// モジュール検証
    void verifyModule();

    /// 最適化
    void optimize();

    /// 出力
    void emit();

    /// オブジェクトファイル出力
    void emitObjectFile();

    /// アセンブリ出力
    void emitAssembly();

    /// LLVM IR出力（テキスト）
    void emitLLVMIR();

    /// ビットコード出力
    void emitBitcode();

    /// 実行可能ファイル生成（リンク）
    void emitExecutable();

    /// ランタイムライブラリのパスを検索
    std::string findRuntimeLibrary();

    /// ランタイムをオンデマンドでコンパイル
    std::string compileRuntimeOnDemand();

    /// WASMランタイムをオンデマンドでコンパイル
    std::string compileWasmRuntimeOnDemand();

    /// GPU関数の使用を検出
    bool checkForGPUUsage() const;

    /// Net関数の使用を検出
    bool checkForNetUsage() const;

    /// Sync関数の使用を検出
    bool checkForSyncUsage() const;

    /// Thread関数の使用を検出
    bool checkForThreadUsage() const;

    /// HTTP関数の使用を検出
    bool checkForHTTPUsage() const;

    /// GPUランタイムライブラリのパスを検索
    std::string findGPURuntimeLibrary();

    /// stdランタイムライブラリの汎用検索
    std::string findStdRuntimeLibrary(const std::string& name);

    /// インポートされた外部関数があるかチェック
    bool checkForImports() const;
};

}  // namespace cm::codegen::llvm_backend