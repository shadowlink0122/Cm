// SystemVerilogコード生成バックエンド: SV出力・階層サブモジュール連結・cm testシミュレーション起動。
// 例外境界はコード生成呼び出しのtryに限定する

#include "../driver.hpp"
#include "internal/base/i18n.hpp"
#include "internal/codegen/sv/codegen.hpp"
#include "internal/codegen/sv/hierarchy.hpp"
#include "internal/mir/nodes.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace cm::driver {

int emit_sv(BuildContext& ctx, mir::MirProgram& mir) {
    cli::Options& opts = ctx.opts;
    (void)opts;
    const std::string& code = ctx.code;
    const std::string& sv_top_module = ctx.sv_top_module;
    const std::vector<std::string>& sv_hierarchy_submodules = ctx.sv_hierarchy_submodules;
    const bool run_sv_sim = ctx.run_sv_sim;
    const auto compile_start = ctx.compile_start;

    if (opts.verbose) {
        std::cout << "=== SystemVerilog Code Generation ===\n";
    }

    // SVバックエンドオプション設定
    cm::codegen::sv::SVCodeGenOptions sv_opts;

    // 出力ファイル設定
    if (opts.output_file.empty()) {
        // デフォルト出力先は.tmp/（ルートディレクトリを汚さない）
        std::filesystem::create_directories(".tmp");
        sv_opts.outputFile = ".tmp/output.sv";
    } else {
        sv_opts.outputFile = opts.output_file;
    }

    sv_opts.verbose = opts.verbose || opts.debug;
    sv_opts.sourceFile = opts.input_file;
    sv_opts.topModule = sv_top_module;
    sv_opts.emitMemfile = opts.emit_memfile;
    sv_opts.strictLint = opts.sv_strict_lint;
    sv_opts.keepAlwaysFF = opts.sv_always_ff;
    sv_opts.warnNba = opts.sv_warn_nba;
    sv_opts.emitConstraints = opts.emit_constraints;
    {
        // //! sv: device: / option: ディレクティブを反映
        auto dirs = codegen::sv::parse_sv_project_directives(code);
        sv_opts.devicePN = dirs.device_pn;
        sv_opts.deviceVersion = dirs.device_version;
        sv_opts.toolOptions = dirs.tool_options;
    }

    // SystemVerilog コード生成
    try {
        cm::codegen::sv::SVCodeGen codegen(sv_opts);
        codegen.compile(mir);

        // sv階層化: サブモジュールを個別コンパイルして連結
        if (!sv_hierarchy_submodules.empty()) {
            std::string hier_error;
            if (!codegen::sv::append_submodules(ctx.argv0, opts.input_file, sv_hierarchy_submodules,
                                                sv_opts.outputFile, opts.optimization_level,
                                                opts.emit_memfile, hier_error)) {
                std::cerr << i18n::msgf(i18n::MsgId::CliSvHierarchyError, hier_error);
                return 1;
            }
            if (!opts.quiet) {
                std::cout << i18n::msgf(i18n::MsgId::CliConcatenatedSubmoduleS,
                                        sv_hierarchy_submodules.size());
            }
        }

        if (!opts.quiet) {
            auto compile_end = std::chrono::steady_clock::now();
            auto compile_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(compile_end - compile_start)
                    .count();
            std::cout << i18n::msgf(i18n::MsgId::CliSystemverilogGenerationCompleteMs,
                                    sv_opts.outputFile, compile_ms);
        }

        // cm test (SVフロー): 生成物をiverilog+vvpでシミュレーション実行
        if (run_sv_sim) {
            return run_sv_test_simulation(sv_opts.outputFile, opts.quiet);
        }
    } catch (const std::exception& e) {
        std::cerr << i18n::msgf(i18n::MsgId::CliSystemverilogCodeGenerationError, e.what());
        return 1;
    }

    return kExitSuccess;
}

}  // namespace cm::driver
