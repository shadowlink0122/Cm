// ============================================================
// SVコード生成: エントリポイント（compile）とファイル出力
// ============================================================
// メンバ関数の実体はcodegen/配下に責務別で分割（types.cpp・emitter.cpp・module.cpp・expr.cpp・stmt.cpp）
#include "codegen.hpp"

#include "internal/base/i18n.hpp"
#include "internal/syntax/ast/typedef.hpp"

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace cm::codegen::sv {

// `module NAME;` ヘッダ宣言（本体なしのトップレベルModuleDecl）から
// トップモジュール名を取得する。importの内容はnamespaceに包まれて
// トップレベルには現れないため、最初に見つかったものがメインファイルの宣言
std::string extract_top_module_name(const ast::Program& program) {
    for (const auto& decl : program.declarations) {
        if (!decl) {
            continue;
        }
        if (const auto* mod = decl->as<ast::ModuleDecl>()) {
            if (mod->declarations.empty() && !mod->path.segments.empty()) {
                return mod->path.segments.back();
            }
        }
    }
    return "";
}

SVCodeGen::SVCodeGen(const SVCodeGenOptions& options) : options_(options) {}

// === メインコンパイル処理 ===

void SVCodeGen::compile(const mir::MirProgram& program) {
    // 構造体定義の索引を構築（フィールドのメンバ名アクセスに使用）
    struct_defs_.clear();
    for (const auto& st : program.structs) {
        if (st) {
            struct_defs_[st->name] = st.get();
        }
    }
    // typedef enum として出力される値enum名（type'(expr) キャストの判定用。Tagged Unionは対象外）
    enum_typedef_names_.clear();
    for (const auto& e : program.enums) {
        if (e && !e->is_tagged_union()) {
            enum_typedef_names_.insert(e->name);
        }
    }

    // 非合成型チェック（エラーがあればコンパイル停止）
    if (!validateSynthesizableTypes(program)) {
        throw std::runtime_error(i18n::msg(i18n::MsgId::SvNonSynthesizableTypesDetectedOn));
    }

    validateReservedIdentifiers(program);

    // --sv-warn-nba: 代入済み状態変数の参照（前サイクル値）を警告
    warnNbaReadback(program);

    loop_name_counter_ = 0;
    begin_generation();

    // ファイルヘッダー
    emitFileHeader();

    // MIR解析
    analyzeMIR(program);

    // 各モジュールを出力
    for (const auto& mod : modules_) {
        emitModule(mod);
    }

    generated_code_ = end_generation();

    // ファイルに出力
    writeToFile(generated_code_, options_.outputFile);

    if (options_.verbose) {
        std::cout << i18n::msgf(i18n::MsgId::SvSystemverilogGenerationComplete,
                                options_.outputFile);
        std::cout << i18n::msgf(i18n::MsgId::SvLines, get_stats().total_lines);
        std::cout << i18n::msgf(i18n::MsgId::SvSizeBytes, get_stats().total_bytes);
    }

    // テストベンチ自動生成（テスト内容がある場合のみ。
    // #[test] 関数も //! test: ディレクティブも無いビルドでは
    // 空のテストベンチを出力しない）
    if (!modules_.empty()) {
        std::string tb_code = generateTestbench(modules_[0]);
        testbench_code_ = tb_code;
        if (!tb_code.empty()) {
            std::string tb_path = options_.outputFile;
            auto dot = tb_path.rfind('.');
            if (dot != std::string::npos) {
                tb_path = tb_path.substr(0, dot) + "_tb.sv";
            } else {
                tb_path += "_tb.sv";
            }
            writeToFile(tb_code, tb_path);
        }
    }

    // Gowin制約出力（--emit-constraints指定時）
    if (options_.emitConstraints) {
        std::string stem = options_.outputFile;
        auto dot = stem.rfind('.');
        if (dot != std::string::npos) {
            stem = stem.substr(0, dot);
        }
        std::string cst = generateCST(program);
        std::string cst_path;
        if (!cst.empty()) {
            cst_path = stem + ".cst";
            writeToFile(cst, cst_path);
            if (options_.verbose) {
                std::cout << i18n::msgf(i18n::MsgId::SvPinConstraintsGenerated, cst_path);
            }
        }
        std::string module_name = modules_.empty() ? "top" : modules_[0].name;
        std::string tcl = generateProjectTCL(module_name, options_.outputFile, cst_path);
        if (!tcl.empty()) {
            std::string tcl_path = stem + "_build.tcl";
            writeToFile(tcl, tcl_path);
            if (options_.verbose) {
                std::cout << i18n::msgf(i18n::MsgId::SvProjectScriptGenerated, tcl_path);
            }
        }
    }

    // XDC制約出力（ピン属性があれば）
    std::string xdc = generateXDC(program);
    if (!xdc.empty()) {
        std::string xdc_path = options_.outputFile;
        auto dot = xdc_path.rfind('.');
        if (dot != std::string::npos) {
            xdc_path = xdc_path.substr(0, dot) + ".xdc";
        } else {
            xdc_path += ".xdc";
        }
        writeToFile(xdc, xdc_path);
    }
}

// === ファイル出力 ===

void SVCodeGen::writeToFile(const std::string& content, const std::string& path) {
    std::ofstream ofs(path);
    if (!ofs) {
        std::cerr << i18n::msgf(i18n::MsgId::SvCannotOpenFile, path);
        return;
    }
    ofs << content;
}

}  // namespace cm::codegen::sv
