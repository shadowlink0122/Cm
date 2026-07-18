#include "import.hpp"

#include "import_internal.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <sstream>

#ifdef __APPLE__
#include <limits.h>
#include <mach-o/dyld.h>
#endif

#ifdef __linux__
#include <limits.h>
#include <unistd.h>
#endif

#ifdef _WIN32
#include <windows.h>
#endif

namespace cm::preprocessor {

// ========== 高速文字列ユーティリティ（std::regex 置換用） ==========

// 実行ファイルのディレクトリを取得するヘルパー関数
static std::filesystem::path get_executable_directory() {
#ifdef __APPLE__
    char path[PATH_MAX];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) == 0) {
        return std::filesystem::path(path).parent_path();
    }
#endif

#ifdef __linux__
    char path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len != -1) {
        path[len] = '\0';
        return std::filesystem::path(path).parent_path();
    }
#endif

#ifdef _WIN32
    char path[MAX_PATH];
    if (GetModuleFileNameA(NULL, path, MAX_PATH) != 0) {
        return std::filesystem::path(path).parent_path();
    }
#endif

    return {};  // フォールバック: 空のパス
}

ImportPreprocessor::ImportPreprocessor(bool debug) : debug_mode(debug) {
    // プロジェクトルートを検出
    project_root = find_project_root(std::filesystem::current_path());

    // デフォルトの検索パスを設定
    // 1. プロジェクトルート
    search_paths.push_back(project_root);

    // 2. カレントディレクトリ（プロジェクトルートと異なる場合）
    auto current_dir = std::filesystem::current_path();
    if (current_dir != project_root) {
        search_paths.push_back(current_dir);
    }

    // 3. 標準ライブラリパス（環境変数 CM_STD_PATH）
    if (const char* std_env = std::getenv("CM_STD_PATH")) {
        auto std_env_path = std::filesystem::path(std_env);
        if (std::filesystem::exists(std_env_path)) {
            search_paths.push_back(std_env_path);
        }
    }

    // 4. 実行ファイルの場所を基準（異なるディレクトリから実行される場合に重要）
    // 注: std::io は std/io に変換される。libs/std/io を見つけるため exe_dir/libs を追加
    auto exe_dir = get_executable_directory();
    if (!exe_dir.empty()) {
        auto exe_libs = exe_dir / "libs";
        if (std::filesystem::exists(exe_libs)) {
            search_paths.push_back(exe_libs);
        }

        // インストールレイアウト（~/.cm/bin/cm → ~/.cm/libs）にも対応する。
        // リポジトリ外からインストール済みバイナリで実行しても std::* が解決できるようにする
        auto install_libs = exe_dir.parent_path() / "libs";
        if (std::filesystem::exists(install_libs)) {
            search_paths.push_back(install_libs);
        }
    }

    // 5. プロジェクトルート（project_root/libs/std/io 等を探すため）
    auto project_libs = project_root / "libs";
    if (std::filesystem::exists(project_libs)) {
        search_paths.push_back(project_libs);
    }

    // 5. システムインストールパス（プラットフォーム依存）
#ifdef __APPLE__
    // macOS: Homebrew や /usr/local
    std::vector<std::filesystem::path> system_paths = {
        "/usr/local/lib/cm/std", "/opt/homebrew/lib/cm/std",
        std::filesystem::path(std::getenv("HOME") ? std::getenv("HOME") : "") / ".cm/std"};
#elif defined(_WIN32)
    // Windows: AppData や Program Files
    std::vector<std::filesystem::path> system_paths = {
        std::filesystem::path(std::getenv("LOCALAPPDATA") ? std::getenv("LOCALAPPDATA") : "") /
            "Cm/std",
        std::filesystem::path(std::getenv("PROGRAMFILES") ? std::getenv("PROGRAMFILES") : "") /
            "Cm/std"};
#else
    // Linux/Unix
    std::vector<std::filesystem::path> system_paths = {
        "/usr/lib/cm/std", "/usr/local/lib/cm/std",
        std::filesystem::path(std::getenv("HOME") ? std::getenv("HOME") : "") / ".cm/std"};
#endif
    for (const auto& sys_path : system_paths) {
        if (!sys_path.empty() && std::filesystem::exists(sys_path)) {
            search_paths.push_back(sys_path);
        }
    }

    // 6. 環境変数 CM_MODULE_PATH（追加の検索パス）
    if (const char* env_path = std::getenv("CM_MODULE_PATH")) {
        std::stringstream ss(env_path);
        std::string path;
#ifdef _WIN32
        const char delimiter = ';';
#else
        const char delimiter = ':';
#endif
        while (std::getline(ss, path, delimiter)) {
            if (!path.empty()) {
                search_paths.push_back(std::filesystem::path(path));
            }
        }
    }

    if (debug_mode) {
        std::cout << "[PREPROCESSOR] Search paths:\n";
        for (const auto& p : search_paths) {
            std::cout << "  - " << p << "\n";
        }
    }
}

void ImportPreprocessor::add_search_path(const std::filesystem::path& path) {
    search_paths.push_back(path);
}

