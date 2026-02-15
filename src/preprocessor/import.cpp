#include "import.hpp"

#include <algorithm>
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

// 行頭の空白をスキップした位置を返す
static size_t skip_ws(const std::string& s, size_t pos = 0) {
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t'))
        pos++;
    return pos;
}

// 指定位置からキーワードが始まるか（キーワード後に非英数字 or 行末）
static bool starts_with_keyword(const std::string& s, size_t pos, const char* keyword) {
    size_t klen = std::strlen(keyword);
    if (pos + klen > s.size())
        return false;
    if (s.compare(pos, klen, keyword) != 0)
        return false;
    // キーワード後は非英数字 or 行末であること
    if (pos + klen < s.size()) {
        char next = s[pos + klen];
        if (std::isalnum(static_cast<unsigned char>(next)) || next == '_')
            return false;
    }
    return true;
}

// 行が空白の後に import or from で始まるかを高速チェック
static bool is_import_line(const std::string& line) {
    size_t pos = skip_ws(line);
    return starts_with_keyword(line, pos, "import") || starts_with_keyword(line, pos, "from");
}

// 行が空白の後に指定キーワードで始まるかチェック
static bool line_starts_with(const std::string& line, const char* keyword) {
    size_t pos = skip_ws(line);
    return starts_with_keyword(line, pos, keyword);
}

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

            // exportキーワードを削除
            module_source = remove_export_keywords(module_source);

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

std::filesystem::path ImportPreprocessor::find_module_file(
    const std::string& module_name, const std::filesystem::path& current_file) {
    // モジュール名をファイルパスに変換
    std::string filename = module_name;
    std::replace(filename.begin(), filename.end(), ':', '/');
    filename += ".cm";

    // まず相対パスでチェック（現在のファイルからの相対）
    if (!current_file.empty()) {
        auto relative_path = current_file.parent_path() / filename;
        if (std::filesystem::exists(relative_path)) {
            return relative_path;
        }
    }

    // 検索パスから探す
    for (const auto& search_path : search_paths) {
        auto full_path = search_path / filename;
        if (std::filesystem::exists(full_path)) {
            return full_path;
        }

        // mod.cm ファイルも試す
        auto mod_path = search_path / module_name / "mod.cm";
        if (std::filesystem::exists(mod_path)) {
            return mod_path;
        }
    }

    return {};  // 見つからない
}

