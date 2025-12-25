// LLVM version check (must be first)
#ifdef CM_LLVM_ENABLED
#include <llvm/Config/llvm-config.h>
#endif

// LLVM codegen (if enabled)
#ifdef CM_LLVM_ENABLED
#include "codegen/llvm/native/codegen.hpp"
#endif

// JavaScript codegen
#include "codegen/interpreter/interpreter.hpp"
#include "codegen/js/codegen.hpp"
#include "common/debug_messages.hpp"
#include "common/source_location.hpp"
#include "frontend/ast/target_filtering_visitor.hpp"
#include "frontend/lexer/lexer.hpp"
#include "frontend/parser/parser.hpp"
#include "frontend/types/type_checker.hpp"
#include "hir/lowering/lowering.hpp"
#include "mir/lowering/lowering.hpp"
#include "mir/optimizations/all_passes.hpp"
#include "mir/printer.hpp"
#include "module/resolver.hpp"
#include "preprocessor/import.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <vector>

namespace cm {

// バージョン情報を取得
std::string get_version() {
    std::ifstream version_file(".VERSION");
    if (!version_file.is_open()) {
        // フォールバック
        return "0.1.0";
    }
    std::string version;
    std::getline(version_file, version);
    return version;
}

// コマンドラインオプション
enum class Command { None, Run, Compile, Check, Help };

struct Options {
    Command command = Command::None;
    std::string input_file;
    bool show_ast = false;
    bool show_hir = false;
    bool show_mir = false;
    bool show_mir_opt = false;
    bool emit_llvm = false;
    bool emit_js = false;         // JavaScript生成
    std::string target = "";      // ターゲット (native, wasm, js, web)
    bool run_after_emit = false;  // 生成後に実行
    int optimization_level = 0;
    bool debug = false;
    std::string debug_level = "info";
    bool verbose = false;     // デフォルトは静かなモード
    std::string output_file;  // -o オプション
};

// ヘルプメッセージを表示
void print_help(const char* program_name) {
    std::cout << "Cm言語コンパイラ v" << get_version() << "\n\n";
    std::cout << "使用方法:\n";
    std::cout << "  " << program_name << " <コマンド> [オプション] <ファイル>\n\n";
    std::cout << "コマンド:\n";
    std::cout << "  run <file>            プログラムを実行（インタプリタ）\n";
    std::cout << "  compile <file>        プログラムをコンパイル（LLVM）\n";
    std::cout << "  check <file>          構文と型チェックのみ実行\n";
    std::cout << "  help                  このヘルプを表示\n\n";
    std::cout << "オプション:\n";
    std::cout << "  -o <file>             出力ファイル名を指定\n";
    std::cout << "  -O<n>                 最適化レベル（0-3）\n";
    std::cout << "  --verbose, -v         詳細な出力を表示\n";
    std::cout << "  --debug, -d           デバッグ出力を有効化\n";
    std::cout << "  -d=<level>            デバッグレベル（trace/debug/info/warn/error）\n\n";
    std::cout << "コンパイル時オプション:\n";
    std::cout << "  --target=<target>     コンパイルターゲット (native/wasm/js/web)\n";
    std::cout << "                        native: ネイティブ実行ファイル（デフォルト）\n";
    std::cout << "                        wasm:   WebAssembly\n";
    std::cout << "                        js:     JavaScript (Node.js向け)\n";
    std::cout << "                        web:    JavaScript + HTML (ブラウザ向け)\n";
    std::cout << "  --emit-llvm           LLVM IRを生成\n";
    std::cout << "  --emit-js             JavaScriptを生成\n";
    std::cout << "  --run                 生成後に実行\n";
    std::cout << "  --ast                 AST（抽象構文木）を表示\n";
    std::cout << "  --hir                 HIR（高レベル中間表現）を表示\n";
    std::cout << "  --mir                 MIR（中レベル中間表現）を表示\n";
    std::cout << "  --mir-opt             最適化後のMIRを表示\n\n";
    std::cout << "その他のオプション:\n";
    std::cout << "  --lang=ja             日本語デバッグメッセージ\n";
    std::cout << "  --version             バージョン情報を表示\n\n";
    std::cout << "例:\n";
    std::cout << "  " << program_name << " run examples/hello.cm\n";
    std::cout << "  " << program_name << " compile -O2 -o output src/main.cm\n";
    std::cout << "  " << program_name
              << " compile --backend=llvm --target=wasm -o app.wasm main.cm\n";
    std::cout << "  " << program_name
              << " compile --backend=llvm --target=bm -o firmware.o main.cm\n";
    std::cout << "  " << program_name << " check --verbose src/lib.cm\n";
}

// コマンドラインオプションをパース
Options parse_options(int argc, char* argv[]) {
    Options opts;

    if (argc < 2) {
        return opts;  // コマンドなし
    }

    // 最初の引数でコマンドを判定
    std::string cmd = argv[1];
    if (cmd == "run") {
        opts.command = Command::Run;
    } else if (cmd == "compile") {
        opts.command = Command::Compile;
    } else if (cmd == "check") {
        opts.command = Command::Check;
    } else if (cmd == "help" || cmd == "--help" || cmd == "-h") {
        opts.command = Command::Help;
        return opts;
    } else if (cmd == "--version") {
        std::cout << "Cm言語コンパイラ v0.1.0 (開発版)\n";
        std::exit(0);
    } else if (cmd[0] != '-') {
        // 旧形式は使用不可 - ヘルプを表示
        std::cerr << "エラー: 不正なコマンド形式です\n";
        std::cerr << "ファイル '" << cmd << "' を実行するには 'cm run " << cmd
                  << "' を使用してください\n\n";
        opts.command = Command::Help;
        return opts;
    } else {
        std::cerr << "不明なコマンド: " << cmd << "\n";
        std::cerr << "'cm help' でヘルプを表示\n";
        std::exit(1);
    }

    // 残りの引数を処理
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--verbose" || arg == "-v") {
            opts.verbose = true;
        } else if (arg == "--ast") {
            opts.show_ast = true;
        } else if (arg == "--hir") {
            opts.show_hir = true;
        } else if (arg == "--mir") {
            opts.show_mir = true;
        } else if (arg == "--mir-opt") {
            opts.show_mir_opt = true;
        } else if (arg == "--emit-llvm") {
            opts.emit_llvm = true;
        } else if (arg == "--emit-js") {
            opts.emit_js = true;
        } else if (arg.substr(0, 9) == "--target=") {
            opts.target = arg.substr(9);
        } else if (arg == "--run") {
            opts.run_after_emit = true;
        } else if (arg == "-o") {
            if (i + 1 < argc) {
                opts.output_file = argv[++i];
            } else {
                std::cerr << "-o オプションには出力ファイル名が必要です\n";
                std::exit(1);
            }
        } else if (arg.substr(0, 2) == "-O") {
            if (arg.length() > 2) {
                opts.optimization_level = arg[2] - '0';
                if (opts.optimization_level < 0 || opts.optimization_level > 3) {
                    std::cerr << "最適化レベルは0-3の範囲で指定してください\n";
                    std::exit(1);
                }
            }
        } else if (arg == "--debug" || arg == "-d") {
            opts.debug = true;
            debug::set_debug_mode(true);
        } else if (arg.substr(0, 3) == "-d=") {
            opts.debug = true;
            opts.debug_level = arg.substr(3);
            debug::set_debug_mode(true);
            debug::set_level(debug::parse_level(opts.debug_level));
        } else if (arg == "--lang=ja") {
            debug::set_lang(1);
        } else if (arg[0] != '-') {
            if (opts.input_file.empty()) {
                opts.input_file = arg;
            } else {
                std::cerr << "複数の入力ファイルは指定できません\n";
                std::exit(1);
            }
        } else {
            std::cerr << "不明なオプション: " << arg << "\n";
            std::cerr << "'cm help' でヘルプを表示\n";
            std::exit(1);
        }
    }

    return opts;
}

