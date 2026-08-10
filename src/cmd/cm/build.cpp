// run/compile/test のコンパイルパイプライン。
// 各段階（前処理・構文解析・型検査・IR変換）ごとに狭いtryで捕捉し、失敗した段階名付きで報告する。
// バックエンド（JIT/SV/JS/LLVM）へのディスパッチは backend_*.cpp が担う

#include "driver.hpp"
#include "frontend.hpp"
#include "internal/base/debug_messages.hpp"
#include "internal/base/diag_emitter.hpp"
#include "internal/base/i18n.hpp"
#include "internal/base/source_location.hpp"
#include "internal/codegen/sv/codegen.hpp"
#include "internal/codegen/sv/hierarchy.hpp"
#include "internal/hir/lowering/lowering.hpp"
#include "internal/hir/type_audit.hpp"
#include "internal/macro/derive.hpp"
#include "internal/mir/lowering/lowering.hpp"
#include "internal/mir/passes/cleanup/dce.hpp"
#include "internal/mir/passes/cleanup/program_dce.hpp"
#include "internal/mir/passes/cleanup/string_reassign_free.hpp"
#include "internal/mir/passes/core/manager.hpp"
#include "internal/mir/passes/instrumentation/bounds.hpp"
#include "internal/mir/passes/instrumentation/undefined.hpp"
#include "internal/mir/passes/loop/const_unroll.hpp"
#include "internal/mir/passes/scalar/folding.hpp"
#include "internal/mir/printer.hpp"
#include "internal/module/resolver.hpp"
#include "internal/preprocessor/conditional.hpp"
#include "internal/syntax/ast/target_filtering_visitor.hpp"
#include "internal/syntax/lexer/lexer.hpp"
#include "internal/syntax/parser/parser.hpp"
#include "internal/types/type_checker.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cm::driver {

using cli::Command;

