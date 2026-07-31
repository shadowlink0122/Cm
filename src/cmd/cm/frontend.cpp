#include "frontend.hpp"

#include "internal/base/i18n.hpp"
#include "internal/module/resolver.hpp"
#include "internal/preprocessor/conditional.hpp"
#include "internal/syntax/lexer/lexer.hpp"
#include "internal/syntax/parser/parser.hpp"

#include <chrono>
#include <fstream>
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

    // ========== 前処理段階（import展開・条件付きコンパイル）==========
    try {
        if (params.debug) {
            std::cout << "=== Import Preprocessor ===\n";
        }
        auto phase_preprocess_start = std::chrono::steady_clock::now();
        preprocessor::ImportPreprocessor import_preprocessor(params.debug);
        // 非export関数の選択importへの警告（H7の段階導入。checkは常時、buildは--force-check/--strict時）
        if (params.warn_non_exported) {
            import_preprocessor.set_warn_non_exported(true);
        }
        result.preprocess = import_preprocessor.process(code, params.input_file);
        result.phase_preprocess_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::steady_clock::now() - phase_preprocess_start)
                                         .count();

        if (!result.preprocess.success) {
            result.preprocess_error = result.preprocess.error_message;
            return result;
        }

        if (params.dump_preprocessed) {
            // デバッグ支援: プリプロセス結果を常時ダンプ（build経路）
            std::ofstream out(".tmp/preprocessed.cm");
            if (out) {
                out << result.preprocess.processed_source;
            }
        }

        if (params.debug && !result.preprocess.imported_modules.empty()) {
            std::cout << i18n::msg(i18n::MsgId::CliImportedModules);
            for (const auto& module : result.preprocess.imported_modules) {
                std::cout << "  - " << module << "\n";
            }
            std::cout << "\n";
        }

        code = result.preprocess.processed_source;

        if (params.debug) {
            std::cout << "=== Conditional Preprocessor ===\n";
        }
        preprocessor::ConditionalPreprocessor conditional;
        for (const auto& def : params.defines) {
            conditional.define(def);
        }
        // テストモード（cm test / --test）: TEST を自動定義（#[test] と連動するテスト補助コードを #ifdef TEST で書けるようにする）
        if (params.test_mode) {
            conditional.define("TEST");
        }
        // ターゲットに応じたプリプロセッサ定数
        if (params.target == "baremetal-arm" || params.target == "bm" ||
            params.target == "baremetal-x86" || params.target == "bm-x86") {
            conditional.define("__NO_STD__");
            conditional.define("__BAREMETAL__");
        } else if (params.target == "uefi") {
            conditional.define("__NO_STD__");
            conditional.define("__BAREMETAL__");  // UEFIもベアメタルサブカテゴリ
            conditional.define("__UEFI__");
            conditional.define("__EFI__");
        }
        code = conditional.process(code);
        if (params.debug) {
            std::cout << i18n::msg(i18n::MsgId::CliDefinedSymbols);
            for (const auto& def : conditional.definitions()) {
                std::cout << def << " ";
            }
            std::cout << "\n";
            std::cout << "=== Preprocessed Code ===\n";
            std::cout << code << "\n";
            std::cout << "=== End Preprocessed Code ===\n\n";
        }
    } catch (const std::exception& e) {
        result.internal_error_stage = "preprocess";
        result.internal_error = e.what();
        return result;
    }
    result.preprocess_ok = true;
    result.code = code;

    // ========== 構文解析段階（字句解析・構文解析）==========
    try {
        auto phase_parse_start = std::chrono::steady_clock::now();
        if (params.debug) {
            std::cout << "=== Lexer ===\n";
        }
        // ターゲット指定があればSVレキサーを明示選択、無ければディレクティブで自動検出
        LexerPlatform lexer_platform = LexerPlatform::Default;
        if (params.target == "sv" || params.target == "verilog" ||
            params.target == "systemverilog") {
            lexer_platform = LexerPlatform::SV;
        }
        Lexer lexer(result.code, lexer_platform);
        auto tokens = lexer.tokenize();
        result.is_sv = lexer.is_sv();

        if (params.debug) {
            std::cout << i18n::msgf(i18n::MsgId::CliTokens, tokens.size());
            std::cout << "=== Parser ===\n";
        }
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
        result.preprocess_ok = true;
        return result;
    }

    return result;
}

}  // namespace cm::cli
