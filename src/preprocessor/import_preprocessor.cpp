#include "import_preprocessor.hpp"

#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>

namespace cm::preprocessor {

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

    // 3. 標準ライブラリパス
    auto std_path = project_root / "std";
    if (std::filesystem::exists(std_path)) {
        search_paths.push_back(std_path);
    }

    // 4. 環境変数 CM_MODULE_PATH
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
}

void ImportPreprocessor::add_search_path(const std::filesystem::path& path) {
    search_paths.push_back(path);
}

ImportPreprocessor::ProcessResult ImportPreprocessor::process(
    const std::string& source_code,
    const std::filesystem::path& source_file
) {
    ProcessResult result;
    result.success = true;

    try {
        std::unordered_set<std::string> imported_files;

        // インポートを処理
        result.processed_source = process_imports(source_code, source_file, imported_files);

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

std::string ImportPreprocessor::process_imports(
    const std::string& source,
    const std::filesystem::path& current_file,
    std::unordered_set<std::string>& imported_files
) {
    std::stringstream result;
    std::stringstream input(source);
    std::string line;

    // 各行を処理
    while (std::getline(input, line)) {
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
            // コメントを除去してからパース
            std::string import_statement = line.substr(0, line.find("//"));
            // 末尾の空白を除去
            import_statement.erase(import_statement.find_last_not_of(" \t\n\r;") + 1);

            // インポート文をパース
            auto import_info = parse_import_statement(import_statement);

            // 標準ライブラリのインポートはスキップ（コンパイラに処理させる）
            if (import_info.module_name.find("std::") == 0 || import_info.module_name == "std") {
                if (debug_mode) {
                    std::cout << "[PREPROCESSOR] Skipping standard library import: " << import_info.module_name << "\n";
                }
                result << line << "\n";
                continue;
            }

            if (debug_mode) {
                std::cout << "[PREPROCESSOR] Found import: " << import_info.module_name;
                if (!import_info.alias.empty()) {
                    std::cout << " as " << import_info.alias;
                }
                std::cout << "\n";
            }

            // モジュールパスを解決
            auto module_path = resolve_module_path(import_info.module_name, current_file);
            if (module_path.empty()) {
                throw std::runtime_error("Module not found: " + import_info.module_name);
            }

            // 正規化されたパスを取得
            std::string canonical_path = std::filesystem::canonical(module_path).string();

            // 再インポート防止チェック
            if (imported_modules.count(canonical_path) > 0) {
                if (debug_mode) {
                    std::cout << "[PREPROCESSOR] Skipping already imported: " << canonical_path << "\n";
                }
                result << "// Already imported: " << import_info.module_name << "\n";
                continue;
            }

            // 循環依存チェック
            if (std::find(import_stack.begin(), import_stack.end(), canonical_path) != import_stack.end()) {
                throw std::runtime_error(format_circular_dependency_error(import_stack, canonical_path));
            }

            // インポートスタックに追加
            import_stack.push_back(canonical_path);
            imported_modules.insert(canonical_path);

            // キャッシュチェック
            std::string module_source;
            if (module_cache.count(canonical_path) > 0) {
                module_source = module_cache[canonical_path];
            } else {
                // モジュールファイルを読み込む
                module_source = load_module_file(module_path);

                // モジュール内のインポートを再帰的に処理
                module_source = process_imports(module_source, module_path, imported_files);

                // キャッシュに保存
                module_cache[canonical_path] = module_source;
            }

            // インポートスタックから削除
            import_stack.pop_back();

            // エクスポートフィルタリング（選択的インポートの場合）
            if (!import_info.items.empty() && !import_info.is_wildcard) {
                module_source = filter_exports(module_source, import_info.items);
            }

            // exportキーワードを削除
            module_source = remove_export_keywords(module_source);

            // エイリアスの処理
            if (!import_info.alias.empty()) {
                result << "\n// ===== Begin module: " << import_info.module_name
                      << " (as " << import_info.alias << ") =====\n";
                result << "namespace " << import_info.alias << " {\n";
                result << module_source;
                result << "} // namespace " << import_info.alias << "\n";
                result << "// ===== End module: " << import_info.module_name << " =====\n\n";
            } else if (import_info.is_from_import && !import_info.items.empty()) {
                // from構文の場合、using宣言を生成
                result << "\n// ===== From " << import_info.module_name << " =====\n";
                result << module_source << "\n";
                // using宣言を追加
                for (const auto& item : import_info.items) {
                    result << "// using " << item << ";\n";
                }
                result << "// ===== End from " << import_info.module_name << " =====\n\n";
            } else {
                // 通常のインポート
                result << "\n// ===== Begin module: " << import_info.module_name << " =====\n";
                result << module_source;
                result << "\n// ===== End module: " << import_info.module_name << " =====\n\n";
            }

            // imported_filesに追加（後方互換性のため）
            imported_files.insert(canonical_path);
        } else {
            // インポート文以外はそのまま出力
            result << line << "\n";
        }
    }

    return result.str();
}

std::filesystem::path ImportPreprocessor::find_module_file(
    const std::string& module_name,
    const std::filesystem::path& current_file
) {
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

std::string ImportPreprocessor::filter_exports(
    const std::string& module_source,
    const std::vector<std::string>& import_items
) {
    // 選択的インポート：指定されたアイテムのみを抽出
    std::stringstream result;
    std::stringstream input(module_source);
    std::string line;
    bool in_export_block = false;
    std::string current_export_name;
    std::vector<std::string> block_lines;
    int brace_depth = 0;

    while (std::getline(input, line)) {
        // エクスポートされた関数/構造体を検出
        std::smatch match;
        if (std::regex_search(line, match, std::regex(R"(export\s+(?:int|void|float|double|bool|char|struct|interface|enum)\s+(\w+))"))) {
            current_export_name = match[1];

            // 指定されたアイテムかチェック
            if (std::find(import_items.begin(), import_items.end(), current_export_name) != import_items.end()) {
                in_export_block = true;
                block_lines.clear();
                block_lines.push_back(line);

                // 開き括弧をカウント
                for (char c : line) {
                    if (c == '{') brace_depth++;
                    else if (c == '}') brace_depth--;
                }

                // 1行で完結する場合
                if (brace_depth == 0 && (line.find(';') != std::string::npos || line.find('}') != std::string::npos)) {
                    for (const auto& block_line : block_lines) {
                        result << block_line << "\n";
                    }
                    in_export_block = false;
                    block_lines.clear();
                }
            }
        } else if (in_export_block) {
            // エクスポートブロック内
            block_lines.push_back(line);

            // 括弧の深さを追跡
            for (char c : line) {
                if (c == '{') brace_depth++;
                else if (c == '}') brace_depth--;
            }

            // ブロックの終了を検出
            if (brace_depth == 0) {
                // ブロック全体を出力
                for (const auto& block_line : block_lines) {
                    result << block_line << "\n";
                }
                in_export_block = false;
                block_lines.clear();
            }
        } else if (line.find("export") == std::string::npos) {
            // エクスポートされていない行はそのまま保持（型定義など）
            // ただし、関数定義は除外
            if (!std::regex_search(line, std::regex(R"(^\s*(?:int|void|float|double|bool|char)\s+\w+\s*\()"))) {
                result << line << "\n";
            }
        }
    }

    return result.str();
}

std::string ImportPreprocessor::remove_export_keywords(const std::string& source) {
    std::stringstream result;
    std::stringstream input(source);
    std::string line;

    while (std::getline(input, line)) {
        // export キーワードを削除
        // "export" を削除
        std::regex export_regex(R"(\bexport\s+)");
        line = std::regex_replace(line, export_regex, "");

        // "export default" を削除（すでに "export " が削除されているので "default" のみ削除）
        std::regex default_regex(R"(\bdefault\s+)");
        line = std::regex_replace(line, default_regex, "");

        result << line << "\n";
    }

    return result.str();
}

ImportPreprocessor::ImportInfo ImportPreprocessor::parse_import_statement(
    const std::string& import_line
) {
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

    // パターン7: import module  (シンプル)
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

std::filesystem::path ImportPreprocessor::find_project_root(const std::filesystem::path& current_path) {
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
    const std::string& module_specifier,
    const std::filesystem::path& current_file
) {
    // 相対パス（./ または ../）の場合
    if (module_specifier.substr(0, 2) == "./" || module_specifier.substr(0, 3) == "../") {
        if (current_file.empty()) {
            throw std::runtime_error("Relative imports require a current file context");
        }

        auto base_dir = current_file.parent_path();
        auto relative_path = base_dir / module_specifier;

        // .cmファイルを試す
        auto cm_file = relative_path;
        cm_file += ".cm";
        if (std::filesystem::exists(cm_file)) {
            return std::filesystem::canonical(cm_file);
        }

        // ディレクトリ内のエントリーポイントを探す
        if (std::filesystem::exists(relative_path) && std::filesystem::is_directory(relative_path)) {
            auto entry = find_module_entry_point(relative_path);
            if (!entry.empty()) {
                return entry;
            }
        }

        throw std::runtime_error("Module not found: " + module_specifier);
    }

    // 標準ライブラリ（std::）の場合
    if (module_specifier.substr(0, 5) == "std::") {
        // 標準ライブラリは後で処理されるのでそのまま返す
        return {};
    }

    // 絶対パス（プロジェクトルートから）
    std::string filename = module_specifier;
    std::replace(filename.begin(), filename.end(), ':', '/');

    // 検索パスから探す
    for (const auto& search_path : search_paths) {
        // .cmファイルを試す
        auto full_path = search_path / (filename + ".cm");
        if (std::filesystem::exists(full_path)) {
            return std::filesystem::canonical(full_path);
        }

        // ディレクトリ内のエントリーポイントを探す
        auto dir_path = search_path / filename;
        if (std::filesystem::exists(dir_path) && std::filesystem::is_directory(dir_path)) {
            auto entry = find_module_entry_point(dir_path);
            if (!entry.empty()) {
                return entry;
            }
        }
    }

    return {};  // 見つからない
}

std::filesystem::path ImportPreprocessor::find_module_entry_point(const std::filesystem::path& directory) {
    // module文を含むファイルを探す
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().extension() == ".cm") {
            std::ifstream file(entry.path());
            std::string line;
            // 最初の数行だけチェック
            int line_count = 0;
            while (std::getline(file, line) && line_count++ < 10) {
                // コメントをスキップ
                if (line.find("//") == 0) continue;

                // module文を検出
                std::regex module_regex(R"(^\s*module\s+([a-zA-Z_][a-zA-Z0-9_:]*)\s*;)");
                if (std::regex_search(line, module_regex)) {
                    return entry.path();
                }
            }
        }
    }

    // module文が見つからない場合、mod.cmを探す（後方互換性）
    auto mod_path = directory / "mod.cm";
    if (std::filesystem::exists(mod_path)) {
        return mod_path;
    }

    return {};  // エントリーポイントが見つからない
}

std::string ImportPreprocessor::format_circular_dependency_error(
    const std::vector<std::string>& stack,
    const std::string& module
) {
    std::stringstream error;
    error << "Circular dependency detected:\n";
    for (size_t i = 0; i < stack.size(); ++i) {
        error << "  " << (i + 1) << ". " << stack[i] << "\n";
    }
    error << "  " << (stack.size() + 1) << ". " << module << " (circular reference)\n";
    return error.str();
}

}  // namespace cm::preprocessor