ImportPreprocessor::ProcessResult ImportPreprocessor::process(
    const std::string& source_code, const std::filesystem::path& source_file) {
    ProcessResult result;
    result.success = true;

    try {
        std::unordered_set<std::string> imported_files;

        // インポートを処理（ソースマップも生成）
        result.processed_source =
            process_imports(source_code, source_file, imported_files, result.source_map,
                            result.module_ranges, source_file.string());

        if (debug_mode) {
            std::cerr << "[IMPORT-FINAL] processed_source=" << result.processed_source.size()
                      << " source_map=" << result.source_map.size() << "\n";
        }

        // インポートされたモジュールリストを作成
        for (const auto& file : imported_files) {
            result.imported_modules.push_back(file);
        }

        // resolved_filesを構築（キャッシュフィンガープリント用）
        // メインのソースファイルを追加
        if (!source_file.empty() && std::filesystem::exists(source_file)) {
            result.resolved_files.push_back(std::filesystem::canonical(source_file).string());
        }
        // インポートされた全ファイルを追加
        for (const auto& file : imported_files) {
            result.resolved_files.push_back(file);
        }

        // ソースマップからモジュール範囲を再構築
        // process_importsでは正確なバイトオフセットを追跡できないためここで計算
        if (!result.source_map.empty() && !result.processed_source.empty()) {
            result.module_ranges.clear();

            std::string current_file;
            size_t start_offset = 0;
            size_t line_idx = 0;
            size_t pos = 0;
            size_t len = result.processed_source.length();

            while (pos < len && line_idx < result.source_map.size()) {
                // 次の改行を探す
                size_t next_newline = result.processed_source.find('\n', pos);
                // 改行を含む行末位置
                size_t line_end = (next_newline == std::string::npos) ? len : next_newline + 1;

                const auto& entry = result.source_map[line_idx];

                // ファイルが切り替わったら範囲を保存
                if (entry.original_file != current_file) {
                    if (!current_file.empty()) {
                        result.module_ranges.push_back({current_file, "", 0, start_offset, pos});
                    }
                    current_file = entry.original_file;
                    start_offset = pos;
                }

                pos = line_end;
                line_idx++;
            }

            // 最後の範囲を保存
            if (!current_file.empty()) {
                result.module_ranges.push_back({current_file, "", 0, start_offset, len});
            }
        }
    } catch (const std::exception& e) {
        result.success = false;
        result.error_message = e.what();
    }

    return result;
}

