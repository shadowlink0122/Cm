#include "frontend.hpp"

#include "internal/base/i18n.hpp"
#include "internal/module/graph.hpp"
#include "internal/module/resolver.hpp"
#include "internal/syntax/lexer/lexer.hpp"
#include "internal/syntax/parser/parser.hpp"

#include <chrono>
#include <iostream>

namespace cm::cli {

DiagnosticEmitter FrontendResult::make_emitter() const {
    return DiagnosticEmitter(code, input_file_, &preprocess.source_map);
}

FrontendResult run_frontend(const FrontendParams& params, std::string code) {
    FrontendResult result;
    result.input_file_ = params.input_file;

    // モジュールリゾルバ初期化（ファイルごとの独立状態）
    module::initialize_module_resolver();

    // ========== 前処理段階（構造化import: モジュールグラフ+選択的包含）==========
    // ファイル単位パースの依存グラフで循環検出・重複抑止・選択import・可視性検査を行い、
    // 依存順連結+行単位source_mapを生成する（module-system-structural-imports）
    auto phase_preprocess_start = std::chrono::steady_clock::now();
    module_graph::GraphParams gparams;
    gparams.defines = params.defines;
    gparams.target = params.target;
    gparams.test_mode = params.test_mode;
    gparams.debug = params.debug;
    auto graph = module_graph::build(params.input_file, code, gparams);
    result.phase_preprocess_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - phase_preprocess_start)
                                     .count();
    if (!graph.ok) {
        result.preprocess_error = graph.error;
        return result;
    }
    result.preprocess.success = true;
    result.preprocess.processed_source = graph.combined_source;
    result.preprocess.source_map = std::move(graph.source_map);
    result.preprocess.module_ranges = std::move(graph.module_ranges);
    result.preprocess.imported_modules = std::move(graph.imported_modules);
    result.preprocess_ok = true;
    result.code = result.preprocess.processed_source;
    code = result.code;

    if (params.debug && !result.preprocess.imported_modules.empty()) {
        std::cout << i18n::msg(i18n::MsgId::CliImportedModules);
        for (const auto& module : result.preprocess.imported_modules) {
            std::cout << "  - " << module << "\n";
        }
        std::cout << "\n";
    }

    // ========== 構文解析段階（字句解析・構文解析）==========
    try {
        auto phase_parse_start = std::chrono::steady_clock::now();
        // ターゲット指定があればSVレキサーを明示選択、無ければディレクティブで自動検出
        LexerPlatform lexer_platform = LexerPlatform::Default;
        if (params.target == "sv" || params.target == "verilog" ||
            params.target == "systemverilog") {
            lexer_platform = LexerPlatform::SV;
        }
        Lexer lexer(result.code, lexer_platform);
        auto tokens = lexer.tokenize();
        result.is_sv = lexer.is_sv();
        Parser parser(std::move(tokens), result.is_sv);
        result.program = parser.parse();
        result.parser_diagnostics = parser.diagnostics();
        result.parse_ok = !parser.has_errors();
        result.phase_parse_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now() - phase_parse_start)
                                    .count();
    } catch (const std::exception& e) {
        result.internal_error_stage = "parse";
        result.internal_error = e.what();
    }
    return result;
}

}  // namespace cm::cli
