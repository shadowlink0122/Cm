#pragma once

// SVモジュール階層の保持
//
// 相対import（import ./alu; 等）の先がexportされたIO構造体（方向属性フィールドを持つ export struct）を宣言している場合、
// そのimportをフラット化せず、
//   1. IO構造体から extern struct を自動生成してimport文を置換（親ソース中の `モジュール名::IO構造体名` は `モジュール名` へ置換）
//   2. import先を個別にSVコンパイルし、生成モジュールをトップの.svに連結することで、SV出力のモジュール階層を保持する。
// exportされたIO構造体を持たない相対importは従来どおりフラット化される

#include <string>
#include <vector>

namespace cm::codegen::sv {

struct HierarchyResult {
    bool enabled = false;                      // 階層化対象のimportが1つ以上あったか
    std::string transformed_source;            // import置換後のソース
    std::vector<std::string> submodule_files;  // サブモジュールの絶対パス（重複なし）
    std::string error;                         // エラーメッセージ（空なら成功）
};

// exportされたIO構造体を持つ相対importをextern struct宣言へ置換する。
// 階層化対象のimportが無い場合は enabled=false でソースは変更しない
HierarchyResult process_sv_hierarchy(const std::string& source, const std::string& input_file);

// サブモジュールを自プロセスの再帰起動でSVコンパイルし、生成モジュールをトップの出力ファイルへ連結する
bool append_submodules(const std::string& exe_path, const std::string& top_input_file,
                       const std::vector<std::string>& submodule_files,
                       const std::string& top_output, int opt_level, bool emit_memfile,
                       std::string& error);

}  // namespace cm::codegen::sv