int run_build(cli::Options& opts, const char* argv0) {
    BuildContext ctx{opts, argv0};

    if (opts.command == Command::None || opts.input_file.empty()) {
        if (opts.command == Command::None) {
            std::cerr << i18n::msg(i18n::MsgId::CliNoCommandSpecified);
            std::cerr << i18n::msg(i18n::MsgId::CliRunCmHelpForUsage);
        } else {
            std::cerr << i18n::msg(i18n::MsgId::CliNoInputFileSpecified);
        }
        return 1;
    }

    // ファイルを読み込む
    auto file_result = read_file(opts.input_file);
    if (!file_result.success) {
        std::cerr << file_result.error_message << "\n";
        return 1;
    }
    std::string code = std::move(file_result.content);

    // ========== cm test: プラットフォームディレクティブでバックエンドを振り分け ==========
    // //! platform: sv → SVテストベンチ生成 + iverilog/vvpシミュレーション
    // それ以外        → JITで各 #[test] 関数を実行
    bool run_sv_sim = false;  // SVフローで生成後にシミュレーションを実行する
    if (opts.command == Command::Test) {
        std::string directive = parse_platform_directive(code);
        bool sv_platform = false;
        {
            std::istringstream ss(directive);
            std::string token;
            while (std::getline(ss, token, '|')) {
                if (token == "sv" || token == "verilog" || token == "systemverilog") {
                    sv_platform = true;
                }
            }
        }
        if (sv_platform) {
            // SVフロー: compileと同じ経路でSV+TBを生成し、後段でシミュレーションを実行
            opts.command = Command::Compile;
            opts.target = "sv";
            run_sv_sim = true;
            if (opts.output_file.empty()) {
                std::filesystem::create_directories(".tmp/test");
                std::string stem = std::filesystem::path(opts.input_file).stem().string();
                opts.output_file = ".tmp/test/" + stem + ".sv";
            }
        } else {
            // JITフロー: Runと同じ経路で #[test] 関数を実行
            if (!match_platform_directive(directive, "native") &&
                !match_platform_directive(directive, "jit")) {
                std::cerr << i18n::msgf(i18n::MsgId::CliTestExecutionForPlatformIs, directive);
                std::cerr << i18n::msgf(i18n::MsgId::CliFile, opts.input_file);
                return 1;
            }
            opts.command = Command::Run;
        }
    }

    // //! platform: ディレクティブチェック
    {
        std::string directive = parse_platform_directive(code);
        if (!directive.empty()) {
            std::string current = opts.target;
            if (current.empty()) {
                if (opts.emit_js)
                    current = "js";
                else
                    current = "native";
            }

            // R14: ディレクティブ中の不明なプラットフォーム名（タイポ等）は専用エラーにする（従来はwarning表記でrc=1、かつヒントがタイポをそのまま--target=svv等と提案していた）
            {
                static const std::set<std::string> kValidPlatforms = {
                    "native", "jit", "js", "ts", "web", "wasm", "sv", "uefi", "baremetal", "bm"};
                std::string tokens = directive;
                if (!tokens.empty() && tokens[0] == '!') {
                    tokens = tokens.substr(1);
                }
                std::istringstream ss(tokens);
                std::string token;
                while (std::getline(ss, token, '|')) {
                    if (!token.empty() && kValidPlatforms.count(token) == 0) {
                        std::cerr << i18n::msgf(i18n::MsgId::CliUnknownPlatformDirective, token);
                        std::cerr << i18n::msgf(i18n::MsgId::CliFile, opts.input_file);
                        return 1;
                    }
                }
            }
            if (!match_platform_directive(directive, current)) {
                std::cerr << i18n::msgf(i18n::MsgId::CliThisFileTargetsPlatformCurrent, directive,
                                        current);
                std::cerr << i18n::msgf(i18n::MsgId::CliFile, opts.input_file);
                std::cerr << i18n::msgf(i18n::MsgId::CliUseTargetToSpecifyThe, directive);
                return 1;
            }
        }
    }

    // ========== SVモジュール階層の保持（exportされたIO構造体を持つ相対import）==========
    // 対象importをextern struct宣言に置換し、import先を後段で個別コンパイルする
    std::vector<std::string> sv_hierarchy_submodules;
    {
        bool is_sv_early =
            (opts.target == "sv" || opts.target == "verilog" || opts.target == "systemverilog");
        if (is_sv_early) {
            auto hres = codegen::sv::process_sv_hierarchy(code, opts.input_file);
            if (!hres.error.empty()) {
                std::cerr << i18n::msgf(i18n::MsgId::CliSvHierarchyError, hres.error);
                return 1;
            }
            if (hres.enabled) {
                code = hres.transformed_source;
                sv_hierarchy_submodules = hres.submodule_files;
                if (opts.verbose && !sv_hierarchy_submodules.empty()) {
                    std::cout << i18n::msgf(i18n::MsgId::CliSvHierarchySubmoduleSDetected,
                                            sv_hierarchy_submodules.size());
                }
            }
        }
    }

    if (opts.verbose) {
        switch (opts.command) {
            case Command::Run:
                std::cout << i18n::msgf(i18n::MsgId::CliRunning, opts.input_file);
                break;
            case Command::Compile:
                std::cout << i18n::msgf(i18n::MsgId::CliCompiling, opts.input_file);
                break;
            default:
                break;
        }
    }

    // ========== フロントエンド（import展開・条件コンパイル・字句・構文解析。共有パイプライン frontend.cpp）==========
    ast::Program program;
    auto& preprocess_result = ctx.preprocess;
    {
        cli::FrontendParams fparams;
        fparams.input_file = opts.input_file;
        fparams.defines = opts.defines;
        fparams.target = opts.target;
        fparams.test_mode = opts.test_mode;
        fparams.debug = opts.debug;
        auto front = cli::run_frontend(fparams, std::move(code));

        if (!front.internal_error_stage.empty()) {
            std::cerr << i18n::msgf(i18n::MsgId::CliInternalError, front.internal_error_stage,
                                    front.internal_error);
            return kExitFailure;
        }
        if (!front.preprocess_ok) {
            // R14: 位置情報付きの構文エラーはpreprocessor errorでなくsyntax errorとして表示する
            if (front.preprocess_error_has_location) {
                std::cerr << i18n::msgf(i18n::MsgId::CliSyntaxError, front.preprocess_error);
            } else {
                std::cerr << i18n::msgf(i18n::MsgId::CliPreprocessorError2, front.preprocess_error);
            }
            return 1;
        }

        // コンパイル時間計測開始
        ctx.compile_start = std::chrono::steady_clock::now();
        ctx.phase_preprocess_ms = front.phase_preprocess_ms;
        ctx.phase_parse_ms = front.phase_parse_ms;
        preprocess_result = std::move(front.preprocess);
        code = std::move(front.code);

        if (!front.parse_ok) {
            std::cerr << i18n::msg(i18n::MsgId::CliSyntaxErrorsOccurred);
            // 診断表示はDiagnosticEmitterへ一元化（source_map写像・参照ファイル読込を含む。X5）
            DiagnosticEmitter emitter(code, opts.input_file, &preprocess_result.source_map);
            emitter.emit_all(front.parser_diagnostics);
            return 1;  // エラー時は1で終了
        }
        program = std::move(front.program);
    }
    if (opts.debug)
        std::cout << i18n::msgf(i18n::MsgId::CliDeclarations, program.declarations.size());

    // ========== ターゲットフィルタ段階（SVトップモジュール抽出を含む）==========
    try {
        // SVターゲット用: `module NAME;` ヘッダ宣言からトップモジュール名を取得（lowering前に取得する。宣言が無ければ空文字＝ファイル名から推定）
        ctx.sv_top_module = codegen::sv::extract_top_module_name(program);

        // ========== Target Filtering ==========
        {
            Target active_target = Target::Native;
            if (opts.command == Command::Run) {
                active_target = Target::Native;  // JIT uses native target
            } else if (!opts.target.empty()) {
                active_target = string_to_target(opts.target);
            } else if (opts.emit_js) {
                active_target = Target::JS;
            }

            debug::ast::log(debug::ast::Id::Validate, "target=" + target_to_string(active_target),
                            debug::Level::Info);
            // テストモード以外では #[test] 宣言も除去される
            ast::TargetFilteringVisitor target_filter(active_target, opts.test_mode);
            target_filter.visit(program);
        }

        // with/derive自動実装のソース展開（derive-as-source-expansion 第1段: Eq）。
        // 合成implは通常の型検査→HIR→MIRを通り、手組みMIR生成は展開済みトレイトについて無効化されている
        macro_expand::expand_derives(program);

        // ASTを表示
        if (opts.show_ast) {
            print_ast(program);
        }
    } catch (const std::exception& e) {
        std::cerr << i18n::msgf(i18n::MsgId::CliInternalError, "parse", e.what());
        return kExitFailure;
    }

    // ========== 型検査段階 ==========
    try {
        // ========== Type Checker ==========
        if (opts.debug)
            std::cout << "=== Type Checker ===\n";
        auto phase_typecheck_start = std::chrono::steady_clock::now();
        TypeChecker checker;
        // SV入力ポート代入検査（R16。ターゲット指定またはソースの //! platform: sv で有効化）
        checker.set_sv_platform(opts.target == "sv" || opts.target == "verilog" ||
                                opts.target == "systemverilog" ||
                                code.find("//! platform: sv") != std::string::npos);
        // js/ts系は配列HOFを構造的にlowerするため、非スカラ要素のHOFゲートを解除する（局所処理調査E系。ターゲット指定またはソースの //! platform: 指定で有効化）
        checker.set_structural_array_lowering(opts.target == "js" || opts.target == "ts" ||
                                              opts.target == "web" ||
                                              code.find("//! platform: js") != std::string::npos ||
                                              code.find("//! platform: ts") != std::string::npos ||
                                              code.find("//! platform: web") != std::string::npos);
        // Check/Lintコマンド、または--force-check/--strict指定時にLint警告を有効化
        if (opts.force_check) {
            checker.set_enable_lint_warnings(true);
        }
        bool type_check_ok = checker.check(program);
        ctx.phase_typecheck_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - phase_typecheck_start)
                                     .count();

        // 診断情報（エラー・警告）の表示はDiagnosticEmitterへ一元化
        if (!checker.diagnostics().empty()) {
            DiagnosticEmitter emitter(code, opts.input_file, &preprocess_result.source_map);
            emitter.emit_all(checker.diagnostics());
        }

        // エラーがあった場合は終了
        if (!type_check_ok) {
            return 1;
        }
        if (opts.debug)
            std::cout << i18n::msg(i18n::MsgId::CliTypeCheckOk);
    } catch (const std::exception& e) {
        std::cerr << i18n::msgf(i18n::MsgId::CliInternalError, "typecheck", e.what());
        return kExitFailure;
    }

    // ========== IR変換段階（HIR/MIR lowering・最適化・検証）==========
    // MIRは #[test] 関数・SV initialブロックでHIR文への参照（const HirStmt*）を保持するため、HIRはバックエンド完了まで生存させる
    mir::MirProgram mir;
    hir::HirProgram hir;
    try {
        // ========== HIR Lowering ==========
        if (opts.debug)
            std::cout << "=== HIR Lowering ===\n";
        auto phase_hir_start = std::chrono::steady_clock::now();
        hir::HirLowering hir_lowering;
        // SVターゲットではリダクション演算子をnative出力用にビルトイン呼び出しへ残し（SV-N2）、don't-care matchをswitchへ脱糖する（SV-N3）。
        // 実際の出力ターゲットのみで判定する（//! platform: sv のファイルは非SVターゲットでは上のディレクティブ不一致検査で停止済み。cm testはこの時点でtargetをsvへ書換え済み）
        hir_lowering.set_sv_target(opts.target == "sv" || opts.target == "verilog" ||
                                   opts.target == "systemverilog");
        hir = hir_lowering.lower(program);
        // HIR型不変条件の監査（typed-hir-single-source）: CM_HIR_TYPE_AUDIT=1で違反集計、=2でサンプル出力
        if (const char* audit_env = std::getenv("CM_HIR_TYPE_AUDIT");
            audit_env && audit_env[0] != '0') {
            auto audit = hir::audit_types(hir);
            hir::report_type_audit(audit, opts.input_file, std::string(audit_env) == "2");
        }
        ctx.phase_hir_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - phase_hir_start)
                               .count();
        if (opts.debug)
            std::cout << i18n::msgf(i18n::MsgId::CliHirDeclarations, hir.declarations.size());

        // HIRを表示
        if (opts.show_hir) {
            print_hir(hir);
        }

        // ========== MIR Lowering ==========
        if (opts.debug)
            std::cout << "=== MIR Lowering ===\n";

        debug::log(debug::Stage::Mir, debug::Level::Info, "Starting MIR lowering");
        auto phase_mir_start = std::chrono::steady_clock::now();
        mir::MirLowering mir_lowering;
        // プリプロセッサのモジュール範囲情報を設定（ソースファイルベースの分割用）
        mir_lowering.set_module_ranges(&ctx.preprocess.module_ranges);
        debug::log(debug::Stage::Mir, debug::Level::Info, "Calling lower() function");
        mir = mir_lowering.lower(hir);
        debug::log(debug::Stage::Mir, debug::Level::Info, "MIR lowering completed");

        // 実行系コマンドでmainが無い場合は明示エラーにする
        // （従来はモジュール単体ファイルが構文エラーで早期失敗していたが、モジュール方言のパーサ対応で
        // ここまで到達するようになった。main無しのJIT実行は未定義動作系の不定挙動だったため確定診断へ）
        // js/ts/webターゲットのcompileはNodeで実行されるスクリプトを生成するため、
        // nativeのリンクエラー（_main未定義）に相当する検査としてここで確定させる
        const bool is_executable_script_target =
            opts.command == Command::Compile &&
            (opts.target == "js" || opts.target == "ts" || opts.target == "web");
        // テストモード（cm test / --test）は#[test]関数を個別エントリで実行するためmainを要求しない
        if ((opts.command == Command::Run || is_executable_script_target) && !opts.test_mode) {
            bool has_main = false;
            for (const auto& func : mir.functions) {
                if (func && func->name == "main") {
                    has_main = true;
                    break;
                }
            }
            if (!has_main) {
                std::cerr << i18n::msg(i18n::MsgId::CliEntryPointMainNotFound);
                return 1;
            }
        }

        // MIR段階の診断を表示し、エラーがあればcodegenへ進まない（diagnostics-engine-unification 第2段。従来はログのみでコンパイル続行し黙って壊れたコードを出していた）
        if (!mir_lowering.mir_diagnostics().empty()) {
            DiagnosticEmitter emitter(code, opts.input_file, &preprocess_result.source_map);
            emitter.emit_all(mir_lowering.mir_diagnostics());
            if (mir_lowering.has_diagnostic_errors()) {
                return 1;
            }
        }
        ctx.phase_mir_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - phase_mir_start)
                               .count();

        if (opts.debug)
            std::cout << i18n::msgf(i18n::MsgId::CliMirFunctions, mir.functions.size())
                      << std::flush;

        // MIRを表示（最適化前）
        if (opts.show_mir && !opts.show_mir_opt) {
            std::cout << i18n::msg(i18n::MsgId::CliMirBeforeOptimization);
            mir::MirPrinter printer;
            printer.print(mir, std::cout);
        }

        // SVターゲット判定（最適化とDCEのスキップ判定に使用）
        bool is_sv =
            (opts.target == "sv" || opts.target == "verilog" || opts.target == "systemverilog");

        // ========== Optimization ==========
        auto phase_opt_start = std::chrono::steady_clock::now();
        // SVターゲットではMIR最適化をスキップ（合成ツールが最適化を行う）
        // DCE/CopyProp/ConstFoldが一時変数代入を除去しHWロジックが消失するため
        if ((opts.optimization_level > 0 || opts.show_mir_opt || opts.unroll_loops) && !is_sv) {
            if (cm::debug::debug_mode())
                std::cerr << "[OPT] Starting optimization at level " << opts.optimization_level
                          << std::endl;
            if (opts.debug)
                std::cout << "=== Optimization (Level " << opts.optimization_level << ") ===\n"
                          << std::flush;

            // MIR最適化パスマネージャーv2を使用（収束管理と無限ループ防止機能付き）
            mir::opt::MirOptimizationOptions user_opts;
            user_opts.unroll_loops = opts.unroll_loops;
            user_opts.unroll_max_trips = opts.unroll_max_trips;
            // js/tsは構造体コピーが深いクローンのため、集約コピーの伝播は不健全（別実体の変異になる）
            user_opts.no_aggregate_copy_prop =
                (opts.target == "js" || opts.target == "ts" || opts.target == "web");
            mir::opt::run_optimization_passes(mir, opts.optimization_level,
                                              opts.debug || opts.verbose, user_opts);
            if (cm::debug::debug_mode())
                std::cerr << "[OPT] Optimization complete" << std::endl;

            if (opts.debug)
                std::cout << i18n::msg(i18n::MsgId::CliOptimizationComplete);
        } else if (!is_sv) {
            // O0でも文字列再代入の旧バッファ解放（C12）だけは実行する。
            // メモリ健全性のためのパスであり最適化ではないため、最適化レベルに依存させない
            mir::opt::StringReassignFree o0_reassign_free;
            for (auto& func : mir.functions) {
                if (func) {
                    o0_reassign_free.run(*func);
                }
            }
        }

        // SVターゲット: 定数トリップカウントのループを静的展開する（generate/genvar相当。合成ツールは動的whileを展開できないため）
        if (is_sv) {
            mir::opt::unroll_constant_loops(mir);
            // 合成前の定数畳み込み・恒等式簡約（2*3+4→10、x*1→x 等）。
            // 文数・CFG形状を変えない書き換えのみで、DCE/CopyProp等の文除去系パスはHWロジックを消すため引き続き実行しない
            if (opts.optimization_level > 0) {
                mir::opt::ConstantFolding sv_folding(/*fold_terminators=*/false);
                sv_folding.run_on_program(mir);
            }
        }
        ctx.phase_opt_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - phase_opt_start)
                               .count();

        // 関数レベルのDCE（コンパイル時のみ、SVターゲットではスキップ）
        // SVターゲットではハードウェアモジュールとして全関数を保持する
        if (opts.command == Command::Compile && !is_sv) {
            mir::opt::DeadCodeElimination dce;
            for (auto& func : mir.functions) {
                if (func) {
                    dce.run(*func);
                }
            }
        }

        // プログラムレベルのデッドコード削除
        // 未使用の自動生成関数を削除する
        // 注意: インタプリタではインターフェースメソッドの動的ディスパッチがあるため、DCEはコンパイル時のみ実行する
        // SVターゲットでは全関数をハードウェアモジュールとして保持
        if (opts.command == Command::Compile && !is_sv) {
            mir::opt::ProgramDeadCodeElimination program_dce;
            program_dce.run(mir);
        }

        // MIRを表示（最適化後）
        if (opts.show_mir_opt) {
            std::cout << i18n::msg(i18n::MsgId::CliMirAfterOptimization);
            mir::MirPrinter printer;
            printer.print(mir, std::cout);
            return 0;
        }

        // ========== async/awaitバリデーション ==========
        // 非JSターゲットでasync/awaitが使用されている場合はエラー
        {
            bool is_js_target = (opts.target == "js" || opts.target == "web" ||
                                 opts.target == "ts" || opts.emit_js);
            bool is_sv_target =
                (opts.target == "sv" || opts.target == "verilog" || opts.target == "systemverilog");
            if (!is_js_target && !is_sv_target) {
                bool has_async = false;
                std::string async_func_name;
                bool has_await = false;
                std::string await_func_name;

                for (const auto& func : mir.functions) {
                    if (!func)
                        continue;
                    if (func->is_async) {
                        has_async = true;
                        async_func_name = func->name;
                    }
                    for (const auto& block : func->basic_blocks) {
                        if (!block || !block->terminator)
                            continue;
                        if (block->terminator->kind == mir::MirTerminator::Call) {
                            const auto& data =
                                std::get<mir::MirTerminator::CallData>(block->terminator->data);
                            if (data.is_awaited) {
                                has_await = true;
                                await_func_name = func->name;
                            }
                        }
                    }
                }

                if (has_async || has_await) {
                    std::cerr << i18n::msg(i18n::MsgId::CliAsyncAwaitIsOnlySupported);
                    if (has_async) {
                        std::cerr << i18n::msgf(i18n::MsgId::CliAsyncFunctionDetected,
                                                async_func_name);
                    }
                    if (has_await) {
                        std::cerr << i18n::msgf(i18n::MsgId::CliAwaitExpressionDetectedFunction,
                                                await_func_name);
                    }
                    std::cerr << i18n::msg(i18n::MsgId::CliSpecifyTheJsTargetWith);
                    return 1;
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << i18n::msgf(i18n::MsgId::CliInternalError, "lowering", e.what());
        return kExitFailure;
    }

    // ========== バックエンドディスパッチ ==========
    ctx.code = std::move(code);
    ctx.run_sv_sim = run_sv_sim;
    ctx.sv_hierarchy_submodules = std::move(sv_hierarchy_submodules);
    // --sanitize の実行系別許可: native/wasm=全種、jit/js=bounds・undefined等のランタイム不要検査のみ、sv=非対応
    if (!opts.sanitizers.empty()) {
        const bool is_sv =
            opts.target == "sv" || opts.target == "verilog" || opts.target == "systemverilog";
        const bool is_js =
            opts.target == "js" || opts.target == "web" || opts.target == "ts" || opts.emit_js;
        std::string unsupported;
        if (is_sv) {
            unsupported = opts.sanitizers.front();
        } else if (is_js) {
            // JSはMIRレベル計装のundefined/boundsのみ対応（LLVM計装パス・サニタイザランタイムは適用不能）
            for (const auto& sanitizer : opts.sanitizers) {
                if (sanitizer != "undefined" && sanitizer != "bounds") {
                    unsupported = sanitizer;
                    break;
                }
            }
        } else if (opts.command == Command::Run) {
            // JITはcmプロセス内実行のためASan/TSan/MSanランタイムを後付けできない（bounds/undefinedはtrap・panic方式で動作する）
            for (const auto& sanitizer : opts.sanitizers) {
                if (sanitizer != "bounds" && sanitizer != "undefined") {
                    unsupported = sanitizer;
                    break;
                }
            }
        }
        if (!unsupported.empty()) {
            std::string target_name = is_sv || is_js
                                          ? (opts.target.empty() ? "js" : opts.target)
                                          : (opts.command == Command::Run ? "jit" : opts.target);
            std::cerr << i18n::msgf(i18n::MsgId::CliSanitizeNotSupportedOnTarget, unsupported,
                                    target_name);
            std::cerr << i18n::msg(i18n::MsgId::CliSanitizeValidValues);
            return 1;
        }
        // undefined: MIRレベルの計装をここで適用する（LLVM系・JSの全実行系で共通の検査になる）
        if (std::find(opts.sanitizers.begin(), opts.sanitizers.end(), "undefined") !=
            opts.sanitizers.end()) {
            mir::opt::instrument_undefined_checks(mir);
        }
        // bounds: スライスアクセスのMIRレベル境界検査（M1）。
        // LLVMのBoundsCheckingPass（固定長配列専用）と補完関係で、スライスは
        // ランタイム関数呼び出し越しのアクセスのためMIRで明示検査を挿入する
        if (!is_sv && std::find(opts.sanitizers.begin(), opts.sanitizers.end(), "bounds") !=
                          opts.sanitizers.end()) {
            mir::opt::instrument_bounds_checks(mir);
        }
    }
    if (opts.command == Command::Run) {
        return emit_jit_run(ctx, mir);
    }
    if (opts.command == Command::Compile) {
        if (opts.target == "sv" || opts.target == "verilog" || opts.target == "systemverilog") {
            return emit_sv(ctx, mir);
        }
        if (opts.target == "js" || opts.target == "web" || opts.target == "ts" || opts.emit_js) {
            return emit_js(ctx, mir);
        }
        return emit_llvm(ctx, mir);
    }
    return kExitSuccess;
}

}  // namespace cm::driver
