#include "import_preprocessor.hpp"

#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>

namespace cm::preprocessor {

ImportPreprocessor::ImportPreprocessor(bool debug) : debug_mode(debug) {
    // デフォルトの検索パスを設定
    auto current_dir = std::filesystem::current_path();

    // 1. カレントディレクトリ
    search_paths.push_back(current_dir);

    // 2. 標準ライブラリパス
    auto std_path = current_dir / "std";
    if (std::filesystem::exists(std_path)) {
        search_paths.push_back(std_path);
    }

    // 3. 環境変数 CM_MODULE_PATH
    if (const char* env_path = std::getenv("CM_MODULE_PATH")) {
        std::stringstream ss(env_path);
        std::string path;
        while (std::getline(ss, path, ':')) {
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
        // インポート文を検出（コメントも考慮）
        std::regex import_regex(R"(^\s*import\s+([^;]+)\s*;.*$)");
        std::smatch match;

        if (debug_mode) {
            std::cout << "[PREPROCESSOR] Processing line: " << line << "\n";
        }

        if (std::regex_match(line, match, import_regex)) {
            if (debug_mode) {
                std::cout << "[PREPROCESSOR] Matched import regex: " << match[0] << "\n";
            }
            // コメントを除去してからパース
            std::string import_statement = line.substr(0, line.find("//"));
            // 末尾の空白を除去
            import_statement.erase(import_statement.find_last_not_of(" \t\n\r") + 1);

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
                std::cout << "[PREPROCESSOR] Found import: " << import_info.module_name << "\n";
            }

            // モジュールファイルを探す
            auto module_path = find_module_file(import_info.module_name, current_file);
            if (module_path.empty()) {
                throw std::runtime_error("Module not found: " + import_info.module_name);
            }

            // 循環参照チェック
            std::string canonical_path = std::filesystem::canonical(module_path).string();
            if (imported_files.count(canonical_path) > 0) {
                // すでにインポート済み - スキップ
                if (debug_mode) {
                    std::cout << "[PREPROCESSOR] Skipping already imported: " << canonical_path << "\n";
                }
                result << "// Already imported: " << import_info.module_name << "\n";
                continue;
            }

            // 処理中マーク
            imported_files.insert(canonical_path);

            // モジュールファイルを読み込む
            std::string module_source = load_module_file(module_path);

            // モジュール内のインポートを再帰的に処理
            module_source = process_imports(module_source, module_path, imported_files);

            // エクスポートフィルタリング（選択的インポートの場合）
            // TODO: 選択的インポートのフィルタリング実装を改善する必要がある
            // 現在は全てのエクスポートを含める
            // if (!import_info.items.empty() && !import_info.is_wildcard) {
            //     module_source = filter_exports(module_source, import_info.items);
            // }

            // exportキーワードを削除
            module_source = remove_export_keywords(module_source);

            // モジュールコードを挿入
            result << "\n// ===== Begin module: " << import_info.module_name << " =====\n";
            result << module_source;
            result << "\n// ===== End module: " << import_info.module_name << " =====\n\n";
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

    // import module_name; 形式
    std::regex simple_import(R"(^\s*import\s+([a-zA-Z_][a-zA-Z0-9_:]*)\s*;)");

    // import module_name::*; 形式（ワイルドカード）
    std::regex wildcard_import(R"(^\s*import\s+([a-zA-Z_][a-zA-Z0-9_:]*)::\*\s*;)");

    // import module_name::{item1, item2}; 形式（選択的）
    std::regex selective_import(R"(^\s*import\s+([a-zA-Z_][a-zA-Z0-9_]*)::\{([^}]+)\}\s*;)");

    std::smatch match;

    if (std::regex_match(import_line, match, selective_import)) {
        info.module_name = match[1];
        // アイテムをパース
        std::string items_str = match[2];
        std::stringstream ss(items_str);
        std::string item;
        while (std::getline(ss, item, ',')) {
            // トリム
            item.erase(0, item.find_first_not_of(" \t"));
            item.erase(item.find_last_not_of(" \t") + 1);
            if (!item.empty()) {
                info.items.push_back(item);
            }
        }
    } else if (std::regex_match(import_line, match, wildcard_import)) {
        info.module_name = match[1];
        info.is_wildcard = true;
    } else if (std::regex_match(import_line, match, simple_import)) {
        info.module_name = match[1];
        // 全体インポート（アイテムリストは空）
    }

    return info;
}

bool ImportPreprocessor::check_circular_dependency(const std::string& module_path) {
    return processing_modules.count(module_path) > 0;
}

}  // namespace cm::preprocessor