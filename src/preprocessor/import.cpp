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
    // 注: std::io は std/io に変換されるため、exe_dir 自体を追加
    auto exe_dir = get_executable_directory();
    if (!exe_dir.empty() && std::filesystem::exists(exe_dir / "std")) {
        search_paths.push_back(exe_dir);
    }

    // 5. プロジェクトルート（project_root/std/io を探すため）
    if (std::filesystem::exists(project_root / "std")) {
        search_paths.push_back(project_root);
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
        std::regex import_regex(R"(^\s*(import|from)\s+.+)");
        std::smatch match;

        if (debug_mode) {
            std::cout << "[PREPROCESSOR] Processing line: " << line << "\n";
        }

        if (std::regex_search(line, match, import_regex)) {
            if (debug_mode) {
                std::cout << "[PREPROCESSOR] Matched import regex: " << match[0] << "\n";
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

            if (module_cache.count(canonical_path) > 0) {
                module_source = module_cache[canonical_path];
            } else {
                // モジュールファイルを読み込む
                module_source = load_module_file(module_path);

                // モジュール内のインポートを再帰的に処理（ダミーソースマップを使用）
                module_source =
                    process_imports(module_source, module_path, imported_files, dummy_source_map,
                                    dummy_module_ranges, module_chain, line_number);

                // キャッシュに保存
                module_cache[canonical_path] = module_source;
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
        // エクスポートされた関数/構造体/定数/implを検出
        std::smatch match;
        // 定数: export const type name = ...
        std::regex const_export_regex(
            R"(export\s+const\s+(?:int|float|double|bool|char|string|uint)\s+(\w+)\s*=)");
        // impl: export impl Type for Interface (implはexportが付かないこともある)
        std::regex impl_regex(R"(^\s*(?:export\s+)?impl\s+(\w+)\s+for\s+(\w+))");
        // 関数/構造体など: export (type|struct|interface|enum) name
        // typeは組み込み型またはユーザー定義型（識別子）
        std::regex export_regex(
            R"(export\s+(?:int|void|float|double|bool|char|string|uint|struct|interface|enum|\w+\*?)\s+(\w+))");

        bool matched = false;

        // まず定数パターンを試す
        if (!in_wanted_block && !in_unwanted_block &&
            std::regex_search(line, match, const_export_regex)) {
            current_export_name = match[1];
            matched = true;
        }
        // implパターンを試す (Type for Interface) - exportの有無に関わらず
        else if (!in_wanted_block && !in_unwanted_block &&
                 std::regex_search(line, match, impl_regex)) {
            // impl TypeはインポートリストのTypeに一致
            current_export_name = match[1];
            matched = true;
        }
        // 次に通常のパターンを試す
        else if (!in_wanted_block && !in_unwanted_block &&
                 std::regex_search(line, match, export_regex)) {
            current_export_name = match[1];
            matched = true;
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
        // module名はワードキャラクター、コロン、ドットを含む
        std::regex module_regex(R"(^\s*module\s+[\w:.]+\s*;\s*$)");
        if (std::regex_match(line, module_regex)) {
            result << "// " << line << " (removed)\n";
            continue;
        }

        // import 宣言もコメント化（既に処理済み）
        std::regex import_regex(R"(^\s*import\s+)");
        if (std::regex_search(line, import_regex)) {
            result << "// " << line << "\n";
            continue;
        }

        // Note: exportキーワードは保持する
        // 以前はエクスポートされた宣言からexportを削除していたが、
        // これにより関数定義がパーサーで変数宣言として誤認識される問題があった
        // パーサーはexportキーワードを適切に処理するため、削除不要

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
        const std::string& line = lines[i];

        // use libc { ... } ブロックを検出
        std::regex use_libc_regex(R"(^\s*use\s+libc\s*(?:as\s+\w+\s*)?\{)");
        std::smatch match;
        if (std::regex_search(line, match, use_libc_regex)) {
            // ブロック全体を収集
            std::string def = line;
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

                // 関数宣言を検出: 戻り値型 関数名(...)
                std::regex ffi_func_regex(R"(\s+(\w+)\s*\()");
                std::smatch func_match;
                if (std::regex_search(lines[j], func_match, ffi_func_regex)) {
                    ffi_func_names.push_back(func_match[1].str());
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

        // 関数定義を検出
        std::regex func_regex(
            R"(^\s*(?:export\s+)?(?:int|float|double|void|bool|char)\s+(\w+)\s*\()");
        if (std::regex_search(line, match, func_regex)) {
            std::string name = match[1];
            std::string def = line;
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
        }

        // 構造体定義を検出
        std::regex struct_regex(R"(^\s*(?:export\s+)?struct\s+(\w+)\s*\{)");
        if (std::regex_search(line, match, struct_regex)) {
            std::string name = match[1];
            std::string def = line;
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

    // Phase 2: export { name1, name2, ... } および export { ns::{item1, item2} } を検出
    std::regex export_list_regex(R"(^\s*export\s*\{([^}]+)\})");
    // 階層再構築パターン: ns::{item1, item2}
    std::regex hierarchical_regex(R"((\w+)::\{([^}]+)\})");
    bool has_export_list = false;

    for (size_t i = 0; i < lines.size(); ++i) {
        std::smatch match;
        if (std::regex_search(lines[i], match, export_list_regex)) {
            has_export_list = true;
            std::string names = match[1];

            // 階層再構築パターンをチェック: io::{file, stream}
            std::smatch hier_match;
            if (std::regex_search(names, hier_match, hierarchical_regex)) {
                std::string namespace_name = hier_match[1];
                std::string sub_items = hier_match[2];

                // サブアイテムをパース
                std::stringstream sub_ss(sub_items);
                std::string sub_item;
                while (std::getline(sub_ss, sub_item, ',')) {
                    // トリム
                    sub_item.erase(0, sub_item.find_first_not_of(" \t\n\r"));
                    sub_item.erase(sub_item.find_last_not_of(" \t\n\r") + 1);
                    if (!sub_item.empty()) {
                        // 階層形式: namespace::item として記録
                        exported_names.insert(namespace_name + "::" + sub_item);
                    }
                }
            } else {
                // 通常の名前リストをパース
                std::stringstream ss(names);
                std::string name;
                while (std::getline(ss, name, ',')) {
                    // トリム
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
        // export NS { ... } を検出
        std::regex ns_export_regex(R"(^\s*export\s+(\w+)\s*\{)");
        std::smatch match;

        if (!in_namespace_export && std::regex_search(line, match, ns_export_regex)) {
            // サブ名前空間エクスポートの開始
            namespace_name = match[1];
            in_namespace_export = true;
            brace_depth = 1;

            // namespace宣言に変換
            result << "namespace " << namespace_name << " {\n";

            // 開き括弧の後の内容があればそれも処理
            size_t brace_pos = line.find('{');
            if (brace_pos != std::string::npos && brace_pos + 1 < line.length()) {
                std::string rest = line.substr(brace_pos + 1);
                namespace_content.push_back(rest);
            }
        } else if (in_namespace_export) {
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
        } else {
            // 通常の行
            result << line << "\n";
        }
    }

    return result.str();
}

ImportPreprocessor::ImportInfo ImportPreprocessor::parse_import_statement(
    const std::string& import_line) {
    ImportInfo info;

    // セミコロンを削除
    std::string line = import_line;
    if (line.back() == ';') {
        line.pop_back();
    }

    // 相対パスチェック
    if (line.find("./") != std::string::npos || line.find("../") != std::string::npos) {
        info.is_relative = true;
    }

    // パターン1: import module as alias
    std::regex import_as(R"(^\s*import\s+([^\s]+)\s+as\s+(\w+)\s*$)");

    // パターン2: import { items } from module
    std::regex from_import(R"(^\s*import\s+\{([^}]+)\}\s+from\s+([^\s]+)\s*$)");

    // パターン3: from module import { items }
    std::regex from_import_alt(R"(^\s*from\s+([^\s]+)\s+import\s+\{([^}]+)\}\s*$)");

    // パターン4: import module::*  (ワイルドカード)
    std::regex wildcard_import(R"(^\s*import\s+([^\s]+)::\*\s*$)");

    // パターン5: import * from module
    std::regex wildcard_from(R"(^\s*import\s+\*\s+from\s+([^\s]+)\s*$)");

    // パターン6: import module::{items}  (選択的)
    std::regex selective_import(R"(^\s*import\s+([^\s]+)::\{([^}]+)\}\s*$)");

    // パターン7: import ./path/* (ワイルドカード - ディレクトリ内全モジュール)
    std::regex wildcard_dir_import(R"(^\s*import\s+([^\s]+)/\*\s*$)");

    // パターン8: import module  (シンプル)
    std::regex simple_import(R"(^\s*import\s+([^\s]+)\s*$)");

    std::smatch match;

    // import module as alias
    if (std::regex_match(line, match, import_as)) {
        info.module_name = match[1];
        info.alias = match[2];
    }
    // import { items } from module
    else if (std::regex_match(line, match, from_import)) {
        info.module_name = match[2];
        info.is_from_import = true;
        // アイテムをパース
        std::string items_str = match[1];
        parse_import_items(items_str, info);
    }
    // from module import { items }
    else if (std::regex_match(line, match, from_import_alt)) {
        info.module_name = match[1];
        info.is_from_import = true;
        // アイテムをパース
        std::string items_str = match[2];
        parse_import_items(items_str, info);
    }
    // import * from module
    else if (std::regex_match(line, match, wildcard_from)) {
        info.module_name = match[1];
        info.is_wildcard = true;
        info.is_from_import = true;
    }
    // import ./path/* (ワイルドカード)
    else if (std::regex_match(line, match, wildcard_dir_import)) {
        info.module_name = match[1];
        info.is_recursive_wildcard = true;
        info.is_wildcard = true;
    }
    // import module::*
    else if (std::regex_match(line, match, wildcard_import)) {
        info.module_name = match[1];
        info.is_wildcard = true;
    }
    // import module::{items}
    else if (std::regex_match(line, match, selective_import)) {
        info.module_name = match[1];
        // アイテムをパース
        std::string items_str = match[2];
        parse_import_items(items_str, info);
    }
    // import module (シンプル)
    else if (std::regex_match(line, match, simple_import)) {
        info.module_name = match[1];

        // ./path/module::submodule::item 形式をチェック
        // 最後が ::identifier (小文字始まり = 関数/変数) の場合、それをアイテムとして扱う
        std::string& name = info.module_name;
        // 最後の :: の位置を見つける
        size_t last_colon = name.rfind("::");
        if (last_colon != std::string::npos && last_colon > 0) {
            std::string last_part = name.substr(last_colon + 2);
            // * の場合はワイルドカード
            if (last_part == "*") {
                info.is_wildcard = true;
                info.module_name = name.substr(0, last_colon);
            }
            // 小文字始まりの場合、関数/変数として扱う
            // ただし、相対パスで :: が1つだけの場合はサブモジュール全体として扱う
            // 例: ./path/module::submodule → サブモジュール全体
            // 例: ./path/module::submodule::func → funcを選択的インポート
            // 例: std::io::println → printlnを選択的インポート
            else if (!last_part.empty() && std::islower(last_part[0])) {
                // 相対パスの場合、::が2つ以上あるかチェック
                // ./path/module::sub::func のように、funcは関数として扱う
                size_t first_colon = name.find("::");
                if (!info.is_relative || first_colon != last_colon) {
                    // 絶対パス、または相対パスで::が複数ある場合
                    // 最後のセグメントは関数/変数
                    info.items.push_back(last_part);
                    info.module_name = name.substr(0, last_colon);
                }
                // 相対パスで::が1つだけの場合（例: ./path/std::io）
                // サブモジュールパスとして保持（名前空間としてインポート）
            }
        }
    }

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
    std::string module_path = full_filename;
    if (segments.size() >= 3) {
        // 最後のセグメントが小文字始まりなら、関数/変数名として扱う
        const std::string& last_segment = segments.back();
        if (!last_segment.empty() && std::islower(last_segment[0])) {
            // モジュールパスは最後の1つ手前まで
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
        }

        // 1. ルートコンポーネントのファイル（std.cm）を最初に試す
        //    これは再エクスポートベースの解決に必要
        auto root_path = current_dir / (root_filename + ".cm");
        if (std::filesystem::exists(root_path)) {
            if (debug_mode) {
                std::cout << "[PREPROCESSOR] Found root module: " << root_path << "\n";
            }
            return std::filesystem::canonical(root_path);
        }

        // 2. ルートディレクトリ内のエントリーポイント（std/std.cm）
        auto root_dir_path = current_dir / root_filename;
        if (std::filesystem::exists(root_dir_path) &&
            std::filesystem::is_directory(root_dir_path)) {
            auto entry = find_module_entry_point(root_dir_path);
            if (!entry.empty()) {
                return entry;
            }
        }

        // 3. 完全パス（std/io.cm など）を試す
        auto full_path = current_dir / (full_filename + ".cm");
        if (std::filesystem::exists(full_path)) {
            return std::filesystem::canonical(full_path);
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

    // 検索パスから探す
    for (const auto& search_path : search_paths) {
        // セグメントが3つ以上（選択的インポート）の場合、module_pathを優先
        if (segments.size() >= 3 && module_path != full_filename) {
            // 0. モジュールパス（mem.cm など）を試す
            auto mod_file_path = search_path / (module_path + ".cm");
            if (std::filesystem::exists(mod_file_path)) {
                if (debug_mode) {
                    std::cout << "[PREPROCESSOR] Found module file in search path: "
                              << mod_file_path << "\n";
                }
                return std::filesystem::canonical(mod_file_path);
            }

            // 0.5. ディレクトリ内のエントリーポイント（mem/mod.cm など）
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
        }

        // 1. 完全パスを試す
        auto full_path = search_path / (full_filename + ".cm");
        if (std::filesystem::exists(full_path)) {
            return std::filesystem::canonical(full_path);
        }

        // 2. ディレクトリ内のエントリーポイントを探す
        auto dir_path = search_path / full_filename;
        if (std::filesystem::exists(dir_path) && std::filesystem::is_directory(dir_path)) {
            auto entry = find_module_entry_point(dir_path);
            if (!entry.empty()) {
                return entry;
            }
        }

        // 3. ルートコンポーネントを試す（検索パスでは後ろ側に）
        auto root_path = search_path / (root_filename + ".cm");
        if (std::filesystem::exists(root_path)) {
            return std::filesystem::canonical(root_path);
        }

        // 4. ルートディレクトリ内のエントリーポイント
        auto root_dir_path = search_path / root_filename;
        if (std::filesystem::exists(root_dir_path) &&
            std::filesystem::is_directory(root_dir_path)) {
            auto entry = find_module_entry_point(root_dir_path);
            if (!entry.empty()) {
                return entry;
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
    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".cm") {
            // module宣言があるか確認
            std::ifstream file(entry.path());
            std::string line;
            int line_count = 0;
            while (std::getline(file, line) && line_count++ < 20) {
                // コメントをスキップ
                if (line.find("//") == 0)
                    continue;

                // module文を検出
                std::regex module_regex(R"(^\s*module\s+([a-zA-Z_][a-zA-Z0-9_:]*)\s*;)");
                if (std::regex_search(line, module_regex)) {
                    modules.push_back(entry.path());
                    break;
                }
            }
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
