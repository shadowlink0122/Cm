#pragma once

#include "internal/base/module_range.hpp"
#include "internal/base/source_map.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace cm::preprocessor {

// モジュール指定子リゾルバ
// import指定子（mod.cm・相対import・階層import対応）をファイルパスへ解決する。
// テキストインライン展開は廃止済みで、展開は構造化importのモジュールグラフ（module/graph.cpp）が行う
class ImportPreprocessor {
   public:
    // ソースマップ（実体はbaseの共有型。診断表示DiagnosticEmitterが消費するため最下層に定義）
    using SourceMapEntry = cm::SourceMapEntry;
    using SourceMap = cm::SourceMap;

    // モジュール範囲（実体はbaseの共有型。MIR loweringが消費するため最下層に定義）
    using ModuleRange = cm::ModuleRange;

    // 前処理結果（構造化importのモジュールグラフ出力を格納する共有型）
    struct ProcessResult {
        std::string processed_source;               // 処理後のソースコード
        std::vector<std::string> imported_modules;  // インポートされたモジュール
        SourceMap source_map;                       // ソースマップ
        std::vector<ModuleRange> module_ranges;     // モジュール範囲情報
        std::vector<std::string> resolved_files;  // 全参照ファイルの絶対パス（キャッシュキー用）
        bool success = false;
        std::string error_message;
    };

   private:
    // モジュール検索パス
    std::vector<std::filesystem::path> search_paths;
    std::filesystem::path project_root;  // プロジェクトルート

    // デバッグモード
    bool debug_mode;

   public:
    ImportPreprocessor(bool debug = false);

    // モジュール検索パスを追加
    void add_search_path(const std::filesystem::path& path);

    // モジュール指定子の解決（mod.cm・相対import・階層import対応）
    std::filesystem::path resolve_module_path(const std::string& module_specifier,
                                              const std::filesystem::path& current_file);

   private:
    // モジュールファイルを探す
    std::filesystem::path find_module_file(const std::string& module_name,
                                           const std::filesystem::path& current_file);

    // プロジェクトルートを検出
    std::filesystem::path find_project_root(const std::filesystem::path& current_path);

    // module文でエントリーポイントを検出
    std::filesystem::path find_module_entry_point(const std::filesystem::path& directory);
};

}  // namespace cm::preprocessor
