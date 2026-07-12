// LLVM version check (must be first)
#ifdef CM_LLVM_ENABLED
#include <llvm/Config/llvm-config.h>
#endif

// LLVM codegen (if enabled)
#ifdef CM_LLVM_ENABLED
#include "codegen/llvm/jit/jit_engine.hpp"
#include "codegen/llvm/monitoring/compilation_guard.hpp"
#include "codegen/llvm/native/codegen.hpp"
#endif

// JavaScript codegen
#include "codegen/js/codegen.hpp"

// MIR validation
#include "cli/options.hpp"
#include "codegen/sv/hierarchy.hpp"
#include "common/cache_manager.hpp"
#include "common/debug_messages.hpp"
#include "common/source_location.hpp"
#include "fmt/formatter.hpp"
#include "frontend/ast/target_filtering_visitor.hpp"
#include "frontend/lexer/lexer.hpp"
#include "frontend/parser/parser.hpp"
#include "frontend/types/type_checker.hpp"
#include "hir/lowering/lowering.hpp"
#include "lint/config.hpp"
#include "lint/lint_runner.hpp"
#include "mir/lowering/lowering.hpp"
#include "mir/passes/cleanup/dce.hpp"
#include "mir/passes/cleanup/program_dce.hpp"
#include "mir/passes/core/manager.hpp"
#include "mir/passes/loop/const_unroll.hpp"
#include "mir/passes/validation/no_std_checker.hpp"
#include "mir/printer.hpp"
#include "module/resolver.hpp"
#include "preprocessor/conditional.hpp"
#include "preprocessor/import.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <string>

// SVバックエンド（常に利用可能）
#include "codegen/sv/codegen.hpp"
#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#include <vector>

namespace fs = std::filesystem;

