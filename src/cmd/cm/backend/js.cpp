// JavaScriptコード生成バックエンド: JS/web出力・インクリメンタルキャッシュ保存・--runでのnode実行。
// 例外境界はコード生成呼び出しのtryに限定する

#include "cmd/cm/driver.hpp"
#include "internal/base/i18n.hpp"
#include "internal/codegen/js/codegen.hpp"
#include "internal/mir/nodes.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#if !defined(_WIN32)
#include <sys/wait.h>
#endif

namespace cm::driver {

int emit_js(BuildContext& ctx, mir::MirProgram& mir) {
    cli::Options& opts = ctx.opts;
    (void)opts;
    if (opts.verbose) {
        std::cout << "=== JavaScript Code Generation ===\n";
    }

    // JavaScript バックエンドオプション設定
    cm::codegen::js::JSCodeGenOptions js_opts;

    // TypeScript出力（--target=ts）: 型注釈とstruct interfaceを付与する
    js_opts.emitTypeScript = (opts.target == "ts");

    // 出力ファイル設定
    if (opts.output_file.empty()) {
        js_opts.outputFile = js_opts.emitTypeScript ? "output.ts" : "output.js";
    } else {
        js_opts.outputFile = opts.output_file;
    }

    js_opts.generateHTML = (opts.target == "web");
    js_opts.verbose = opts.verbose || opts.debug;

    // JavaScript コード生成
    try {
        cm::codegen::js::JSCodeGen codegen(js_opts);
        codegen.compile(mir);

        if (opts.verbose) {
            std::cout << i18n::msgf(i18n::MsgId::CliJavascriptCodeGenerationComplete,
                                    js_opts.outputFile);
        }

        // --runオプションがある場合は実行（Node.js）
        if (opts.run_after_emit && opts.target != "web") {
            std::string run_file = js_opts.outputFile;
            // TS出力はnodeが直接実行できるとは限らない（v23.6未満）ため、型注釈を除去したJSツイン（TSと同一のコード生成結果）を生成して実行する
            if (js_opts.emitTypeScript) {
                cm::codegen::js::JSCodeGenOptions twin_opts = js_opts;
                twin_opts.emitTypeScript = false;
                twin_opts.outputFile = js_opts.outputFile + ".run.js";
                cm::codegen::js::JSCodeGen twin(twin_opts);
                twin.compile(mir);
                run_file = twin_opts.outputFile;
            }
            if (opts.verbose) {
                std::cout << i18n::msgf(i18n::MsgId::CliRunningNode, run_file);
            }
            std::string cmd = "node " + run_file;
            int exec_result = std::system(cmd.c_str());
#if defined(_WIN32)
            return exec_result;  // Windowsでは直接終了コードを返す
#else
            return WEXITSTATUS(exec_result);
#endif
        }
    } catch (const std::exception& e) {
        std::cerr << i18n::msgf(i18n::MsgId::CliJavascriptCodeGenerationError, e.what());
        return 1;
    }

    return kExitSuccess;
}

}  // namespace cm::driver