std::string ImportPreprocessor::process_imports(const std::string& source,
                                                const std::filesystem::path& current_file,
                                                std::unordered_set<std::string>& imported_files,
                                                SourceMap& source_map,
                                                std::vector<ModuleRange>& module_ranges,
                                                const std::string& import_chain,
                                                size_t /* import_line_in_parent */) {
    std::stringstream result;
    std::stringstream input(source);
    std::string line;
    size_t line_number = 0;  // 元ファイルの行番号を追跡

    std::string current_file_str =
        current_file.empty()
            ? "<unknown>"
            : std::filesystem::relative(current_file, std::filesystem::current_path()).string();

    // 出力行を追加するヘルパー
    auto emit_line = [&](const std::string& output_line, const std::string& orig_file,
                         size_t orig_line, const std::string& chain) {
        result << output_line << "\n";
        source_map.push_back({orig_file, orig_line, chain});
    };

    // 複数行のソースを追加するヘルパー
    auto emit_source = [&](const std::string& src, const std::string& orig_file,
                           const std::string& chain, size_t start_line = 1) {
        std::stringstream ss(src);
        std::string l;
        size_t ln = start_line;
        while (std::getline(ss, l)) {
            emit_line(l, orig_file, ln++, chain);
        }
    };

    // 各行を処理
    while (std::getline(input, line)) {
        line_number++;
        // インポート文を検出（複数パターンに対応）
        // 基本: import module;
        // エイリアス: import module as alias;
        // from構文: import { items } from module;
        // 相対: import ./module;
        // std::regex を排除 — 高速な文字列チェックに置換
        if (debug_mode) {
            std::cout << "[PREPROCESSOR] Processing line: " << line << "\n";
        }

        if (is_import_line(line)) {
            if (debug_mode) {
                std::cout << "[PREPROCESSOR] Matched import line: " << line << "\n";
            }
            // コメントを除去してからパース（複数行対応）
            auto strip_comment = [](const std::string& text) {
                return text.substr(0, text.find("//"));
            };
            std::string import_statement = strip_comment(line);
            std::string import_source_line = line;

            auto count_braces = [](const std::string& text) {
                int count = 0;
                for (char c : text) {
                    if (c == '{') {
                        count++;
                    } else if (c == '}') {
                        count--;
                    }
                }
                return count;
            };

            size_t import_line_number = line_number;
            int brace_depth = count_braces(import_statement);
            bool has_semicolon = import_statement.find(';') != std::string::npos;

            while ((!has_semicolon || brace_depth > 0) && std::getline(input, line)) {
                line_number++;
                if (debug_mode) {
                    std::cout << "[PREPROCESSOR] Processing line: " << line << "\n";
                }
                import_source_line += "\n" + line;
                std::string part = strip_comment(line);
                import_statement += " " + part;
                brace_depth += count_braces(part);
                if (part.find(';') != std::string::npos) {
                    has_semicolon = true;
                }
            }

            // 末尾の空白を除去
            import_statement.erase(import_statement.find_last_not_of(" \t\n\r;") + 1);

            // インポート文をパース
            auto import_info = parse_import_statement(import_statement);
            import_info.line_number = import_line_number;
            // ファイル名を相対パスに変換
            import_info.source_file =
                std::filesystem::relative(current_file, std::filesystem::current_path()).string();
            import_info.source_line = import_source_line;

            // 階層的インポート（std::io）を処理
            // std::ioは3 に解決する必要がある

            if (debug_mode) {
                std::cout << "[PREPROCESSOR] Found import: " << import_info.module_name;
                if (!import_info.alias.empty()) {
                    std::cout << " as " << import_info.alias;
                }
                if (import_info.is_recursive_wildcard) {
                    std::cout << " (recursive wildcard)";
                }
                std::cout << "\n";
            }

            // 再帰的ワイルドカードインポートの処理
            if (import_info.is_recursive_wildcard) {
                // ディレクトリパスを解決
                std::filesystem::path base_dir;
                if (import_info.module_name.substr(0, 2) == "./" ||
                    import_info.module_name.substr(0, 3) == "../") {
                    base_dir = current_file.parent_path() / import_info.module_name;
                } else {
                    base_dir = project_root / import_info.module_name;
                }

                if (!std::filesystem::exists(base_dir) ||
                    !std::filesystem::is_directory(base_dir)) {
                    std::stringstream error;
                    error << import_info.source_file << ":" << import_info.line_number << ":8: ";
                    error << "エラー: ディレクトリが見つかりません: " << import_info.module_name
                          << "\n";
                    throw std::runtime_error(error.str());
                }

                // パスを正規化（相対パス計算のため）
                base_dir = std::filesystem::canonical(base_dir);

                // すべてのモジュールを再帰的に検出
                auto all_modules = find_all_modules_recursive(base_dir);

                if (debug_mode) {
                    std::cout << "[PREPROCESSOR] Found " << all_modules.size() << " modules in "
                              << base_dir << "\n";
                }

                // 選択的インポートの場合、モジュール名でフィルタリング
                // import ./path/*::{mod1, mod2} 形式
                if (!import_info.items.empty()) {
                    std::vector<std::filesystem::path> filtered;
                    for (const auto& mod_path : all_modules) {
                        std::string stem = mod_path.stem().string();
                        if (std::find(import_info.items.begin(), import_info.items.end(), stem) !=
                            import_info.items.end()) {
                            filtered.push_back(mod_path);
                        }
                    }
                    all_modules = std::move(filtered);

                    if (debug_mode) {
                        std::cout << "[PREPROCESSOR] Filtered to " << all_modules.size()
                                  << " modules\n";
                    }
                }

                // 基準パスからの相対パスを計算（正規化する）
                auto parent_dir = current_file.parent_path();
                if (parent_dir.empty()) {
                    parent_dir = std::filesystem::current_path();
                } else {
                    parent_dir = std::filesystem::canonical(parent_dir);
                }

                // 各モジュールをインポート
                for (const auto& mod_path : all_modules) {
                    // 相対パスを計算してインポート文を生成
                    auto rel_path = std::filesystem::relative(mod_path, parent_dir);
                    std::string rel_str = rel_path.string();
                    // 拡張子を削除
                    if (rel_str.length() > 3 && rel_str.substr(rel_str.length() - 3) == ".cm") {
                        rel_str = rel_str.substr(0, rel_str.length() - 3);
                    }
                    // ./プレフィックスを追加
                    if (rel_str[0] != '.') {
                        rel_str = "./" + rel_str;
                    }

                    if (debug_mode) {
                        std::cout << "[PREPROCESSOR] Recursive import: " << rel_str << "\n";
                    }

                    // 擬似的なインポート文を作成して処理
                    std::string pseudo_import = "import " + rel_str + ";";
                    result << "// Recursive import: " << rel_str << "\n";

                    // 再帰的に処理（このインポートを追加）
                    auto sub_info = parse_import_statement(pseudo_import);
                    sub_info.line_number = import_info.line_number;
                    sub_info.source_file = import_info.source_file;
                    sub_info.source_line = pseudo_import;

                    // モジュールパスを解決して処理
                    auto sub_module_path = resolve_module_path(sub_info.module_name, current_file);
                    if (sub_module_path.empty())
                        continue;

                    std::string sub_canonical =
                        std::filesystem::canonical(sub_module_path).string();

                    // 既にインポート済みならスキップ
                    if (imported_modules.count(sub_canonical) > 0)
                        continue;

                    // 循環依存チェック
                    if (std::find(import_stack.begin(), import_stack.end(), sub_canonical) !=
                        import_stack.end())
                        continue;

                    import_stack.push_back(sub_canonical);
                    imported_modules.insert(sub_canonical);

                    // モジュールを読み込み
                    std::string sub_module_source = load_module_file(sub_module_path);
                    std::string sub_file_str =
                        std::filesystem::relative(sub_module_path, std::filesystem::current_path())
                            .string();
                    std::string sub_chain = import_chain + " -> " + sub_file_str;
                    sub_module_source =
                        process_imports(sub_module_source, sub_module_path, imported_files,
                                        source_map, module_ranges, sub_chain, line_number);

                    // exportブロック抽出用にオリジナルソースを保存（remove前に）
                    std::string original_sub_source = sub_module_source;
                    sub_module_source = remove_export_keywords(sub_module_source);

                    import_stack.pop_back();

                    // 名前空間パスを計算（ディレクトリ構造から）
                    auto dir_rel =
                        std::filesystem::relative(sub_module_path.parent_path(), base_dir);
                    std::string ns_path = dir_rel.string();
                    // バックスラッシュをスラッシュに変換
                    std::replace(ns_path.begin(), ns_path.end(), '\\', '/');

                    // 名前空間を構築
                    std::vector<std::string> ns_parts;
                    std::stringstream ns_ss(ns_path);
                    std::string ns_part;
                    while (std::getline(ns_ss, ns_part, '/')) {
                        if (!ns_part.empty() && ns_part != ".") {
                            ns_parts.push_back(ns_part);
                        }
                    }

                    // 名前空間を開く
                    for (const auto& ns : ns_parts) {
                        result << "namespace " << ns << " {\n";
                    }

                    result << sub_module_source;

                    // 名前空間を逆順で閉じる
                    for (auto it = ns_parts.rbegin(); it != ns_parts.rend(); ++it) {
                        result << "} // namespace " << *it << "\n";
                    }

                    // exportされたシンボルをnamespace外にも展開
                    std::string sub_exported = extract_exported_blocks(original_sub_source);
                    if (!sub_exported.empty()) {
                        result << "// ===== Exported symbols from " << rel_str
                               << " (direct access) =====\n";
                        result << sub_exported << "\n";
                        result << "// ===== End exported symbols =====\n";
                    }

                    imported_files.insert(sub_canonical);
                }

                continue;  // 次の行へ
            }

            // モジュールパスを解決
            auto module_path = resolve_module_path(import_info.module_name, current_file);
            if (module_path.empty()) {
                // 詳細なエラーメッセージ
                std::stringstream error;
                error << import_info.source_file << ":" << import_info.line_number << ":8: ";
                error << "エラー: モジュールが見つかりません: " << import_info.module_name << "\n";
                error << "  " << import_info.source_line << "\n";
                error << "         ^" << std::string(import_info.module_name.length() - 1, '~')
                      << "\n";
                throw std::runtime_error(error.str());
            }

            // 正規化されたパスを取得
            std::string canonical_path = std::filesystem::canonical(module_path).string();

            // 循環依存チェック（再インポート防止より先に行う）
            if (std::find(import_stack.begin(), import_stack.end(), canonical_path) !=
                import_stack.end()) {
                // 詳細なエラーメッセージを生成
                std::stringstream error;
                error << "Circular dependency detected:\n";
                error << import_info.source_file << ":" << import_info.line_number << ":1: ";
                error << "エラー: 循環依存が検出されました\n";
                error << "  " << import_info.source_line << "\n";

                // インポートスタックを表示（相対パスで）
                error << "\n依存関係:\n";
                auto cwd = std::filesystem::current_path();
                for (size_t i = 0; i < import_stack.size(); ++i) {
                    auto rel_path = std::filesystem::relative(import_stack[i], cwd);
                    error << "  " << (i + 1) << ". " << rel_path.string() << "\n";
                }
                auto rel_canonical = std::filesystem::relative(canonical_path, cwd);
                error << "  " << (import_stack.size() + 1) << ". " << rel_canonical.string()
                      << " (循環参照)\n";

                throw std::runtime_error(error.str());
            }

            // 選択的インポートの場合、新しいシンボルがあるかチェック
            bool need_process = false;
            std::vector<std::string> new_items;

            if (!import_info.items.empty() && !import_info.is_wildcard) {
                // 選択的インポート: 新しいシンボルのみをインポート
                auto& imported = imported_symbols[canonical_path];
                for (const auto& item : import_info.items) {
                    if (imported.find(item) == imported.end()) {
                        new_items.push_back(item);
                        imported.insert(item);
                        need_process = true;
                    }
                }

                if (!need_process) {
                    if (debug_mode) {
                        std::cout << "[PREPROCESSOR] All symbols already imported from: "
                                  << canonical_path << "\n";
                    }
                    result << "// All symbols already imported from: " << import_info.module_name
                           << "\n";
                    continue;
                }

                if (debug_mode) {
                    std::cout << "[PREPROCESSOR] New symbols to import: ";
                    for (const auto& item : new_items) {
                        std::cout << item << " ";
                    }
                    std::cout << "\n";
                }
            } else {
                // ワイルドカードまたはモジュール全体のインポート
                // 再インポート防止チェック
                if (imported_modules.count(canonical_path) > 0) {
                    if (debug_mode) {
                        std::cout << "[PREPROCESSOR] Skipping already imported: " << canonical_path
                                  << "\n";
                    }
                    result << "// Already imported: " << import_info.module_name << "\n";
                    continue;
                }
                imported_modules.insert(canonical_path);
                need_process = true;
            }

            // インポートスタックに追加
            import_stack.push_back(canonical_path);

            // キャッシュチェック
            std::string module_source;
            std::string module_file_str =
                std::filesystem::relative(module_path, std::filesystem::current_path()).string();
            std::string module_chain = import_chain + " -> " + module_file_str;

            // 再帰呼び出し用のダミーソースマップ（実際のマッピングは出力時に行う）
            SourceMap dummy_source_map;
            std::vector<ModuleRange> dummy_module_ranges;

            // export抽出用にオリジナルファイル（再帰import展開前）を保存
            std::string raw_module_source;

            if (module_cache.count(canonical_path) > 0) {
                module_source = module_cache[canonical_path];
                // キャッシュからraw sourceも取得
                raw_module_source = raw_module_cache[canonical_path];
            } else {
                // モジュールファイルを読み込む
                module_source = load_module_file(module_path);
                // 再帰import展開前のソースを保存（export抽出用）
                raw_module_source = module_source;

                // モジュール内のインポートを再帰的に処理（ダミーソースマップを使用）
                module_source =
                    process_imports(module_source, module_path, imported_files, dummy_source_map,
                                    dummy_module_ranges, module_chain, line_number);

                // キャッシュに保存
                module_cache[canonical_path] = module_source;
                raw_module_cache[canonical_path] = raw_module_source;

                if (debug_mode) {
                    std::cerr << "[IMPORT-DBG] " << module_file_str
                              << " raw=" << raw_module_source.size()
                              << " expanded=" << module_source.size()
                              << " smap=" << dummy_source_map.size() << "\n";
                }
            }

            // インポートスタックから削除
            import_stack.pop_back();

            // エクスポートフィルタリング（選択的インポートの場合）
            if (!import_info.items.empty() && !import_info.is_wildcard) {
                // 新しいシンボルのみをフィルタリング
                if (!new_items.empty()) {
                    module_source = filter_exports(module_source, new_items);
                } else {
                    module_source = filter_exports(module_source, import_info.items);
                }
            }

            // exportブロック抽出用にサブインポート展開済みソースを保存
            // （export キーワードあり + Exported symbols セクションあり）
            std::string export_extraction_source = module_source;

            // exportキーワードを削除（キャッシュして重複処理を回避）
            if (processed_module_cache.count(canonical_path) > 0 && import_info.items.empty()) {
                // 選択的importでなければキャッシュを使用
                module_source = processed_module_cache[canonical_path];
            } else {
                module_source = remove_export_keywords(module_source);
                if (import_info.items.empty()) {
                    processed_module_cache[canonical_path] = module_source;
                }
            }

            // エイリアスの処理
            if (!import_info.alias.empty()) {
                result << "\n// ===== Begin module: " << import_info.module_name << " (as "
                       << import_info.alias << ") =====\n";
                result << "namespace " << import_info.alias << " {\n";
                result << module_source;
                result << "} // namespace " << import_info.alias << "\n";
                result << "// ===== End module: " << import_info.module_name << " =====\n\n";
            } else if ((import_info.is_from_import || !import_info.items.empty()) &&
                       !import_info.is_wildcard) {
                // from構文または選択的インポート（::{items}）の場合
                // 名前空間でラップせずにインポート（直接アクセス可能）
                emit_line("", "<generated>", 0, import_chain);
                emit_line("// ===== Selective import from " + import_info.module_name + " =====",
                          "<generated>", 0, import_chain);

                // サブモジュールパスがある場合、そのサブモジュールの名前空間内の内容を展開
                std::string submodule_ns;
                size_t path_end = import_info.module_name.find_last_of("/");
                if (path_end != std::string::npos) {
                    size_t colon_pos = import_info.module_name.find("::", path_end);
                    if (colon_pos != std::string::npos) {
                        submodule_ns = import_info.module_name.substr(colon_pos + 2);
                    }
                }

                std::string source_to_emit;
                if (!submodule_ns.empty()) {
                    // サブモジュールの名前空間内の内容を抽出
                    std::string extracted = extract_namespace_content(module_source, submodule_ns);
                    if (!extracted.empty()) {
                        // 選択的インポートの場合、アイテムのみをフィルタ
                        if (!import_info.items.empty()) {
                            extracted = filter_exports(extracted, import_info.items);
                        }
                        source_to_emit = remove_export_keywords(extracted);
                    } else {
                        source_to_emit = remove_export_keywords(module_source);
                    }
                } else {
                    // フィルタリングして出力
                    if (!import_info.items.empty()) {
                        source_to_emit = filter_exports(module_source, import_info.items);
                    } else {
                        source_to_emit = module_source;
                    }
                    source_to_emit = remove_export_keywords(source_to_emit);
                }

                // emit_sourceでソースマップに追加
                emit_source(source_to_emit, module_file_str, module_chain, 1);

                emit_line(
                    "// ===== End selective import from " + import_info.module_name + " =====",
                    "<generated>", 0, import_chain);
                emit_line("", "<generated>", 0, import_chain);
            } else if (import_info.is_wildcard && !import_info.is_recursive_wildcard) {
                // ワイルドカードインポート（::*）の場合
                // サブモジュールパスがある場合、そのサブモジュールの名前空間内の内容を展開
                std::string submodule_ns;
                size_t path_end = import_info.module_name.find_last_of("/");
                if (path_end != std::string::npos) {
                    size_t colon_pos = import_info.module_name.find("::", path_end);
                    if (colon_pos != std::string::npos) {
                        submodule_ns = import_info.module_name.substr(colon_pos + 2);
                    }
                }

                result << "\n// ===== Wildcard import from " << import_info.module_name
                       << " =====\n";
                if (!submodule_ns.empty()) {
                    std::string extracted = extract_namespace_content(module_source, submodule_ns);
                    if (!extracted.empty()) {
                        result << remove_export_keywords(extracted) << "\n";
                    } else {
                        result << remove_export_keywords(module_source) << "\n";
                    }
                } else {
                    result << remove_export_keywords(module_source) << "\n";
                }
                result << "// ===== End wildcard import from " << import_info.module_name
                       << " =====\n\n";
            } else {
                // 通常のインポート - namespaceでラップ
                emit_line("", "<generated>", 0, import_chain);
                emit_line("// ===== Begin module: " + import_info.module_name + " =====",
                          "<generated>", 0, import_chain);

                // ./path/module::submodule 形式をチェック
                std::string submodule_path;
                std::string base_module_name = import_info.module_name;

                // 相対パス内の :: を探す（パス部分の後）
                size_t path_end = base_module_name.find_last_of("/");
                if (path_end != std::string::npos) {
                    size_t colon_pos = base_module_name.find("::", path_end);
                    if (colon_pos != std::string::npos) {
                        submodule_path = base_module_name.substr(colon_pos + 2);
                        base_module_name = base_module_name.substr(0, colon_pos);
                    }
                }

                // namespace名を決定
                std::string module_namespace;

                // サブモジュールパスがある場合、サブモジュールのみを名前空間として使用
                // （親モジュールの名前空間はスキップ）
                if (!submodule_path.empty()) {
                    module_namespace = submodule_path;
                } else {
                    // 1. モジュールソースから module 宣言を抽出
                    // 2. なければパスの最後のコンポーネントを使用
                    module_namespace = extract_module_namespace(module_source);
                }

                if (module_namespace.empty()) {
                    // module宣言がない場合、パスの最後のコンポーネントを使用
                    std::string namespace_path = base_module_name;
                    // ./ または ../ を削除
                    if (namespace_path.find("./") == 0) {
                        namespace_path = namespace_path.substr(2);
                    } else if (namespace_path.find("../") == 0) {
                        namespace_path = namespace_path.substr(3);
                    }

                    // 最後のコンポーネントを取得（/ または :: で分割）
                    size_t last_sep = namespace_path.find_last_of("/");
                    if (last_sep != std::string::npos) {
                        module_namespace = namespace_path.substr(last_sep + 1);
                    } else {
                        // :: で分割を試みる
                        size_t last_colon = namespace_path.rfind("::");
                        if (last_colon != std::string::npos) {
                            module_namespace = namespace_path.substr(last_colon + 2);
                        } else {
                            module_namespace = namespace_path;
                        }
                    }
                }

                // :: を含む場合は階層的な名前空間を作成
                std::vector<std::string> namespace_parts;
                std::string current;
                for (size_t i = 0; i < module_namespace.length(); ++i) {
                    if (i + 1 < module_namespace.length() && module_namespace[i] == ':' &&
                        module_namespace[i + 1] == ':') {
                        if (!current.empty()) {
                            namespace_parts.push_back(current);
                            current.clear();
                        }
                        ++i;  // skip second ':'
                    } else {
                        current += module_namespace[i];
                    }
                }
                if (!current.empty()) {
                    namespace_parts.push_back(current);
                }

                // 階層的な名前空間を開く
                // サブモジュールパスがある場合は外側の名前空間をスキップ
                // （モジュールソース内ですでに正しい名前空間が生成されている）
                if (submodule_path.empty()) {
                    for (const auto& ns : namespace_parts) {
                        emit_line("namespace " + ns + " {", "<generated>", 0, import_chain);
                    }
                }

                // exportキーワードを削除
                std::string cleaned_source = remove_export_keywords(module_source);
                // モジュールソースの各行をemit_sourceで出力（元ファイルの行番号を追跡）
                emit_source(cleaned_source, module_file_str, module_chain, 1);

                // 名前空間を逆順で閉じる
                if (submodule_path.empty()) {
                    for (auto it = namespace_parts.rbegin(); it != namespace_parts.rend(); ++it) {
                        // namespace閉じ行はコンパイラ生成なので、元ファイル情報なし
                        emit_line("} // namespace " + *it, "<generated>", 0, import_chain);
                    }
                }
                emit_line("// ===== End module: " + import_info.module_name + " =====",
                          "<generated>", 0, import_chain);

                // exportされたシンボルをnamespace外にも展開
                // これにより名前空間修飾なしでも呼び出し可能になる
                // サブインポート展開済みソースを使用し推移的エクスポートも含める
                std::string exported_blocks = extract_exported_blocks(export_extraction_source);
                if (!exported_blocks.empty()) {
                    emit_line("// ===== Exported symbols from " + import_info.module_name +
                                  " (direct access) =====",
                              "<generated>", 0, import_chain);
                    emit_source(exported_blocks, module_file_str, module_chain, 1);
                    emit_line("// ===== End exported symbols =====", "<generated>", 0,
                              import_chain);
                }

                emit_line("", "<generated>", 0, import_chain);
            }

            // imported_filesに追加（後方互換性のため）
            imported_files.insert(canonical_path);
        } else {
            // インポート文以外はそのまま出力
            emit_line(line, current_file_str, line_number, import_chain);
        }
    }

    return result.str();
}