std::string ImportPreprocessor::load_module_file(const std::filesystem::path& module_path) {
    std::ifstream file(module_path);
    if (!file) {
        throw std::runtime_error("Failed to open module file: " + module_path.string());
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string ImportPreprocessor::filter_exports(const std::string& module_source,
                                               const std::vector<std::string>& import_items) {
    // 選択的インポート：指定されたアイテムのみを抽出
    std::stringstream result;
    std::stringstream input(module_source);
    std::string line;
    bool in_wanted_block = false;    // 欲しいエクスポートブロック内
    bool in_unwanted_block = false;  // 不要なエクスポートブロック内
    std::string current_export_name;
    std::vector<std::string> block_lines;
    int brace_depth = 0;
    bool found_opening_brace = false;

    while (std::getline(input, line)) {
        // エクスポートされた関数/構造体/定数/implを検出（regexなし）
        bool matched = false;
        size_t pos = skip_ws(line);

        // impl パターンを先にチェック: [export] impl Type for Interface
        if (!in_wanted_block && !in_unwanted_block) {
            size_t impl_pos = pos;
            bool has_exp = starts_with_keyword(line, pos, "export");
            if (has_exp)
                impl_pos = skip_ws(line, pos + 6);

            if (starts_with_keyword(line, impl_pos, "impl")) {
                size_t after_impl = skip_ws(line, impl_pos + 4);
                size_t name_start = after_impl;
                while (after_impl < line.size() &&
                       (std::isalnum(static_cast<unsigned char>(line[after_impl])) ||
                        line[after_impl] == '_'))
                    after_impl++;
                if (after_impl > name_start) {
                    current_export_name = line.substr(name_start, after_impl - name_start);
                    matched = true;
                }
            }
        }

        // export キーワードで始まる場合
        if (!matched && !in_wanted_block && !in_unwanted_block &&
            starts_with_keyword(line, pos, "export")) {
            size_t after_export = skip_ws(line, pos + 6);

            // const パターン: export const type name =
            if (starts_with_keyword(line, after_export, "const")) {
                size_t after_const = skip_ws(line, after_export + 5);
                // 型名をスキップ
                while (after_const < line.size() &&
                       (std::isalnum(static_cast<unsigned char>(line[after_const])) ||
                        line[after_const] == '_'))
                    after_const++;
                after_const = skip_ws(line, after_const);
                // 名前を取得
                size_t name_start = after_const;
                while (after_const < line.size() &&
                       (std::isalnum(static_cast<unsigned char>(line[after_const])) ||
                        line[after_const] == '_'))
                    after_const++;
                if (after_const > name_start) {
                    current_export_name = line.substr(name_start, after_const - name_start);
                    matched = true;
                }
            }

            // 一般的な export パターン: export [修飾子...] type name
            if (!matched) {
                size_t p = after_export;
                // 修飾子をスキップ: extern "C", <T>, static, inline, async
                while (p < line.size()) {
                    if (starts_with_keyword(line, p, "extern")) {
                        p += 6;
                        p = skip_ws(line, p);
                        if (p < line.size() && line[p] == '"') {
                            auto close = line.find('"', p + 1);
                            if (close != std::string::npos)
                                p = close + 1;
                        }
                        p = skip_ws(line, p);
                    } else if (line[p] == '<') {
                        auto close = line.find('>', p);
                        if (close != std::string::npos)
                            p = close + 1;
                        p = skip_ws(line, p);
                    } else if (starts_with_keyword(line, p, "static") ||
                               starts_with_keyword(line, p, "inline") ||
                               starts_with_keyword(line, p, "async")) {
                        size_t kw_len = starts_with_keyword(line, p, "static")   ? 6
                                        : starts_with_keyword(line, p, "inline") ? 6
                                                                                 : 5;
                        p = skip_ws(line, p + kw_len);
                    } else {
                        break;
                    }
                }
                // 型名を取得
                size_t type_start = p;
                while (p < line.size() && (std::isalnum(static_cast<unsigned char>(line[p])) ||
                                           line[p] == '_' || line[p] == '*'))
                    p++;
                if (p > type_start) {
                    p = skip_ws(line, p);
                    // 名前を取得
                    size_t name_start = p;
                    while (p < line.size() &&
                           (std::isalnum(static_cast<unsigned char>(line[p])) || line[p] == '_'))
                        p++;
                    if (p > name_start) {
                        current_export_name = line.substr(name_start, p - name_start);
                        matched = true;
                    }
                }
            }
        }

        if (matched) {
            // 指定されたアイテムかチェック
            bool is_wanted = std::find(import_items.begin(), import_items.end(),
                                       current_export_name) != import_items.end();

            if (is_wanted) {
                in_wanted_block = true;
            } else {
                in_unwanted_block = true;
            }

            block_lines.clear();
            block_lines.push_back(line);
            brace_depth = 0;
            found_opening_brace = false;

            // 開き括弧をチェック
            if (line.find('{') != std::string::npos) {
                found_opening_brace = true;
                // 括弧の深さを正確にカウント
                for (char c : line) {
                    if (c == '{')
                        brace_depth++;
                    else if (c == '}')
                        brace_depth--;
                }
            }

            // 1行で完結する場合（セミコロンで終わる宣言）
            if (!found_opening_brace && line.find(';') != std::string::npos) {
                if (in_wanted_block) {
                    for (const auto& block_line : block_lines) {
                        result << block_line << "\n";
                    }
                }
                in_wanted_block = false;
                in_unwanted_block = false;
                block_lines.clear();
            }
            // 1行で完結する場合（括弧が閉じている）
            else if (found_opening_brace && brace_depth == 0) {
                if (in_wanted_block) {
                    for (const auto& block_line : block_lines) {
                        result << block_line << "\n";
                    }
                }
                in_wanted_block = false;
                in_unwanted_block = false;
                block_lines.clear();
            }
        } else if (in_wanted_block || in_unwanted_block) {
            // エクスポートブロック内
            block_lines.push_back(line);

            // まだ開き括弧を見つけていない場合
            if (!found_opening_brace && line.find('{') != std::string::npos) {
                found_opening_brace = true;
            }

            // 括弧の深さを追跡
            if (found_opening_brace) {
                for (char c : line) {
                    if (c == '{')
                        brace_depth++;
                    else if (c == '}')
                        brace_depth--;
                }

                // ブロックの終了を検出
                if (brace_depth == 0) {
                    // 欲しいブロックの場合のみ出力
                    if (in_wanted_block) {
                        for (const auto& block_line : block_lines) {
                            result << block_line << "\n";
                        }
                    }
                    in_wanted_block = false;
                    in_unwanted_block = false;
                    block_lines.clear();
                    found_opening_brace = false;
                }
            }
        } else if (line.find("export") == std::string::npos) {
            // エクスポートされていない行はそのまま保持（コメント、型定義など）
            result << line << "\n";
        }
    }

    return result.str();
}

std::string ImportPreprocessor::remove_export_keywords(const std::string& source) {
    // 先にexport構文を処理
    std::string processed = process_export_syntax(source);
    processed = process_namespace_exports(processed);

    // 階層再構築エクスポートを処理: export { ns::{item1, item2} }
    processed = process_hierarchical_reexport(processed);

    // Note: implの暗黙的エクスポートは現在無効化
    // パーサーが "export impl" をサポートしていないため
    // 将来的にはパーサーを修正してサポートする予定
    // processed = process_implicit_impl_export(processed);

    std::stringstream result;
    std::stringstream input(processed);
    std::string line;

    while (std::getline(input, line)) {
        // module 宣言を削除（namespace内で不要）
        if (line_starts_with(line, "module")) {
            // "module <name>;" パターンかチェック
            auto trimmed_line = line;
            while (!trimmed_line.empty() &&
                   (trimmed_line.back() == ' ' || trimmed_line.back() == '\t'))
                trimmed_line.pop_back();
            if (!trimmed_line.empty() && trimmed_line.back() == ';') {
                result << "// " << line << " (removed)\n";
                continue;
            }
        }

        // import 宣言もコメント化（既に処理済み）
        if (line_starts_with(line, "import")) {
            result << "// " << line << "\n";
            continue;
        }

        // ジェネリック関数の export <T> type name() から export を除去
        // パーサーは export <T> 構文を未サポートのため
        {
            size_t pos = skip_ws(line);
            if (starts_with_keyword(line, pos, "export")) {
                size_t after_export = pos + 6;
                size_t next = skip_ws(line, after_export);
                if (next < line.size() && line[next] == '<') {
                    // export <T> ... → <T> ...
                    line = line.substr(0, pos) + line.substr(next);
                }
            }
        }

        // Note: 通常のexportキーワードは保持する
        result << line << "\n";
    }

    return result.str();
}

std::string ImportPreprocessor::process_export_syntax(const std::string& source) {
    std::stringstream result;
    std::stringstream input(source);
    std::string line;
    std::vector<std::string> lines;

    // 全行を読み込む
    while (std::getline(input, line)) {
        lines.push_back(line);
    }

    // 定義を収集するためのマップ
    std::map<std::string, std::pair<int, std::string>>
        definitions;  // name -> (line_start, definition)
    std::set<std::string> exported_names;

    // Phase 1: 定義を収集
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string& cur_line = lines[i];
        size_t pos = skip_ws(cur_line);

        // use libc { ... } ブロックを検出
        if (starts_with_keyword(cur_line, pos, "use")) {
            size_t after_use = skip_ws(cur_line, pos + 3);
            if (starts_with_keyword(cur_line, after_use, "libc") &&
                cur_line.find('{') != std::string::npos) {
                // ブロック全体を収集
                std::string def = cur_line;
                int brace_count = 1;
                size_t start_i = i;

                // use libc ブロック内の関数名も収集
                std::vector<std::string> ffi_func_names;

                for (size_t j = i + 1; j < lines.size() && brace_count > 0; ++j) {
                    def += "\n" + lines[j];
                    for (char c : lines[j]) {
                        if (c == '{')
                            brace_count++;
                        else if (c == '}')
                            brace_count--;
                    }

                    // 関数宣言を検出: 空白 関数名(
                    {
                        size_t fpos = skip_ws(lines[j]);
                        // 戻り型をスキップ
                        while (fpos < lines[j].size() &&
                               (std::isalnum(static_cast<unsigned char>(lines[j][fpos])) ||
                                lines[j][fpos] == '_'))
                            fpos++;
                        fpos = skip_ws(lines[j], fpos);
                        // 関数名を取得
                        size_t fname_start = fpos;
                        while (fpos < lines[j].size() &&
                               (std::isalnum(static_cast<unsigned char>(lines[j][fpos])) ||
                                lines[j][fpos] == '_'))
                            fpos++;
                        if (fpos > fname_start && fpos < lines[j].size() && lines[j][fpos] == '(') {
                            ffi_func_names.push_back(
                                lines[j].substr(fname_start, fpos - fname_start));
                        }
                    }

                    if (brace_count == 0) {
                        i = j;
                        break;
                    }
                }

                // 各FFI関数を定義として登録
                for (const auto& func_name : ffi_func_names) {
                    definitions[func_name] = {static_cast<int>(start_i), def};
                }
                continue;
            }
        }

        // export があれば除去して検査
        bool has_export = starts_with_keyword(cur_line, pos, "export");
        size_t decl_pos = has_export ? skip_ws(cur_line, pos + 6) : pos;

        // 関数定義を検出: 型名 関数名(
        // 簡易チェック: 識別子 空白 識別子 ( のパターン
        {
            size_t p = decl_pos;
            // 型名をスキップ
            size_t type_start = p;
            while (p < cur_line.size() &&
                   (std::isalnum(static_cast<unsigned char>(cur_line[p])) || cur_line[p] == '_'))
                p++;
            if (p > type_start) {
                size_t after_type = skip_ws(cur_line, p);
                // 関数名を取得
                size_t fname_start = after_type;
                while (after_type < cur_line.size() &&
                       (std::isalnum(static_cast<unsigned char>(cur_line[after_type])) ||
                        cur_line[after_type] == '_'))
                    after_type++;
                if (after_type > fname_start && after_type < cur_line.size() &&
                    cur_line[after_type] == '(') {
                    std::string name = cur_line.substr(fname_start, after_type - fname_start);
                    std::string def = cur_line;
                    int brace_count = 0;
                    size_t j = i;

                    // 関数本体を収集
                    for (; j < lines.size(); ++j) {
                        if (j > i)
                            def += "\n" + lines[j];
                        for (char c : lines[j]) {
                            if (c == '{')
                                brace_count++;
                            else if (c == '}') {
                                brace_count--;
                                if (brace_count == 0)
                                    break;
                            }
                        }
                        if (brace_count == 0 && lines[j].find('}') != std::string::npos)
                            break;
                    }

                    definitions[name] = {static_cast<int>(i), def};
                    i = j;  // スキップ
                    continue;
                }
            }
        }

        // 構造体定義を検出: [export] struct Name {
        if (starts_with_keyword(cur_line, decl_pos, "struct")) {
            size_t after_struct = skip_ws(cur_line, decl_pos + 6);
            size_t sname_start = after_struct;
            while (after_struct < cur_line.size() &&
                   (std::isalnum(static_cast<unsigned char>(cur_line[after_struct])) ||
                    cur_line[after_struct] == '_'))
                after_struct++;
            if (after_struct > sname_start) {
                std::string name = cur_line.substr(sname_start, after_struct - sname_start);
                size_t brace_pos = cur_line.find('{', after_struct);
                if (brace_pos != std::string::npos) {
                    std::string def = cur_line;
                    int brace_count = 1;

                    // 構造体本体を収集
                    for (size_t j = i + 1; j < lines.size() && brace_count > 0; ++j) {
                        def += "\n" + lines[j];
                        for (char c : lines[j]) {
                            if (c == '{')
                                brace_count++;
                            else if (c == '}')
                                brace_count--;
                        }
                        if (brace_count == 0) {
                            i = j;
                            break;
                        }
                    }

                    definitions[name] = {static_cast<int>(i), def};
                }
            }
        }
    }

    // Phase 2: export { name1, name2, ... } および export { ns::{item1, item2} } を検出
    bool has_export_list = false;

    for (size_t i = 0; i < lines.size(); ++i) {
        // export { ... } パターンを文字列で検出
        size_t pos = skip_ws(lines[i]);
        if (!starts_with_keyword(lines[i], pos, "export"))
            continue;
        size_t after_export = skip_ws(lines[i], pos + 6);
        if (after_export >= lines[i].size() || lines[i][after_export] != '{')
            continue;

        size_t close_brace = lines[i].find('}', after_export + 1);
        if (close_brace == std::string::npos)
            continue;

        has_export_list = true;
        std::string names = lines[i].substr(after_export + 1, close_brace - after_export - 1);

        // 階層再構築パターンをチェック: ns::{item1, item2}
        size_t hier_pos = names.find("::{");
        if (hier_pos != std::string::npos) {
            // 名前空間名を抽出
            std::string before = names.substr(0, hier_pos);
            // 末尾のトリム
            size_t ns_end = before.find_last_not_of(" \t");
            // 先頭のトリム
            size_t ns_start = before.find_first_not_of(" \t");
            if (ns_start != std::string::npos && ns_end != std::string::npos) {
                std::string namespace_name = before.substr(ns_start, ns_end - ns_start + 1);
                size_t sub_close = names.find('}', hier_pos + 3);
                if (sub_close != std::string::npos) {
                    std::string sub_items = names.substr(hier_pos + 3, sub_close - hier_pos - 3);

                    // サブアイテムをパース
                    std::stringstream sub_ss(sub_items);
                    std::string sub_item;
                    while (std::getline(sub_ss, sub_item, ',')) {
                        sub_item.erase(0, sub_item.find_first_not_of(" \t\n\r"));
                        sub_item.erase(sub_item.find_last_not_of(" \t\n\r") + 1);
                        if (!sub_item.empty()) {
                            exported_names.insert(namespace_name + "::" + sub_item);
                        }
                    }
                }
            }
        } else {
            // 通常の名前リストをパース
            std::stringstream ss(names);
            std::string name;
            while (std::getline(ss, name, ',')) {
                name.erase(0, name.find_first_not_of(" \t\n\r"));
                name.erase(name.find_last_not_of(" \t\n\r") + 1);
                if (!name.empty()) {
                    exported_names.insert(name);
                }
            }
        }

        // export {...} 行をコメント化
        lines[i] = "// " + lines[i] + " (processed)";
    }

    // Phase 3: 出力を生成
    if (has_export_list) {
        // 名前列挙形式の場合、定義を再配置
        std::set<int> processed_lines;
        std::set<int> output_lines;  // 既に出力した行

        // エクスポートされた定義を先に出力
        for (const auto& name : exported_names) {
            if (definitions.count(name) > 0) {
                int line_num = definitions[name].first;
                // 同じ行番号の定義は一度だけ出力
                if (output_lines.count(line_num) == 0) {
                    result << definitions[name].second << "\n";
                    output_lines.insert(line_num);
                }
                processed_lines.insert(line_num);
            }
        }

        // その他の行を出力
        for (size_t i = 0; i < lines.size(); ++i) {
            if (processed_lines.count(i) == 0) {
                // 既に処理された定義はスキップ
                bool skip = false;
                for (const auto& [name, info] : definitions) {
                    if (info.first == static_cast<int>(i) && exported_names.count(name) > 0) {
                        skip = true;
                        break;
                    }
                }
                if (!skip) {
                    result << lines[i] << "\n";
                }
            }
        }
    } else {
        // 通常の形式はそのまま返す
        return source;
    }

    return result.str();
}

std::string ImportPreprocessor::process_namespace_exports(const std::string& source) {
    std::stringstream result;
    std::stringstream input(source);
    std::string line;
    bool in_namespace_export = false;
    std::string namespace_name;
    std::vector<std::string> namespace_content;
    int brace_depth = 0;

    while (std::getline(input, line)) {
        // export NS { ... } を検出（regexなし）
        if (!in_namespace_export) {
            size_t pos = skip_ws(line);
            bool matched_ns_export = false;
            if (starts_with_keyword(line, pos, "export")) {
                size_t after_export = skip_ws(line, pos + 6);
                // 名前を取得
                size_t name_start = after_export;
                while (after_export < line.size() &&
                       (std::isalnum(static_cast<unsigned char>(line[after_export])) ||
                        line[after_export] == '_'))
                    after_export++;
                if (after_export > name_start) {
                    size_t after_name = skip_ws(line, after_export);
                    if (after_name < line.size() && line[after_name] == '{') {
                        // サブ名前空間エクスポートの開始
                        namespace_name = line.substr(name_start, after_export - name_start);
                        in_namespace_export = true;
                        brace_depth = 1;
                        matched_ns_export = true;

                        // namespace宣言に変換
                        result << "namespace " << namespace_name << " {\n";

                        // 開き括弧の後の内容があればそれも処理
                        if (after_name + 1 < line.length()) {
                            std::string rest = line.substr(after_name + 1);
                            namespace_content.push_back(rest);
                        }
                    }
                }
            }
            if (!matched_ns_export) {
                // 通常の行
                result << line << "\n";
            }
        } else {
            // サブ名前空間内のコンテンツを収集
            for (char c : line) {
                if (c == '{')
                    brace_depth++;
                else if (c == '}') {
                    brace_depth--;
                    if (brace_depth == 0) {
                        // 名前空間の終了
                        in_namespace_export = false;

                        // 収集したコンテンツを出力
                        for (const auto& content_line : namespace_content) {
                            result << "    " << content_line << "\n";
                        }

                        // 閉じ括弧の前までを出力
                        size_t close_pos = line.find('}');
                        if (close_pos > 0) {
                            std::string before_close = line.substr(0, close_pos);
                            if (!before_close.empty()) {
                                result << "    " << before_close << "\n";
                            }
                        }

                        result << "} // namespace " << namespace_name << "\n";
                        namespace_content.clear();
                        break;
                    }
                }
            }

            if (in_namespace_export) {
                // まだ名前空間内
                namespace_content.push_back(line);
            }
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
        goto finalize;
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
                    goto finalize;
                }
            }
        }

        // ========== import * from module ==========
        if (rest.rfind("* from ", 0) == 0) {
            info.module_name = trim(rest.substr(7));
            info.is_wildcard = true;
            info.is_from_import = true;
            goto finalize;
        }

        // ========== import module as alias ==========
        {
            size_t as_pos = rest.find(" as ");
            if (as_pos != std::string::npos) {
                info.module_name = trim(rest.substr(0, as_pos));
                info.alias = trim(rest.substr(as_pos + 4));
                goto finalize;
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
                    std::string items_str = rest.substr(wildcard_sel + 5, close - wildcard_sel - 5);
                    parse_import_items(items_str, info);
                }
                goto finalize;
            }
        }

        // ========== import path/* ==========
        if (rest.size() >= 2 && rest.substr(rest.size() - 2) == "/*") {
            info.module_name = trim(rest.substr(0, rest.size() - 2));
            info.is_recursive_wildcard = true;
            info.is_wildcard = true;
            goto finalize;
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
                    goto finalize;
                }
            }
        }

        // ========== import module::* ==========
        if (rest.size() >= 3 && rest.substr(rest.size() - 3) == "::*") {
            info.module_name = trim(rest.substr(0, rest.size() - 3));
            info.is_wildcard = true;
            goto finalize;
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

finalize:
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

std::filesystem::path ImportPreprocessor::find_project_root(
    const std::filesystem::path& current_path) {
    auto path = std::filesystem::absolute(current_path);

    // プロジェクトルートの検出優先順位：
    // 1. cm.toml がある場所
    // 2. .git がある場所
    // 3. 環境変数 CM_PROJECT_ROOT
    // 4. 現在のディレクトリ

    // 上に向かって検索
    while (!path.empty() && path != path.parent_path()) {
        // cm.tomlを探す
        if (std::filesystem::exists(path / "cm.toml")) {
            return path;
        }
        // .gitディレクトリを探す
        if (std::filesystem::exists(path / ".git")) {
            return path;
        }
        path = path.parent_path();
    }

    // 環境変数をチェック
    if (const char* env_root = std::getenv("CM_PROJECT_ROOT")) {
        auto env_path = std::filesystem::path(env_root);
        if (std::filesystem::exists(env_path)) {
            return std::filesystem::absolute(env_path);
        }
    }

    // デフォルトは現在のディレクトリ
    return std::filesystem::current_path();
}

std::filesystem::path ImportPreprocessor::resolve_module_path(
    const std::string& module_specifier, const std::filesystem::path& current_file) {
    // 相対パス（./ または ../）の場合
    if (module_specifier.substr(0, 2) == "./" || module_specifier.substr(0, 3) == "../") {
        if (current_file.empty()) {
            throw std::runtime_error("Relative imports require a current file context");
        }

        auto base_dir = current_file.parent_path();

        // ./path/module::submodule 形式をチェック
        // パス部分と::サブモジュール部分を分離
        std::string path_part = module_specifier;
        size_t colon_pos = module_specifier.find("::");
        if (colon_pos != std::string::npos) {
            // ./path/module::sub の場合、./path/module がファイルパス
            path_part = module_specifier.substr(0, colon_pos);
        }

        auto relative_path = base_dir / path_part;

        // .cmファイルを試す
        auto cm_file = relative_path;
        cm_file += ".cm";
        if (std::filesystem::exists(cm_file)) {
            return std::filesystem::canonical(cm_file);
        }

        // ディレクトリ内のエントリーポイントを探す
        if (std::filesystem::exists(relative_path) &&
            std::filesystem::is_directory(relative_path)) {
            auto entry = find_module_entry_point(relative_path);
            if (!entry.empty()) {
                return entry;
            }
        }

        throw std::runtime_error("Module not found: " + module_specifier);
    }

    // 階層的インポート（std::io, lib::utils::strutil など）の場合
    // 階層パスを解決

    // :: で分割
    std::vector<std::string> segments;
    std::string current_segment;
    for (size_t i = 0; i < module_specifier.length(); ++i) {
        if (i + 1 < module_specifier.length() && module_specifier[i] == ':' &&
            module_specifier[i + 1] == ':') {
            if (!current_segment.empty()) {
                segments.push_back(current_segment);
                current_segment.clear();
            }
            ++i;  // skip second ':'
        } else {
            current_segment += module_specifier[i];
        }
    }
    if (!current_segment.empty()) {
        segments.push_back(current_segment);
    }

    // ファイル名を生成
    // segments が3つ以上の場合（例: std::mem::malloc）、
    // 最後の要素は関数/変数名として扱い、モジュールパスは最後の1つ手前まで
    std::string full_filename = module_specifier;
    std::replace(full_filename.begin(), full_filename.end(), ':', '/');

    // モジュールパス（最後のセグメントが小文字始まりの場合、それは関数/変数名）
    // ただし、完全パスに対応するファイルが存在する場合はモジュールとして扱う
    std::string module_path = full_filename;
    if (segments.size() >= 3) {
        // 最後のセグメントが小文字始まりなら、関数/変数名として扱う
        const std::string& last_segment = segments.back();
        if (!last_segment.empty() && std::islower(last_segment[0])) {
            // まずフルパスがモジュールファイルとして存在するかチェック
            // (例: std/sync/mutex.cm が存在するなら mutex はモジュール名)
            bool full_path_exists = false;
            if (!current_file.empty()) {
                auto check_path = current_file.parent_path() / (full_filename + ".cm");
                if (std::filesystem::exists(check_path)) {
                    full_path_exists = true;
                }
            }
            if (!full_path_exists) {
                for (const auto& sp : search_paths) {
                    auto check_path = sp / (full_filename + ".cm");
                    if (std::filesystem::exists(check_path)) {
                        full_path_exists = true;
                        break;
                    }
                }
            }

            if (full_path_exists) {
                // フルパスがファイルとして存在する → 最後のセグメントもモジュール名
                // module_path はそのまま full_filename を維持
                if (debug_mode) {
                    std::cout << "[PREPROCESSOR] Full path exists as module file, keeping: "
                              << module_path << "\n";
                }
            } else {
                // フルパスが存在しない → 最後のセグメントは関数/変数名
                module_path = "";
                for (size_t i = 0; i < segments.size() - 1; ++i) {
                    if (i > 0)
                        module_path += "/";
                    module_path += segments[i];
                }
                if (debug_mode) {
                    std::cout << "[PREPROCESSOR] Selective import detected, module path: "
                              << module_path << "\n";
                }
            }
        }
    }

    // 最初のコンポーネントだけのファイル名
    std::string root_filename = segments.empty() ? module_specifier : segments[0];

    // まず現在のファイルと同じディレクトリをチェック
    if (!current_file.empty()) {
        auto current_dir = current_file.parent_path();

        // セグメントが3つ以上（選択的インポート）の場合、module_pathを優先
        if (segments.size() >= 3 && module_path != full_filename) {
            // 1. モジュールパス（std/mem.cm など）を試す
            auto mod_file_path = current_dir / (module_path + ".cm");
            if (std::filesystem::exists(mod_file_path)) {
                if (debug_mode) {
                    std::cout << "[PREPROCESSOR] Found module file: " << mod_file_path << "\n";
                }
                return std::filesystem::canonical(mod_file_path);
            }

            // 2. ディレクトリ内のエントリーポイント（std/mem/mod.cm など）
            auto mod_dir_path = current_dir / module_path;
            if (std::filesystem::exists(mod_dir_path) &&
                std::filesystem::is_directory(mod_dir_path)) {
                auto entry = find_module_entry_point(mod_dir_path);
                if (!entry.empty()) {
                    if (debug_mode) {
                        std::cout << "[PREPROCESSOR] Found module entry point: " << entry << "\n";
                    }
                    return entry;
                }
            }

            // サブモジュールが見つからない場合、ルートへのフォールバックは行わない
            // (import std::nonexistent::foo が std/mod.cm に解決されるのを防ぐ)
            // 検索パスも試行する（下の for ループに委ねる）
        } else {
            // 非選択的インポート: ルートフォールバックを通常通り試行

            // 1. 完全パス（std/io/file.cm など）を最初に試す
            //    サブモジュールへの直接アクセスを優先
            auto full_path = current_dir / (full_filename + ".cm");
            if (std::filesystem::exists(full_path)) {
                if (debug_mode) {
                    std::cout << "[PREPROCESSOR] Found full path module: " << full_path << "\n";
                }
                return std::filesystem::canonical(full_path);
            }

            // 2. ルートコンポーネントのファイル（std.cm）を試す
            //    これは再エクスポートベースの解決に必要
            auto root_path = current_dir / (root_filename + ".cm");
            if (std::filesystem::exists(root_path)) {
                if (debug_mode) {
                    std::cout << "[PREPROCESSOR] Found root module: " << root_path << "\n";
                }
                return std::filesystem::canonical(root_path);
            }

            // 3. ルートディレクトリ内のエントリーポイント（std/std.cm）
            //    ただし2セグメント以上の場合、サブモジュールが実在するか検証
            auto root_dir_path = current_dir / root_filename;
            if (std::filesystem::exists(root_dir_path) &&
                std::filesystem::is_directory(root_dir_path)) {
                // サブモジュール存在チェック: std::io → std/io.cm or std/io/ が必要
                bool submodule_valid = true;
                if (segments.size() >= 2) {
                    auto sub_file = root_dir_path / (segments[1] + ".cm");
                    auto sub_dir = root_dir_path / segments[1];
                    if (!std::filesystem::exists(sub_file) &&
                        !(std::filesystem::exists(sub_dir) &&
                          std::filesystem::is_directory(sub_dir))) {
                        submodule_valid = false;
                        if (debug_mode) {
                            std::cout << "[PREPROCESSOR] Submodule '" << segments[1]
                                      << "' not found in " << root_dir_path << "\n";
                        }
                    }
                }
                if (submodule_valid) {
                    auto entry = find_module_entry_point(root_dir_path);
                    if (!entry.empty()) {
                        return entry;
                    }
                }
            }

            // 4. ディレクトリ内のエントリーポイント（std/io/io.cm など）
            auto dir_path = current_dir / full_filename;
            if (std::filesystem::exists(dir_path) && std::filesystem::is_directory(dir_path)) {
                auto entry = find_module_entry_point(dir_path);
                if (!entry.empty()) {
                    return entry;
                }
            }
        }
    }

    // 検索パスから探す
    for (const auto& search_path : search_paths) {
        // 選択的インポート（3セグメント以上）の場合、module_pathを優先
        if (segments.size() >= 3 && module_path != full_filename) {
            // 1. モジュールパス（std/nonexistent.cm など）を試す
            auto mod_file_path = search_path / (module_path + ".cm");
            if (std::filesystem::exists(mod_file_path)) {
                if (debug_mode) {
                    std::cout << "[PREPROCESSOR] Found module file in search path: "
                              << mod_file_path << "\n";
                }
                return std::filesystem::canonical(mod_file_path);
            }

            // 2. ディレクトリ内のエントリーポイント（std/mem/mod.cm など）
            auto mod_dir_path = search_path / module_path;
            if (std::filesystem::exists(mod_dir_path) &&
                std::filesystem::is_directory(mod_dir_path)) {
                auto entry = find_module_entry_point(mod_dir_path);
                if (!entry.empty()) {
                    if (debug_mode) {
                        std::cout << "[PREPROCESSOR] Found module entry point in search path: "
                                  << entry << "\n";
                    }
                    return entry;
                }
            }

            // サブモジュールが見つからない場合、ルートへのフォールバックは行わない
            continue;
        }

        // 1. 完全パスを最優先で試す (std/io/file.cm など)
        auto full_path = search_path / (full_filename + ".cm");
        if (std::filesystem::exists(full_path)) {
            if (debug_mode) {
                std::cout << "[PREPROCESSOR] Found full path in search path: " << full_path << "\n";
            }
            return std::filesystem::canonical(full_path);
        }

        // 2. ディレクトリ内のエントリーポイントを探す (std/io/file/mod.cm)
        auto dir_path = search_path / full_filename;
        if (std::filesystem::exists(dir_path) && std::filesystem::is_directory(dir_path)) {
            auto entry = find_module_entry_point(dir_path);
            if (!entry.empty()) {
                if (debug_mode) {
                    std::cout << "[PREPROCESSOR] Found module entry point in search path: " << entry
                              << "\n";
                }
                return entry;
            }
        }

        // 3. ルートコンポーネントを試す（検索パスでは後ろ側に）
        auto root_path = search_path / (root_filename + ".cm");
        if (std::filesystem::exists(root_path)) {
            return std::filesystem::canonical(root_path);
        }

        // 4. ルートディレクトリ内のエントリーポイント
        //    ただし2セグメント以上の場合、サブモジュールが実在するか検証
        auto root_dir_path = search_path / root_filename;
        if (std::filesystem::exists(root_dir_path) &&
            std::filesystem::is_directory(root_dir_path)) {
            bool submodule_valid = true;
            if (segments.size() >= 2) {
                auto sub_file = root_dir_path / (segments[1] + ".cm");
                auto sub_dir = root_dir_path / segments[1];
                if (!std::filesystem::exists(sub_file) &&
                    !(std::filesystem::exists(sub_dir) && std::filesystem::is_directory(sub_dir))) {
                    submodule_valid = false;
                }
            }
            if (submodule_valid) {
                auto entry = find_module_entry_point(root_dir_path);
                if (!entry.empty()) {
                    return entry;
                }
            }
        }
    }

    return {};  // 見つからない
}

std::filesystem::path ImportPreprocessor::find_module_entry_point(
    const std::filesystem::path& directory) {
    // module文を含むファイルを探す
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().extension() == ".cm") {
            std::ifstream file(entry.path());
            std::string line;
            // 最初の数行だけチェック
            int line_count = 0;
            while (std::getline(file, line) && line_count++ < 10) {
                // コメントをスキップ
                if (line.find("//") == 0)
                    continue;

                // module文を検出
                std::regex module_regex(R"(^\s*module\s+([a-zA-Z_][a-zA-Z0-9_:]*)\s*;)");
                if (std::regex_search(line, module_regex)) {
                    return entry.path();
                }
            }
        }
    }

    // module文が見つからない場合、ディレクトリ名と同じ名前の.cmファイルを探す
    auto dir_name = directory.filename().string();
    auto same_name_path = directory / (dir_name + ".cm");
    if (std::filesystem::exists(same_name_path)) {
        return same_name_path;
    }

    // mod.cmを探す（後方互換性）
    auto mod_path = directory / "mod.cm";
    if (std::filesystem::exists(mod_path)) {
        return mod_path;
    }

    return {};  // エントリーポイントが見つからない
}