namespace cm {

// CLIオプション処理（Command/Options/parse_options/print_help/get_version）は
// src/cli/options.{hpp,cpp} へ分離した（013 §4.3-4 巨大TU分割）
using cli::Command;
using cli::get_version;
using cli::Options;
using cli::parse_options;
using cli::print_help;

// ファイル読み込み結果
struct ReadFileResult {
    std::string content;
    bool success = false;
    std::string error_message;
};

// ファイルを読み込む
ReadFileResult read_file(const std::string& filename) {
    ReadFileResult result;
    std::ifstream file(filename);
    if (!file.is_open()) {
        result.success = false;
        result.error_message = "エラー: ファイルを開けません: " + filename;
        return result;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    result.content = buffer.str();
    result.success = true;
    return result;
}

// ソースコード先頭から //! platform: ディレクティブを解析
// 戻り値: ディレクティブ文字列（例: "js", "js|web", "!native"）、なければ空文字列
std::string parse_platform_directive(const std::string& code_content) {
    std::istringstream iss(code_content);
    std::string line;
    int line_count = 0;
    while (std::getline(iss, line) && line_count < 5) {
        line_count++;
        auto pos = line.find("//! platform:");
        if (pos == std::string::npos) {
            pos = line.find("//!platform:");
        }
        if (pos != std::string::npos) {
            std::string directive = line.substr(line.find("platform:") + 9);
            directive.erase(0, directive.find_first_not_of(" \t"));
            directive.erase(directive.find_last_not_of(" \t\r\n") + 1);
            return directive;
        }
    }
    return "";
}

// platformディレクティブと現在のターゲットを比較
// or形式: "js|web" → jsまたはwebならtrue
// not形式: "!native" → native以外ならtrue
bool match_platform_directive(const std::string& directive, const std::string& current_target) {
    if (directive.empty()) {
        return true;
    }

    if (directive[0] == '!') {
        // NOT形式: !native|jit
        std::string negated = directive.substr(1);
        std::istringstream ss(negated);
        std::string token;
        while (std::getline(ss, token, '|')) {
            if (token == current_target) {
                return false;
            }
        }
        return true;
    }

    // OR形式: js|web
    std::istringstream ss(directive);
    std::string token;
    while (std::getline(ss, token, '|')) {
        if (token == current_target) {
            return true;
        }
    }
    return false;
}

// プラットフォームディレクティブからターゲット種別を判定
// baremetal/uefi系ならtrue
bool is_baremetal_platform(const std::string& directive) {
    if (directive.empty())
        return false;
    // directiveに "baremetal" や "uefi" が含まれるか
    return directive.find("baremetal") != std::string::npos ||
           directive.find("uefi") != std::string::npos;
}

// cm test (SVフロー): 生成済みSV+テストベンチを iverilog + vvp でシミュレーション実行する。
// $readmemh の相対パスを解決するため、生成物ディレクトリをCWDにして実行する。
// 戻り値: 終了コード（テストベンチの $fatal で非0 = テスト失敗）
int run_sv_test_simulation(const std::string& sv_path, bool quiet) {
#if defined(_WIN32)
    (void)sv_path;
    (void)quiet;
    std::cerr << "エラー: SVテストのシミュレーション実行はWindowsでは未対応です\n";
    return 1;
#else
    // 子プロセス（iverilog/vvp）の出力と順序が入れ替わらないようフラッシュする
    std::cout.flush();
    namespace fs = std::filesystem;
    fs::path sv(sv_path);
    fs::path dir = sv.parent_path();
    if (dir.empty()) {
        dir = ".";
    }
    std::string stem = sv.stem().string();
    fs::path tb = dir / (stem + "_tb.sv");
    fs::path sim = dir / (stem + "_sim");

    if (std::system("command -v iverilog >/dev/null 2>&1") != 0 ||
        std::system("command -v vvp >/dev/null 2>&1") != 0) {
        std::cerr << "エラー: iverilog / vvp が見つかりません（SVテストの実行に必要）\n";
        std::cerr << "ヒント: macOS: brew install icarus-verilog / "
                     "Ubuntu: sudo apt-get install iverilog\n";
        return 1;
    }
    if (!fs::exists(tb)) {
        std::cerr << "エラー: テストベンチが生成されていません: " << tb.string() << "\n";
        return 1;
    }
    std::string compile_cmd =
        "iverilog -g2012 -o '" + sim.string() + "' '" + sv.string() + "' '" + tb.string() + "'";
    if (std::system(compile_cmd.c_str()) != 0) {
        std::cerr << "エラー: iverilog コンパイルに失敗しました\n";
        return 1;
    }
    std::string run_cmd = "cd '" + dir.string() + "' && vvp '" + fs::absolute(sim).string() + "'";
    int rc = std::system(run_cmd.c_str());
    int exit_code = WIFEXITED(rc) ? WEXITSTATUS(rc) : 1;
    if (exit_code == 0 && !quiet) {
        std::cout << "✓ SVテスト成功\n";
    }
    return exit_code;
#endif
}

// 除外パターンにマッチするか判定
bool matches_exclude_pattern(const std::string& filepath,
                             const std::vector<std::string>& patterns) {
    for (const auto& pattern : patterns) {
        // シンプルなワイルドカードマッチ（*.test.cm対応）
        if (pattern.find('*') != std::string::npos) {
            // *.xxx形式のサフィックスマッチ
            if (pattern[0] == '*') {
                std::string suffix = pattern.substr(1);
                if (filepath.size() >= suffix.size() &&
                    filepath.compare(filepath.size() - suffix.size(), suffix.size(), suffix) == 0) {
                    return true;
                }
            }
        } else {
            // 完全一致または部分一致
            if (filepath.find(pattern) != std::string::npos) {
                return true;
            }
        }
    }
    return false;
}

// .cmファイルを収集（再帰オプション対応）
std::vector<std::string> collect_cm_files(const std::vector<std::string>& paths, bool recursive,
                                          const std::vector<std::string>& excludes) {
    std::vector<std::string> result;

    for (const auto& path : paths) {
        fs::path p(path);

        if (!fs::exists(p)) {
            std::cerr << "エラー: パスが存在しません: " << path << "\n";
            continue;
        }

        if (fs::is_regular_file(p)) {
            // ファイルの場合、.cm拡張子チェック
            if (p.extension() == ".cm") {
                std::string filepath = p.string();
                if (!matches_exclude_pattern(filepath, excludes)) {
                    result.push_back(filepath);
                }
            }
        } else if (fs::is_directory(p)) {
            // ディレクトリの場合
            if (recursive) {
                // 再帰的に走査
                for (const auto& entry : fs::recursive_directory_iterator(p)) {
                    if (entry.is_regular_file() && entry.path().extension() == ".cm") {
                        std::string filepath = entry.path().string();
                        if (!matches_exclude_pattern(filepath, excludes)) {
                            result.push_back(filepath);
                        }
                    }
                }
            } else {
                // 非再帰: ディレクトリ直下のみ
                for (const auto& entry : fs::directory_iterator(p)) {
                    if (entry.is_regular_file() && entry.path().extension() == ".cm") {
                        std::string filepath = entry.path().string();
                        if (!matches_exclude_pattern(filepath, excludes)) {
                            result.push_back(filepath);
                        }
                    }
                }
            }
        }
    }

    // ソートして一貫性を保つ
    std::sort(result.begin(), result.end());
    return result;
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

    // オプションパースでエラーがあった場合
    if (opts.has_error) {
        std::cerr << opts.error_message << "\n";
        return 1;
    }

    // ターゲットのポインタ幅を設定（HIR/MIRの型サイズ計算が参照する）
    // runコマンドはJIT（ホストネイティブ）実行のため既定の8のまま
    if (opts.command != Command::Run) {
        set_target_pointer_size(opts.target);
    }

    // コンパイラバイナリのパスを設定（インクリメンタルビルド用）
    cache::CacheManager::set_compiler_path(argv[0]);

    // コマンドの処理
    if (opts.command == Command::Help) {
        print_help(argv[0]);
        return 0;
    }

    // Check/Lintコマンドの複数ファイル処理
    if (opts.command == Command::Check || opts.command == Command::Lint) {
        if (opts.input_files.empty()) {
            std::cerr << "エラー: 入力ファイルまたはディレクトリが指定されていません\n";
            return 1;
        }

        // ファイルを収集
        auto cm_files = collect_cm_files(opts.input_files, opts.recursive, opts.exclude_patterns);

        if (cm_files.empty()) {
            std::cerr << "エラー: チェック対象の.cmファイルが見つかりません\n";
            return 1;
        }

        if (opts.verbose) {
            std::cout << "チェック対象: " << cm_files.size() << " ファイル\n";
            for (const auto& f : cm_files) {
                std::cout << "  - " << f << "\n";
            }
            std::cout << "\n";
        }

        // 設定ファイルを読み込み
        lint::ConfigLoader config;
        if (config.find_and_load(".")) {
            if (opts.verbose) {
                std::cout << "設定ファイル: " << config.config_path() << "\n\n";
            }
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
                auto preprocess_result = import_preprocessor.process(code, file);

                if (!preprocess_result.success) {
                    std::cerr << file
                              << ": プリプロセッサエラー: " << preprocess_result.error_message
                              << "\n";
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

                if (parser.has_errors()) {
                    SourceLocationManager loc_mgr(code, file);
                    for (const auto& diag : parser.diagnostics()) {
                        std::string error_type =
                            (diag.severity == DiagKind::Error ? "error" : "warning");
                        std::cerr << loc_mgr.format_error_location(
                            diag.span, error_type + ": " + diag.message);
                    }
                    total_errors += parser.diagnostics().size();
                    continue;
                }

                // 型チェック
                TypeChecker checker;
                bool type_check_ok = checker.check(program);
                (void)type_check_ok;  // 警告抑制：将来のエラー処理で使用予定

                // 診断情報を表示
                SourceLocationManager loc_mgr(code, file);

                // インラインコメントによる無効化を解析
                config.clear_line_disables();
                config.parse_disable_comments(code);

                for (const auto& diag : checker.diagnostics()) {
                    // ルールIDを抽出 (メッセージ末尾の [W001] や [L100] など)。
                    // 誤爆防止のため「メッセージ末尾」かつ「英字1-3字+数字2-4桁」の
                    // 形式に限定する（[0:0] や int[3] 等の型表記はルールIDではない）
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

                    std::cerr << loc_mgr.format_error_location(diag.span,
                                                               prefix + ": " + diag.message);
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
                                std::cerr << file << ":" << line_num
                                          << ": warning: ベアメタル環境では '" << func
                                          << "' は使用できません [B001]\n";
                                total_warnings++;
                            }
                        }
                    }
                }

                files_checked++;

            } catch (const std::exception& e) {
                std::cerr << file << ": 例外: " << e.what() << "\n";
                total_errors++;
            }
        }

        // サマリー表示
        std::cout << "\n=== チェック完了 ===\n";
        std::cout << "ファイル数: " << files_checked << "/" << cm_files.size() << "\n";
        std::cout << "エラー: " << total_errors << ", 警告: " << total_warnings << "\n";

