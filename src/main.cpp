#include "common/debug_messages.hpp"
#include "frontend/lexer/lexer.hpp"
#include "frontend/parser/parser.hpp"
#include "frontend/types/type_checker.hpp"
#include "hir/hir_lowering.hpp"
#include "mir/mir_interpreter.hpp"
#include "mir/mir_lowering.hpp"
#include "mir/mir_printer.hpp"
#include "mir/optimizations/all_passes.hpp"

#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
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
    bool emit_rust = false;
    bool emit_ts = false;  // TypeScript出力
    bool emit_llvm = false;
    int optimization_level = 0;
    bool debug = false;
    std::string debug_level = "info";
    bool verbose = false;     // デフォルトは静かなモード
    std::string output_file;  // -o オプション
};

// ヘルプメッセージを表示
void print_help(const char* program_name) {
    std::cout << "Cm言語コンパイラ v" << get_version() << " (開発版)\n\n";
    std::cout << "使用方法:\n";
    std::cout << "  " << program_name << " <コマンド> [オプション] <ファイル>\n\n";
    std::cout << "コマンド:\n";
    std::cout << "  run <file>            プログラムを実行\n";
    std::cout << "  compile <file>        プログラムをコンパイル\n";
    std::cout << "  check <file>          構文と型チェックのみ実行\n";
    std::cout << "  help                  このヘルプを表示\n\n";
    std::cout << "オプション:\n";
    std::cout << "  -o <file>             出力ファイル名を指定\n";
    std::cout << "  -O<n>                 最適化レベル（0-3）\n";
    std::cout << "  --verbose, -v         詳細な出力を表示\n";
    std::cout << "  --debug, -d           デバッグ出力を有効化\n";
    std::cout << "  -d=<level>            デバッグレベル（trace/debug/info/warn/error）\n\n";
    std::cout << "コンパイル時オプション:\n";
    std::cout << "  --emit-rust           Rustコードを生成\n";
    std::cout << "  --emit-ts             TypeScriptコードを生成\n";
    std::cout << "  --emit-llvm           LLVM IRを生成（開発中）\n";
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
        // 後方互換性: 旧形式の使用（警告付き）
        std::cerr << "警告: 古い形式です。'cm run " << cmd << "' を使用してください\n";
        opts.command = Command::Run;
        opts.input_file = cmd;
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
        } else if (arg == "--emit-rust") {
            opts.emit_rust = true;
        } else if (arg == "--emit-ts") {
            opts.emit_ts = true;
        } else if (arg == "--emit-llvm") {
            opts.emit_llvm = true;
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
            return 1;
        }
        if (opts.debug)
            std::cout << "宣言数: " << program.declarations.size() << "\n\n";

        // ASTを表示
        if (opts.show_ast) {
            print_ast(program);
        }

        // ========== Type Checker ==========
        if (opts.debug)
            std::cout << "=== Type Checker ===\n";
        TypeChecker checker;
        if (!checker.check(program)) {
            std::cerr << "型エラーが発生しました\n";
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
        mir::MirLowering mir_lowering;
        auto mir = mir_lowering.lower(hir);
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

            mir::opt::OptimizationPipeline pipeline;
            pipeline.add_standard_passes(opts.optimization_level);

            if (opts.optimization_level >= 2) {
                // -O2以上では収束まで最適化を繰り返す
                pipeline.run_until_fixpoint(mir);
            } else {
                pipeline.run(mir);
            }

            if (opts.debug)
                std::cout << "最適化完了\n\n";
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
            mir::MirInterpreter interpreter;
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
            if (opts.emit_rust) {
                if (opts.verbose) {
                    std::cout << "=== Rust Code Generation ===\n";
                }
                std::cout << "注意: Rustコード生成は開発中です\n";
                // TODO: Rustコード生成を実装・統合
            } else if (opts.emit_ts) {
                if (opts.verbose) {
                    std::cout << "=== TypeScript Code Generation ===\n";
                }
                std::cout << "注意: TypeScriptコード生成は開発中です\n";
                // TODO: TypeScriptコード生成を実装・統合
            } else if (opts.emit_llvm) {
                if (opts.verbose) {
                    std::cout << "=== LLVM IR Generation ===\n";
                }
                std::cout << "注意: LLVM IR生成は開発中です\n";
                // TODO: LLVM IR生成を実装・統合
            } else {
                // デフォルトはネイティブコード（将来実装）
                if (opts.verbose) {
                    std::cout << "✓ コンパイル成功\n";
                    std::cout << "注意: ネイティブコード生成は開発中です\n";
                }
            }
        }

    } catch (const std::exception& e) {
        std::cerr << "エラー: " << e.what() << "\n";
        return 1;
    }

    return 0;
}