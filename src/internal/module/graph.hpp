#pragma once

// ============================================================
// 構造化importのモジュールグラフ（module-system-structural-imports 第1段）
// 各.cmファイルを独立にパースしてimport依存グラフを構築し、循環検出と重複訪問の抑止をグラフ上で行う。
// 出力は「import/export指示行を空行化した全ファイルの依存順連結」と行単位のsource_map・ファイル単位のmodule_rangesで、
// 既存パイプライン（結合バッファ前提のパーサ・診断写像・MIRモジュール分割）とそのまま互換する。
// テキストのexport切り出し・再エクスポート書き換え（extract/rewrite）はこの経路では使用しない。
// CM_STRUCTURED_IMPORTS=1 で有効化（既定は従来のテキスト展開）
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

    std::string combined_source;  // 依存順連結ソース（指示行は空行化）
    cm::SourceMap source_map;     // 連結行→元ファイル・行の写像
    std::vector<cm::ModuleRange> module_ranges;  // 連結バッファ内の各ファイルのバイト範囲
    std::vector<std::string> imported_modules;  // ルート以外の訪問ファイル（絶対パス）
};

// ルートファイルのソースからimportグラフを構築し、依存順の連結ソースを生成する
GraphResult build(const std::string& root_file, const std::string& root_source,
                  const GraphParams& params);

}  // namespace cm::module_graph