ImportPreprocessor::ImportInfo ImportPreprocessor::parse_import_statement(
    const std::string& import_line) {
    ImportInfo info;

    // セミコロンを削除
    std::string line = import_line;
    while (!line.empty() && (line.back() == ';' || line.back() == ' ' || line.back() == '\t'))
        line.pop_back();

    // 相対パスチェック
    if (line.find("./") != std::string::npos || line.find("../") != std::string::npos) {
        info.is_relative = true;
    }

    // トリムヘルパー
    auto trim = [](const std::string& s) -> std::string {
        size_t start = s.find_first_not_of(" \t");
        if (start == std::string::npos)
            return "";
        size_t end = s.find_last_not_of(" \t");
        return s.substr(start, end - start + 1);
    };

    std::string trimmed = trim(line);

    do {
        // ========== from module import { items } ==========
        if (trimmed.rfind("from ", 0) == 0) {
            // from MODULE import { ITEMS }
            std::string rest = trim(trimmed.substr(5));
            size_t import_pos = rest.find(" import ");
            if (import_pos != std::string::npos) {
                info.module_name = trim(rest.substr(0, import_pos));
                info.is_from_import = true;
                std::string items_part = trim(rest.substr(import_pos + 8));
                // { items } の中身を抽出
                if (items_part.front() == '{' && items_part.back() == '}') {
                    std::string items_str = items_part.substr(1, items_part.size() - 2);
                    parse_import_items(items_str, info);
                }
            }
            break;
        }

        // import で始まる場合
        if (trimmed.rfind("import ", 0) == 0) {
            std::string rest = trim(trimmed.substr(7));

            // ========== import { items } from module ==========
            if (!rest.empty() && rest.front() == '{') {
                size_t close_brace = rest.find('}');
                if (close_brace != std::string::npos) {
                    std::string items_str = rest.substr(1, close_brace - 1);
                    std::string after_brace = trim(rest.substr(close_brace + 1));
                    if (after_brace.rfind("from ", 0) == 0) {
                        info.module_name = trim(after_brace.substr(5));
                        info.is_from_import = true;
                        parse_import_items(items_str, info);
                        break;
                    }
                }
            }

            // ========== import * from module ==========
            if (rest.rfind("* from ", 0) == 0) {
                info.module_name = trim(rest.substr(7));
                info.is_wildcard = true;
                info.is_from_import = true;
                break;
            }

            // ========== import module as alias ==========
            {
                size_t as_pos = rest.find(" as ");
                if (as_pos != std::string::npos) {
                    info.module_name = trim(rest.substr(0, as_pos));
                    info.alias = trim(rest.substr(as_pos + 4));
                    break;
                }
            }

            // ========== import path/*::{items} ==========
            {
                size_t wildcard_sel = rest.find("/*::{");
                if (wildcard_sel != std::string::npos) {
                    info.module_name = trim(rest.substr(0, wildcard_sel));
                    info.is_recursive_wildcard = true;
                    info.is_wildcard = true;
                    size_t close = rest.find('}', wildcard_sel + 5);
                    if (close != std::string::npos) {
                        std::string items_str =
                            rest.substr(wildcard_sel + 5, close - wildcard_sel - 5);
                        parse_import_items(items_str, info);
                    }
                    break;
                }
            }

            // ========== import path/* ==========
            if (rest.size() >= 2 && rest.substr(rest.size() - 2) == "/*") {
                info.module_name = trim(rest.substr(0, rest.size() - 2));
                info.is_recursive_wildcard = true;
                info.is_wildcard = true;
                break;
            }

            // ========== import module::{items} ==========
            {
                size_t sel_pos = rest.find("::{");
                if (sel_pos != std::string::npos) {
                    size_t close = rest.find('}', sel_pos + 3);
                    if (close != std::string::npos) {
                        // module::* (ワイルドカード) チェック
                        std::string items_str = rest.substr(sel_pos + 3, close - sel_pos - 3);
                        if (trim(items_str) == "*") {
                            info.module_name = trim(rest.substr(0, sel_pos));
                            info.is_wildcard = true;
                        } else {
                            info.module_name = trim(rest.substr(0, sel_pos));
                            parse_import_items(items_str, info);
                        }
                        break;
                    }
                }
            }

            // ========== import module::* ==========
            if (rest.size() >= 3 && rest.substr(rest.size() - 3) == "::*") {
                info.module_name = trim(rest.substr(0, rest.size() - 3));
                info.is_wildcard = true;
                break;
            }

            // ========== import module (シンプル) ==========
            info.module_name = rest;

            // ./path/module::submodule::item 形式をチェック
            std::string& name = info.module_name;
            size_t last_colon = name.rfind("::");
            if (last_colon != std::string::npos && last_colon > 0) {
                std::string last_part = name.substr(last_colon + 2);
                if (last_part == "*") {
                    info.is_wildcard = true;
                    info.module_name = name.substr(0, last_colon);
                } else if (!last_part.empty() && std::islower(last_part[0])) {
                    size_t first_colon = name.find("::");
                    if (!info.is_relative || first_colon != last_colon) {
                        info.items.push_back(last_part);
                        info.module_name = name.substr(0, last_colon);
                    }
                }
            }
        }
    } while (false);

    // 引用符を除去
    if (info.module_name.size() >= 2) {
        if ((info.module_name.front() == '"' && info.module_name.back() == '"') ||
            (info.module_name.front() == '\'' && info.module_name.back() == '\'')) {
            info.module_name = info.module_name.substr(1, info.module_name.size() - 2);
        }
    }

    return info;
}

