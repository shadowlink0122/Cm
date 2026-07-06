#pragma once

// SVモジュール階層の保持（Stage 2）
//
// `//! sv: hierarchy` ディレクティブ付きのSVターゲットソースでは、
// 相対import（import ./alu; 等）をフラット化せず、
//   1. import先のポート宣言から extern struct を自動生成してimport文を置換
//   2. import先を個別にSVコンパイルし、生成モジュールをトップの.svに連結
// することで、SV出力のモジュール階層を保持する。
// （sv_backend_missing_features.md 項目1 Stage 2）

#include <string>
#include <vector>

namespace cm::codegen::sv {

struct HierarchyResult {
    bool enabled = false;                      // ディレクティブが有効だったか
    std::string transformed_source;            // import置換後のソース
    std::vector<std::string> submodule_files;  // サブモジュールの絶対パス（重複なし）
    std::string error;                         // エラーメッセージ（空なら成功）
};

// `//! sv: hierarchy` を検出し、相対importをextern struct宣言へ置換する。
// ディレクティブが無い場合は enabled=false でソースは変更しない
HierarchyResult process_sv_hierarchy(const std::string& source, const std::string& input_file);

// サブモジュールを自プロセスの再帰起動でSVコンパイルし、
// 生成モジュールをトップの出力ファイルへ連結する
bool append_submodules(const std::string& exe_path, const std::string& top_input_file,
                       const std::vector<std::string>& submodule_files,
                       const std::string& top_output, int opt_level, bool emit_memfile,
                       std::string& error);

}  // namespace cm::codegen::sv
