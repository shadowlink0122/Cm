// cmコマンドのエントリポイント（golang/go の cmd/go とLLVMツールのドライバ構成を参考にした薄いディスパッチャ）。
// 言語・設定の初期化とコマンド分岐のみを行い、各コマンドの実装は driver.hpp 配下のファイルが担う。
// 最上位のtryは想定外例外の最後の砦（各段階・各バックエンドの境界で先に捕捉される）

#include "driver.hpp"
#include "internal/base/debug.hpp"
#include "internal/base/i18n.hpp"
#include "internal/base/target.hpp"
#include "internal/lint/config.hpp"
#include "options.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    using namespace cm;
    using cli::Command;
    using cli::Options;
    using cli::parse_options;
    using cli::print_help;

    // メッセージ言語の初期解決: CM_LANG環境変数（CLIの--lang=が後で上書きする。
    // .cmconfig.yml の language: はCLI/環境変数が無い場合のみ後段で適用される）
    bool lang_from_env = false;
    if (const char* env_lang = std::getenv("CM_LANG")) {
        lang_from_env = i18n::set_language_from_string(env_lang);
        if (lang_from_env && std::string(env_lang) == "ja") {
            debug::set_lang(1);
        }
    }

    // オプションをパース
    Options opts = parse_options(argc, argv);

    // オプションパースでエラーがあった場合
    if (opts.has_error) {
        std::cerr << opts.error_message << "\n";
        return 1;
    }

    // .cmconfig.yml の設定を適用（優先順位: CLI > 環境変数 > config > デフォルト）
    {
        lint::ConfigLoader global_config;
        if (global_config.find_and_load(".")) {
            if (!opts.lang_from_cli && !lang_from_env && !global_config.language().empty()) {
                if (i18n::set_language_from_string(global_config.language())) {
                    if (global_config.language() == "ja") {
                        debug::set_lang(1);
                    }
                } else {
                    std::cerr << i18n::msgf(i18n::MsgId::CliInvalidLanguageInCmconfigYml,
                                            global_config.language());
                }
            }
            if (!opts.opt_level_from_cli && global_config.compile_optimization() >= 0) {
                opts.optimization_level = global_config.compile_optimization();
            }
            if (!opts.target_from_cli && opts.target.empty() &&
                !global_config.compile_target().empty() &&
                global_config.compile_target() != "native") {
                opts.target = global_config.compile_target();
            }
        }
    }

    // ターゲットのポインタ幅を設定（HIR/MIRの型サイズ計算が参照する）
    // runコマンドはJIT（ホストネイティブ）実行のため既定の8のまま
    if (opts.command != Command::Run) {
        set_target_pointer_size(opts.target);
    }

    try {
        switch (opts.command) {
            case Command::Help:
                print_help(argv[0]);
                return driver::kExitSuccess;
            case Command::Check:
            case Command::Lint:
                return driver::run_check(opts);
            case Command::Fmt:
                return driver::run_fmt(opts);
            default:
                // Run / Compile / Test（および入力ファイルの検証）は単一ファイルパイプラインへ
                return driver::run_build(opts, argv[0]);
        }
    } catch (const std::exception& e) {
        std::cerr << i18n::msgf(i18n::MsgId::CliInternalError, "main", e.what());
        return driver::kExitFailure;
    }
}
