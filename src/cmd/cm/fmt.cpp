// fmtコマンド: 複数ファイルの整形（--checkでは書き込まず要整形を報告する）。
// ファイル単位の失敗は失敗数に数え、走査は継続する（例外境界はファイル単位）

#include "driver.hpp"
#include "internal/base/i18n.hpp"
#include "internal/fmt/formatter.hpp"
#include "internal/lint/config.hpp"

#include <fstream>
#include <iostream>
#include <string>
#include <utility>

namespace cm::driver {

int run_fmt(const cli::Options& opts) {
    if (opts.input_files.empty()) {
        std::cerr << i18n::msg(i18n::MsgId::CliNoInputFileOrDirectory);
        return 1;
    }

    // 設定ファイルを読み込み（check/lintと同じ除外をfmtのディレクトリ走査にも適用する。
    // フォーマッタ期待値や意図的な部分コードのフィクスチャを-rで書き換えないため）
    lint::ConfigLoader config;
    if (config.find_and_load(".")) {
        if (opts.verbose) {
            std::cout << i18n::msgf(i18n::MsgId::CliConfigFile, config.config_path());
        }
    }

    // ファイルを収集（設定のexcludeはディレクトリ走査にのみ適用）
    auto cm_files = collect_cm_files(opts.input_files, opts.recursive, opts.exclude_patterns,
                                     config.excludes());

    if (cm_files.empty()) {
        std::cerr << i18n::msg(i18n::MsgId::CliNoCmFilesFoundTo2);
        return 1;
    }

    if (opts.verbose) {
        std::cout << i18n::msgf(i18n::MsgId::CliFormattingFileS, cm_files.size());
    }

    // 各ファイルをフォーマット
    size_t total_changes = 0;
    size_t files_modified = 0;
    size_t files_failed = 0;

    fmt::Formatter formatter;

    for (const auto& file : cm_files) {
        try {
            auto file_result = read_file(file);
            if (!file_result.success) {
                std::cerr << file_result.error_message << "\n";
                files_failed++;
                continue;
            }
            std::string code = std::move(file_result.content);

            // フォーマット実行
            auto result = formatter.format(code);

            if (result.modified) {
                if (opts.fmt_check) {
                    // --check: 書き込まず要整形ファイルとして報告
                    std::cout << i18n::msgf(i18n::MsgId::CliNeedsFormattingPlaceS, file,
                                            result.changes_applied);
                    files_modified++;
                    total_changes += result.changes_applied;
                } else {
                    // 変更があればファイルを上書き
                    std::ofstream ofs(file);
                    if (ofs) {
                        ofs << result.formatted_code;
                        files_modified++;
                        total_changes += result.changes_applied;

                        if (opts.verbose) {
                            std::cout << i18n::msgf(i18n::MsgId::CliPlaceSFormatted, file,
                                                    result.changes_applied);
                        }
                    } else {
                        std::cerr << i18n::msgf(i18n::MsgId::CliCannotWriteFile, file);
                        files_failed++;
                    }
                }
            }

        } catch (const std::exception& e) {
            std::cerr << i18n::msgf(i18n::MsgId::CliMsg, file, e.what());
            files_failed++;
        }
    }

    // サマリー表示（quietモードでは抑制）
    if (!opts.quiet) {
        if (opts.fmt_check) {
            std::cout << i18n::msg(i18n::MsgId::CliFormatCheckComplete);
            std::cout << i18n::msgf(i18n::MsgId::CliNeedsFormattingFileS, files_modified,
                                    cm_files.size());
        } else {
            std::cout << i18n::msg(i18n::MsgId::CliFormatComplete);
            std::cout << i18n::msgf(i18n::MsgId::CliFilesFixed, files_modified, cm_files.size());
            std::cout << i18n::msgf(i18n::MsgId::CliFormattedPlaceS, total_changes);
        }
        if (files_failed > 0) {
            std::cout << i18n::msgf(i18n::MsgId::CliFailedFileS, files_failed);
        }
    }

    // --check では要整形ファイルの存在も失敗として扱う（CIゲート用）
    if (opts.fmt_check && files_modified > 0) {
        return 1;
    }
    return files_failed > 0 ? 1 : 0;
}

}  // namespace cm::driver