// アイテムリストをパースするヘルパー関数
void ImportPreprocessor::parse_import_items(const std::string& items_str, ImportInfo& info) {
    std::stringstream ss(items_str);
    std::string item;
    while (std::getline(ss, item, ',')) {
        // トリム
        item.erase(0, item.find_first_not_of(" \t\n\r"));
        item.erase(item.find_last_not_of(" \t\n\r") + 1);
        if (!item.empty()) {
            // item as alias の形式をチェック
            size_t as_pos = item.find(" as ");
            if (as_pos != std::string::npos) {
                std::string name = item.substr(0, as_pos);
                std::string alias = item.substr(as_pos + 4);
                // トリム
                name.erase(name.find_last_not_of(" \t") + 1);
                alias.erase(0, alias.find_first_not_of(" \t"));
                info.items.push_back(name);
                info.item_aliases.push_back({name, alias});
            } else {
                info.items.push_back(item);
            }
        }
    }
}

std::string ImportPreprocessor::add_module_prefix(const std::string& source,
                                                  const std::string& module_name) {
    // すでにexportキーワードは削除されているので、関数と定数の宣言に
    // モジュール名をプレフィックスとして追加する

    std::string result;
    std::istringstream input(source);
    std::string line;

    while (std::getline(input, line)) {
        // const定数の宣言を検出してプレフィックスを追加
        std::regex const_regex(R"(^(\s*const\s+\w+\s+)(\w+)(\s*=.*)$)");
        std::smatch const_match;
        if (std::regex_match(line, const_match, const_regex)) {
            result += const_match[1].str() + module_name + "::" + const_match[2].str() +
                      const_match[3].str() + "\n";
            continue;
        }

        // 関数宣言を検出してプレフィックスを追加
        // 型 関数名(パラメータ) { の形式
        std::regex func_regex(R"(^(\s*\w+\s+)(\w+)(\s*\([^)]*\)\s*\{.*)$)");
        std::smatch func_match;
        if (std::regex_match(line, func_match, func_regex)) {
            // main関数は除外
            if (func_match[2].str() != "main") {
                result += func_match[1].str() + module_name + "::" + func_match[2].str() +
                          func_match[3].str() + "\n";
                continue;
            }
        }

        // その他の行はそのまま出力
        result += line + "\n";
    }

    return result;
}

