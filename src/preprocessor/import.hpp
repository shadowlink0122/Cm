#pragma once

#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cm::preprocessor {

// インポートプリプロセッサ
// importステートメントを検出し、モジュールコードをインライン展開する
class ImportPreprocessor {
   public:
    // ソースマップエントリ：プリプロセス後の行が元のどのファイル・行に対応するか
    struct SourceMapEntry {
        std::string original_file;  // 元のファイルパス
        size_t original_line;       // 元の行番号（1-indexed）
        std::string import_chain;   // インポートチェーン（デバッグ用）
    };

    // ソースマップ：プリプロセス後の行番号（1-indexed） -> 元の位置情報
    using SourceMap = std::vector<SourceMapEntry>;

    // モジュール範囲：プリプロセス後のコードでモジュールがどの範囲にあるか
    struct ModuleRange {
        std::string file_path;    // モジュールファイルパス
        std::string import_from;  // どのファイルからインポートされたか
        size_t import_line;       // インポート文の行番号
        size_t start_offset;      // プリプロセス後のコードでの開始オフセット
        size_t end_offset;        // プリプロセス後のコードでの終了オフセット
    };

    struct ProcessResult {
        std::string processed_source;               // 処理後のソースコード
        std::vector<std::string> imported_modules;  // インポートされたモジュール
        SourceMap source_map;                       // ソースマップ
        std::vector<ModuleRange> module_ranges;     // モジュール範囲情報
        bool success;
        std::string error_message;
    };

   private:
    // 循環参照検出とキャッシュ
    std::unordered_map<std::string, std::set<std::string>>
        imported_symbols;  // インポート済みシンボル（ファイルパス -> シンボルセット）
    std::unordered_set<std::string>
        imported_modules;  // インポート済みモジュール（再インポート防止）
    std::vector<std::string> import_stack;  // 現在のインポートスタック（循環依存検出）
    std::unordered_map<std::string, std::string> module_cache;  // モジュールキャッシュ

    // モジュール名前空間の追跡（モジュール名 -> 名前空間）
    std::unordered_map<std::string, std::string> module_namespaces;
    // 再エクスポートの追跡（親モジュール -> {子モジュール名, ...}）
    std::unordered_map<std::string, std::vector<std::string>> module_reexports;

    // モジュール検索パス
    std::vector<std::filesystem::path> search_paths;
    std::filesystem::path project_root;  // プロジェクトルート

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
                                std::unordered_set<std::string>& imported_files,
                                SourceMap& source_map, std::vector<ModuleRange>& module_ranges,
                                const std::string& import_chain = "",
                                size_t import_line_in_parent = 0);

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

    // export構文を処理（外部定義+名前列挙のサポート）
    std::string process_export_syntax(const std::string& source);

    // サブ名前空間を処理（export NS { ... }）
    std::string process_namespace_exports(const std::string& source);

    // モジュール名をプレフィックスとして追加
    std::string add_module_prefix(const std::string& source, const std::string& module_name);

    // インポート文をパース
    struct ImportInfo {
        std::string module_name;
        std::string alias;               // "as" エイリアス
        std::vector<std::string> items;  // 選択的インポート項目
        std::vector<std::pair<std::string, std::string>> item_aliases;  // 項目ごとのエイリアス
        bool is_wildcard = false;
        bool is_recursive_wildcard = false;  // import ./path/* 形式
        bool is_from_import = false;         // from構文
        bool is_relative = false;            // 相対パス（./ or ../）
        size_t line_number = 0;              // インポート文の行番号
        std::string source_file;             // ソースファイル名
        std::string source_line;             // インポート文の元のコード
    };
    ImportInfo parse_import_statement(const std::string& import_line);
    ImportInfo parse_import_statement(const std::string& import_line, size_t line_num,
                                      const std::string& filename);

    // インポートアイテムをパース（ヘルパー関数）
    void parse_import_items(const std::string& items_str, ImportInfo& info);

    // プロジェクトルートを検出
    std::filesystem::path find_project_root(const std::filesystem::path& current_path);

    // モジュールパスを解決（相対/絶対パスのサポート）
    std::filesystem::path resolve_module_path(const std::string& module_specifier,
                                              const std::filesystem::path& current_file);

    // module文でエントリーポイントを検出
    std::filesystem::path find_module_entry_point(const std::filesystem::path& directory);

    // 循環依存エラーメッセージを生成
    std::string format_circular_dependency_error(const std::vector<std::string>& stack,
                                                 const std::string& module);

    // モジュール宣言から名前空間を抽出
    std::string extract_module_namespace(const std::string& module_source);

    // 再エクスポートを検出して追跡
    std::vector<std::string> extract_reexports(const std::string& module_source);

    // ディレクトリ内のすべてのモジュールを再帰的に検出
    std::vector<std::filesystem::path> find_all_modules_recursive(
        const std::filesystem::path& directory);

    // 指定した名前空間内の内容を抽出
    std::string extract_namespace_content(const std::string& source,
                                          const std::string& namespace_name);

    // implの暗黙的エクスポート処理
    std::string process_implicit_impl_export(const std::string& source);

    // 階層再構築エクスポート処理: export { ns::{item1, item2} }
    std::string process_hierarchical_reexport(const std::string& source);
};

}  // namespace cm::preprocessor