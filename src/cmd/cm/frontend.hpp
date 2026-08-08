#pragma once

// ============================================================
// 共有フロントエンドパイプライン（compiler-architecture-restructure 第5段）
// import展開→条件付きコンパイル→字句解析→構文解析のステージ配線をbuild.cpp/check.cppの複製から一元化する。
// ドライバはパラメータを渡して結果（プリプロセス後ソース・source_map・AST・診断）を消費するだけにし、ステージ配線の手書き複製を禁止する
// ============================================================

#include "internal/base/diag_emitter.hpp"
#include "internal/base/diagnostics.hpp"
#include "internal/preprocessor/import.hpp"
#include "internal/syntax/ast/nodes.hpp"

#include <string>
#include <vector>

namespace cm::cli {

struct FrontendParams {
    std::string input_file;            // 診断表示・importの基準パス
    std::vector<std::string> defines;  // -D のユーザ定義
    std::string target;  // 条件コンパイル定数・SVレキサー判定（空=ディレクティブ自動検出）
    bool test_mode = false;  // TEST を自動定義（#[test] 連動）
    bool debug = false;      // ステージごとのデバッグ出力
};

struct FrontendResult {
    // 失敗要因の判別（preprocess失敗はpreprocess_error、内部例外はinternal_error_*に格納）
    bool preprocess_ok = false;
    bool parse_ok = false;
    std::string preprocess_error;
    // R14: preprocess_errorが位置情報付きの整形済み構文エラーならtrue（syntax errorラベルで表示する）
    bool preprocess_error_has_location = false;
    std::string internal_error_stage;  // "preprocess" / "parse"（例外発生時のみ）
    std::string internal_error;

    std::string code;  // プリプロセス後ソース（診断のSpanはこの座標系）
    preprocessor::ImportPreprocessor::ProcessResult preprocess;
    ast::Program program;
    std::vector<Diagnostic> parser_diagnostics;
    bool is_sv = false;

    // フェーズ計測（ミリ秒）
    long long phase_preprocess_ms = 0;
    long long phase_parse_ms = 0;

    // 診断表示用エミッタ（プリプロセス後ソース+source_mapを束ねる）
    DiagnosticEmitter make_emitter() const;

   private:
    friend FrontendResult run_frontend(const FrontendParams& params, std::string code);
    std::string input_file_;
};

// codeは読み込み済みの元ソース（ファイルI/Oは呼び出し側の責務。checkはディレクティブ検出に元ソースを使うため）
FrontendResult run_frontend(const FrontendParams& params, std::string code);

}  // namespace cm::cli
