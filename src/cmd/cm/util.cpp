// ドライバ共有ヘルパー: ファイル読込・platformディレクティブ・ファイル走査・AST/HIR簡易プリンタ・
// SVテストシミュレーション実行

#include "driver.hpp"
#include "internal/base/i18n.hpp"
#include "internal/hir/nodes.hpp"
#include "internal/syntax/ast/decl.hpp"
#include "internal/syntax/ast/nodes.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <sys/wait.h>
#endif

namespace fs = std::filesystem;

namespace cm::driver {

// ファイルを読み込む
ReadFileResult read_file(const std::string& filename) {
    ReadFileResult result;
    std::ifstream file(filename);
    if (!file.is_open()) {
        result.success = false;
        result.error_message = i18n::msgf(i18n::MsgId::CliCannotOpenFile, filename);
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
// or形式: "js|web" → jsまたはwebならtrue not形式: "!native" → native以外ならtrue
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
    // ts と js は同一のNode実行系・同一のコード生成経路のため相互に等価なプラットフォームとして扱う
    // （//! platform: ts のファイルは --target=js でも、//! platform: js のファイルは --target=ts でも通す）
    auto normalize = [](const std::string& t) -> std::string { return (t == "ts") ? "js" : t; };
    std::string normalized_target = normalize(current_target);
    std::istringstream ss(directive);
    std::string token;
    while (std::getline(ss, token, '|')) {
        if (normalize(token) == normalized_target) {
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
    std::cerr << i18n::msg(i18n::MsgId::CliSvTestSimulationIsNot);
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
        std::cerr << i18n::msg(i18n::MsgId::CliIverilogVvpNotFoundRequired);
        std::cerr << i18n::msg(i18n::MsgId::CliMacosBrewInstallIcarusVerilog);
        return 1;
    }
    if (!fs::exists(tb)) {
        std::cerr << i18n::msgf(i18n::MsgId::CliTestbenchHasNotBeenGenerated, tb.string());
        return 1;
    }
    std::string compile_cmd =
        "iverilog -g2012 -o '" + sim.string() + "' '" + sv.string() + "' '" + tb.string() + "'";
    if (std::system(compile_cmd.c_str()) != 0) {
        std::cerr << i18n::msg(i18n::MsgId::CliIverilogCompilationFailed);
        return 1;
    }
    std::string run_cmd = "cd '" + dir.string() + "' && vvp '" + fs::absolute(sim).string() + "'";
    int rc = std::system(run_cmd.c_str());
    int exit_code = WIFEXITED(rc) ? WEXITSTATUS(rc) : 1;
    if (exit_code == 0 && !quiet) {
        std::cout << i18n::msg(i18n::MsgId::CliSvTestPassed);
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
                                          const std::vector<std::string>& excludes,
                                          const std::vector<std::string>& dir_scan_excludes) {
    std::vector<std::string> result;

    for (const auto& path : paths) {
        fs::path p(path);

        if (!fs::exists(p)) {
            std::cerr << i18n::msgf(i18n::MsgId::CliPathDoesNotExist, path);
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
            // 明示的に指定されたディレクトリが除外パターン配下の場合は、ユーザーの意図を優先して設定由来の除外を適用しない
            std::vector<std::string> effective_dir_excludes = dir_scan_excludes;
            if (matches_exclude_pattern(fs::absolute(p).string() + "/", dir_scan_excludes)) {
                effective_dir_excludes.clear();
            }
            // ディレクトリの場合
            if (recursive) {
                // 再帰的に走査（一時ディレクトリ .tmp と隠しディレクトリは常に除外）
                for (auto it = fs::recursive_directory_iterator(p);
                     it != fs::recursive_directory_iterator(); ++it) {
                    const auto& entry = *it;
                    if (entry.is_directory()) {
                        const std::string dirname = entry.path().filename().string();
                        if (dirname == ".tmp" || (dirname.size() > 1 && dirname[0] == '.')) {
                            it.disable_recursion_pending();
                        }
                        continue;
                    }
                    if (entry.is_regular_file() && entry.path().extension() == ".cm") {
                        std::string filepath = entry.path().string();
                        // .error（意図的に失敗するネガティブテスト）/.skip（環境依存で実行対象外）が並置されたファイルは走査からスキップする
                        fs::path base = entry.path();
                        if (fs::exists(base.replace_extension(".error")) ||
                            fs::exists(base.replace_extension(".skip"))) {
                            continue;
                        }
                        // 設定ファイル由来の除外（dir_scan_excludes）はディレクトリ走査にのみ
                        // 適用する（明示的なファイル指定は除外しない）
                        if (!matches_exclude_pattern(filepath, excludes) &&
                            !matches_exclude_pattern(filepath, effective_dir_excludes)) {
                            result.push_back(filepath);
                        }
                    }
                }
            } else {
                // 非再帰: ディレクトリ直下のみ
                for (const auto& entry : fs::directory_iterator(p)) {
                    if (entry.is_regular_file() && entry.path().extension() == ".cm") {
                        std::string filepath = entry.path().string();
                        if (!matches_exclude_pattern(filepath, excludes) &&
                            !matches_exclude_pattern(filepath, effective_dir_excludes)) {
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
                std::cout << i18n::msg(i18n::MsgId::CliNoteForWhileStatementsAre);
            }
        }
    }
    std::cout << "\n";
}

}  // namespace cm::driver
