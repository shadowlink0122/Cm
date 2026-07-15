#include "options.hpp"

#include "../common/debug.hpp"

#include <cstdlib>
#include <iostream>

namespace cm::cli {

std::string get_version() {
#ifdef CM_VERSION
    return CM_VERSION;
#else
    return "0.15.1";
#endif
}

void print_help(const char* program_name) {
    std::cout << "Cm言語コンパイラ v" << get_version() << "\n\n";
    std::cout << "使用方法:\n";
    std::cout << "  " << program_name << " <コマンド> [オプション] <ファイル>\n\n";
    std::cout << "コマンド:\n";
    std::cout << "  run <file>            プログラムを実行（JIT、デフォルト）\n";
    std::cout << "  compile <file>        プログラムをコンパイル（LLVM）\n";
    std::cout << "  check <file>          構文と型チェックのみ実行\n";
    std::cout << "  lint <file>           静的解析を実行\n";
    std::cout << "  fmt <file>            コードフォーマット\n";
    std::cout << "  test <file>           #[test] 関数を実行（//! platform: sv は\n";
    std::cout << "                        iverilogシミュレーション、それ以外はJIT実行）\n";
    std::cout << "  help                  このヘルプを表示\n\n";
    std::cout << "オプション:\n";
    std::cout << "  -o <file>             出力ファイル名を指定\n";
    std::cout << "  -O<n>                 最適化レベル（0-3）\n";
    std::cout << "  --verbose, -v         詳細な出力を表示\n";
    std::cout << "  --quiet, -q           出力を抑制\n";
    std::cout << "  --debug, -d           デバッグ出力を有効化\n";
    std::cout << "  -d=<level>            デバッグレベル（trace/debug/info/warn/error）\n";
    std::cout << "  --max-output-size=<n> 最大出力ファイルサイズ（GB、デフォルト16GB）\n";
    std::cout << "  --force-check, --strict コンパイル時に厳格な型チェック/警告を強制実行\n";

    std::cout << "コンパイル時オプション:\n";
    std::cout << "  --target=<target>     コンパイルターゲット\n";
    std::cout << "                        native:        ネイティブ実行ファイル（デフォルト）\n";
    std::cout << "                        wasm:          WebAssembly\n";
    std::cout << "                        js:            JavaScript (Node.js向け)\n";
    std::cout << "                        web:           JavaScript + HTML (ブラウザ向け)\n";
    std::cout << "                        baremetal-arm: ベアメタル ARM Cortex-M\n";
    std::cout << "                        baremetal-x86: ベアメタル x86_64\n";
    std::cout << "                        uefi:          UEFI Application\n";
    std::cout << "                        bm:            baremetal-arm の短縮形\n";
    std::cout << "  --emit-llvm           LLVM IRを生成\n";
    std::cout << "  --emit-js             JavaScriptを生成\n";
    std::cout << "  --emit-memfile        SV: 配列リテラル初期値を.hexファイルとして書き出す\n";
    std::cout << "  --sv-strict-lint      SV: lint_off抑止を出力しない（幅警告を可視化）\n";
    std::cout << "  --sv-always-ff        SV: "
                 "always_ff/always_comb等を保持（既定はGowin互換のalways @）\n";
    std::cout << "  --sv-warn-nba         SV: "
                 "posedge関数内で代入済み状態変数の参照（前サイクル値）を警告\n";
    std::cout << "  --emit-constraints    SV: #[sv::pin]属性から.cst/.tclを生成（Gowin）\n";
    std::cout << "  -D <NAME>             条件付きコンパイル定義を追加（#ifdef用）\n";
    std::cout << "  --test                #[test] 関数を含めてコンパイル（TESTを自動定義）\n";
    std::cout << "  --check               fmt: 整形せず、要整形ファイルがあれば非0終了\n";
    std::cout << "  --run                 生成後に実行\n";
    std::cout << "  --ast                 AST（抽象構文木）を表示\n";
    std::cout << "  --hir                 HIR（高レベル中間表現）を表示\n";
    std::cout << "  --mir                 MIR（中レベル中間表現）を表示\n";
    std::cout << "  --mir-opt             最適化後のMIRを表示\n";
    std::cout << "  --lir-opt             最適化後のLLVM IRを表示（codegen直前）\n\n";
    std::cout << "最適化オプション:\n";
    std::cout << "  --funroll-loops[=N]   定数トリップカウントループをMIRレベルで静的展開\n";
    std::cout << "                        （Nは展開する最大イテレーション数、デフォルト64。\n";
    std::cout << "                         ループ判定の削減による高速化。特にJSターゲットや\n";
    std::cout << "                         低い-Oレベルで効果的）\n\n";
    std::cout << "インクリメンタルビルド:\n";
    std::cout << "  --no-cache            キャッシュを無効化（デフォルト: 有効）\n";
    std::cout << "  --cache-dir=<dir>     キャッシュディレクトリ（デフォルト: .cm-cache）\n";
    std::cout << "  cache clear           キャッシュを全削除\n";
    std::cout << "  cache stats           キャッシュ統計を表示\n\n";
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
    } else if (cmd == "lint") {
        opts.command = Command::Lint;
    } else if (cmd == "fmt") {
        opts.command = Command::Fmt;
    } else if (cmd == "test") {
        // #[test] 関数を実行（//! platform: でSVシミュレーション/JITを振り分け）
        opts.command = Command::Test;
        opts.test_mode = true;
    } else if (cmd == "cache") {
        opts.command = Command::Cache;
        // サブコマンドを取得
        if (argc > 2) {
            opts.cache_subcommand = argv[2];
        }
        return opts;
    } else if (cmd == "help" || cmd == "--help" || cmd == "-h") {
        opts.command = Command::Help;
        return opts;
    } else if (cmd == "--version" || cmd == "-v" || cmd == "-V") {
        std::cout << get_version() << "\n";
        std::exit(0);
    } else if (cmd[0] != '-') {
        // 旧形式は使用不可 - ヘルプを表示
        std::cerr << "エラー: 不正なコマンド形式です\n";
        std::cerr << "ファイル '" << cmd << "' を実行するには 'cm run " << cmd
                  << "' を使用してください\n\n";
        opts.command = Command::Help;
        return opts;
    } else {
        opts.has_error = true;
        opts.error_message = "不明なコマンド: " + cmd + "\n'cm help' でヘルプを表示";
        return opts;
    }