std::string ImportPreprocessor::format_circular_dependency_error(
    const std::vector<std::string>& stack, const std::string& module) {
    std::stringstream error;
    error << "Circular dependency detected:\n";
    for (size_t i = 0; i < stack.size(); ++i) {
        error << "  " << (i + 1) << ". " << stack[i] << "\n";
    }
    error << "  " << (stack.size() + 1) << ". " << module << " (circular reference)\n";
    return error.str();
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

std::vector<std::string> ImportPreprocessor::extract_reexports(const std::string& module_source) {
    // export { M }; または export { M, N, ... }; 形式を検出
    std::vector<std::string> reexports;
    std::regex export_regex(R"(^\s*export\s*\{([^}]+)\}\s*;)");
    std::istringstream input(module_source);
    std::string line;

    while (std::getline(input, line)) {
        std::smatch match;
        if (std::regex_match(line, match, export_regex)) {
            // カンマで分割
            std::string items = match[1].str();
            std::stringstream ss(items);
            std::string item;
            while (std::getline(ss, item, ',')) {
                // 前後の空白を削除
                item.erase(0, item.find_first_not_of(" \t\n\r"));
                item.erase(item.find_last_not_of(" \t\n\r") + 1);
                if (!item.empty()) {
                    reexports.push_back(item);
                }
            }
        }
    }

    return reexports;
}

std::vector<std::filesystem::path> ImportPreprocessor::find_all_modules_recursive(
    const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> modules;

    if (!std::filesystem::exists(directory) || !std::filesystem::is_directory(directory)) {
        return modules;
    }

    // 再帰的にディレクトリを探索
    // .cmファイルはすべてモジュールとして扱う
    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".cm") {
            modules.push_back(entry.path());
        }
    }

    // ソートして一貫した順序を保証
    std::sort(modules.begin(), modules.end());

    return modules;
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

