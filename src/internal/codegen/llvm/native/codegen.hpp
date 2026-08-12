#pragma once

#include "internal/base/debug/codegen.hpp"
#include "internal/codegen/llvm/core/context.hpp"
#include "internal/codegen/llvm/core/intrinsics.hpp"
#include "internal/codegen/llvm/core/mir_to_llvm.hpp"
#include "internal/codegen/llvm/optimizations/mir_pattern_detector.hpp"
#include "internal/mir/nodes.hpp"
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
#include <map>
#include <memory>
#include <string>
#include <vector>

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
        bool sanitizeAddress =
            false;  // --sanitize=address: ASan計装+ランタイムリンク（native専用）
        bool sanitizeThread = false;  // --sanitize=thread: TSan計装+ランタイムリンク（native専用）
        bool sanitizeMemory =
            false;  // --sanitize=memory: MSan計装+ランタイムリンク（native Linux専用）
        bool sanitizeBounds =
            false;  // --sanitize=bounds: 境界チェック計装（native/wasm、trap方式）
    };

    /// サニタイザリンク用のclang++ドライバを探索する（新しいLLVMのランタイムを優先）
    static std::string findSanitizerLinkDriver();

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

    /// モジュール分割情報付きコンパイル結果
    struct ModuleCompileInfo {
        std::vector<std::string> module_names;            // 検出されたモジュール名
        std::vector<std::string> changed_modules;         // 変更されたモジュール名
        std::map<std::string, size_t> module_func_count;  // モジュール別関数数
    };

    /// モジュール分割付きコンパイル（変更モジュール検出 + ログ）
    /// changed_modules_hint: キャッシュから検出された変更モジュール（空なら全再コンパイル）
    ModuleCompileInfo compileWithModuleInfo(
        const mir::MirProgram& program, const std::vector<std::string>& changed_modules_hint = {});

    /// モジュール別オブジェクトファイル情報
    struct ModuleObjectFile {
        std::string module_name;            // モジュール名
        std::filesystem::path object_path;  // .o のパス
        bool from_cache;                    // キャッシュから取得したか
    };

    /// モジュール別差分コンパイル
    /// changed_modules のみ再コンパイル、他はキャッシュから取得
    /// output_dir: モジュール .o の出力先ディレクトリ
    std::vector<ModuleObjectFile> compileModules(
        const mir::MirProgram& program, const std::vector<std::string>& changed_modules,
        const std::map<std::string, std::filesystem::path>& cached_objects,
        const std::filesystem::path& output_dir);

    /// 複数オブジェクトファイルからリンク
    /// program非nullptr時はMIRの呼び出し関数名からランタイムライブラリ（net/sync/thread/GPU/HTTP）の要否を判定する
    /// （モジュール分割経路ではthis->contextが空のリンク用モジュールになり、LLVM宣言ベースの判定が機能しないため）
    void linkObjects(const std::vector<std::filesystem::path>& objects,
                     const std::string& output_file, const mir::MirProgram* program = nullptr);

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

    /// サニタイザ計装（--sanitize指定時のみ。optimize()と独立にO0でも実行する）
    void instrumentSanitizers();

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