    // 残りの引数を処理
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--verbose" || arg == "-v") {
            opts.verbose = true;
        } else if (arg == "--quiet" || arg == "-q") {
            opts.quiet = true;
        } else if (arg == "--ast") {
            opts.show_ast = true;
        } else if (arg == "--hir") {
            opts.show_hir = true;
        } else if (arg == "--mir") {
            opts.show_mir = true;
        } else if (arg == "--mir-opt") {
            opts.show_mir_opt = true;
        } else if (arg == "--lir-opt") {
            opts.show_lir_opt = true;
        } else if (arg == "--emit-llvm") {
            opts.emit_llvm = true;
        } else if (arg == "--emit-js") {
            opts.emit_js = true;
        } else if (arg == "--emit-memfile") {
            opts.emit_memfile = true;
        } else if (arg == "--sv-strict-lint") {
            opts.sv_strict_lint = true;
        } else if (arg == "--sv-always-ff") {
            opts.sv_always_ff = true;
        } else if (arg == "--sv-warn-nba") {
            opts.sv_warn_nba = true;
        } else if (arg == "--emit-constraints") {
            opts.emit_constraints = true;
        } else if (arg == "--test") {
            opts.test_mode = true;
        } else if (arg == "--check") {
            opts.fmt_check = true;
        } else if (arg == "-D" && i + 1 < argc) {
            opts.defines.push_back(argv[++i]);
        } else if (arg.rfind("-D", 0) == 0 && arg.size() > 2) {
            opts.defines.push_back(arg.substr(2));
        } else if (arg.rfind("--define=", 0) == 0) {
            opts.defines.push_back(arg.substr(9));
        } else if (arg == "--funroll-loops") {
            opts.unroll_loops = true;
        } else if (arg.substr(0, 16) == "--funroll-loops=") {
            opts.unroll_loops = true;
            try {
                opts.unroll_max_trips = std::stoi(arg.substr(16));
                if (opts.unroll_max_trips < 1 || opts.unroll_max_trips > 1024) {
                    opts.has_error = true;
                    opts.error_message =
                        "--funroll-loops の展開回数は1-1024の範囲で指定してください";
                    return opts;
                }
            } catch (...) {
                opts.has_error = true;
                opts.error_message = "無効な--funroll-loopsの値: " + arg.substr(16);
                return opts;
            }
        } else if (arg.substr(0, 9) == "--target=") {
            opts.target = arg.substr(9);
        } else if (arg == "--run") {
            opts.run_after_emit = true;
        } else if (arg == "-o") {
            if (i + 1 < argc) {
                opts.output_file = argv[++i];
            } else {
                opts.has_error = true;
                opts.error_message = "-o オプションには出力ファイル名が必要です";
                return opts;
            }
        } else if (arg == "--force-check" || arg == "--strict") {
            opts.force_check = true;
        } else if (arg.substr(0, 2) == "-O") {
            if (arg.length() > 2) {
                opts.optimization_level = arg[2] - '0';
                if (opts.optimization_level < 0 || opts.optimization_level > 3) {
                    opts.has_error = true;
                    opts.error_message = "最適化レベルは0-3の範囲で指定してください";
                    return opts;
                }
            }
        } else if (arg == "--debug" || arg == "-d") {
            opts.debug = true;
            debug::set_debug_mode(true);
        } else if (arg.substr(0, 17) == "--max-output-size") {
            if (arg.length() > 18 && arg[17] == '=') {
                try {
                    opts.max_output_size = std::stoul(arg.substr(18));
                    if (opts.max_output_size < 1 || opts.max_output_size > 1024) {
                        opts.has_error = true;
                        opts.error_message = "最大出力サイズは1-1024GBの範囲で指定してください";
                        return opts;
                    }
                } catch (...) {
                    opts.has_error = true;
                    opts.error_message = "無効な最大出力サイズ: " + arg.substr(18);
                    return opts;
                }
            }
        } else if (arg.substr(0, 3) == "-d=") {
            opts.debug = true;
            opts.debug_level = arg.substr(3);
            debug::set_debug_mode(true);
            debug::set_level(debug::parse_level(opts.debug_level));
        } else if (arg == "--lang=ja") {
            debug::set_lang(1);
        } else if (arg == "-r" || arg == "--recursive") {
            // -r オプション: 再帰的にディレクトリをチェック
            opts.recursive = true;
        } else if (arg == "--incremental") {
            opts.incremental = true;
        } else if (arg == "--no-cache") {
            opts.incremental = false;
        } else if (arg.substr(0, 12) == "--cache-dir=") {
            opts.cache_dir = arg.substr(12);
            opts.incremental = true;  // --cache-dir指定時は暗黙的に有効化
        } else if (arg.substr(0, 10) == "--exclude=") {
            // --exclude=PATTERN: 除外パターン
            opts.exclude_patterns.push_back(arg.substr(10));
        } else if (arg[0] != '-') {
            // check/lint/fmtコマンドでは複数ファイルを許可
            if (opts.command == Command::Check || opts.command == Command::Lint ||
                opts.command == Command::Fmt) {
                opts.input_files.push_back(arg);
            } else {
                // run/compileは単一ファイルのみ
                if (opts.input_file.empty()) {
                    opts.input_file = arg;
                } else {
                    opts.has_error = true;
                    opts.error_message = "複数の入力ファイルは指定できません";
                    return opts;
                }
            }
        } else {
            opts.has_error = true;
            opts.error_message = "不明なオプション: " + arg + "\n'cm help' でヘルプを表示";
            return opts;
        }
    }

    return opts;
}

}  // namespace cm::cli
