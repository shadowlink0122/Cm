// run/compile/test のコンパイルパイプライン。
// 各段階（前処理・構文解析・型検査・IR変換）ごとに狭いtryで捕捉し、失敗した段階名付きで報告する。
// バックエンド（JIT/SV/JS/LLVM）へのディスパッチは backend_*.cpp が担う

#include "driver.hpp"
#include "internal/base/debug_messages.hpp"
#include "internal/base/i18n.hpp"
#include "internal/base/source_location.hpp"
#include "internal/codegen/sv/codegen.hpp"
#include "internal/codegen/sv/hierarchy.hpp"
#include "internal/hir/lowering/lowering.hpp"
#include "internal/mir/lowering/lowering.hpp"
#include "internal/mir/passes/cleanup/dce.hpp"
#include "internal/mir/passes/cleanup/program_dce.hpp"
#include "internal/mir/passes/core/manager.hpp"
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

    // ========== Initialize Module Resolver ==========
    if (opts.debug)
        std::cout << "=== Module Resolver Init ===\n";
    module::initialize_module_resolver();

    // 段階間で共有する状態はBuildContextに置き、参照で従来の変数名に束縛する
    auto& preprocess_result = ctx.preprocess;

    // ========== 前処理段階（import展開・条件付きコンパイル）==========
    try {
        // ========== Import Preprocessor ==========
        if (opts.debug)
            std::cout << "=== Import Preprocessor ===\n";
        auto phase_preprocess_start = std::chrono::steady_clock::now();
        preprocessor::ImportPreprocessor import_preprocessor(opts.debug);
        preprocess_result = import_preprocessor.process(code, opts.input_file);
        ctx.phase_preprocess_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                      std::chrono::steady_clock::now() - phase_preprocess_start)
                                      .count();

        if (!preprocess_result.success) {
            std::cerr << i18n::msgf(i18n::MsgId::CliPreprocessorError2,
                                    preprocess_result.error_message);
            return 1;
        }

        // デバッグ出力
        {
            std::ofstream out(".tmp/preprocessed.cm");
            if (out) {
                out << preprocess_result.processed_source;
            }
        }

        // コンパイル時間計測開始
        ctx.compile_start = std::chrono::steady_clock::now();

        if (opts.debug && !preprocess_result.imported_modules.empty()) {
            std::cout << i18n::msg(i18n::MsgId::CliImportedModules);
            for (const auto& module : preprocess_result.imported_modules) {
                std::cout << "  - " << module << "\n";
            }
            std::cout << "\n";
        }

        // プリプロセス後のコードを使用
        code = preprocess_result.processed_source;

        // ========== Conditional Preprocessor ==========
        if (opts.debug)
            std::cout << "=== Conditional Preprocessor ===\n";
        preprocessor::ConditionalPreprocessor conditional;
        // -D オプションのユーザ定義を追加
        for (const auto& def : opts.defines) {
            conditional.define(def);
        }
        // テストモード（cm test / --test）: TEST を自動定義（#[test] と連動するテスト補助コードを #ifdef TEST で書けるようにする）
        if (opts.test_mode) {
            conditional.define("TEST");
        }
        // ターゲットに応じたプリプロセッサ定数を追加
        if (opts.target == "baremetal-arm" || opts.target == "bm" ||
            opts.target == "baremetal-x86" || opts.target == "bm-x86") {
            conditional.define("__NO_STD__");
            conditional.define("__BAREMETAL__");
        } else if (opts.target == "uefi") {
            conditional.define("__NO_STD__");
            conditional.define("__BAREMETAL__");  // UEFIもベアメタルサブカテゴリ
            conditional.define("__UEFI__");
            conditional.define("__EFI__");
        }
        code = conditional.process(code);
        if (opts.debug) {
            std::cout << i18n::msg(i18n::MsgId::CliDefinedSymbols);
            for (const auto& def : conditional.definitions()) {
                std::cout << def << " ";
            }
            std::cout << "\n";
        }

        // デバッグ時はプリプロセス後のコードを出力
        if (opts.debug) {
            std::cout << "=== Preprocessed Code ===\n";
            std::cout << code << "\n";
            std::cout << "=== End Preprocessed Code ===\n\n";
        }
    } catch (const std::exception& e) {
        std::cerr << i18n::msgf(i18n::MsgId::CliInternalError, "preprocess", e.what());
        return kExitFailure;
    }

    // ========== 構文解析段階（字句解析・構文解析・ターゲットフィルタ）==========
    ast::Program program;
    auto phase_parse_start = std::chrono::steady_clock::now();
    try {
        // ========== Lexer ==========
        if (opts.debug)
            std::cout << "=== Lexer ===\n";
        // ターゲットに応じたレキサープラットフォーム設定
        LexerPlatform lexer_platform = LexerPlatform::Default;
        if (opts.target == "sv" || opts.target == "verilog" || opts.target == "systemverilog") {
            lexer_platform = LexerPlatform::SV;
        }
        Lexer lexer(code, lexer_platform);
        auto tokens = lexer.tokenize();

        if (opts.debug)
            std::cout << i18n::msgf(i18n::MsgId::CliTokens, tokens.size());

        // ========== Parser ==========
        if (opts.debug)
            std::cout << "=== Parser ===\n";
        Parser parser(std::move(tokens), lexer.is_sv());
        program = parser.parse();

        if (parser.has_errors()) {
            std::cerr << i18n::msg(i18n::MsgId::CliSyntaxErrorsOccurred);
            // ソース位置管理を作成
            SourceLocationManager loc_mgr(code, opts.input_file);

            // 診断情報を表示
            for (const auto& diag : parser.diagnostics()) {
                // エラーメッセージをフォーマットして表示
                std::string error_type =
                    (diag.severity == DiagKind::Error ? i18n::msg(i18n::MsgId::CliS)
                                                      : i18n::msg(i18n::MsgId::CliS2));
                std::cerr << loc_mgr.format_error_location(diag.span,
                                                           error_type + ": " + diag.message);
            }
            return 1;  // エラー時は1で終了
        }
        if (opts.debug)
            std::cout << i18n::msgf(i18n::MsgId::CliDeclarations, program.declarations.size());

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
        ctx.phase_parse_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - phase_parse_start)
                                 .count();
        auto phase_typecheck_start = std::chrono::steady_clock::now();
        TypeChecker checker;
        // Check/Lintコマンド、または--force-check/--strict指定時にLint警告を有効化
        if (opts.force_check) {
            checker.set_enable_lint_warnings(true);
        }
        bool type_check_ok = checker.check(program);
        ctx.phase_typecheck_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - phase_typecheck_start)
                                     .count();

        // 診断情報（エラー・警告）を表示
        if (!checker.diagnostics().empty()) {
            // ソース位置管理を作成
            SourceLocationManager loc_mgr(code, opts.input_file);

            // ソースマップがある場合、元ファイルの内容を読み込む
            std::unordered_map<std::string, std::string> file_contents;
            if (!preprocess_result.source_map.empty()) {
                // ソースマップから参照されているファイルを収集
                std::set<std::string> files_to_load;
                for (const auto& entry : preprocess_result.source_map) {
                    if (!entry.original_file.empty() && entry.original_file != "<unknown>" &&
                        entry.original_file != "<generated>") {
                        files_to_load.insert(entry.original_file);
                    }
                    // インポートチェーンからもファイルを収集
                    if (!entry.import_chain.empty()) {
                        std::string remaining = entry.import_chain;
                        std::string delimiter = " -> ";
                        size_t pos;
                        while ((pos = remaining.find(delimiter)) != std::string::npos) {
                            std::string part = remaining.substr(0, pos);
                            if (!part.empty() && part != "<unknown>" && part != "<generated>") {
                                files_to_load.insert(part);
                            }
                            remaining = remaining.substr(pos + delimiter.length());
                        }
                        if (!remaining.empty() && remaining != "<unknown>" &&
                            remaining != "<generated>") {
                            files_to_load.insert(remaining);
                        }
                    }
                }
                // 各ファイルの内容を読み込む
                for (const auto& file : files_to_load) {
                    std::ifstream ifs(file);
                    if (ifs) {
                        std::stringstream buffer;
                        buffer << ifs.rdbuf();
                        file_contents[file] = buffer.str();
                    }
                }
            }

            // 診断情報を表示
            for (const auto& diag : checker.diagnostics()) {
                if (!preprocess_result.source_map.empty()) {
                    std::cerr << loc_mgr.format_error_with_source_map(
                        diag.span, diag.message, preprocess_result.source_map, file_contents,
                        diag.severity == DiagKind::Error ? "error" : "warning");
                } else {
                    std::string error_type =
                        (diag.severity == DiagKind::Error ? i18n::msg(i18n::MsgId::CliS)
                                                          : i18n::msg(i18n::MsgId::CliS2));
                    std::cerr << loc_mgr.format_error_location(diag.span,
                                                               error_type + ": " + diag.message);
                }
            }
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
        hir = hir_lowering.lower(program);
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
            mir::opt::run_optimization_passes(mir, opts.optimization_level,
                                              opts.debug || opts.verbose, user_opts);
            if (cm::debug::debug_mode())
                std::cerr << "[OPT] Optimization complete" << std::endl;

            if (opts.debug)
                std::cout << i18n::msg(i18n::MsgId::CliOptimizationComplete);
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
            // JSはMIRレベル計装のundefinedのみ対応（LLVM計装パス・サニタイザランタイムは適用不能）
            for (const auto& sanitizer : opts.sanitizers) {
                if (sanitizer != "undefined") {
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
