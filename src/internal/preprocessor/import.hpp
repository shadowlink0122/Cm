#pragma once

#include "internal/base/module_range.hpp"
#include "internal/base/source_map.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cm::preprocessor {

// インポートプリプロセッサ
// importステートメントを検出し、モジュールコードをインライン展開する
class ImportPreprocessor {
   public:
    // ソースマップ（実体はbaseの共有型。診断表示DiagnosticEmitterが消費するため最下層に定義）
    using SourceMapEntry = cm::SourceMapEntry;
    using SourceMap = cm::SourceMap;

    // モジュール範囲（実体はbaseの共有型。MIR loweringが消費するため最下層に定義）
    using ModuleRange = cm::ModuleRange;

    struct ProcessResult {
        std::string processed_source;               // 処理後のソースコード
        std::vector<std::string> imported_modules;  // インポートされたモジュール
        SourceMap source_map;                       // ソースマップ
        std::vector<ModuleRange> module_ranges;     // モジュール範囲情報
        std::vector<std::string> resolved_files;  // 全参照ファイルの絶対パス（キャッシュキー用）
        bool success;
        std::string error_message;
    };

   private:
    // 循環参照検出とキャッシュ
    std::unordered_map<std::string, std::set<std::string>>
        imported_symbols;  // インポート済みシンボル（ファイルパス -> シンボルセット）
    std::unordered_set<std::string>
        imported_modules;  // インポート済みモジュール（再インポート防止）
    // トップレベルの選択importで公開された非修飾シンボル名 → 由来モジュールの正規化パス。
    // 異なるモジュールから同名シンボルを取り込む曖昧なimportを診断する（M2）
    std::unordered_map<std::string, std::string> exposed_symbols_;
    std::vector<std::string> import_stack;  // 現在のインポートスタック（循環依存検出）
    std::unordered_map<std::string, std::string> module_cache;  // モジュールキャッシュ（展開済み）
    std::unordered_map<std::string, std::string>
        raw_module_cache;  // オリジナルソースキャッシュ（export抽出用）
    std::unordered_map<std::string, std::string>
        processed_module_cache;  // export処理済みキャッシュ（メモリ最適化）

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

    // 非exportシンボルの選択importへ警告を出す（H7の段階導入。check/lint・--strict時に有効化）
    void set_warn_non_exported(bool enable) { warn_non_exported_ = enable; }

   private:
    // 非export選択importの警告フラグ（H7）
    bool warn_non_exported_ = false;

    // 選択importで指定されたアイテムのうち、モジュールのトップレベルで非export関数として定義されているものを返す（H7）。
    // 型定義（struct/enum/typedef/const）の透過は仕様として維持するため対象外
    std::vector<std::string> find_non_exported_function_items(
        const std::string& module_source, const std::vector<std::string>& items);

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

    // エクスポートされていない要素を削除。
    // incremental=trueは同一ファイルからの2回目以降の選択import用で、
    // 初回展開で出力済みのネストimport領域・非export型/impl・素通し行を再出力せず、
    // 新規要求されたexportシンボル（とその型のimpl）だけを出力する（M7: Duplicate method対策）
    std::string filter_exports(const std::string& module_source,
                               const std::vector<std::string>& import_items,
                               bool incremental = false);

    // H7段階4: モジュールの非exportヘルパー関数名（生ソースから収集、canonicalパス鍵）。
    // 値は (改名プレフィックス, 関数名リスト)
    std::map<std::string, std::pair<std::string, std::vector<std::string>>> module_internal_fns_;

    // 生ソースのトップレベル非export関数名を収集する（H7段階4の改名対象。
    // インラインexport・リストexport（export { a, b }; / export a, b;）は除外、main/efi_mainも除外）
    std::vector<std::string> collect_non_export_function_names(const std::string& module_source);

    // 収集した関数名を __cm_priv_<prefix>_<名前> へ単語境界で一貫改名する。
    // 改名結果は元名を _ 付きで含むため再適用しても変化しない（冪等）
    std::string rename_internal_functions(std::string text, const std::vector<std::string>& names,
                                          const std::string& prefix);

    // module_internal_fns_に基づき、モジュール断片へ内部関数改名を適用する（未登録パスは素通し）
    std::string apply_internal_fn_renames(std::string text,
                                          const std::filesystem::path& module_path);

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
        bool is_reexport = false;  // export import 行（モジュール展開時の再export取り込み）
        size_t line_number = 0;   // インポート文の行番号
        std::string source_file;  // ソースファイル名
        std::string source_line;  // インポート文の元のコード
    };
    ImportInfo parse_import_statement(const std::string& import_line);
    ImportInfo parse_import_statement(const std::string& import_line, size_t line_num,
                                      const std::string& filename);

    // インポートアイテムをパース（ヘルパー関数）
    void parse_import_items(const std::string& items_str, ImportInfo& info);

    // プロジェクトルートを検出
    std::filesystem::path find_project_root(const std::filesystem::path& current_path);

    // モジュールパスを解決（相対/絶対パスのサポート）
   public:
    // モジュール指定子の解決（mod.cm・相対import・階層import対応）。
    // 構造化importのモジュールグラフ（module/graph.cpp）が同一の解決意味論を共有するため公開する
    std::filesystem::path resolve_module_path(const std::string& module_specifier,
                                              const std::filesystem::path& current_file);

   private:
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

    // exportされたブロック（関数・struct・const等）を抽出
    // namespace外へのforward展開用
    std::string extract_exported_blocks(const std::string& module_source);
};

}  // namespace cm::preprocessor