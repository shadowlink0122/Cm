// ============================================================
// SVコード生成: モジュール出力と定数リテラル・配列初期値/メモリファイル
// ============================================================
#include "internal/base/i18n.hpp"
#include "internal/codegen/sv/codegen.hpp"
#include "internal/codegen/sv/sv_internal.hpp"

#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

namespace cm::codegen::sv {

void SVCodeGen::emitModule(const SVModule& mod) {
    // module宣言（#[sv::parameter] があれば #(parameter ...) ヘッダ付き）
    if (!mod.header_parameters.empty()) {
        emitLine("module " + mod.name + " #(");
        increaseIndent();
        for (size_t i = 0; i < mod.header_parameters.size(); ++i) {
            emitLine(mod.header_parameters[i] + (i + 1 < mod.header_parameters.size() ? "," : ""));
        }
        decreaseIndent();
        emitIndented(") ");
    } else {
        emitIndented("module " + mod.name + " ");
    }

    // ポートリスト
    emitPortList(mod.ports);
    append_line("");

    // Verilatorリント警告の抑止メタコメント。
    // UNUSED/UNDRIVEN は式ツリー化・未使用テンポラリ宣言除去（2026-07-05）で全テスト・実デザインとも警告ゼロを確認したため出力しない。
    // WIDTH系は混合幅演算で依然発生するため既定で抑止する（--sv-strict-lint 指定時はすべて省略し、警告を可視化する）
    if (!options_.strictLint) {
        emitLine("/* verilator lint_off WIDTHTRUNC */");
        emitLine("/* verilator lint_off WIDTHEXPAND */");
        append_line("");
    }

    increaseIndent();

    // ==== 使用判定コーパス ====
    // 未使用localparam・未使用テンポラリの除去のため、モジュール本文（全ブロック・宣言・ポート型）のテキストを集約して識別子の使用を判定する
    std::string usage_corpus;
    for (const auto* vec :
         {&mod.always_ff_blocks, &mod.always_comb_blocks, &mod.always_latch_blocks,
          &mod.assign_statements, &mod.function_blocks, &mod.instance_blocks, &mod.initial_blocks,
          &mod.wire_declarations, &mod.reg_declarations, &mod.type_declarations,
          &mod.header_parameters}) {
        for (const auto& b : *vec) {
            usage_corpus += b;
            usage_corpus += '\n';
        }
    }
    for (const auto& port : mod.ports) {
        usage_corpus += port.sv_type + " " + port.array_suffix + "\n";
    }
    // localparam宣言文から名前を取り出す（" = " より前の最後の識別子トークン）
    auto param_decl_name = [](const std::string& decl) -> std::string {
        auto eq = decl.find(" = ");
        if (eq == std::string::npos) {
            return "";
        }
        std::string head = decl.substr(0, eq);
        // 末尾から識別子トークンを探す（配列サフィックス "[0:N]" 等は読み飛ばす）
        size_t end = head.size();
        while (end > 0) {
            size_t start = head.find_last_of(" \t", end - 1);
            std::string tok = (start == std::string::npos)
                                  ? head.substr(0, end)
                                  : head.substr(start + 1, end - start - 1);
            if (!tok.empty() &&
                (std::isalpha(static_cast<unsigned char>(tok[0])) || tok[0] == '_')) {
                return tok;
            }
            if (start == std::string::npos) {
                break;
            }
            end = start;
        }
        return "";
    };

    // parameter宣言（定数畳み込み後に未使用となったlocalparamは出力しない。
    // localparam同士の参照があり得るため固定点まで反復する）
    std::vector<std::string> live_params = mod.parameters;
    {
        bool removed = true;
        while (removed) {
            removed = false;
            for (size_t i = 0; i < live_params.size(); ++i) {
                std::string name = param_decl_name(live_params[i]);
                if (name.empty()) {
                    continue;
                }
                std::string others;
                for (size_t j = 0; j < live_params.size(); ++j) {
                    if (j != i) {
                        others += live_params[j];
                        others += '\n';
                    }
                }
                if (!contains_identifier(usage_corpus, name) &&
                    !contains_identifier(others, name)) {
                    live_params.erase(live_params.begin() + static_cast<ptrdiff_t>(i));
                    removed = true;
                    break;
                }
            }
        }
    }
    for (const auto& param : live_params) {
        emitLine(param);
    }
    if (!live_params.empty()) {
        append_line("");
    }

    // typedef enum / struct packed 宣言
    for (const auto& td : mod.type_declarations) {
        emitLine(td);
    }
    if (!mod.type_declarations.empty()) {
        append_line("");
    }

    // 内部ワイヤ宣言
    for (const auto& wire : mod.wire_declarations) {
        emitLine(wire);
    }

    // 内部レジスタ宣言。
    // どのブロックでも使用されない _tNNN テンポラリ宣言はここで除去する（式ツリー化によりインライン展開されたテンポラリ。テンポラリ名は関数間で衝突するため、全ブロックのテキストを対象に判定する）
    std::string all_blocks_text;
    for (const auto* vec : {&mod.always_ff_blocks, &mod.always_comb_blocks,
                            &mod.always_latch_blocks, &mod.assign_statements, &mod.function_blocks,
                            &mod.instance_blocks, &mod.initial_blocks}) {
        for (const auto& b : *vec) {
            all_blocks_text += b;
            all_blocks_text += '\n';
        }
    }
    auto temp_used_somewhere = [&](const std::string& name) {
        return contains_identifier(all_blocks_text, name);
    };
    bool emitted_any_reg = false;
    for (const auto& reg : mod.reg_declarations) {
        auto space_pos = reg.rfind(' ');
        auto semi_pos = reg.rfind(';');
        if (space_pos != std::string::npos && semi_pos != std::string::npos &&
            semi_pos > space_pos) {
            std::string var_name = reg.substr(space_pos + 1, semi_pos - space_pos - 1);
            if (var_name.size() > 2 && var_name[0] == '_' && var_name[1] == 't' &&
                std::isdigit(static_cast<unsigned char>(var_name[2])) &&
                !temp_used_somewhere(var_name)) {
                continue;  // 未使用テンポラリ宣言を除去
            }
        }
        emitLine(reg);
        emitted_any_reg = true;
    }

    if (!mod.wire_declarations.empty() || emitted_any_reg) {
        append_line("");
    }

    // always_ff ブロック
    for (const auto& block : mod.always_ff_blocks) {
        // Gowin EDA 互換のため always_ff @ を always @ に置換（--sv-always-ff 指定時は保持し、多重ドライバ検査等のSV機能を活かす）
        std::string modified =
            options_.keepAlwaysFF ? block : replace_all(block, "always_ff @", "always @");
        emit(modified);
        append_line("");
    }

    // always_comb ブロック
    for (const auto& block : mod.always_comb_blocks) {
        // Gowin EDA 互換のため always_comb を always @(*) に置換
        std::string modified = options_.keepAlwaysFF
                                   ? block
                                   : replace_all(block, "always_comb begin", "always @(*) begin");
        emit(modified);
        append_line("");
    }

    // always_latch ブロック
    for (const auto& block : mod.always_latch_blocks) {
        // Gowin EDA 互換のため always_latch を always @(*) に置換
        std::string modified = options_.keepAlwaysFF
                                   ? block
                                   : replace_all(block, "always_latch begin", "always @(*) begin");
        emit(modified);
        append_line("");
    }

    // assign 文
    for (const auto& stmt : mod.assign_statements) {
        emitLine(stmt);
    }

    // extern struct インスタンス化文
    for (const auto& inst : mod.instance_blocks) {
        append_line("");
        // 複数行のインスタンス化文を行ごとに出力
        std::istringstream iss(inst);
        std::string line;
        while (std::getline(iss, line)) {
            emitLine(line);
        }
    }

    // initial ブロック（シミュレーション用）。
    // ビルダーは無インデントで生成し、ここでモジュールインデントを一律付与する
    for (const auto& init : mod.initial_blocks) {
        append_line("");
        std::istringstream init_stream(init);
        std::string init_line;
        while (std::getline(init_stream, init_line)) {
            if (init_line.empty()) {
                append_line("");
            } else {
                emitLine(init_line);
            }
        }
    }

    // function/task ブロック
    for (const auto& fn : mod.function_blocks) {
        append_line("");
        emit(fn);
    }

    decreaseIndent();
    emitLine("endmodule");
    append_line("");
}

// === 定数リテラル ===

std::string SVCodeGen::emitConstant(const mir::MirConstant& constant, const hir::TypePtr& type,
                                    int target_width) {
    if (std::holds_alternative<std::string>(constant.value)) {
        return "\"" + std::get<std::string>(constant.value) + "\"";
    }
    int width = getBitWidth(type);

    if (std::holds_alternative<bool>(constant.value)) {
        return std::get<bool>(constant.value) ? "1'b1" : "1'b0";
    }

    if (std::holds_alternative<int64_t>(constant.value)) {
        int64_t val = std::get<int64_t>(constant.value);

        // SV幅付きリテラルの場合、元のベース形式を保持して出力
        if (constant.bit_info && !constant.bit_info->original.empty()) {
            // target_width が明示幅より大きい場合は拡張（混合幅演算の警告防止）
            int lit_width = constant.bit_info->width;
            if (target_width > 0 && target_width > lit_width) {
                lit_width = target_width;
            }
            // 元の表記をそのまま使用: N'bXXX, N'hXXX, N'dXXX
            return std::to_string(lit_width) + "'" + constant.bit_info->base +
                   constant.bit_info->original;
        }

        // ビット幅決定の優先順位:
        // 1. SV幅付きリテラルの明示幅
        // 2. target_width（相手オペランドの型から推論）
        // 3. 定数自身の型の幅
        int effective_width = (constant.bit_info ? constant.bit_info->width : 0);
        if (effective_width == 0 && target_width > 0)
            effective_width = target_width;
        if (effective_width == 0)
            effective_width = width;
        // signed型かどうか判定（定数の型に従う）。
        // 以前はtarget_width指定時にunsigned扱いにしていたが、SVでは片方がunsignedだと比較全体がunsignedになり、s < 32'd0 のような符号付き
        // 比較が壊れるため、'sd を維持する
        bool is_signed =
            type && (type->kind == hir::TypeKind::Int || type->kind == hir::TypeKind::Short ||
                     type->kind == hir::TypeKind::Tiny || type->kind == hir::TypeKind::Long ||
                     type->kind == hir::TypeKind::ISize);
        std::string prefix = std::to_string(effective_width) + (is_signed ? "'sd" : "'d");
        if (val < 0) {
            return "-" + prefix + std::to_string(-val);
        }
        return prefix + std::to_string(val);
    }

    if (std::holds_alternative<std::monostate>(constant.value)) {
        int ew = (target_width > 0 ? target_width : width);
        return std::to_string(ew) + "'d0";
    }

    return "0";
}

// === 配列初期値のinitialブロック生成 ===

// 配列リテラル初期値を initial ブロックとして生成する。
// FPGA合成ツールはROM/RAMの初期内容として扱い、シミュレーションでは時刻0に初期化される
std::string SVCodeGen::buildArrayInitial(const mir::MirGlobalVar& gv, const std::string& var_name) {
    if (!gv.init_expr) {
        return "";
    }
    const auto* arr = std::get_if<std::unique_ptr<hir::HirArrayLiteral>>(&gv.init_expr->kind);
    if (!arr || !*arr || (*arr)->elements.empty()) {
        return "";
    }
    std::ostringstream ss;
    ss << "initial begin\n";
    for (size_t i = 0; i < (*arr)->elements.size(); ++i) {
        const auto& elem = (*arr)->elements[i];
        if (!elem) {
            continue;
        }
        ss << "    " << var_name << "[" << i << "] = " << emitHirExpr(*elem) << ";\n";
    }
    ss << "end\n";
    return ss.str();
}

// #[sv::memfile("path.hex")] / #[verilog::memfile("path.hex")] 属性からパスを抽出する
std::string SVCodeGen::getMemfilePath(const mir::MirGlobalVar& gv) {
    for (const auto& attr : gv.attributes) {
        for (const char* prefix : {"sv::memfile(\"", "verilog::memfile(\""}) {
            if (attr.rfind(prefix, 0) == 0 && attr.size() >= 2 && attr.back() == ')') {
                // name("path") の path 部分を取り出す
                size_t start = std::string(prefix).size();
                size_t end = attr.find('"', start);
                if (end != std::string::npos) {
                    return attr.substr(start, end - start);
                }
            }
        }
    }
    return "";
}

// #[sv::memfile("f.bin", radix: bin)] の基数指定を取り出す（既定はhex。SV-N8）
static bool memfileRadixIsBin(const mir::MirGlobalVar& gv) {
    for (const auto& attr : gv.attributes) {
        if ((attr.rfind("sv::memfile(", 0) == 0 || attr.rfind("verilog::memfile(", 0) == 0) &&
            attr.find("radix:bin") != std::string::npos) {
            return true;
        }
    }
    return false;
}

// memfile属性があれば $readmemh/$readmemb を、無ければ要素代入のinitialブロックを生成する
std::string SVCodeGen::buildArrayInitialOrReadmem(const mir::MirGlobalVar& gv,
                                                  const std::string& var_name) {
    std::string memfile = getMemfilePath(gv);
    if (memfile.empty()) {
        return buildArrayInitial(gv, var_name);
    }
    emitMemfileIfRequested(gv, memfile);
    // radix: bin 指定は2進メモリファイル（$readmemb）として読み込む
    const char* readmem = memfileRadixIsBin(gv) ? "$readmemb" : "$readmemh";
    return std::string("initial ") + readmem + "(\"" + memfile + "\", " + var_name + ");\n";
}

// --emit-memfile 指定時、配列リテラル初期値を.hexファイルとして書き出す。
// 出力先は生成SVファイルと同じディレクトリ（$readmemhの相対パス解決に合わせる）
void SVCodeGen::emitMemfileIfRequested(const mir::MirGlobalVar& gv,
                                       const std::string& memfile_path) {
    if (!options_.emitMemfile || !gv.init_expr) {
        return;
    }
    const auto* arr = std::get_if<std::unique_ptr<hir::HirArrayLiteral>>(&gv.init_expr->kind);
    if (!arr || !*arr || (*arr)->elements.empty()) {
        return;
    }

    std::filesystem::path out_path(memfile_path);
    if (out_path.is_relative()) {
        std::filesystem::path sv_dir = std::filesystem::path(options_.outputFile).parent_path();
        if (!sv_dir.empty()) {
            out_path = sv_dir / out_path;
        }
    }

    std::ofstream file(out_path);
    if (!file.is_open()) {
        std::cerr << i18n::msgf(i18n::MsgId::SvCannotWriteMemfile, out_path.string());
        return;
    }
    for (const auto& elem : (*arr)->elements) {
        uint64_t value = 0;
        if (elem) {
            if (const auto* lit_ptr = std::get_if<std::unique_ptr<hir::HirLiteral>>(&elem->kind)) {
                const auto& lit = **lit_ptr;
                if (const auto* ival = std::get_if<int64_t>(&lit.value)) {
                    value = static_cast<uint64_t>(*ival);
                } else if (const auto* bval = std::get_if<bool>(&lit.value)) {
                    value = *bval ? 1 : 0;
                } else if (const auto* cval = std::get_if<char>(&lit.value)) {
                    value = static_cast<uint64_t>(static_cast<unsigned char>(*cval));
                }
            }
        }
        // 要素幅に合わせた値を1行1要素で出力（radix: bin なら$readmemb形式の2進、既定は$readmemh形式の16進）
        int width = getBitWidth(gv.type ? gv.type->element_type : nullptr);
        uint64_t mask = (width >= 64) ? ~0ULL : ((1ULL << width) - 1);
        if (memfileRadixIsBin(gv)) {
            int bin_digits = width > 0 ? width : 1;
            std::string bits(static_cast<size_t>(bin_digits), '0');
            for (int b = 0; b < bin_digits; ++b) {
                if ((value >> b) & 1) {
                    bits[static_cast<size_t>(bin_digits - 1 - b)] = '1';
                }
            }
            file << bits << "\n";
        } else {
            int hex_digits = (width + 3) / 4;
            if (hex_digits <= 0) {
                hex_digits = 1;
            }
            file << std::hex << std::setw(hex_digits) << std::setfill('0') << (value & mask)
                 << "\n";
        }
    }
    if (options_.verbose) {
        std::cout << "Generated memfile: " << out_path.string() << "\n";
    }
}

}  // namespace cm::codegen::sv