        return (total_errors > 0) ? 1 : 0;
    }

    // Fmtコマンドの複数ファイル処理
    if (opts.command == Command::Fmt) {
        if (opts.input_files.empty()) {
            std::cerr << "エラー: 入力ファイルまたはディレクトリが指定されていません\n";
            return 1;
        }

        // ファイルを収集
        auto cm_files = collect_cm_files(opts.input_files, opts.recursive, opts.exclude_patterns);

        if (cm_files.empty()) {
            std::cerr << "エラー: フォーマット対象の.cmファイルが見つかりません\n";
            return 1;
        }

        if (opts.verbose) {
            std::cout << "フォーマット対象: " << cm_files.size() << " ファイル\n";
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
                        std::cout << file << ": 要整形（" << result.changes_applied << " 箇所）\n";
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
                                std::cout << file << ": " << result.changes_applied
                                          << " 箇所の整形\n";
                            }
                        } else {
                            std::cerr << "エラー: ファイルに書き込めません: " << file << "\n";
                            files_failed++;
                        }
                    }
                }

            } catch (const std::exception& e) {
                std::cerr << "エラー: " << file << ": " << e.what() << "\n";
                files_failed++;
            }
        }

        // サマリー表示（quietモードでは抑制）
        if (!opts.quiet) {
            if (opts.fmt_check) {
                std::cout << "\n=== フォーマットチェック完了 ===\n";
                std::cout << "要整形: " << files_modified << "/" << cm_files.size()
                          << " ファイル\n";
            } else {
                std::cout << "\n=== フォーマット完了 ===\n";
                std::cout << "ファイル数: " << files_modified << "/" << cm_files.size()
                          << " 修正\n";
                std::cout << "整形箇所: " << total_changes << " 箇所\n";
            }
            if (files_failed > 0) {
                std::cout << "失敗: " << files_failed << " ファイル\n";
            }
        }

        // --check では要整形ファイルの存在も失敗として扱う（CIゲート用）
        if (opts.fmt_check && files_modified > 0) {
            return 1;
        }
        return files_failed > 0 ? 1 : 0;
    }

    // ========== cache コマンド ==========
    if (opts.command == Command::Cache) {
        cache::CacheConfig config;
        config.cache_dir = opts.cache_dir;
        cache::CacheManager cache_mgr(config);

        if (opts.cache_subcommand == "clear") {
            if (cache_mgr.clear()) {
                std::cout << "✓ キャッシュを削除しました: " << opts.cache_dir << "\n";
            } else {
                std::cout << "キャッシュが存在しません: " << opts.cache_dir << "\n";
            }
            return 0;
        } else if (opts.cache_subcommand == "stats") {
            auto stats = cache_mgr.get_stats();
            auto entries = cache_mgr.get_all_entries();
            std::cout << "=== キャッシュ統計 ===\n";
            std::cout << "ディレクトリ: " << opts.cache_dir << "\n";
            std::cout << "エントリ数: " << stats.total_entries << "\n";
            std::cout << "合計サイズ: " << (stats.total_size_bytes / 1024) << " KB\n";
            if (!entries.empty()) {
                // 最古・最新のエントリを表示
                std::string oldest = entries.begin()->second.created_at;
                std::string newest = entries.begin()->second.created_at;
                for (const auto& [_, entry] : entries) {
                    if (!entry.created_at.empty()) {
                        if (entry.created_at < oldest || oldest.empty())
                            oldest = entry.created_at;
                        if (entry.created_at > newest)
                            newest = entry.created_at;
                    }
                }
                if (!oldest.empty()) {
                    std::cout << "最古:     " << oldest << "\n";
                    std::cout << "最新:     " << newest << "\n";
                }
            }
            return 0;
        } else {
            std::cerr << "不明なcacheサブコマンド: " << opts.cache_subcommand << "\n";
            std::cerr << "使用法: cm cache clear | cm cache stats\n";
            return 1;
        }
    }

    // run/compileコマンドは単一ファイル
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
                std::cerr << "エラー: platform '" << directive
                          << "' のテスト実行は未対応です（sv または native/JIT のみ）\n";
                std::cerr << "  ファイル: " << opts.input_file << "\n";
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
                std::cerr << "警告: このファイルはプラットフォーム '" << directive
                          << "' 向けです（現在: " << current << "）\n";
                std::cerr << "  ファイル: " << opts.input_file << "\n";
                std::cerr << "ヒント: --target=" << directive
                          << " オプションで対象プラットフォームを指定してください\n\n";
                return 1;
            }
        }
    }

    // ========== SVモジュール階層の保持（//! sv: hierarchy）==========
    // 相対importをextern struct宣言に置換し、import先を後段で個別コンパイルする
    std::vector<std::string> sv_hierarchy_submodules;
    {
        bool is_sv_early =
            (opts.target == "sv" || opts.target == "verilog" || opts.target == "systemverilog");
        if (is_sv_early) {
            auto hres = codegen::sv::process_sv_hierarchy(code, opts.input_file);
            if (!hres.error.empty()) {
                std::cerr << "sv階層化エラー: " << hres.error << "\n";
                return 1;
            }
            if (hres.enabled) {
                code = hres.transformed_source;
                sv_hierarchy_submodules = hres.submodule_files;
                if (opts.verbose && !sv_hierarchy_submodules.empty()) {
                    std::cout << "sv階層化: " << sv_hierarchy_submodules.size()
                              << " 個のサブモジュールを検出\n";
                }
            }
        }
    }

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

        // ========== 高速キャッシュ判定（ImportPreprocessor の前に実行）==========
        // ファイルのタイムスタンプ+サイズで変更がないか高速チェック
        // ヒットすれば ImportPreprocessor (1.6秒) + SHA-256 (0.4秒) をスキップ
        std::string prev_build_fingerprint;
        if (opts.incremental && opts.command == Command::Compile) {
            std::string target_key = opts.target.empty() ? "native" : opts.target;
            cache::CacheConfig qc_config;
            qc_config.cache_dir = opts.cache_dir;
            cache::CacheManager qc_mgr(qc_config);
            auto qc_result =
                qc_mgr.quick_check(opts.input_file, target_key, opts.optimization_level);
            prev_build_fingerprint = qc_result.fingerprint;
            if (qc_result.valid) {
                // 高速キャッシュヒット: ファイルコピーのみ
                auto cached_obj = qc_mgr.cache_dir() / "objects" / qc_result.object_file;
                std::string output = opts.output_file;
                if (output.empty()) {
                    if (opts.target == "js" || opts.target == "web" || opts.emit_js) {
                        output = "output.js";
                    } else if (opts.target == "wasm") {
                        output = "a.wasm";
                    } else {
                        output = "a.out";
                    }
                }
                try {
                    std::filesystem::copy_file(cached_obj, output,
                                               std::filesystem::copy_options::overwrite_existing);
                    if (opts.verbose || !opts.quiet) {
                        std::cout << "✓ キャッシュヒット: " << output << "\n";
                    }
                    return 0;
                } catch (const std::exception& e) {
                    // 高速パス失敗 → 通常パスにフォールバック
                    if (opts.verbose) {
                        std::cout << "高速キャッシュ復元失敗: " << e.what() << " → 通常パス\n";
                    }
                }
            }
        }

        // ========== Import Preprocessor ==========
        if (opts.debug)
            std::cout << "=== Import Preprocessor ===\n";
        auto phase_preprocess_start = std::chrono::steady_clock::now();
        preprocessor::ImportPreprocessor import_preprocessor(opts.debug);
        auto preprocess_result = import_preprocessor.process(code, opts.input_file);
        auto phase_preprocess_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::steady_clock::now() - phase_preprocess_start)
                                       .count();

        if (!preprocess_result.success) {
            std::cerr << "プリプロセッサエラー: " << preprocess_result.error_message << "\n";
            return 1;
        }

        // デバッグ出力
        {
            std::ofstream out(".tmp/preprocessed.cm");
            if (out) {
                out << preprocess_result.processed_source;
            }
        }

        // ========== インクリメンタルビルド: キャッシュチェック ==========
        std::string cache_fingerprint;             // 後でキャッシュ保存に使用
        std::vector<std::string> changed_modules;  // 変更されたモジュール一覧
        cache::CacheConfig cache_config;
        cache_config.cache_dir = opts.cache_dir;
        cache_config.enabled = opts.incremental;

        // コンパイル時間計測開始
        auto compile_start = std::chrono::steady_clock::now();

        if (opts.incremental) {
            // Run/Compile両方でフィンガープリントを計算
            std::string target_key;
            if (opts.command == Command::Run) {
                target_key = "jit";
            } else {
                target_key = opts.target.empty() ? "native" : opts.target;
            }

            cache::CacheManager cache_mgr(cache_config);
            cache_fingerprint = cache_mgr.compute_fingerprint(preprocess_result.resolved_files,
                                                              target_key, opts.optimization_level);

            if (!cache_fingerprint.empty() && opts.command == Command::Compile) {
                auto cached = cache_mgr.lookup(cache_fingerprint);
                if (cached) {
                    // キャッシュヒット: オブジェクトファイルをコピーしてコンパイルをスキップ
                    auto cached_obj = cache_mgr.cache_dir() / "objects" / cached->object_file;
                    std::string output = opts.output_file;
                    if (output.empty()) {
                        // ターゲットに応じたデフォルト出力ファイル名
                        if (opts.target == "js" || opts.target == "web" || opts.emit_js) {
                            output = "output.js";
                        } else if (opts.target == "wasm") {
                            output = "a.wasm";
                        } else {
                            output = "a.out";
                        }
                    }

                    try {
                        std::filesystem::copy_file(
                            cached_obj, output, std::filesystem::copy_options::overwrite_existing);
                        if (opts.verbose || !opts.quiet) {
                            auto hit_end = std::chrono::steady_clock::now();
                            auto hit_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                              hit_end - compile_start)
                                              .count();
                            std::cout << "✓ キャッシュヒット: " << output << " (" << hit_ms
                                      << "ms)\n";
                        }
                        // 高速キャッシュ判定用の情報を更新（次回は高速パスが使えるように）
                        cache_mgr.save_quick_check(
                            opts.input_file, target_key, opts.optimization_level, cache_fingerprint,
                            cached->object_file, preprocess_result.resolved_files);
                        return 0;
                    } catch (const std::exception& e) {
                        if (opts.verbose) {
                            std::cout << "キャッシュ復元失敗: " << e.what() << " → 再コンパイル\n";
                        }
                    }
                } else {
                    // キャッシュミス: 変更モジュールを検出
                    // モジュール情報の構築
                    std::map<std::string, std::vector<std::string>> module_files;
                    for (const auto& mr : preprocess_result.module_ranges) {
                        auto abs_path = std::filesystem::absolute(mr.file_path).string();
                        auto mod_name =
                            cm::mir::MirSplitter::source_file_to_module_name(mr.file_path);
                        module_files[mod_name].push_back(abs_path);
                    }

                    // キャッシュミス: 変更モジュールを検出
                    auto prev = prev_build_fingerprint.empty()
                                    ? std::nullopt
                                    : cache_mgr.lookup(prev_build_fingerprint);

                    if (prev && !prev->module_fingerprints.empty()) {
                        // 前回のモジュールフィンガープリントと比較
                        auto current_fps = cache_mgr.compute_module_fingerprints(module_files);
                        changed_modules = cache_mgr.detect_changed_modules(
                            prev->module_fingerprints, current_fps);
                    } else {
                        // 初回ビルドまたは前回情報なし: 全モジュールを変更扱い
                        for (const auto& [name, _] : module_files) {
                            changed_modules.push_back(name);
                        }
                    }
                    if (opts.verbose) {
                        std::cout << "キャッシュミス: フルコンパイルを実行\n";
                        // 変更ファイルを表示
                        auto changed = cache_mgr.detect_changed_files(
                            preprocess_result.resolved_files, target_key, opts.optimization_level);
                        if (!changed.empty()) {
                            std::cout << "変更検出 (" << changed.size() << "ファイル):\n";
                            for (const auto& f : changed) {
                                auto name = std::filesystem::path(f).filename().string();
                                std::cout << "  → " << name << "\n";
                            }
                        }
                    }
                }
            }
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

        // ========== Conditional Preprocessor ==========
        if (opts.debug)
            std::cout << "=== Conditional Preprocessor ===\n";
        preprocessor::ConditionalPreprocessor conditional;
        // -D オプションのユーザ定義を追加
        for (const auto& def : opts.defines) {
            conditional.define(def);
        }
        // テストモード（cm test / --test）: TEST を自動定義
        // （#[test] と連動するテスト補助コードを #ifdef TEST で書けるようにする）
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
            std::cout << "定義済みシンボル: ";
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

        // ========== Lexer ==========
        if (opts.debug)
            std::cout << "=== Lexer ===\n";
        auto phase_parse_start = std::chrono::steady_clock::now();
        // ターゲットに応じたレキサープラットフォーム設定
        LexerPlatform lexer_platform = LexerPlatform::Default;
        if (opts.target == "sv" || opts.target == "verilog" || opts.target == "systemverilog") {
            lexer_platform = LexerPlatform::SV;
        }
        Lexer lexer(code, lexer_platform);
        auto tokens = lexer.tokenize();

        if (opts.debug)
            std::cout << "トークン数: " << tokens.size() << "\n\n";

        // ========== Parser ==========
        if (opts.debug)
            std::cout << "=== Parser ===\n";
        Parser parser(std::move(tokens), lexer.is_sv());
        auto program = parser.parse();

        if (parser.has_errors()) {
            std::cerr << "構文エラーが発生しました\n";
            // ソース位置管理を作成
            SourceLocationManager loc_mgr(code, opts.input_file);

            // 診断情報を表示
            for (const auto& diag : parser.diagnostics()) {
                // エラーメッセージをフォーマットして表示
                std::string error_type = (diag.severity == DiagKind::Error ? "エラー" : "警告");
                std::cerr << loc_mgr.format_error_location(diag.span,
                                                           error_type + ": " + diag.message);
            }
            return 1;  // エラー時は1で終了
        }
        if (opts.debug)
            std::cout << "宣言数: " << program.declarations.size() << "\n\n";

        // SVターゲット用: `module NAME;` ヘッダ宣言からトップモジュール名を取得
        // （lowering前に取得する。宣言が無ければ空文字＝ファイル名から推定）
        const std::string sv_top_module = codegen::sv::extract_top_module_name(program);

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

        // ========== Type Checker ==========
        if (opts.debug)
            std::cout << "=== Type Checker ===\n";
        auto phase_parse_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::now() - phase_parse_start)
                                  .count();
        auto phase_typecheck_start = std::chrono::steady_clock::now();
        TypeChecker checker;
        // Check/Lintコマンド、または--force-check/--strict指定時にLint警告を有効化
        if (opts.command == Command::Check || opts.force_check) {
            checker.set_enable_lint_warnings(true);
        }
        bool type_check_ok = checker.check(program);
        auto phase_typecheck_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
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
                        diag.span, diag.message, preprocess_result.source_map, file_contents);
                } else {
                    std::string error_type = (diag.severity == DiagKind::Error ? "エラー" : "警告");
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
            std::cout << "型チェック: OK\n\n";

        // チェックコマンドの場合はここで終了
        if (opts.command == Command::Check) {
            if (opts.verbose) {
                std::cout << "✓ 構文と型チェックが成功しました\n";
            }
            return 0;
        }

        // ========== Lint Command ==========
        if (opts.command == Command::Lint) {
            if (opts.debug)
                std::cout << "=== Lint ===\n";

            // LintRunnerを作成
            lint::LintRunner runner;

            // Lintを実行
            auto result = runner.run(program);

            // 結果を表示
            Source source(code, opts.input_file);
            runner.print(source);

            if (opts.verbose) {
                std::cout << "✓ Lint完了\n";
            }

            return (result.error_count > 0) ? 1 : 0;
        }

        // ========== Fmt Command ==========
        if (opts.command == Command::Fmt) {
            if (opts.debug)
                std::cout << "=== Fmt ===\n";

            // Formatterを作成
            fmt::Formatter formatter;

            // フォーマット実行
            auto result = formatter.format(code);

            // 変更があればファイルを上書き
            if (result.modified) {
                std::ofstream ofs(opts.input_file);
                if (ofs) {
                    ofs << result.formatted_code;
                    formatter.print_summary(result);
                }
            } else {
                if (opts.verbose) {
                    std::cout << "✓ 整形不要\n";
                }
            }

            return 0;
        }

        // ========== HIR Lowering ==========
        if (opts.debug)
            std::cout << "=== HIR Lowering ===\n";
        auto phase_hir_start = std::chrono::steady_clock::now();
        hir::HirLowering hir_lowering;
        auto hir = hir_lowering.lower(program);
        auto phase_hir_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - phase_hir_start)
                                .count();
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
        auto phase_mir_start = std::chrono::steady_clock::now();
        mir::MirLowering mir_lowering;
        // プリプロセッサのモジュール範囲情報を設定（ソースファイルベースの分割用）
        mir_lowering.set_module_ranges(&preprocess_result.module_ranges);
        debug::log(debug::Stage::Mir, debug::Level::Info, "Calling lower() function");
        auto mir = mir_lowering.lower(hir);
        debug::log(debug::Stage::Mir, debug::Level::Info, "MIR lowering completed");
        auto phase_mir_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - phase_mir_start)
                                .count();

        if (opts.debug)
            std::cout << "MIR関数数: " << mir.functions.size() << "\n\n" << std::flush;

        // MIRを表示（最適化前）
        if (opts.show_mir && !opts.show_mir_opt) {
            std::cout << "=== MIR (最適化前) ===\n";
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
                std::cout << "最適化完了\n\n";
        }

        // SVターゲット: 定数トリップカウントのループを静的展開する
        // （generate/genvar相当。合成ツールは動的whileを展開できないため）
        if (is_sv) {
            mir::opt::unroll_constant_loops(mir);
        }
        auto phase_opt_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
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
        // 注意: インタプリタではインターフェースメソッドの動的ディスパッチがあるため、
        // DCEはコンパイル時のみ実行する
        // SVターゲットでは全関数をハードウェアモジュールとして保持
        if (opts.command == Command::Compile && !is_sv) {
            mir::opt::ProgramDeadCodeElimination program_dce;
            program_dce.run(mir);
        }

        // MIRを表示（最適化後）
        if (opts.show_mir_opt) {
            std::cout << "=== MIR (最適化後) ===" << std::endl;
            mir::MirPrinter printer;
            printer.print(mir, std::cout);
            return 0;
        }

        // ========== async/awaitバリデーション ==========
        // 非JSターゲットでasync/awaitが使用されている場合はエラー
        {
            bool is_js_target = (opts.target == "js" || opts.target == "web" || opts.emit_js);
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
                    std::cerr << "エラー: async/awaitはJSターゲット専用の機能です" << std::endl;
                    if (has_async) {
                        std::cerr << "  async関数が検出されました: " << async_func_name
                                  << std::endl;
                    }
                    if (has_await) {
                        std::cerr << "  await式が検出されました（関数: " << await_func_name << "）"
                                  << std::endl;
                    }
                    std::cerr << "ヒント: --target=js オプションでJSターゲットを指定して"
                                 "ください"
                              << std::endl;
                    return 1;
                }
            }
        }

        // ========== Backend ==========
        if (opts.command == Command::Run) {
            // ========== --target指定時のディスパッチ ==========
            // 従来は--targetを無視して常にJIT実行していた（JS指定でもネイティブ意味論で
            // 実行され誤解を招くため、実際のバックエンドで実行するか明示エラーにする）
            if (opts.target == "js" || opts.target == "web") {
                // JS生成 → Node.jsで実行
                cm::codegen::js::JSCodeGenOptions js_opts;
                js_opts.outputFile =
                    opts.output_file.empty()
                        ? (std::filesystem::temp_directory_path() / "cm_run_output.js").string()
                        : opts.output_file;
                js_opts.generateHTML = false;
                js_opts.verbose = opts.verbose || opts.debug;
                try {
                    cm::codegen::js::JSCodeGen codegen(js_opts);
                    codegen.compile(mir);
                } catch (const std::exception& e) {
                    std::cerr << "JavaScript コード生成エラー: " << e.what() << "\n";
                    return 1;
                }
                if (std::system("command -v node > /dev/null 2>&1") != 0) {
                    std::cerr << "エラー: node が見つかりません（--target=js の実行に必要です）\n";
                    std::cerr << "ヒント: cm compile --target=js で生成した .js を任意の"
                                 "JS実行系で実行してください\n";
                    return 1;
                }
                std::string cmd = "node " + js_opts.outputFile;
                int exec_result = std::system(cmd.c_str());
#if defined(_WIN32)
                return exec_result;
#else
                return WEXITSTATUS(exec_result);
#endif
            }
            if (opts.target == "wasm") {
                std::cerr << "エラー: cm run は --target=wasm の直接実行に未対応です\n";
                std::cerr << "ヒント: cm compile --emit-llvm --target=wasm -o out.wasm の後、"
                             "wasmtime out.wasm 等で実行してください\n";
                return 1;
            }
            if (opts.target == "sv" || opts.target == "verilog" || opts.target == "systemverilog") {
                std::cerr << "エラー: cm run は --target=sv の直接実行に未対応です\n";
                std::cerr << "ヒント: シミュレーション実行は cm test（//! platform: sv）を"
                             "使用してください\n";
                return 1;
            }

#ifdef CM_LLVM_ENABLED
            // ========== ネイティブテストランナー（cm test / run --test）==========
            // #[test] 関数を宣言順に、関数ごとに独立したJITで実行する（状態隔離）。
            // 成功 = 関数が正常リターン。assert失敗は exit(1) で即時停止する。
            if (opts.test_mode) {
                std::vector<const mir::MirFunction*> test_fns;
                for (const auto& func : mir.functions) {
                    if (!func) {
                        continue;
                    }
                    for (const auto& attr : func->attributes) {
                        if (attr == "test") {
                            test_fns.push_back(func.get());
                            break;
                        }
                    }
                }
                if (test_fns.empty()) {
                    std::cerr << "エラー: #[test] 関数が見つかりません: " << opts.input_file
                              << "\n";
                    return 1;
                }
                // step() はSVプラットフォーム専用（クロック概念が実行系に存在しない）
                for (const auto* fn : test_fns) {
                    for (const auto& block : fn->basic_blocks) {
                        if (!block || !block->terminator ||
                            block->terminator->kind != mir::MirTerminator::Call) {
                            continue;
                        }
                        const auto& data =
                            std::get<mir::MirTerminator::CallData>(block->terminator->data);
                        if (data.func && data.func->kind == mir::MirOperand::FunctionRef &&
                            std::get<std::string>(data.func->data) == "step") {
                            std::cerr << "エラー: step() は //! platform: sv のテストでのみ"
                                         "使用できます（テスト関数: "
                                      << fn->name << "）\n";
                            std::cerr << "ヒント: クロック駆動のテストはファイル先頭に "
                                         "//! platform: sv を指定してください\n";
                            return 1;
                        }
                    }
                }
                std::setvbuf(stdout, nullptr, _IONBF, 0);
                for (const auto* fn : test_fns) {
                    cm::codegen::jit::JITEngine jit;
                    auto result = jit.execute(mir, fn->name, opts.optimization_level);
                    if (!result.success) {
                        std::cerr << "JIT実行エラー (" << fn->name << "): " << result.errorMessage
                                  << "\n";
                        return 1;
                    }
                    std::cout << "[PASS] " << fn->name << "\n";
                }
                std::cout << "✓ " << test_fns.size() << " 件のテストが成功\n";
                return 0;
            }

            // JITキャッシュ: ソースが変更されていない場合、キャッシュされたバイナリを直接実行
            if (opts.incremental && !cache_fingerprint.empty()) {
                cache::CacheManager cache_mgr(cache_config);
                auto cached = cache_mgr.lookup(cache_fingerprint);
                if (cached) {
                    auto cached_exec = cache_mgr.cache_dir() / "objects" / cached->object_file;
                    if (std::filesystem::exists(cached_exec)) {
                        if (opts.verbose) {
                            auto hit_end = std::chrono::steady_clock::now();
                            auto hit_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                              hit_end - compile_start)
                                              .count();
                            std::cout << "✓ JITキャッシュヒット (" << hit_ms << "ms)\n";
                        }
                        // キャッシュされたバイナリを直接実行
                        std::string cmd = cached_exec.string();
                        int exec_result = std::system(cmd.c_str());
#if defined(_WIN32)
                        return exec_result;
#else
                        return WEXITSTATUS(exec_result);
#endif
                    }
                }
            }

            // JITコンパイラで実行
            if (opts.verbose) {
                std::cout << "=== JIT Compiler ===" << std::endl;
            }

            // JITキャッシュ用: バックグラウンドでネイティブバイナリも生成
            // MIRからLLVMコンパイルしてJIT実行
            cm::codegen::jit::JITEngine jit;

            // JIT実行時はstdoutをアンバッファにして即時出力されるようにする
            std::setvbuf(stdout, nullptr, _IONBF, 0);

            auto result = jit.execute(mir, "main", opts.optimization_level);

            if (!result.success) {
                std::cerr << "JIT実行エラー: " << result.errorMessage << std::endl;
                return 1;
            }

            // 注意: JIT実行後のキャッシュ保存（codegen.compile）は、
            // LLVM globalの再初期化問題とstdout汚染のため実装しない。
            // JITキャッシュは「cm compile」で生成されたキャッシュの再利用のみサポート。

            if (opts.verbose) {
                std::cout << "プログラム終了コード: " << result.exitCode << std::endl;
                std::cout << "✓ JIT実行完了" << std::endl;
            }

            return result.exitCode;