std::string ImportPreprocessor::process_implicit_impl_export(const std::string& source) {
    // exportされた構造体を検出
    std::set<std::string> exported_structs;

    std::regex export_struct_regex(R"(export\s+struct\s+(\w+))");
    std::smatch match;
    std::string::const_iterator search_start = source.cbegin();
    while (std::regex_search(search_start, source.cend(), match, export_struct_regex)) {
        exported_structs.insert(match[1].str());
        search_start = match.suffix().first;
    }

    if (exported_structs.empty()) {
        return source;  // エクスポートされた構造体がない場合はそのまま返す
    }

    // implを検出して、エクスポートされた構造体のimplにexportを追加
    std::stringstream result;
    std::istringstream input(source);
    std::string line;

    // impl Type for Interface パターン
    std::regex impl_regex(R"(^(\s*)impl\s+(\w+)\s+for\s+(\w+))");
    // impl Type パターン（コンストラクタ用）
    std::regex impl_ctor_regex(R"(^(\s*)impl\s+(\w+)\s*\{)");

    while (std::getline(input, line)) {
        std::smatch impl_match;

        // impl Type for Interface をチェック
        if (std::regex_search(line, impl_match, impl_regex)) {
            std::string type_name = impl_match[2].str();
            // エクスポートされた構造体のimplの場合、exportキーワードを追加
            // （まだexportキーワードがない場合のみ）
            if (exported_structs.count(type_name) > 0 && line.find("export") == std::string::npos) {
                std::string indent = impl_match[1].str();
                line = indent + "export " + line.substr(indent.length());
            }
        }
        // impl Type { (コンストラクタ用) をチェック
        else if (std::regex_search(line, impl_match, impl_ctor_regex)) {
            std::string type_name = impl_match[2].str();
            if (exported_structs.count(type_name) > 0 && line.find("export") == std::string::npos) {
                std::string indent = impl_match[1].str();
                line = indent + "export " + line.substr(indent.length());
            }
        }

        result << line << "\n";
    }

    return result.str();
}

