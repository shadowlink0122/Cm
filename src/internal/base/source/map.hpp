#pragma once

// ============================================================
// ソースマップ（プリプロセス後の行→元ファイル・行の写像）
// preprocessorが生成し、診断表示（DiagnosticEmitter）が元ソース座標の復元に消費する共有型。
// preprocessor型を表示側が直接includeする層違反を避けるため最下層baseに置く
// （diagnostics-engine-unification 第1段）
// ============================================================

#include <cstddef>
#include <string>
#include <vector>

namespace cm {

// ソースマップエントリ：プリプロセス後の行が元のどのファイル・行に対応するか
struct SourceMapEntry {
    std::string original_file;  // 元のファイルパス
    size_t original_line;       // 元の行番号（1-indexed）
    std::string import_chain;   // インポートチェーン（デバッグ用）
};

// ソースマップ：プリプロセス後の行番号（1-indexed） -> 元の位置情報
using SourceMap = std::vector<SourceMapEntry>;

}  // namespace cm