#else
            std::cerr << "エラー: JITコンパイラが無効です。LLVM対応ビルドが必要です。" << std::endl;
            return 1;
#endif
        }

        // コンパイルコマンドの場合
        if (opts.command == Command::Compile) {
            // SystemVerilog ターゲットの場合
            if (opts.target == "sv" || opts.target == "verilog" || opts.target == "systemverilog") {
                if (opts.verbose) {
                    std::cout << "=== SystemVerilog Code Generation ===\n";
                }

                // SVバックエンドオプション設定
                cm::codegen::sv::SVCodeGenOptions sv_opts;

                // 出力ファイル設定
                if (opts.output_file.empty()) {
                    // デフォルト出力先は.tmp/（ルートディレクトリを汚さない）
                    std::filesystem::create_directories(".tmp");
                    sv_opts.outputFile = ".tmp/output.sv";
                } else {
                    sv_opts.outputFile = opts.output_file;
                }

                sv_opts.verbose = opts.verbose || opts.debug;
                sv_opts.sourceFile = opts.input_file;
                sv_opts.topModule = sv_top_module;
                sv_opts.emitMemfile = opts.emit_memfile;
                sv_opts.strictLint = opts.sv_strict_lint;
                sv_opts.keepAlwaysFF = opts.sv_always_ff;
                sv_opts.warnNba = opts.sv_warn_nba;
                sv_opts.emitConstraints = opts.emit_constraints;
                {
                    // //! sv: device: / option: ディレクティブを反映
                    auto dirs = codegen::sv::parse_sv_project_directives(code);
                    sv_opts.devicePN = dirs.device_pn;
                    sv_opts.deviceVersion = dirs.device_version;
                    sv_opts.toolOptions = dirs.tool_options;
                }

                // SystemVerilog コード生成
                try {
                    cm::codegen::sv::SVCodeGen codegen(sv_opts);
                    codegen.compile(mir);

                    // sv階層化: サブモジュールを個別コンパイルして連結
                    if (!sv_hierarchy_submodules.empty()) {
                        std::string hier_error;
                        if (!codegen::sv::append_submodules(
                                argv[0], opts.input_file, sv_hierarchy_submodules,
                                sv_opts.outputFile, opts.optimization_level, opts.emit_memfile,
                                hier_error)) {
                            std::cerr << "sv階層化エラー: " << hier_error << "\n";
                            return 1;
                        }
                        if (!opts.quiet) {
                            std::cout << "✓ サブモジュール " << sv_hierarchy_submodules.size()
                                      << " 個を連結\n";
                        }
                    }

                    if (!opts.quiet) {
                        auto compile_end = std::chrono::steady_clock::now();
                        auto compile_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                              compile_end - compile_start)
                                              .count();
                        std::cout << "✓ SystemVerilog 生成完了: " << sv_opts.outputFile << " ("
                                  << compile_ms << "ms)\n";
                    }

                    // cm test (SVフロー): 生成物をiverilog+vvpでシミュレーション実行
                    if (run_sv_sim) {
                        return run_sv_test_simulation(sv_opts.outputFile, opts.quiet);
                    }
                } catch (const std::exception& e) {
                    std::cerr << "SystemVerilog コード生成エラー: " << e.what() << "\n";
                    return 1;
                }
            }
            // JavaScript ターゲットの場合
            else if (opts.target == "js" || opts.target == "web" || opts.emit_js) {
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

                    // インクリメンタルビルド: コンパイル成功後にキャッシュに保存
                    if (opts.incremental && !cache_fingerprint.empty()) {
                        cache::CacheManager cache_mgr(cache_config);
                        cache::CacheEntry entry;
                        entry.fingerprint = cache_fingerprint;
                        entry.target = opts.target.empty() ? "js" : opts.target;
                        entry.optimization_level = opts.optimization_level;
                        entry.compiler_version = cache::CacheManager::get_compiler_version();
                        entry.object_file = cache_fingerprint.substr(0, 16) + ".js";
                        entry.created_at = cache::CacheManager::current_timestamp();
                        for (const auto& f : preprocess_result.resolved_files) {
                            entry.source_hashes[f] = cache::CacheManager::compute_file_hash(f);
                        }

                        // モジュール別フィンガープリントを計算
                        std::map<std::string, std::vector<std::string>> module_files;
                        for (const auto& mr : preprocess_result.module_ranges) {
                            auto abs_path = std::filesystem::absolute(mr.file_path).string();
                            module_files[mr.file_path].push_back(abs_path);
                        }
                        if (!module_files.empty()) {
                            entry.module_fingerprints =
                                cache_mgr.compute_module_fingerprints(module_files);
                        }

                        if (cache_mgr.store(cache_fingerprint, js_opts.outputFile, entry)) {
                            if (opts.verbose) {
                                std::cout << "✓ キャッシュ保存完了: " << entry.object_file << "\n";
                            }
                            // 高速キャッシュ判定用の情報を保存
                            std::string target_key = opts.target.empty() ? "native" : opts.target;
                            cache_mgr.save_quick_check(opts.input_file, target_key,
                                                       opts.optimization_level, cache_fingerprint,
                                                       entry.object_file,
                                                       preprocess_result.resolved_files);
                        }
                    }

                    // --runオプションがある場合は実行（Node.js）
                    if (opts.run_after_emit && opts.target != "web") {
                        if (opts.verbose) {
                            std::cout << "実行中: node " << js_opts.outputFile << "\n";
                        }
                        std::string cmd = "node " + js_opts.outputFile;
                        int exec_result = std::system(cmd.c_str());
#if defined(_WIN32)
                        return exec_result;  // Windowsでは直接終了コードを返す
#else
                        return WEXITSTATUS(exec_result);
#endif
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
                } else if (opts.target == "uefi") {
                    llvm_opts.target = cm::codegen::llvm_backend::BuildTarget::BaremetalUEFI;
                    llvm_opts.format =
                        cm::codegen::llvm_backend::LLVMCodeGen::OutputFormat::ObjectFile;
                } else if (opts.target == "baremetal-arm" || opts.target == "bm") {
                    llvm_opts.target = cm::codegen::llvm_backend::BuildTarget::Baremetal;
                    llvm_opts.format =
                        cm::codegen::llvm_backend::LLVMCodeGen::OutputFormat::ObjectFile;
                } else if (opts.target == "baremetal-x86" || opts.target == "bm-x86") {
                    llvm_opts.target = cm::codegen::llvm_backend::BuildTarget::BaremetalX86;
                    llvm_opts.format =
                        cm::codegen::llvm_backend::LLVMCodeGen::OutputFormat::ObjectFile;
                } else if (!opts.target.empty() && opts.target != "native") {
                    std::cerr << "エラー: 不明なターゲット '" << opts.target << "'\n";
                    std::cerr << "有効なターゲット: native, wasm, js, web, bm, bm-x86, "
                                 "baremetal-arm, baremetal-x86, uefi\n";
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
                    } else if (llvm_opts.target ==
                               cm::codegen::llvm_backend::BuildTarget::BaremetalUEFI) {
                        llvm_opts.outputFile = "bootx64.efi";
                    } else if (llvm_opts.target ==
                                   cm::codegen::llvm_backend::BuildTarget::Baremetal ||
                               llvm_opts.target ==
                                   cm::codegen::llvm_backend::BuildTarget::BaremetalX86) {
                        llvm_opts.outputFile = "firmware.o";
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
                    // CompilationGuardの設定
                    {
                        auto& guard = cm::codegen::get_compilation_guard();
                        guard.configure(opts.max_output_size);  // 最大出力サイズの設定
                        if (opts.debug) {
                            guard.set_debug_mode(true);
                            guard.set_collect_statistics(true);
                        }
                    }

                    cm::codegen::llvm_backend::LLVMCodeGen codegen(llvm_opts);

                    // no_std環境でのOS依存機能チェック
                    if (llvm_opts.target == cm::codegen::llvm_backend::BuildTarget::Baremetal ||
                        llvm_opts.target == cm::codegen::llvm_backend::BuildTarget::BaremetalX86 ||
                        llvm_opts.target == cm::codegen::llvm_backend::BuildTarget::BaremetalUEFI) {
                        cm::mir::opt::NoStdChecker checker;
                        auto check_result = checker.check(mir);
                        if (check_result.has_errors) {
                            for (const auto& err : check_result.errors) {
                                std::cerr << err << "\n";
                            }
                            return 1;
                        }
                    }

                    // モジュール分割情報を事前計算
                    // 注意: 現在モジュール分割コンパイルは無効化されている
                    // （フロントエンドが毎回全実行されるため、効果が限定的）
                    // 将来 --split-modules オプションで有効化可能
                    cm::codegen::llvm_backend::LLVMCodeGen::ModuleCompileInfo module_info_pre;

                    if (cm::debug::debug_mode())
                        std::cerr << "[LLVM] Starting codegen.compile()" << std::endl;
                    auto phase_llvm_start = std::chrono::steady_clock::now();

                    // モジュール別差分コンパイルの判定
                    // 現在は常に全体コンパイルを使用
                    // モジュール分割はフロントエンドの差分化が実装されるまで無効
                    bool use_module_compile = false;

                    // モジュール情報（事前計算）
                    cm::codegen::llvm_backend::LLVMCodeGen::ModuleCompileInfo module_info;

                    if (use_module_compile) {
                        // === モジュール別差分コンパイル ===
                        // 現在無効化中 - フロントエンド差分化後に再有効化予定
                        if (opts.verbose) {
                            std::cout << "⚡ モジュール別差分コンパイル: " << changed_modules.size()
                                      << "/" << module_info_pre.module_names.size()
                                      << " モジュール再コンパイル\n";
                        }

                        // キャッシュ済みオブジェクトのマップ
                        std::map<std::string, std::filesystem::path> cached_objects;
                        if (opts.incremental) {
                            cache::CacheManager cache_mgr(cache_config);
                            // 前回のビルドで生成されたオブジェクトファイルを取得
                            std::string fp_to_use = prev_build_fingerprint.empty()
                                                        ? cache_fingerprint
                                                        : prev_build_fingerprint;
                            cached_objects = cache_mgr.get_cached_module_objects(fp_to_use);
                        }

                        // 一時ディレクトリの作成
                        std::filesystem::path module_output_dir =
                            std::filesystem::path(cache_config.cache_dir) / "module_objs" /
                            cache_fingerprint.substr(0, 16);

                        // モジュール別コンパイル実行
                        auto module_objects = codegen.compileModules(
                            mir, changed_modules, cached_objects, module_output_dir);

                        // リンク
                        std::vector<std::filesystem::path> all_objects;
                        for (const auto& mo : module_objects) {
                            all_objects.push_back(mo.object_path);
                        }
                        codegen.linkObjects(all_objects, llvm_opts.outputFile);

                        // モジュール情報を構築
                        module_info = module_info_pre;
                        module_info.changed_modules = changed_modules;

                        // 新しいモジュール .o をキャッシュに保存
                        if (opts.incremental) {
                            cache::CacheManager cache_mgr(cache_config);
                            for (const auto& mo : module_objects) {
                                if (!mo.from_cache) {
                                    // モジュール固有のフィンガープリントを生成
                                    std::string mod_fp =
                                        cache_fingerprint.substr(0, 16) + "_" + mo.module_name;
                                    cache_mgr.store_module_object(cache_fingerprint, mo.module_name,
                                                                  mod_fp, mo.object_path.string());
                                }
                            }
                        }

                        if (opts.verbose) {
                            size_t cached_count = 0;
                            for (const auto& mo : module_objects) {
                                if (mo.from_cache)
                                    cached_count++;
                            }
                            std::cout << "  キャッシュヒット: " << cached_count << "/"
                                      << module_objects.size() << " モジュール\n";
                        }
                    } else {
                        // === 従来の全体コンパイル ===
                        module_info = codegen.compileWithModuleInfo(mir, changed_modules);
                    }

                    auto phase_llvm_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                             std::chrono::steady_clock::now() - phase_llvm_start)
                                             .count();
                    if (cm::debug::debug_mode())
                        std::cerr << "[LLVM] codegen.compile() complete" << std::endl;

                    // --lir-opt: 最適化後のLLVM IRを表示
                    if (opts.show_lir_opt) {
                        std::cout << "=== LLVM IR (最適化後) ===\n";
                        std::cout << codegen.getIRString();
                        std::cout << "========================\n";
                        return 0;
                    }

                    if (!opts.quiet) {
                        auto compile_end = std::chrono::steady_clock::now();
                        auto compile_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                              compile_end - compile_start)
                                              .count();
                        std::cout << "✓ コンパイル完了: " << llvm_opts.outputFile << " ("
                                  << compile_ms << "ms)\n";
                        if (opts.verbose) {
                            auto frontend_ms = phase_preprocess_ms + phase_parse_ms +
                                               phase_typecheck_ms + phase_hir_ms + phase_mir_ms +
                                               phase_opt_ms;
                            std::cout << "  プリプロセス: " << phase_preprocess_ms << "ms\n";
                            std::cout
                                << "  パース+型チェック: " << phase_parse_ms + phase_typecheck_ms
                                << "ms\n";
                            std::cout << "  HIR+MIR変換: " << phase_hir_ms + phase_mir_ms << "ms\n";
                            std::cout << "  MIR最適化: " << phase_opt_ms << "ms\n";
                            std::cout << "  LLVM codegen: " << phase_llvm_ms << "ms\n";
                            std::cout << "  フロントエンド合計: " << frontend_ms << "ms ("
                                      << (compile_ms > 0 ? frontend_ms * 100 / compile_ms : 0)
                                      << "%)\n";

                            // モジュール分割情報を表示
                            if (!module_info.module_names.empty()) {
                                std::cout << "  モジュール: " << module_info.module_names.size()
                                          << " 検出";
                                if (!module_info.changed_modules.empty() &&
                                    module_info.changed_modules.size() <
                                        module_info.module_names.size()) {
                                    std::cout << " (" << module_info.changed_modules.size()
                                              << " 変更)";
                                }
                                std::cout << "\n";
                                for (const auto& [name, count] : module_info.module_func_count) {
                                    std::cout << "    " << name << ": " << count << " 関数\n";
                                }
                            }
                        }
                    }

                    // インクリメンタルビルド: コンパイル成功後にキャッシュに保存
                    if (opts.incremental && !cache_fingerprint.empty()) {
                        cache::CacheManager cache_mgr(cache_config);
                        cache::CacheEntry entry;
                        entry.fingerprint = cache_fingerprint;
                        entry.target = opts.target.empty() ? "native" : opts.target;
                        entry.optimization_level = opts.optimization_level;
                        entry.compiler_version = cache::CacheManager::get_compiler_version();
                        entry.object_file = cache_fingerprint.substr(0, 16) + ".o";
                        entry.created_at = cache::CacheManager::current_timestamp();

                        // 各ソースファイルのハッシュを記録
                        for (const auto& f : preprocess_result.resolved_files) {
                            entry.source_hashes[f] = cache::CacheManager::compute_file_hash(f);
                        }

                        // モジュール別フィンガープリントを計算
                        std::map<std::string, std::vector<std::string>> module_files;
                        for (const auto& mr : preprocess_result.module_ranges) {
                            auto abs_path = std::filesystem::absolute(mr.file_path).string();
                            module_files[mr.file_path].push_back(abs_path);
                        }
                        if (!module_files.empty()) {
                            entry.module_fingerprints =
                                cache_mgr.compute_module_fingerprints(module_files);
                        }

                        if (cache_mgr.store(cache_fingerprint, llvm_opts.outputFile, entry)) {
                            if (opts.verbose) {
                                std::cout << "✓ キャッシュ保存完了: " << entry.object_file << "\n";
                            }
                            // 高速キャッシュ判定用の情報を保存
                            std::string target_key = opts.target.empty() ? "native" : opts.target;
                            cache_mgr.save_quick_check(opts.input_file, target_key,
                                                       opts.optimization_level, cache_fingerprint,
                                                       entry.object_file,
                                                       preprocess_result.resolved_files);
                        }
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
