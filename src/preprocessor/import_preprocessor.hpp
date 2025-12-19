#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace cm::preprocessor {

// インポートプリプロセッサ
// importステートメントを検出し、モジュールコードをインライン展開する
class ImportPreprocessor {
public:
    struct ProcessResult {
        std::string processed_source;  // 処理後のソースコード
        std::vector<std::string> imported_modules;  // インポートされたモジュール
        bool success;
        std::string error_message;
    };

private:
    // 循環参照検出のためのセット
    std::unordered_set<std::string> processing_modules;
    std::unordered_set<std::string> processed_modules;

    // モジュール検索パス
    std::vector<std::filesystem::path> search_paths;

    // デバッグモード
    bool debug_mode;

public:
    ImportPreprocessor(bool debug = false);

    // ソースコードを処理してインポートを展開
    ProcessResult process(const std::string& source_code,
                         const std::filesystem::path& source_file = "");

    // モジュール検索パスを追加
    void add_search_path(const std::filesystem::path& path);

private:
    // インポート文を検出して処理
    std::string process_imports(const std::string& source,
                               const std::filesystem::path& current_file,
                               std::unordered_set<std::string>& imported_files);

    // モジュールファイルを探す
    std::filesystem::path find_module_file(const std::string& module_name,
                                          const std::filesystem::path& current_file);

    // モジュールファイルを読み込む
    std::string load_module_file(const std::filesystem::path& module_path);

    // エクスポートされていない要素を削除
    std::string filter_exports(const std::string& module_source,
                               const std::vector<std::string>& import_items);

    // exportキーワードを削除
    std::string remove_export_keywords(const std::string& source);

    // インポート文をパース
    struct ImportInfo {
        std::string module_name;
        std::vector<std::string> items;  // 空の場合は全てインポート
        bool is_wildcard = false;
    };
    ImportInfo parse_import_statement(const std::string& import_line);

    // 循環参照をチェック
    bool check_circular_dependency(const std::string& module_path);
};

}  // namespace cm::preprocessor