std::string ImportPreprocessor::extract_module_namespace(const std::string& module_source) {
    // module M; 宣言を検出
    std::regex module_regex(R"(^\s*module\s+(\w+)\s*;)");
    std::istringstream input(module_source);
    std::string line;

    while (std::getline(input, line)) {
        std::smatch match;
        if (std::regex_match(line, match, module_regex)) {
            return match[1].str();
        }
    }

    return "";  // module宣言がない
}

std::string ImportPreprocessor::extract_namespace_content(const std::string& source,
                                                          const std::string& namespace_name) {
    // 指定した名前空間内の内容を抽出
    // namespace X { ... } の ... 部分を返す

    std::stringstream result;
    std::istringstream input(source);
    std::string line;
    bool in_target_namespace = false;
    int brace_depth = 0;

    // namespace X { パターン
    std::regex ns_start_regex(R"(^\s*namespace\s+(\w+)\s*\{)");

    while (std::getline(input, line)) {
        std::smatch match;

        if (!in_target_namespace && std::regex_search(line, match, ns_start_regex)) {
            if (match[1].str() == namespace_name) {
                in_target_namespace = true;
                brace_depth = 1;
                // 開き括弧の後の内容があれば追加
                size_t brace_pos = line.find('{');
                if (brace_pos != std::string::npos && brace_pos + 1 < line.length()) {
                    std::string after_brace = line.substr(brace_pos + 1);
                    if (!after_brace.empty() &&
                        after_brace.find_first_not_of(" \t\n\r") != std::string::npos) {
                        result << after_brace << "\n";
                    }
                }
                continue;
            }
        }

        if (in_target_namespace) {
            // 括弧の深さを追跡
            for (char c : line) {
                if (c == '{')
                    brace_depth++;
                else if (c == '}')
                    brace_depth--;
            }

            // 名前空間の終了を検出
            if (brace_depth == 0) {
                // 閉じ括弧の前の内容を追加
                size_t close_pos = line.find('}');
                if (close_pos > 0) {
                    std::string before_close = line.substr(0, close_pos);
                    if (!before_close.empty() &&
                        before_close.find_first_not_of(" \t\n\r") != std::string::npos) {
                        result << before_close << "\n";
                    }
                }
                break;
            } else {
                result << line << "\n";
            }
        }
    }

    return result.str();
}

}  // namespace cm::preprocessor
