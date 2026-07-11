#pragma once

// コマンドラインインターフェース: オプション定義とパース
// （main.cpp から分離。docs/archive/013 §4.3-4 巨大TU分割の一環）

#include <string>
#include <vector>

namespace cm::cli {

// サブコマンド
enum class Command { None, Run, Compile, Check, Lint, Fmt, Help, Cache, Test };

// コマンドラインオプション
struct Options {
    Command command = Command::None;
    std::string input_file;                     // 単一ファイル (run/compile用)
    std::vector<std::string> input_files;       // 複数ファイル (check/lint用)
    bool recursive = false;                     // -r オプション
    std::vector<std::string> exclude_patterns;  // --exclude パターン
    bool show_ast = false;
    bool show_hir = false;
    bool show_mir = false;
    bool show_mir_opt = false;
    bool show_lir_opt = false;  // 最適化後のLLVM IRを表示
    bool emit_llvm = false;
    bool emit_js = false;  // JavaScript生成
    bool emit_memfile = false;  // SV: 配列リテラル初期値を.hexファイルとして書き出す
    std::string target = "";      // ターゲット (native, wasm, js, web)
    bool run_after_emit = false;  // 生成後に実行
    int optimization_level = 3;   // デフォルト最適化レベル3
    bool debug = false;
    std::string debug_level = "info";
    bool verbose = false;         // デフォルトは静かなモード
    bool quiet = false;           // 出力を抑制するモード
    std::string output_file;      // -o オプション
    size_t max_output_size = 16;  // 最大出力サイズ（GB）、デフォルト16GB
    bool use_jit = true;          // JITコンパイラ使用（デフォルト）
    // インクリメンタルビルド設定
    bool incremental = false;             // デフォルトで無効（--incrementalで有効化）
    std::string cache_dir = ".cm-cache";  // キャッシュディレクトリ
    std::string cache_subcommand;         // cache サブコマンド（clear/stats）
    // SVターゲットオプション
    bool sv_strict_lint = false;       // --sv-strict-lint: lint_off抑止を出力しない
    bool sv_always_ff = false;         // --sv-always-ff: always_ff/always_comb等を保持
    bool sv_warn_nba = false;          // --sv-warn-nba: 代入済み状態変数の参照を警告
    bool emit_constraints = false;     // --emit-constraints: .cst/.tcl制約ファイルを生成
    std::vector<std::string> defines;  // -D NAME: 条件付きコンパイル定義
    // テストモード（cm test / --test）: #[test]関数を含めてコンパイルし、TESTを自動定義
    bool test_mode = false;
    // fmt --check: 整形せず、要整形ファイルがあれば非0終了（CIゲート用）
    bool fmt_check = false;
    // ユーザ指定のMIR最適化オプション
    bool unroll_loops = false;  // --funroll-loops: 定数ループの静的展開
    int unroll_max_trips = 64;  // --funroll-loops=N: 展開する最大イテレーション数
    // エラー処理
    bool has_error = false;     // パースエラーフラグ
    std::string error_message;  // エラーメッセージ
    bool force_check = false;   // コンパイル時に厳格な型チェックを強制実行
};

// バージョン情報を取得（CMakeでコンパイル時に埋め込み）
std::string get_version();

// ヘルプメッセージを表示
void print_help(const char* program_name);

// コマンドラインオプションをパース
Options parse_options(int argc, char* argv[]);

}  // namespace cm::cli
