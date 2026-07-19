#include "options.hpp"

#include "help.hpp"
#include "internal/base/debug.hpp"
#include "internal/base/i18n.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>

namespace cm::cli {

std::string get_version() {
#ifdef CM_VERSION
    return CM_VERSION;
#else
// バージョンはVERSIONファイル由来のCM_VERSION定義が唯一のソース。
// フォールバックで古いバージョンを返すと実体と乖離するため、未定義はビルドエラーにする
#error "CM_VERSION が定義されていません（ビルド設定でVERSIONファイルから定義してください）"
#endif
}

void print_help(const char* program_name) {
    print_help_text(program_name, get_version());
}

Options parse_options(int argc, char* argv[]) {
    Options opts;

    if (argc < 2) {
        return opts;  // コマンドなし
    }

    // --lang= は位置に関係なく最初に解決する（help/cache等の早期returnでも有効にするため。
    // 不正値の報告は後段の引数ループで行う）
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind("--lang=", 0) == 0) {
            std::string lang = arg.substr(7);
            if (i18n::set_language_from_string(lang)) {
                opts.lang_from_cli = true;
                debug::set_lang(lang == "ja" ? 1 : 0);
            }
        }
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
    } else if (cmd == "help" || cmd == "--help" || cmd == "-h") {
        opts.command = Command::Help;
        return opts;
    } else if (cmd == "--version" || cmd == "-v" || cmd == "-V") {
        std::cout << get_version() << "\n";
        std::exit(0);
    } else if (cmd[0] != '-') {
        // 旧形式は使用不可 - ヘルプを表示
        std::cerr << i18n::msg(i18n::MsgId::CliInvalidCommandForm);
        std::cerr << i18n::msgf(i18n::MsgId::CliToRunAFileUse, cmd);
        opts.command = Command::Help;
        return opts;
    } else {
        opts.has_error = true;
        opts.error_message = i18n::msgf(i18n::MsgId::CliUnknownCommandRunCmHelp, cmd);
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
        } else if (arg.rfind("--sanitize=", 0) == 0) {
            // カンマ区切りで分割し、値の妥当性はここで検証する（ターゲットとの組み合わせ検証はバックエンド側で行う）
            std::string list = arg.substr(11);
            size_t pos = 0;
            while (true) {
                size_t comma = list.find(',', pos);
                std::string value =
                    (comma == std::string::npos) ? list.substr(pos) : list.substr(pos, comma - pos);
                if (value != "address" && value != "bounds") {
                    opts.has_error = true;
                    opts.error_message = i18n::msgf(i18n::MsgId::CliSanitizeUnknownValue, value) +
                                         i18n::msg(i18n::MsgId::CliSanitizeValidValues);
                    return opts;
                }
                if (std::find(opts.sanitizers.begin(), opts.sanitizers.end(), value) ==
                    opts.sanitizers.end()) {
                    opts.sanitizers.push_back(value);
                }
                if (comma == std::string::npos) {
                    break;
                }
                pos = comma + 1;
            }
        } else if (arg == "--funroll-loops") {
            opts.unroll_loops = true;
        } else if (arg.substr(0, 16) == "--funroll-loops=") {
            opts.unroll_loops = true;
            try {
                opts.unroll_max_trips = std::stoi(arg.substr(16));
                if (opts.unroll_max_trips < 1 || opts.unroll_max_trips > 1024) {
                    opts.has_error = true;
                    opts.error_message = i18n::msg(i18n::MsgId::CliFunrollLoopsCountMustBe);
                    return opts;
                }
            } catch (...) {
                opts.has_error = true;
                opts.error_message =
                    i18n::msgf(i18n::MsgId::CliInvalidFunrollLoopsValue, arg.substr(16));
                return opts;
            }
        } else if (arg.substr(0, 9) == "--target=") {
            opts.target = arg.substr(9);
            opts.target_from_cli = true;
        } else if (arg == "--run") {
            opts.run_after_emit = true;
        } else if (arg == "-o") {
            if (i + 1 < argc) {
                opts.output_file = argv[++i];
            } else {
                opts.has_error = true;
                opts.error_message = i18n::msg(i18n::MsgId::CliTheOOptionRequiresAn);
                return opts;
            }
        } else if (arg == "--force-check" || arg == "--strict") {
            opts.force_check = true;
        } else if (arg.substr(0, 2) == "-O") {
            if (arg.length() > 2) {
                opts.optimization_level = arg[2] - '0';
                opts.opt_level_from_cli = true;
                if (opts.optimization_level < 0 || opts.optimization_level > 3) {
                    opts.has_error = true;
                    opts.error_message = i18n::msg(i18n::MsgId::CliOptimizationLevelMustBeIn);
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
                        opts.error_message = i18n::msg(i18n::MsgId::CliMaximumOutputSizeMustBe);
                        return opts;
                    }
                } catch (...) {
                    opts.has_error = true;
                    opts.error_message =
                        i18n::msgf(i18n::MsgId::CliInvalidMaximumOutputSize, arg.substr(18));
                    return opts;
                }
            }
        } else if (arg.substr(0, 3) == "-d=") {
            opts.debug = true;
            opts.debug_level = arg.substr(3);
            debug::set_debug_mode(true);
            debug::set_level(debug::parse_level(opts.debug_level));
        } else if (arg.rfind("--lang=", 0) == 0) {
            // メッセージ言語（デフォルトen。ja指定時はデバッグメッセージも日本語化）
            std::string lang = arg.substr(7);
            if (!i18n::set_language_from_string(lang)) {
                opts.has_error = true;
                opts.error_message = i18n::msgf(i18n::MsgId::CliInvalidLangValueEnOr, lang);
                return opts;
            }
            opts.lang_from_cli = true;
            debug::set_lang(lang == "ja" ? 1 : 0);
        } else if (arg == "-r" || arg == "--recursive") {
            // -r オプション: 再帰的にディレクトリをチェック
            opts.recursive = true;
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
                    opts.error_message = i18n::msg(i18n::MsgId::CliMultipleInputFilesAreNot);
                    return opts;
                }
            }
        } else {
            opts.has_error = true;
            opts.error_message = i18n::msgf(i18n::MsgId::CliUnknownOptionRunCmHelp, arg);
            return opts;
        }
    }

    return opts;
}

}  // namespace cm::cli
