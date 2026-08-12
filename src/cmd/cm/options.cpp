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

bool parse_sanitizer_list(const std::string& list, std::vector<std::string>& out,
                          std::string& error_message) {
    // カンマ区切りで分割し、既知の値のみ受け付ける（重複は除去する）
    static const char* kValidSanitizers[] = {"address", "thread", "memory", "bounds", "undefined"};
    size_t pos = 0;
    while (true) {
        size_t comma = list.find(',', pos);
        std::string value =
            (comma == std::string::npos) ? list.substr(pos) : list.substr(pos, comma - pos);
        bool known = false;
        for (const char* valid : kValidSanitizers) {
            if (value == valid) {
                known = true;
                break;
            }
        }
        if (!known) {
            error_message = i18n::msgf(i18n::MsgId::CliSanitizeUnknownValue, value) +
                            i18n::msg(i18n::MsgId::CliSanitizeValidValues);
            return false;
        }
        if (std::find(out.begin(), out.end(), value) == out.end()) {
            out.push_back(value);
        }
        if (comma == std::string::npos) {
            break;
        }
        pos = comma + 1;
    }
    return true;
}

namespace {

// 真偽フラグの表（名前・別名・設定先。compiler-architecture-restructure 第5段）。
// 値付き・検証付き・副作用付きのオプションは表化せずparse_options内の明示分岐で扱う
struct BoolFlag {
    const char* name;
    const char* alias;  // 無ければnullptr
    bool Options::*field;
};
constexpr BoolFlag kBoolFlags[] = {
    {"--verbose", "-v", &Options::verbose},
    {"--quiet", "-q", &Options::quiet},
    {"--ast", nullptr, &Options::show_ast},
    {"--hir", nullptr, &Options::show_hir},
    {"--mir", nullptr, &Options::show_mir},
    {"--mir-opt", nullptr, &Options::show_mir_opt},
    {"--lir-opt", nullptr, &Options::show_lir_opt},
    {"--emit-llvm", nullptr, &Options::emit_llvm},
    {"--emit-js", nullptr, &Options::emit_js},
    {"--emit-memfile", nullptr, &Options::emit_memfile},
    {"--sv-strict-lint", nullptr, &Options::sv_strict_lint},
    {"--sv-always-ff", nullptr, &Options::sv_always_ff},
    {"--sv-warn-nba", nullptr, &Options::sv_warn_nba},
    {"--emit-constraints", nullptr, &Options::emit_constraints},
    {"--funroll-loops", nullptr, &Options::unroll_loops},
    {"--test", nullptr, &Options::test_mode},
    {"--check", nullptr, &Options::fmt_check},
    {"--run", nullptr, &Options::run_after_emit},
    {"--force-check", "--strict", &Options::force_check},
    {"--recursive", "-r", &Options::recursive},
};

bool apply_bool_flag(Options& opts, const std::string& arg) {
    for (const auto& flag : kBoolFlags) {
        if (arg == flag.name || (flag.alias && arg == flag.alias)) {
            opts.*(flag.field) = true;
            return true;
        }
    }
    return false;
}

// サブコマンドの表（名前→コマンド種別。test_modeはTestコマンドの付随設定）
struct CommandEntry {
    const char* name;
    Command command;
    bool test_mode;
};
constexpr CommandEntry kCommands[] = {
    {"run", Command::Run, false},
    {"compile", Command::Compile, false},
    {"check", Command::Check, false},
    {"lint", Command::Lint, false},
    {"fmt", Command::Fmt, false},
    // #[test] 関数を実行（//! platform: でSVシミュレーション/JITを振り分け）
    {"test", Command::Test, true},
};

}  // namespace

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
    bool command_matched = false;
    for (const auto& entry : kCommands) {
        if (cmd == entry.name) {
            opts.command = entry.command;
            if (entry.test_mode) {
                opts.test_mode = true;
            }
            command_matched = true;
            break;
        }
    }
    if (command_matched) {
        // 表で解決済み
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

        if (arg == "--" && opts.command == Command::Run) {
            // -- 以降はスクリプトへ渡すコマンドライン引数（std::env::args()で取得）
            for (int j = i + 1; j < argc; ++j) {
                opts.program_args.push_back(argv[j]);
            }
            break;
        }
        if (apply_bool_flag(opts, arg)) {
            // 真偽フラグは表で解決
        } else if (arg == "-D" && i + 1 < argc) {
            opts.defines.push_back(argv[++i]);
        } else if (arg.rfind("-D", 0) == 0 && arg.size() > 2) {
            opts.defines.push_back(arg.substr(2));
        } else if (arg.rfind("--define=", 0) == 0) {
            opts.defines.push_back(arg.substr(9));
        } else if (arg.rfind("--sanitize=", 0) == 0) {
            // 値の妥当性はここで検証する（ターゲットとの組み合わせ検証はバックエンド側で行う）
            if (!parse_sanitizer_list(arg.substr(11), opts.sanitizers, opts.error_message)) {
                opts.has_error = true;
                return opts;
            }
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
        } else if (arg == "-o") {
            if (i + 1 < argc) {
                opts.output_file = argv[++i];
            } else {
                opts.has_error = true;
                opts.error_message = i18n::msg(i18n::MsgId::CliTheOOptionRequiresAn);
                return opts;
            }
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
