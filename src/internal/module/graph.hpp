#pragma once

// ============================================================
// 構造化importのモジュールグラフ（module-system-structural-imports）
// 各.cmファイルを独立にパースしてimport依存グラフを構築し、循環検出・重複訪問抑止・選択的import・
// エイリアス・再export・可視性検査をAST駆動の要求伝播で行う（importの唯一の実装経路）。
// 出力は「選択されなかった宣言を行数保存で空行化した全ファイルの依存順連結」と行単位のsource_map・
// ファイル単位のmodule_rangesで、既存パイプライン（結合バッファ前提のパーサ・診断写像・MIRモジュール分割）とそのまま互換する。
// ============================================================

#include "internal/base/module_range.hpp"
#include "internal/base/source_map.hpp"

#include <string>
#include <vector>

namespace cm::module_graph {

struct GraphParams {
    std::vector<std::string> defines;  // -D のユーザ定義（条件コンパイル）
    std::string target;                // ターゲット定数（bm/uefi等）
    bool test_mode = false;            // TEST 自動定義
    bool debug = false;
};

struct GraphResult {
    bool ok = false;
    std::string error;  // 循環・未解決import・依存ファイルの構文エラー等
    // R14: errorが位置情報（file:line:col+該当行+キャレット）を含む整形済み構文エラーならtrue（呼び出し側はpreprocessor errorでなくsyntax errorラベルで表示する）
    bool error_has_location = false;

    std::string combined_source;  // 依存順連結ソース（指示行は空行化）
    cm::SourceMap source_map;     // 連結行→元ファイル・行の写像
    std::vector<cm::ModuleRange> module_ranges;  // 連結バッファ内の各ファイルのバイト範囲
    std::vector<std::string> imported_modules;  // ルート以外の訪問ファイル（絶対パス）
};

// ルートファイルのソースからimportグラフを構築し、依存順の連結ソースを生成する
GraphResult build(const std::string& root_file, const std::string& root_source,
                  const GraphParams& params);

}  // namespace cm::module_graph
