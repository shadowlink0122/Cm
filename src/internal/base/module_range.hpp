#pragma once

// ============================================================
// モジュール範囲情報（プリプロセス後コードのバイトオフセット→モジュール対応）
// preprocessorが生成しMIR loweringがソースファイル解決に消費する共有型。
// preprocessor型をMIRが直接includeする層違反を避けるため最下層baseに置く
// （compiler-architecture-restructure 第1段）
// ============================================================

#include <cstddef>
#include <string>

namespace cm {

// プリプロセス後のコードでモジュールがどの範囲にあるか
struct ModuleRange {
    std::string file_path;    // モジュールファイルパス
    std::string import_from;  // どのファイルからインポートされたか
    size_t import_line;       // インポート文の行番号
    size_t start_offset;      // プリプロセス後のコードでの開始オフセット
    size_t end_offset;        // プリプロセス後のコードでの終了オフセット
};

}  // namespace cm