// ファイルを読み込む
std::string read_file(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "エラー: ファイルを開けません: " << filename << "\n";
        std::exit(1);
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// ASTを表示
void print_ast(const ast::Program& program) {
    std::cout << "=== AST (Abstract Syntax Tree) ===\n";
    std::cout << "Declarations: " << program.declarations.size() << "\n\n";

    for (const auto& decl : program.declarations) {
        if (auto* func = std::get_if<std::unique_ptr<ast::FunctionDecl>>(&decl->kind)) {
            std::cout << "Function: " << (*func)->name << "\n";
            std::cout << "  Parameters: " << (*func)->params.size() << "\n";
            std::cout << "  Body statements: " << (*func)->body.size() << "\n";
        } else if (auto* st = std::get_if<std::unique_ptr<ast::StructDecl>>(&decl->kind)) {
            std::cout << "Struct: " << (*st)->name << "\n";
            std::cout << "  Fields: " << (*st)->fields.size() << "\n";
        }
    }
    std::cout << "\n";
}

// HIRを表示
void print_hir(const hir::HirProgram& program) {
    std::cout << "=== HIR (High-level Intermediate Representation) ===\n";
    std::cout << "Declarations: " << program.declarations.size() << "\n\n";

    for (const auto& decl : program.declarations) {
        if (auto* func = std::get_if<std::unique_ptr<hir::HirFunction>>(&decl->kind)) {
            std::cout << "Function: " << (*func)->name << "\n";
            std::cout << "  Parameters: " << (*func)->params.size() << "\n";
            std::cout << "  Body statements: " << (*func)->body.size() << "\n";

            // for文の脱糖を確認
            bool has_loop = false;
            for (const auto& stmt : (*func)->body) {
                if (std::get_if<std::unique_ptr<hir::HirLoop>>(&stmt->kind)) {
                    // Loop文があれば、for文がループに脱糖された可能性
                    has_loop = true;
                }
            }
            if (has_loop) {
                std::cout << "  Note: for/while文がHirLoopに変換されています\n";
            }
        }
    }
    std::cout << "\n";
}

}  // namespace cm

