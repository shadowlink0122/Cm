// check/lintコマンド: 複数ファイルを走査して構文・型・Lint診断を実行する。
// ファイル単位の失敗はそのファイルのエラーとして数え、全体の走査は継続する（例外境界はファイル単位）

#include "driver.hpp"
#include "internal/base/i18n.hpp"
#include "internal/base/source_location.hpp"
#include "internal/lint/config.hpp"
#include "internal/module/resolver.hpp"
#include "internal/preprocessor/conditional.hpp"
#include "internal/syntax/ast/target_filtering_visitor.hpp"
#include "internal/syntax/lexer/lexer.hpp"
#include "internal/syntax/parser/parser.hpp"
#include "internal/types/type_checker.hpp"

#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace cm::driver {

using cli::Command;

int run_check(const cli::Options& opts) {
    if (opts.input_files.empty()) {
        std::cerr << i18n::msg(i18n::MsgId::CliNoInputFileOrDirectory);
        return 1;
    }

    // 設定ファイルを読み込み（除外パターンを収集前に反映する）
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
        std::cerr << i18n::msg(i18n::MsgId::CliNoCmFilesFoundTo);
        return 1;
    }

    if (opts.verbose) {
        std::cout << i18n::msgf(i18n::MsgId::CliFilesToCheckFileS, cm_files.size());
        for (const auto& f : cm_files) {
            std::cout << "  - " << f << "\n";
        }
        std::cout << "\n";
    }

    // 各ファイルをチェック
    int total_errors = 0;
    int total_warnings = 0;
    int files_checked = 0;

    for (const auto& file : cm_files) {
        try {
            auto file_result = read_file(file);
            if (!file_result.success) {
                std::cerr << file_result.error_message << "\n";
                total_errors++;
                continue;
            }
            std::string code = std::move(file_result.content);

            // //! platform: ディレクティブ検出
            std::string platform_directive = parse_platform_directive(code);
            bool is_baremetal_file = is_baremetal_platform(platform_directive);

            // モジュールリゾルバ初期化
            module::initialize_module_resolver();

            // Import処理
            preprocessor::ImportPreprocessor import_preprocessor(opts.debug);
            // check/lintでは非export関数の選択importへ警告を出す（H7の段階導入）
            import_preprocessor.set_warn_non_exported(true);
            auto preprocess_result = import_preprocessor.process(code, file);

            if (!preprocess_result.success) {
                std::cerr << i18n::msgf(i18n::MsgId::CliPreprocessorError, file,
                                        preprocess_result.error_message);
                total_errors++;
                continue;
            }

            code = preprocess_result.processed_source;

            // 条件付きコンパイル
            preprocessor::ConditionalPreprocessor conditional;
            for (const auto& def : opts.defines) {
                conditional.define(def);
            }
            // テストモード（--test）: TEST を自動定義
            if (opts.test_mode) {
                conditional.define("TEST");
            }
            code = conditional.process(code);

            // パース
            Lexer lexer(code);  // lint/checkではディレクティブで自動検出
            auto tokens = lexer.tokenize();
            Parser parser(std::move(tokens), lexer.is_sv());
            auto program = parser.parse();

            // コンパイルと同様に、テストモード以外では #[test] 宣言を除去する（#[test] 関数だけが参照するシンボルの誤検出を防ぐ）
            {
                Target lint_target = lexer.is_sv() ? Target::SV : Target::Native;
                ast::TargetFilteringVisitor target_filter(lint_target, opts.test_mode);
                target_filter.visit(program);
            }

            if (parser.has_errors()) {
                SourceLocationManager loc_mgr(code, file);
                for (const auto& diag : parser.diagnostics()) {
                    std::string error_type =
                        (diag.severity == DiagKind::Error ? "error" : "warning");
                    std::cerr << loc_mgr.format_error_location(diag.span,
                                                               error_type + ": " + diag.message);
                }
                total_errors += parser.diagnostics().size();
                continue;
            }

            // 型チェック
            TypeChecker checker;
            // check/lintではLint警告（W001等）を有効化する
            checker.set_enable_lint_warnings(true);
            // --strict指定時は宣言の命名規則チェック（L001）を有効化
            if (opts.force_check) {
                checker.set_enable_naming_check(true);
            }
            bool type_check_ok = checker.check(program);
            (void)type_check_ok;  // 警告抑制：将来のエラー処理で使用予定

            // 診断情報を表示
            SourceLocationManager loc_mgr(code, file);

            // インラインコメントによる無効化を解析
            config.clear_line_disables();
            config.parse_disable_comments(code);

            for (const auto& diag : checker.diagnostics()) {
                // ルールIDを抽出 (メッセージ末尾の [W001] や [L100] など)。
                // 誤爆防止のため「メッセージ末尾」かつ「英字1-3字+数字2-4桁」の形式に限定する（[0:0] や int[3] 等の型表記はルールIDではない）
                std::string rule_id;
                auto bracket_pos = diag.message.rfind('[');
                auto close_pos = diag.message.rfind(']');
                if (bracket_pos != std::string::npos && close_pos != std::string::npos &&
                    close_pos > bracket_pos && close_pos == diag.message.size() - 1) {
                    std::string candidate =
                        diag.message.substr(bracket_pos + 1, close_pos - bracket_pos - 1);
                    static const std::regex rule_pattern("^[A-Za-z]{1,3}[0-9]{2,4}$");
                    if (std::regex_match(candidate, rule_pattern)) {
                        rule_id = candidate;
                    }
                }

                // 設定で無効化されているルールはスキップ
                if (!rule_id.empty() && config.is_disabled(rule_id)) {
                    continue;
                }

                // インラインコメントで無効化されている行はスキップ
                if (!rule_id.empty()) {
                    auto line_col = loc_mgr.get_line_column(diag.span.start);
                    if (config.is_line_disabled(line_col.line, rule_id)) {
                        continue;
                    }
                }

                // 設定されたレベルに基づいて表示を決定
                std::string prefix;
                bool count_as_error = false;

                if (!rule_id.empty()) {
                    // 設定ファイルでレベルが指定されている場合
                    auto level = config.get_level(rule_id);
                    switch (level) {
                        case lint::RuleLevel::Error:
                            prefix = "error";
                            count_as_error = true;
                            break;
                        case lint::RuleLevel::Warning:
                            prefix = "warning";
                            break;
                        case lint::RuleLevel::Hint:
                            prefix = "hint";
                            break;
                        default:
                            prefix = "warning";
                            break;
                    }
                } else {
                    // ルールIDがない場合は元の診断レベルを使用
                    prefix = (diag.severity == DiagKind::Error) ? "error" : "warning";
                    count_as_error = (diag.severity == DiagKind::Error);
                }

                std::cerr << loc_mgr.format_error_location(diag.span, prefix + ": " + diag.message);
                if (count_as_error) {
                    total_errors++;
                } else {
                    total_warnings++;
                }
            }

            // ベアメタルファイルの場合、ソースコード上で禁止関数呼び出しを検出
            if (is_baremetal_file && opts.command == Command::Lint) {
                static const std::vector<std::string> forbidden = {
                    "println", "print",  "printf",         "puts",         "putchar",
                    "malloc",  "free",   "calloc",         "realloc",      "exit",
                    "fopen",   "fclose", "fread",          "fwrite",       "socket",
                    "connect", "bind",   "pthread_create", "pthread_join",
                };
                // ソースコードの各行をスキャン
                std::istringstream scan(code);
                std::string scan_line;
                int line_num = 0;
                while (std::getline(scan, scan_line)) {
                    line_num++;
                    // コメント行はスキップ
                    auto trimmed = scan_line;
                    trimmed.erase(0, trimmed.find_first_not_of(" \t"));
                    if (trimmed.find("//") == 0)
                        continue;

                    for (const auto& func : forbidden) {
                        // "func_name(" パターンを検出
                        std::string pattern = func + "(";
                        auto fpos = scan_line.find(pattern);
                        if (fpos != std::string::npos) {
                            // 直前が英数字やアンダースコアならスキップ（部分一致防止）
                            if (fpos > 0) {
                                char prev = scan_line[fpos - 1];
                                if (std::isalnum(prev) || prev == '_')
                                    continue;
                            }
                            std::cerr << i18n::msgf(i18n::MsgId::CliWarningIsNotAvailableIn, file,
                                                    line_num, func);
                            total_warnings++;
                        }
                    }
                }
            }

            files_checked++;

        } catch (const std::exception& e) {
            std::cerr << i18n::msgf(i18n::MsgId::CliException, file, e.what());
            total_errors++;
        }
    }

    // サマリー表示
    std::cout << i18n::msg(i18n::MsgId::CliCheckComplete);
    std::cout << i18n::msgf(i18n::MsgId::CliFiles, files_checked, cm_files.size());
    std::cout << i18n::msgf(i18n::MsgId::CliSWarnings, total_errors, total_warnings);

    return (total_errors > 0) ? 1 : 0;
}

}  // namespace cm::driver
