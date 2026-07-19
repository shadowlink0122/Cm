#pragma once

// cmコマンドのドライバ層（golang/go の cmd/go とLLVMツールのドライバ構成を参考にした分割）
// main.cpp は言語初期化とディスパッチのみを担い、各コマンドの実装は以下のファイルに分かれる:
//   check.cpp        — check/lint（複数ファイル走査）
//   fmt.cpp          — fmt（複数ファイル整形）
//   build.cpp        — run/compile/test のコンパイルパイプライン（段階関数に分割）
//   backend/run.cpp  — Runコマンドのバックエンド（JIT・テストランナー・JS実行）
//   backend/sv.cpp   — SystemVerilogコード生成
//   backend/js.cpp   — JavaScriptコード生成
//   backend/llvm.cpp — LLVMコード生成（native/wasm/baremetal/uefi）
//   util.cpp         — ファイル読込・ディレクティブ解析・走査・簡易プリンタ等の共有ヘルパー
// 例外は各段階・各バックエンドの境界で捕捉し、main() の最上位catchは最後の砦とする

#include "internal/preprocessor/import.hpp"
#include "options.hpp"

#include <chrono>
#include <string>
#include <vector>

namespace cm::ast {
struct Program;
}
namespace cm::hir {
struct HirProgram;
}
namespace cm::mir {
struct MirProgram;
}

namespace cm::driver {

// 終了コード（LLVMツール慣例: 0 = 成功、1 = エラー）
inline constexpr int kExitSuccess = 0;
inline constexpr int kExitFailure = 1;

// ===== コマンドエントリ =====
int run_check(const cli::Options& opts);
int run_fmt(const cli::Options& opts);
int run_build(cli::Options& opts, const char* argv0);

// ===== ビルドパイプラインの共有状態（build.cpp と backend_*.cpp で受け渡す）=====
struct BuildContext {
    cli::Options& opts;
    const char* argv0;
    std::string code;
    bool run_sv_sim = false;  // cm test（SVフロー）で生成後にシミュレーションを実行する
    std::string sv_top_module;
    std::vector<std::string> sv_hierarchy_submodules;
    preprocessor::ImportPreprocessor::ProcessResult preprocess;
    // フェーズ計測
    std::chrono::steady_clock::time_point compile_start{};
    long long phase_preprocess_ms = 0;
    long long phase_parse_ms = 0;
    long long phase_typecheck_ms = 0;
    long long phase_hir_ms = 0;
    long long phase_mir_ms = 0;
    long long phase_opt_ms = 0;
};

// ===== バックエンド =====
int emit_jit_run(BuildContext& ctx, mir::MirProgram& mir);
int emit_sv(BuildContext& ctx, mir::MirProgram& mir);
int emit_js(BuildContext& ctx, mir::MirProgram& mir);
int emit_llvm(BuildContext& ctx, mir::MirProgram& mir);

// ===== 共有ヘルパー（driver_util.cpp）=====
struct ReadFileResult {
    std::string content;
    bool success = false;
    std::string error_message;
};

ReadFileResult read_file(const std::string& filename);
std::string parse_platform_directive(const std::string& code_content);
bool match_platform_directive(const std::string& directive, const std::string& current_target);
bool is_baremetal_platform(const std::string& directive);
int run_sv_test_simulation(const std::string& sv_path, bool quiet);
bool matches_exclude_pattern(const std::string& filepath, const std::vector<std::string>& patterns);
std::vector<std::string> collect_cm_files(const std::vector<std::string>& paths, bool recursive,
                                          const std::vector<std::string>& excludes,
                                          const std::vector<std::string>& dir_scan_excludes = {});
void print_ast(const ast::Program& program);
void print_hir(const hir::HirProgram& program);

}  // namespace cm::driver