std::string ImportPreprocessor::process_hierarchical_reexport(const std::string& source) {
    // export { ns::{item1, item2} } パターンを検出
    // 例: export { io::{file, stream} }
    // これは、fileとstreamの名前空間をio名前空間内に移動する

    // コメント化された形式: // export { io::{file, stream} }; (processed)
    std::regex hier_export_regex(R"(//\s*export\s*\{\s*(\w+)::\{([^}]+)\}\s*\};\s*\(processed\))");
    std::smatch match;

    if (!std::regex_search(source, match, hier_export_regex)) {
        return source;  // 階層再構築パターンがない場合はそのまま返す
    }

    std::string parent_ns = match[1].str();  // 例: "io"
    std::string items_str = match[2].str();  // 例: "file, stream"

    // アイテムをパース
    std::set<std::string> items_to_move;
    std::stringstream items_ss(items_str);
    std::string item;
    while (std::getline(items_ss, item, ',')) {
        // トリム
        item.erase(0, item.find_first_not_of(" \t\n\r"));
        item.erase(item.find_last_not_of(" \t\n\r") + 1);
        if (!item.empty()) {
            items_to_move.insert(item);
        }
    }

    if (items_to_move.empty()) {
        return source;
    }

    // 各アイテムの名前空間を抽出して、親名前空間内に配置
    std::stringstream result;
    std::istringstream input(source);
    std::string line;

    // 移動する名前空間の内容を収集
    std::map<std::string, std::string> namespace_contents;
    std::string current_ns;
    std::stringstream current_content;
    int brace_depth = 0;
    bool in_target_ns = false;

    // Pass 1: 対象の名前空間を収集
    while (std::getline(input, line)) {
        std::regex ns_start_regex(R"(^\s*namespace\s+(\w+)\s*\{)");
        std::smatch ns_match;

        if (!in_target_ns && std::regex_search(line, ns_match, ns_start_regex)) {
            std::string ns_name = ns_match[1].str();
            if (items_to_move.count(ns_name) > 0) {
                current_ns = ns_name;
                in_target_ns = true;
                brace_depth = 1;
                current_content.str("");
                current_content.clear();
                continue;  // namespace行自体はスキップ
            }
        }

        if (in_target_ns) {
            for (char c : line) {
                if (c == '{')
                    brace_depth++;
                else if (c == '}')
                    brace_depth--;
            }

            if (brace_depth == 0) {
                // 名前空間の終了（閉じ括弧の行は含めない）
                namespace_contents[current_ns] = current_content.str();
                in_target_ns = false;
                current_ns.clear();
            } else {
                current_content << line << "\n";
            }
        }
    }

    // Pass 2: 出力を生成
    input.clear();
    input.str(source);
    in_target_ns = false;
    brace_depth = 0;
    // bool parent_ns_opened = false;
    bool items_inserted = false;

    while (std::getline(input, line)) {
        std::regex ns_start_regex(R"(^\s*namespace\s+(\w+)\s*\{)");
        std::smatch ns_match;

        // 対象の名前空間をスキップ
        if (!in_target_ns && std::regex_search(line, ns_match, ns_start_regex)) {
            std::string ns_name = ns_match[1].str();
            if (items_to_move.count(ns_name) > 0) {
                in_target_ns = true;
                brace_depth = 1;
                current_ns = ns_name;
                continue;
            }
        }

        if (in_target_ns) {
            for (char c : line) {
                if (c == '{')
                    brace_depth++;
                else if (c == '}')
                    brace_depth--;
            }

            if (brace_depth == 0) {
                in_target_ns = false;
                current_ns.clear();
            }
            continue;  // 対象の名前空間は出力しない
        }

        // export コメントの位置で親名前空間と内容を挿入
        if (!items_inserted && line.find("(processed)") != std::string::npos &&
            line.find(parent_ns + "::") != std::string::npos) {
            result << "namespace " << parent_ns << " {\n";
            for (const auto& [ns_name, content] : namespace_contents) {
                result << "namespace " << ns_name << " {\n";
                result << content;
                result << "} // namespace " << ns_name << "\n";
            }
            result << "} // namespace " << parent_ns << "\n";
            result << "// " << line << "\n";  // 元のコメントもコメントとして保持
            items_inserted = true;
            continue;
        }

        result << line << "\n";
    }

    return result.str();
}

}  // namespace cm::preprocessor