int main(int argc, char* argv[]) {
    using namespace cm;

    // オプションをパース
    Options opts = parse_options(argc, argv);

    // コマンドの処理
    if (opts.command == Command::Help) {
        print_help(argv[0]);
        return 0;
    }

    if (opts.command == Command::None || opts.input_file.empty()) {
        if (argc == 1) {
            std::cerr << "エラー: コマンドが指定されていません\n";
            std::cerr << "'cm help' でヘルプを表示\n";
        } else {
            std::cerr << "エラー: 入力ファイルが指定されていません\n";
        }
        return 1;
    }

    // ファイルを読み込む
    std::string code = read_file(opts.input_file);

    if (opts.verbose) {
        switch (opts.command) {
            case Command::Run:
                std::cout << "実行中: " << opts.input_file << "\n\n";
                break;
            case Command::Compile:
                std::cout << "コンパイル中: " << opts.input_file << "\n\n";
                break;
            case Command::Check:
                std::cout << "チェック中: " << opts.input_file << "\n\n";
                break;
            default:
                break;
        }
    }

    try {
        // ========== Initialize Module Resolver ==========
        if (opts.debug)
            std::cout << "=== Module Resolver Init ===\n";

        module::initialize_module_resolver();

        // ========== Import Preprocessor ==========
        if (opts.debug)
            std::cout << "=== Import Preprocessor ===\n";

        preprocessor::ImportPreprocessor import_preprocessor(opts.debug);
        auto preprocess_result = import_preprocessor.process(code, opts.input_file);

        if (!preprocess_result.success) {
            std::cerr << "プリプロセッサエラー: " << preprocess_result.error_message << "\n";
            return 1;
        }

        if (opts.debug && !preprocess_result.imported_modules.empty()) {
            std::cout << "インポートされたモジュール:\n";
            for (const auto& module : preprocess_result.imported_modules) {
                std::cout << "  - " << module << "\n";
            }
            std::cout << "\n";
        }

        // プリプロセス後のコードを使用
        code = preprocess_result.processed_source;

        // デバッグ時はプリプロセス後のコードを出力
        if (opts.debug) {
            std::cout << "=== Preprocessed Code ===\n";
            std::cout << code << "\n";
            std::cout << "=== End Preprocessed Code ===\n\n";
        }

        // ========== Lexer ==========
        if (opts.debug)
            std::cout << "=== Lexer ===\n";
        Lexer lexer(code);
        auto tokens = lexer.tokenize();
        if (opts.debug)
            std::cout << "トークン数: " << tokens.size() << "\n\n";

        // ========== Parser ==========
        if (opts.debug)
            std::cout << "=== Parser ===\n";
        Parser parser(std::move(tokens));
        auto program = parser.parse();

        if (parser.has_errors()) {
            std::cerr << "構文エラーが発生しました\n";
            // ソース位置管理を作成
            SourceLocationManager loc_mgr(code, opts.input_file);

            // 診断情報を表示
            for (const auto& diag : parser.diagnostics()) {
                // エラーメッセージをフォーマットして表示
                std::string error_type = (diag.kind == DiagKind::Error ? "エラー" : "警告");
                std::cerr << loc_mgr.format_error_location(diag.span,
                                                           error_type + ": " + diag.message);
            }
            std::exit(1);  // エラー時はexit(1)で終了
        }
        if (opts.debug)
            std::cout << "宣言数: " << program.declarations.size() << "\n\n";

        // ========== Target Filtering ==========
        {
            Target active_target = Target::Native;
            if (opts.command == Command::Run) {
                active_target = Target::Interpreter;
            } else if (!opts.target.empty()) {
                active_target = string_to_target(opts.target);
            } else if (opts.emit_js) {
                active_target = Target::JS;
            }

            debug::ast::log(debug::ast::Id::Validate,
                            "target=" + target_to_string(active_target), debug::Level::Info);
            ast::TargetFilteringVisitor target_filter(active_target);
            target_filter.visit(program);
        }

        // ASTを表示
        if (opts.show_ast) {
            print_ast(program);
        }

        // ========== Type Checker ==========
        if (opts.debug)
            std::cout << "=== Type Checker ===\n";
        TypeChecker checker;
        if (!checker.check(program)) {
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
                        diag.span, diag.message, preprocess_result.source_map, file_contents);
                } else {
                    std::string error_type = (diag.kind == DiagKind::Error ? "エラー" : "警告");
                    std::cerr << loc_mgr.format_error_location(diag.span,
                                                               error_type + ": " + diag.message);
                }
            }
            return 1;
        }
        if (opts.debug)
            std::cout << "型チェック: OK\n\n";

        // チェックコマンドの場合はここで終了
        if (opts.command == Command::Check) {
            if (opts.verbose) {
                std::cout << "✓ 構文と型チェックが成功しました\n";
            }
            return 0;
        }

        // ========== HIR Lowering ==========
        if (opts.debug)
            std::cout << "=== HIR Lowering ===\n";
        hir::HirLowering hir_lowering;
        auto hir = hir_lowering.lower(program);
        if (opts.debug)
            std::cout << "HIR宣言数: " << hir.declarations.size() << "\n\n";

        // HIRを表示
        if (opts.show_hir) {
            print_hir(hir);
        }

        // ========== MIR Lowering ==========
        if (opts.debug)
            std::cout << "=== MIR Lowering ===\n";

        debug::log(debug::Stage::Mir, debug::Level::Info, "Starting MIR lowering");
        mir::MirLowering mir_lowering;
        debug::log(debug::Stage::Mir, debug::Level::Info, "Calling lower() function");
        auto mir = mir_lowering.lower(hir);
        debug::log(debug::Stage::Mir, debug::Level::Info, "MIR lowering completed");

        if (opts.debug)
            std::cout << "MIR関数数: " << mir.functions.size() << "\n\n";

        // MIRを表示（最適化前）
        if (opts.show_mir && !opts.show_mir_opt) {
            std::cout << "=== MIR (最適化前) ===\n";
            mir::MirPrinter printer;
            printer.print(mir);
        }

        // ========== Optimization ==========
        if (opts.optimization_level > 0 || opts.show_mir_opt) {
            if (opts.debug)
                std::cout << "=== Optimization (Level " << opts.optimization_level << ") ===\n";

            // Note: Function-level optimizations are temporarily disabled due to potential issues
            // mir::opt::OptimizationPipeline pipeline;
            // pipeline.add_standard_passes(opts.optimization_level);
            // if (opts.optimization_level >= 2) {
            //     pipeline.run_until_fixpoint(mir);
            // } else {
            //     pipeline.run(mir);
            // }

            if (opts.debug)
                std::cout << "最適化完了\n\n";
        }

        // プログラムレベルのデッドコード削除
        // 未使用の自動生成関数を削除する
        // 注意: インタプリタではインターフェースメソッドの動的ディスパッチがあるため、
        // DCEはコンパイル時のみ実行する
        if (opts.command == Command::Compile) {
            mir::opt::ProgramDeadCodeElimination program_dce;
            program_dce.run(mir);
        }

        // MIRを表示（最適化後）
        if (opts.show_mir_opt) {
            std::cout << "=== MIR (最適化後) ===\n";
            mir::MirPrinter printer;
            printer.print(mir);
        }

        // ========== Backend ==========
        if (opts.command == Command::Run) {
            if (opts.verbose) {
                std::cout << "=== Interpreter ===\n";
            }
            mir::interp::Interpreter interpreter;
            auto result = interpreter.execute(mir);

            if (!result.success) {
                std::cerr << "実行エラー: " << result.error_message << "\n";
                return 1;
            }

            // 戻り値がある場合
            int exit_code = 0;
            if (result.return_value.has_value()) {
                if (result.return_value.type() == typeid(int64_t)) {
                    exit_code = static_cast<int>(std::any_cast<int64_t>(result.return_value));
                    if (opts.verbose) {
                        std::cout << "プログラム終了コード: " << exit_code << "\n";
                    }
                } else if (result.return_value.type() == typeid(bool)) {
                    exit_code = std::any_cast<bool>(result.return_value) ? 1 : 0;
                    if (opts.verbose) {
                        std::cout << "プログラム終了コード: " << exit_code << "\n";
                    }
                }
            }
            if (opts.verbose) {
                std::cout << "✓ 実行完了\n";
            }

            // プログラムの戻り値を終了コードとして返す
            return exit_code;
        }

        // コンパイルコマンドの場合
        if (opts.command == Command::Compile) {
            // JavaScript ターゲットの場合
            if (opts.target == "js" || opts.target == "web" || opts.emit_js) {
                if (opts.verbose) {
                    std::cout << "=== JavaScript Code Generation ===\n";
                }

                // JavaScript バックエンドオプション設定
                cm::codegen::js::JSCodeGenOptions js_opts;

                // 出力ファイル設定
                if (opts.output_file.empty()) {
                    js_opts.outputFile = "output.js";
                } else {
                    js_opts.outputFile = opts.output_file;
                }

                js_opts.generateHTML = (opts.target == "web");
                js_opts.verbose = opts.verbose || opts.debug;

                // JavaScript コード生成
                try {
                    cm::codegen::js::JSCodeGen codegen(js_opts);
                    codegen.compile(mir);

                    if (opts.verbose) {
                        std::cout << "✓ JavaScript コード生成完了: " << js_opts.outputFile << "\n";
                    }

                    // --runオプションがある場合は実行（Node.js）
                    if (opts.run_after_emit && opts.target != "web") {
                        if (opts.verbose) {
                            std::cout << "実行中: node " << js_opts.outputFile << "\n";
                        }
                        std::string cmd = "node " + js_opts.outputFile;
                        int exec_result = std::system(cmd.c_str());
                        return WEXITSTATUS(exec_result);
                    }
                } catch (const std::exception& e) {
                    std::cerr << "JavaScript コード生成エラー: " << e.what() << "\n";
                    return 1;
                }
            } else {
                // LLVM ターゲットの場合
#ifdef CM_LLVM_ENABLED
                if (opts.verbose) {
                    std::cout << "=== LLVM Code Generation ===\n";
                }

                // LLVM バックエンドオプション設定
                cm::codegen::llvm_backend::LLVMCodeGen::Options llvm_opts;

                // ターゲット設定
                if (opts.target == "wasm") {
                    llvm_opts.target = cm::codegen::llvm_backend::BuildTarget::Wasm;
                    llvm_opts.format =
                        cm::codegen::llvm_backend::LLVMCodeGen::OutputFormat::Executable;
                } else if (!opts.target.empty() && opts.target != "native") {
                    std::cerr << "エラー: 不明なターゲット '" << opts.target << "'\n";
                    std::cerr << "有効なターゲット: native, wasm, js, web\n";
                    return 1;
                } else {
                    llvm_opts.target = cm::codegen::llvm_backend::BuildTarget::Native;
                    llvm_opts.format =
                        cm::codegen::llvm_backend::LLVMCodeGen::OutputFormat::Executable;
                }

                // 出力ファイル設定
                if (opts.output_file.empty()) {
                    if (llvm_opts.target == cm::codegen::llvm_backend::BuildTarget::Wasm) {
                        llvm_opts.outputFile = "a.wasm";
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
                    cm::codegen::llvm_backend::LLVMCodeGen codegen(llvm_opts);
                    codegen.compile(mir);

                    if (opts.verbose) {
                        std::cout << "✓ LLVM コード生成完了: " << llvm_opts.outputFile << "\n";
                    }

                    // --runオプションがある場合は実行
                    if (opts.run_after_emit &&
                        llvm_opts.target == cm::codegen::llvm_backend::BuildTarget::Native) {
                        if (opts.verbose) {
                            std::cout << "実行中: " << llvm_opts.outputFile << "\n";
                        }
                        int exec_result = std::system(llvm_opts.outputFile.c_str());
                        return WEXITSTATUS(exec_result);
                    }
                } catch (const std::exception& e) {
                    std::cerr << "LLVM コード生成エラー: " << e.what() << "\n";
                    return 1;
                }
#else
                std::cerr << "エラー: LLVM バックエンドが有効になっていません。\n";
                std::cerr << "CMakeで -DCM_USE_LLVM=ON を指定してビルドしてください。\n";
                return 1;
#endif
            }
        }

    } catch (const std::exception& e) {
        std::cerr << "エラー: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
