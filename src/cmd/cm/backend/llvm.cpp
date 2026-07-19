// LLVMコード生成バックエンド: native/wasm/baremetal/uefi向け出力・no_std検査・
// モジュール別差分コンパイル（現在は無効）・インクリメンタルキャッシュ保存・--run実行。
// 例外境界はコード生成呼び出しのtryに限定する

#include "cmd/cm/driver.hpp"
#include "internal/base/i18n.hpp"
#include "internal/mir/nodes.hpp"
#include "internal/mir/passes/validation/no_std_checker.hpp"

#ifdef CM_LLVM_ENABLED
#include "internal/codegen/llvm/monitoring/compilation_guard.hpp"
#include "internal/codegen/llvm/native/codegen.hpp"
#endif

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>

#if !defined(_WIN32)
#include <sys/wait.h>
#endif

namespace cm::driver {

int emit_llvm(BuildContext& ctx, mir::MirProgram& mir) {
    cli::Options& opts = ctx.opts;
    (void)opts;
    const auto compile_start = ctx.compile_start;
    const auto phase_preprocess_ms = ctx.phase_preprocess_ms;
    const auto phase_parse_ms = ctx.phase_parse_ms;
    const auto phase_typecheck_ms = ctx.phase_typecheck_ms;
    const auto phase_hir_ms = ctx.phase_hir_ms;
    const auto phase_mir_ms = ctx.phase_mir_ms;
    const auto phase_opt_ms = ctx.phase_opt_ms;
    (void)mir;

#ifdef CM_LLVM_ENABLED
    if (opts.verbose) {
        std::cout << "=== LLVM Code Generation ===\n";
    }

    // LLVM バックエンドオプション設定
    cm::codegen::llvm_backend::LLVMCodeGen::Options llvm_opts;

    // ターゲット設定
    if (opts.target == "wasm") {
        llvm_opts.target = cm::codegen::llvm_backend::BuildTarget::Wasm;
        llvm_opts.format = cm::codegen::llvm_backend::LLVMCodeGen::OutputFormat::Executable;
    } else if (opts.target == "uefi") {
        llvm_opts.target = cm::codegen::llvm_backend::BuildTarget::BaremetalUEFI;
        llvm_opts.format = cm::codegen::llvm_backend::LLVMCodeGen::OutputFormat::ObjectFile;
    } else if (opts.target == "baremetal-arm" || opts.target == "bm") {
        llvm_opts.target = cm::codegen::llvm_backend::BuildTarget::Baremetal;
        llvm_opts.format = cm::codegen::llvm_backend::LLVMCodeGen::OutputFormat::ObjectFile;
    } else if (opts.target == "baremetal-x86" || opts.target == "bm-x86") {
        llvm_opts.target = cm::codegen::llvm_backend::BuildTarget::BaremetalX86;
        llvm_opts.format = cm::codegen::llvm_backend::LLVMCodeGen::OutputFormat::ObjectFile;
    } else if (!opts.target.empty() && opts.target != "native") {
        std::cerr << i18n::msgf(i18n::MsgId::CliUnknownTarget, opts.target);
        std::cerr << i18n::msg(i18n::MsgId::CliValidTargetsNativeWasmJs);
        return 1;
    } else {
        llvm_opts.target = cm::codegen::llvm_backend::BuildTarget::Native;
        llvm_opts.format = cm::codegen::llvm_backend::LLVMCodeGen::OutputFormat::Executable;
    }

    // 出力ファイル設定
    if (opts.output_file.empty()) {
        if (llvm_opts.target == cm::codegen::llvm_backend::BuildTarget::Wasm) {
            llvm_opts.outputFile = "a.wasm";
        } else if (llvm_opts.target == cm::codegen::llvm_backend::BuildTarget::BaremetalUEFI) {
            llvm_opts.outputFile = "bootx64.efi";
        } else if (llvm_opts.target == cm::codegen::llvm_backend::BuildTarget::Baremetal ||
                   llvm_opts.target == cm::codegen::llvm_backend::BuildTarget::BaremetalX86) {
            llvm_opts.outputFile = "firmware.o";
        } else {
            llvm_opts.outputFile = "a.out";
        }
    } else {
        llvm_opts.outputFile = opts.output_file;
    }

    // 最適化レベル
    llvm_opts.optimizationLevel = opts.optimization_level;
    llvm_opts.debugInfo = opts.debug;
    llvm_opts.verbose = opts.verbose || opts.debug;
    llvm_opts.verifyIR = true;

    // LLVM コード生成
    try {
        // CompilationGuardの設定
        {
            auto& guard = cm::codegen::get_compilation_guard();
            guard.configure(opts.max_output_size);  // 最大出力サイズの設定
            if (opts.debug) {
                guard.set_debug_mode(true);
                guard.set_collect_statistics(true);
            }
        }

        cm::codegen::llvm_backend::LLVMCodeGen codegen(llvm_opts);

        // no_std環境でのOS依存機能チェック
        if (llvm_opts.target == cm::codegen::llvm_backend::BuildTarget::Baremetal ||
            llvm_opts.target == cm::codegen::llvm_backend::BuildTarget::BaremetalX86 ||
            llvm_opts.target == cm::codegen::llvm_backend::BuildTarget::BaremetalUEFI) {
            cm::mir::opt::NoStdChecker checker;
            auto check_result = checker.check(mir);
            if (check_result.has_errors) {
                for (const auto& err : check_result.errors) {
                    std::cerr << err << "\n";
                }
                return 1;
            }
        }

        if (cm::debug::debug_mode())
            std::cerr << "[LLVM] Starting codegen.compile()" << std::endl;
        auto phase_llvm_start = std::chrono::steady_clock::now();

        // モジュール情報付きの全体コンパイル
        cm::codegen::llvm_backend::LLVMCodeGen::ModuleCompileInfo module_info;
        module_info = codegen.compileWithModuleInfo(mir, {});

        auto phase_llvm_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - phase_llvm_start)
                                 .count();
        if (cm::debug::debug_mode())
            std::cerr << "[LLVM] codegen.compile() complete" << std::endl;

        // --lir-opt: 最適化後のLLVM IRを表示
        if (opts.show_lir_opt) {
            std::cout << i18n::msg(i18n::MsgId::CliLlvmIrAfterOptimization);
            std::cout << codegen.getIRString();
            std::cout << "========================\n";
            return 0;
        }

        if (!opts.quiet) {
            auto compile_end = std::chrono::steady_clock::now();
            auto compile_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(compile_end - compile_start)
                    .count();
            std::cout << i18n::msgf(i18n::MsgId::CliCompilationCompleteMs, llvm_opts.outputFile,
                                    compile_ms);
            if (opts.verbose) {
                auto frontend_ms = phase_preprocess_ms + phase_parse_ms + phase_typecheck_ms +
                                   phase_hir_ms + phase_mir_ms + phase_opt_ms;
                std::cout << i18n::msgf(i18n::MsgId::CliPreprocessMs, phase_preprocess_ms);
                std::cout << i18n::msgf(i18n::MsgId::CliParseTypeCheckMs,
                                        phase_parse_ms + phase_typecheck_ms);
                std::cout << i18n::msgf(i18n::MsgId::CliHirMirLoweringMs,
                                        phase_hir_ms + phase_mir_ms);
                std::cout << i18n::msgf(i18n::MsgId::CliMirOptimizationMs, phase_opt_ms);
                std::cout << "  LLVM codegen: " << phase_llvm_ms << "ms\n";
                std::cout << i18n::msgf(i18n::MsgId::CliFrontendTotalMs, frontend_ms,
                                        (compile_ms > 0 ? frontend_ms * 100 / compile_ms : 0));

                // モジュール分割情報を表示
                if (!module_info.module_names.empty()) {
                    std::cout << i18n::msgf(i18n::MsgId::CliModulesDetected,
                                            module_info.module_names.size());
                    std::cout << "\n";
                    for (const auto& [name, count] : module_info.module_func_count) {
                        std::cout << i18n::msgf(i18n::MsgId::CliFunctionS, name, count);
                    }
                }
            }
        }

        // --runオプションがある場合は実行
        if (opts.run_after_emit &&
            llvm_opts.target == cm::codegen::llvm_backend::BuildTarget::Native) {
            if (opts.verbose) {
                std::cout << i18n::msgf(i18n::MsgId::CliRunning2, llvm_opts.outputFile);
            }
            int exec_result = std::system(llvm_opts.outputFile.c_str());
            return WEXITSTATUS(exec_result);
        }
    } catch (const std::exception& e) {
        std::cerr << i18n::msgf(i18n::MsgId::CliLlvmCodeGenerationError, e.what());
        return 1;
    }
#else
    std::cerr << i18n::msg(i18n::MsgId::CliTheLlvmBackendIsNot);
    std::cerr << i18n::msg(i18n::MsgId::CliRebuildWithDcmUseLlvm);
    return 1;
#endif

    return kExitSuccess;
}

}  // namespace cm::driver