// exportされたブロック（関数・struct・const等）をモジュールソースから抽出する
// namespace外へのforward展開用: namespaceラップされたモジュールのexportシンボルを
// namespace外にも出力して、名前空間修飾なしで呼び出し可能にする
std::string cm::preprocessor::ImportPreprocessor::extract_exported_blocks(
    const std::string& module_source) {
    std::stringstream result;
    std::stringstream input(module_source);
    std::string line;
    bool in_export_block = false;
    bool in_sub_exported_section = false;
    std::vector<std::string> block_lines;
    int brace_depth = 0;
    bool found_opening_brace = false;

    while (std::getline(input, line)) {
        // サブモジュールのExported symbolsセクションを検出してパススルー
        // これにより推移的なエクスポートが可能になる
        // （モジュールAがBをimport、BがCをimport → AからCのexport関数を呼べる）
        if (!in_export_block && !in_sub_exported_section &&
            line.find("// ===== Exported symbols from ") != std::string::npos) {
            in_sub_exported_section = true;
            continue;  // セクション開始コメントはスキップ
        }
        if (in_sub_exported_section) {
            if (line.find("// ===== End exported symbols =====") != std::string::npos) {
                in_sub_exported_section = false;
                continue;  // セクション終了コメントはスキップ
            }
            // サブモジュールのexported関数をそのまま出力
            result << line << "\n";
            continue;
        }

        // module宣言やimport文はスキップ
        if (line.find("module ") != std::string::npos && line.find(';') != std::string::npos) {
            std::regex module_regex(R"(^\s*/?\s*module\s+[\w:.]+\s*;\s*$)");
            if (std::regex_match(line, module_regex)) {
                continue;
            }
        }
        if (line.find("import ") != std::string::npos) {
            std::regex import_regex(R"(^\s*import\s+)");
            if (std::regex_search(line, import_regex)) {
                continue;
            }
        }

        // exportで始まる行を検出（シンプルなパターン）
        if (!in_export_block && line.find("export ") != std::string::npos) {
            // 行がexportで始まるか確認（先頭空白は許容）
            std::regex export_start(R"(^\s*export\s+)");
            if (std::regex_search(line, export_start)) {
                // 再エクスポート構文（export { ... }）はスキップ
                // export NAME1, NAME2; 形式のリストエクスポートもスキップ
                std::regex reexport_regex(R"(^\s*export\s*\{)");
                std::regex list_export_regex(R"(^\s*export\s+\w+\s*,)");
                std::regex name_only_export_regex(R"(^\s*export\s+\w+\s*;)");
                if (std::regex_search(line, reexport_regex) ||
                    std::regex_search(line, list_export_regex) ||
                    std::regex_search(line, name_only_export_regex)) {
                    continue;
                }
                in_export_block = true;
                block_lines.clear();
                block_lines.push_back(line);
                brace_depth = 0;
                found_opening_brace = false;

                // 開き括弧をチェック
                for (char c : line) {
                    if (c == '{') {
                        found_opening_brace = true;
                        brace_depth++;
                    } else if (c == '}') {
                        brace_depth--;
                    }
                }

                // 1行で完結する場合（セミコロンで終わる宣言、括弧なし）
                if (!found_opening_brace && line.find(';') != std::string::npos) {
                    for (auto& bl : block_lines) {
                        std::regex rm_export(R"(\bexport\s+)");
                        result << std::regex_replace(bl, rm_export, "") << "\n";
                    }
                    in_export_block = false;
                    block_lines.clear();
                }
                // 1行で括弧が閉じている場合
                else if (found_opening_brace && brace_depth == 0) {
                    for (auto& bl : block_lines) {
                        std::regex rm_export(R"(\bexport\s+)");
                        result << std::regex_replace(bl, rm_export, "") << "\n";
                    }
                    in_export_block = false;
                    block_lines.clear();
                }

                continue;
            }
        }

        if (in_export_block) {
            block_lines.push_back(line);

            if (!found_opening_brace && line.find('{') != std::string::npos) {
                found_opening_brace = true;
            }

            for (char c : line) {
                if (c == '{')
                    brace_depth++;
                else if (c == '}')
                    brace_depth--;
            }

            if (found_opening_brace && brace_depth == 0) {
                // exportキーワードを除去して出力
                for (auto& bl : block_lines) {
                    std::regex rm_export(R"(\bexport\s+)");
                    result << std::regex_replace(bl, rm_export, "") << "\n";
                }
                in_export_block = false;
                block_lines.clear();
                found_opening_brace = false;
            }
        }
        // exportされていない行はスキップ（namespace外にはexportのみ展開）
    }

    return result.str();
}
