#include "common/debug_messages.hpp"
#include "frontend/lexer/lexer.hpp"
#include "frontend/parser/parser.hpp"
#include "frontend/types/type_checker.hpp"
#include "hir/hir_lowering.hpp"
#include "mir/mir_lowering.hpp"
#include "mir/mir_printer.hpp"
#include "mir/optimizations/all_passes.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <memory>

namespace cm {

// コマンドラインオプション
struct Options {
    std::string input_file;
    bool show_ast = false;
    bool show_hir = false;
    bool show_mir = false;
    bool show_mir_opt = false;
    bool run_interpreter = false;
    bool emit_rust = false;
    bool check_only = false;
    int optimization_level = 0;
    bool debug = false;
    std::string debug_level = "info";
    bool help = false;
};

// ヘルプメッセージを表示
void print_help(const char* program_name) {
    std::cout << "使用方法: " << program_name << " [オプション] <入力ファイル>\n\n";
    std::cout << "オプション:\n";
    std::cout << "  --help, -h            ヘルプを表示\n";
    std::cout << "  --ast                 AST（抽象構文木）を表示\n";
    std::cout << "  --hir                 HIR（高レベル中間表現）を表示\n";
    std::cout << "  --mir                 MIR（中レベル中間表現）を表示\n";
    std::cout << "  --mir-opt             最適化されたMIRを表示\n";
    std::cout << "  --run                 インタプリタで実行（開発中）\n";
    std::cout << "  --emit-rust           Rustコードを生成（開発中）\n";
    std::cout << "  --check               構文と型チェックのみ実行\n";
    std::cout << "  -O<n>                 最適化レベル（0-2）\n";
    std::cout << "  --debug, -d           デバッグ出力を有効化\n";
    std::cout << "  -d=<level>            デバッグレベル（trace/debug/info/warn/error）\n";
    std::cout << "  --lang=ja             日本語デバッグメッセージ\n";
}

// コマンドラインオプションをパース
Options parse_options(int argc, char* argv[]) {
    Options opts;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            opts.help = true;
        } else if (arg == "--ast") {
            opts.show_ast = true;
        } else if (arg == "--hir") {
            opts.show_hir = true;
        } else if (arg == "--mir") {
            opts.show_mir = true;
        } else if (arg == "--mir-opt") {
            opts.show_mir_opt = true;
        } else if (arg == "--run") {
            opts.run_interpreter = true;
        } else if (arg == "--emit-rust") {
            opts.emit_rust = true;
        } else if (arg == "--check") {
            opts.check_only = true;
        } else if (arg.substr(0, 2) == "-O") {
            if (arg.length() > 2) {
                opts.optimization_level = arg[2] - '0';
            }
        } else if (arg == "--debug" || arg == "-d") {
            opts.debug = true;
            debug::set_debug_mode(true);
        } else if (arg.substr(0, 3) == "-d=") {
            opts.debug = true;
            opts.debug_level = arg.substr(3);
            debug::set_debug_mode(true);
            debug::set_level(debug::parse_level(opts.debug_level));
        } else if (arg == "--lang=ja" || arg == "--ja") {
            debug::set_lang(1);
        } else if (arg[0] != '-') {
            opts.input_file = arg;
        } else {
            std::cerr << "不明なオプション: " << arg << "\n";
            std::cerr << "ヘルプを表示するには --help を使用してください\n";
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

} // namespace cm

int main(int argc, char* argv[]) {
    using namespace cm;

    // オプションをパース
    Options opts = parse_options(argc, argv);

    // ヘルプ表示
    if (opts.help || opts.input_file.empty()) {
        print_help(argv[0]);
        return opts.help ? 0 : 1;
    }

    // ファイルを読み込む
    std::string code = read_file(opts.input_file);

    std::cout << "コンパイル中: " << opts.input_file << "\n\n";

    try {
        // ========== Lexer ==========
        if (opts.debug) std::cout << "=== Lexer ===\n";
        Lexer lexer(code);
        auto tokens = lexer.tokenize();
        if (opts.debug) std::cout << "トークン数: " << tokens.size() << "\n\n";

        // ========== Parser ==========
        if (opts.debug) std::cout << "=== Parser ===\n";
        Parser parser(std::move(tokens));
        auto program = parser.parse();

        if (parser.has_errors()) {
            std::cerr << "構文エラーが発生しました\n";
            return 1;
        }
        if (opts.debug) std::cout << "宣言数: " << program.declarations.size() << "\n\n";

        // ASTを表示
        if (opts.show_ast) {
            print_ast(program);
        }

        // ========== Type Checker ==========
        if (opts.debug) std::cout << "=== Type Checker ===\n";
        TypeChecker checker;
        if (!checker.check(program)) {
            std::cerr << "型エラーが発生しました\n";
            return 1;
        }
        if (opts.debug) std::cout << "型チェック: OK\n\n";

        // チェックのみの場合はここで終了
        if (opts.check_only) {
            std::cout << "✓ 構文と型チェックが成功しました\n";
            return 0;
        }

        // ========== HIR Lowering ==========
        if (opts.debug) std::cout << "=== HIR Lowering ===\n";
        hir::HirLowering hir_lowering;
        auto hir = hir_lowering.lower(program);
        if (opts.debug) std::cout << "HIR宣言数: " << hir.declarations.size() << "\n\n";

        // HIRを表示
        if (opts.show_hir) {
            print_hir(hir);
        }

        // ========== MIR Lowering ==========
        if (opts.debug) std::cout << "=== MIR Lowering ===\n";
        mir::MirLowering mir_lowering;
        auto mir = mir_lowering.lower(hir);
        if (opts.debug) std::cout << "MIR関数数: " << mir.functions.size() << "\n\n";

        // MIRを表示（最適化前）
        if (opts.show_mir && !opts.show_mir_opt) {
            std::cout << "=== MIR (最適化前) ===\n";
            mir::MirPrinter printer;
            printer.print(mir);
        }

        // ========== Optimization ==========
        if (opts.optimization_level > 0 || opts.show_mir_opt) {
            if (opts.debug) std::cout << "=== Optimization (Level " << opts.optimization_level << ") ===\n";

            mir::opt::OptimizationPipeline pipeline;
            pipeline.add_standard_passes(opts.optimization_level);

            if (opts.optimization_level >= 2) {
                // -O2以上では収束まで最適化を繰り返す
                pipeline.run_until_fixpoint(mir);
            } else {
                pipeline.run(mir);
            }

            if (opts.debug) std::cout << "最適化完了\n\n";
        }

        // MIRを表示（最適化後）
        if (opts.show_mir_opt) {
            std::cout << "=== MIR (最適化後) ===\n";
            mir::MirPrinter printer;
            printer.print(mir);
        }

        // ========== Backend ==========
        if (opts.run_interpreter) {
            std::cout << "=== Interpreter ===\n";
            std::cout << "注意: インタプリタは開発中です\n";
            // TODO: インタプリタを実装・統合
        }

        if (opts.emit_rust) {
            std::cout << "=== Rust Code Generation ===\n";
            std::cout << "注意: Rustコード生成は開発中です\n";
            // TODO: Rustコード生成を実装・統合
        }

        if (!opts.show_ast && !opts.show_hir && !opts.show_mir &&
            !opts.show_mir_opt && !opts.run_interpreter && !opts.emit_rust) {
            std::cout << "✓ コンパイル成功\n";
            std::cout << "表示オプションを使用してコンパイル結果を確認できます（--help参照）\n";
        }

    } catch (const std::exception& e) {
        std::cerr << "エラー: " << e.what() << "\n";
        return 1;
    }

    return 0;
}