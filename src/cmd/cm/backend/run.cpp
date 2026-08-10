// Runコマンドのバックエンド: JIT実行・#[test]テストランナー・JS(node)実行への振り分け。
// 例外境界はJITエンジン内（resultで返る）とJSコード生成のtryに限定する

#include "cmd/cm/driver.hpp"
#include "internal/base/i18n.hpp"
#include "internal/codegen/js/codegen.hpp"
#include "internal/mir/nodes.hpp"

#ifdef CM_LLVM_ENABLED
#include "internal/codegen/llvm/jit/jit_engine.hpp"
#endif

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <sys/wait.h>
#endif

namespace cm::driver {

int emit_jit_run(BuildContext& ctx, mir::MirProgram& mir) {
    cli::Options& opts = ctx.opts;
    (void)opts;
    (void)mir;

    // ========== --target指定時のディスパッチ ==========
    // 従来は--targetを無視して常にJIT実行していた（JS指定でもネイティブ意味論で実行され誤解を招くため、実際のバックエンドで実行するか明示エラーにする）
    if (opts.target == "js" || opts.target == "web" || opts.target == "ts") {
        // JS生成 → Node.jsで実行（tsターゲットも実行は型注釈を除去したJSで行う。TSは同一コード生成結果へstripされる）
        cm::codegen::js::JSCodeGenOptions js_opts;
        std::string default_js_out;
        if (opts.output_file.empty()) {
            // 中間JSは他の中間生成物（.tmp/test・.tmp/module-cache等）と同じく.tmp配下へ置く（従来はシステムのtempディレクトリで固定名が全プロセス共有だった）
            std::filesystem::create_directories(".tmp/run");
            default_js_out =
                ".tmp/run/" + std::filesystem::path(opts.input_file).stem().string() + ".js";
        }
        js_opts.outputFile = opts.output_file.empty() ? default_js_out : opts.output_file;
        js_opts.generateHTML = false;
        js_opts.verbose = opts.verbose || opts.debug;
        try {
            cm::codegen::js::JSCodeGen codegen(js_opts);
            codegen.compile(mir);
        } catch (const std::exception& e) {
            std::cerr << i18n::msgf(i18n::MsgId::CliJavascriptCodeGenerationError, e.what());
            return 1;
        }
        if (std::system("command -v node > /dev/null 2>&1") != 0) {
            std::cerr << i18n::msg(i18n::MsgId::CliNodeNotFoundRequiredTo);
            std::cerr << i18n::msg(i18n::MsgId::CliGenerateJsWithCmCompile);
            return 1;
        }
        std::string cmd = "node " + js_opts.outputFile;
        int exec_result = std::system(cmd.c_str());
#if defined(_WIN32)
        return exec_result;
#else
        return WEXITSTATUS(exec_result);
#endif
    }
    if (opts.target == "wasm") {
        std::cerr << i18n::msg(i18n::MsgId::CliCmRunDoesNotSupport);
        std::cerr << i18n::msg(i18n::MsgId::CliRunCmCompileEmitLlvm);
        return 1;
    }
    if (opts.target == "sv" || opts.target == "verilog" || opts.target == "systemverilog") {
        std::cerr << i18n::msg(i18n::MsgId::CliCmRunDoesNotSupport2);
        std::cerr << i18n::msg(i18n::MsgId::CliUseCmTestPlatformSv);
        return 1;
    }

#ifdef CM_LLVM_ENABLED
    // ========== ネイティブテストランナー（cm test / run --test）==========
    // #[test] 関数を宣言順に、関数ごとに独立したJITで実行する（状態隔離）。
    // 成功 = 関数が正常リターン。assert失敗は exit(1) で即時停止する。
    if (opts.test_mode) {
        std::vector<const mir::MirFunction*> test_fns;
        for (const auto& func : mir.functions) {
            if (!func) {
                continue;
            }
            for (const auto& attr : func->attributes) {
                if (attr == "test") {
                    test_fns.push_back(func.get());
                    break;
                }
            }
        }
        if (test_fns.empty()) {
            std::cerr << i18n::msgf(i18n::MsgId::CliNoTestFunctionsFound, opts.input_file);
            return 1;
        }
        // step() はSVプラットフォーム専用（クロック概念が実行系に存在しない）
        for (const auto* fn : test_fns) {
            for (const auto& block : fn->basic_blocks) {
                if (!block || !block->terminator ||
                    block->terminator->kind != mir::MirTerminator::Call) {
                    continue;
                }
                const auto& data = std::get<mir::MirTerminator::CallData>(block->terminator->data);
                if (data.func && data.func->kind == mir::MirOperand::FunctionRef &&
                    std::get<std::string>(data.func->data) == "step") {
                    std::cerr << i18n::msgf(i18n::MsgId::CliStepIsOnlyAvailableIn, fn->name);
                    std::cerr << i18n::msg(i18n::MsgId::CliClockDrivenTestsRequirePlatform);
                    return 1;
                }
            }
        }
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        const bool sanitize_bounds_tests = std::find(opts.sanitizers.begin(), opts.sanitizers.end(),
                                                     "bounds") != opts.sanitizers.end();
        for (const auto* fn : test_fns) {
            cm::codegen::jit::JITEngine jit;
            auto result =
                jit.execute(mir, fn->name, opts.optimization_level, sanitize_bounds_tests);
            if (!result.success) {
                std::cerr << i18n::msgf(i18n::MsgId::CliJitExecutionError, fn->name,
                                        result.errorMessage);
                return 1;
            }
            std::cout << "[PASS] " << fn->name << "\n";
        }
        std::cout << i18n::msgf(i18n::MsgId::CliTestSPassed, test_fns.size());
        return 0;
    }

    // JITコンパイラで実行
    if (opts.verbose) {
        std::cout << "=== JIT Compiler ===" << std::endl;
    }

    // JITキャッシュ用: バックグラウンドでネイティブバイナリも生成
    // MIRからLLVMコンパイルしてJIT実行
    cm::codegen::jit::JITEngine jit;

    // JIT実行時はstdoutをアンバッファにして即時出力されるようにする
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    const bool sanitize_bounds = std::find(opts.sanitizers.begin(), opts.sanitizers.end(),
                                           "bounds") != opts.sanitizers.end();

    // スクリプト引数（cm run file.cm -- args...）。argv[0]は入力ファイルパス
    std::vector<std::string> program_args;
    program_args.push_back(opts.input_file);
    for (const auto& a : opts.program_args) {
        program_args.push_back(a);
    }
    auto result = jit.execute(mir, "main", opts.optimization_level, sanitize_bounds, program_args);

    if (!result.success) {
        std::cerr << i18n::msgf(i18n::MsgId::CliJitExecutionError2, result.errorMessage);
        return 1;
    }

    // 注意: JIT実行後のキャッシュ保存（codegen.compile）は、LLVM globalの再初期化問題とstdout汚染のため実装しない。
    // JITキャッシュは「cm compile」で生成されたキャッシュの再利用のみサポート。

    if (opts.verbose) {
        std::cout << i18n::msgf(i18n::MsgId::CliProgramExitCode, result.exitCode);
        std::cout << i18n::msg(i18n::MsgId::CliJitExecutionComplete);
    }

    return result.exitCode;
#else
    std::cerr << i18n::msg(i18n::MsgId::CliJitCompilerIsDisabledAn);
    return 1;
#endif
}

}  // namespace cm::driver
