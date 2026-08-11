#include "codegen.hpp"

#include "internal/base/i18n.hpp"
#include "internal/base/text_utils.hpp"
#include "internal/mir/analysis/dominators.hpp"
#include "internal/syntax/ast/typedef.hpp"
#include "sv_internal.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

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

// === 型マッピング ===

std::string SVCodeGen::mapType(const hir::TypePtr& type) const {
    if (!type)
        return "logic";

    switch (type->kind) {
        case hir::TypeKind::Bool:
            return "logic";
        case hir::TypeKind::Tiny:
            return "logic signed [7:0]";
        case hir::TypeKind::UTiny:
            return "logic [7:0]";
        case hir::TypeKind::Short:
            return "logic signed [15:0]";
        case hir::TypeKind::UShort:
            return "logic [15:0]";
        case hir::TypeKind::Int:
            return "logic signed [31:0]";
        case hir::TypeKind::UInt:
            return "logic [31:0]";
        case hir::TypeKind::Long:
            return "logic signed [63:0]";
        case hir::TypeKind::ULong:
            return "logic [63:0]";
        case hir::TypeKind::ISize:
            return "logic signed [63:0]";
        case hir::TypeKind::USize:
            return "logic [63:0]";
        // SV固有型
        case hir::TypeKind::Posedge:
        case hir::TypeKind::Negedge:
            return "logic";  // クロック/リセット信号は1bit
        case hir::TypeKind::Wire:
        case hir::TypeKind::Reg:
            // element_typeがあればそれを使用
            if (type->element_type)
                return mapType(type->element_type);
            return "logic [31:0]";
        case hir::TypeKind::Bit:
            return "logic";  // bit単体は1bit、bit[N]はArray処理で幅変換
        case hir::TypeKind::Array:
            // bit[N] → logic [N-1:0] に変換
            if (type->element_type && type->element_type->kind == hir::TypeKind::Bit) {
                // #[sv::parameter] によるサイズ指定は記号のまま出力（bit[WIDTH]）
                if (!type->size_param_name.empty() &&
                    sv_param_names_.count(type->size_param_name) > 0) {
                    return "logic [" + type->size_param_name + "-1:0]";
                }
                if (type->array_size && *type->array_size > 1) {
                    return "logic [" + std::to_string(*type->array_size - 1) + ":0]";
                }
                return "logic";
            }
            // 通常の配列: element_type name [0:N-1] → element_typeだけ返す
            if (type->element_type) {
                return mapType(type->element_type);
            }
            return "logic [31:0]";
        case hir::TypeKind::Struct:
            return type->name;
        case hir::TypeKind::String:
            return "logic [23:0]";
        default:
            return "logic [31:0]";  // デフォルトは32bit
    }
}

int SVCodeGen::getBitWidth(const hir::TypePtr& type) const {
    if (!type)
        return 32;

    switch (type->kind) {
        case hir::TypeKind::Bool:
            return 1;
        case hir::TypeKind::Tiny:
        case hir::TypeKind::UTiny:
            return 8;
        case hir::TypeKind::Short:
        case hir::TypeKind::UShort:
            return 16;
        case hir::TypeKind::Int:
        case hir::TypeKind::UInt:
            return 32;
        case hir::TypeKind::Long:
        case hir::TypeKind::ULong:
        case hir::TypeKind::ISize:
        case hir::TypeKind::USize:
            return 64;
        // SV固有型
        case hir::TypeKind::Posedge:
        case hir::TypeKind::Negedge:
            return 1;  // クロック/リセット信号は1bit
        case hir::TypeKind::Wire:
        case hir::TypeKind::Reg:
            if (type->element_type)
                return getBitWidth(type->element_type);
            return 32;
        case hir::TypeKind::Bit:
            return 1;  // bit単体は1bit
        case hir::TypeKind::Array:
            // bit[N] → Nビット
            if (type->element_type && type->element_type->kind == hir::TypeKind::Bit) {
                return type->array_size.value_or(1);
            }
            if (type->element_type)
                return getBitWidth(type->element_type);
            return 32;
        case hir::TypeKind::String:
            return 24;
        default:
            return 32;
    }
}

// === 配列サフィックス生成 ===

std::string SVCodeGen::getArraySuffix(const hir::TypePtr& type) const {
    if (!type)
        return "";
    // 通常の配列型（非bit配列）の場合、アンパックドディメンションを生成
    // （#[sv::parameter]の記号深度はarray_sizeが無くてもsize_param_nameで出力する）
    if (type->kind == hir::TypeKind::Array &&
        ((type->array_size && *type->array_size > 0) ||
         (!type->size_param_name.empty() && sv_param_names_.count(type->size_param_name) > 0))) {
        // bit[N] は packed dimension として mapType で処理済みなのでスキップ
        if (type->element_type && type->element_type->kind == hir::TypeKind::Bit) {
            return "";
        }
        // #[sv::parameter] によるサイズ指定は記号のまま出力
        if (!type->size_param_name.empty() && sv_param_names_.count(type->size_param_name) > 0) {
            return " [0:" + type->size_param_name + "-1]" + getArraySuffix(type->element_type);
        }
        return " [0:" + std::to_string(*type->array_size - 1) + "]" +
               getArraySuffix(type->element_type);
    }
    return "";
}

// === コード出力ヘルパー ===

void SVCodeGen::emit(const std::string& code) {
    append(code);
}

void SVCodeGen::emitLine(const std::string& code) {
    append_line(indent() + code);
}

void SVCodeGen::emitIndented(const std::string& code) {
    append(indent() + code);
}

void SVCodeGen::increaseIndent() {
    indent_level_++;
}

void SVCodeGen::decreaseIndent() {
    if (indent_level_ > 0)
        indent_level_--;
}

std::string SVCodeGen::indent() const {
    return std::string(indent_level_ * options_.indentSpaces, ' ');
}

// === ファイルヘッダー ===

void SVCodeGen::emitFileHeader() {
    emitLine("// SystemVerilog generated by Cm compiler");
    emitLine("// ターゲット: SystemVerilog IEEE 1800-2017");
    emitLine("`timescale 1ns / 1ps");
    append_line("");
}

// === モジュール生成 ===

void SVCodeGen::emitPortList(const std::vector<SVPort>& ports) {
    if (ports.empty()) {
        emitLine("();");
        return;
    }

    emitLine("(");
    increaseIndent();
    for (size_t i = 0; i < ports.size(); i++) {
        const auto& port = ports[i];
        std::string dir;
        switch (port.direction) {
            case SVPort::Input:
                dir = "input ";
                break;
            case SVPort::Output:
                dir = "output ";
                break;
            case SVPort::InOut:
                dir = "inout ";
                break;
        }
        std::string line = dir + port.sv_type + " " + port.name + port.array_suffix;
        if (!port.init_value.empty()) {
            line += " = " + port.init_value;
        }
        if (i < ports.size() - 1) {
            line += ",";
        }
        emitLine(line);
    }
    decreaseIndent();
    emitLine(");");
}

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

// === Place（左辺値）生成 ===

std::string SVCodeGen::emitPlace(const mir::MirPlace& place, const mir::MirFunction& func) {
    // ローカル変数名を取得
    std::string name;
    if (place.local < func.locals.size()) {
        name = func.locals[place.local].name;
        if (name.empty()) {
            name = "_" + std::to_string(place.local);
        }
    } else {
        name = "_" + std::to_string(place.local);
    }

    // self. プレフィックスを除去（SVでは不要）
    if (name.find("self.") == 0) {
        name = name.substr(5);
    }

    // フィールド/インデックスアクセスの投影を適用
    hir::TypePtr current_type =
        (place.local < func.locals.size()) ? func.locals[place.local].type : nullptr;
    bool first_projection = true;
    for (const auto& proj : place.projections) {
        if (proj.kind == mir::ProjectionKind::Field) {
            // IOインスタンスのフィールドアクセス（io.field）はポート名へ写像する
            auto io_it = io_instance_fields_.find(name);
            if (first_projection && io_it != io_instance_fields_.end() &&
                proj.field_id < io_it->second.size()) {
                name = io_it->second[proj.field_id];
            } else {
                // データ構造体（typedef struct packed）のフィールドはメンバ名でアクセスする。
                // [index] はpacked structではビット選択になり誤った値を読むため使用しない
                const mir::MirStruct* struct_def = nullptr;
                if (current_type && current_type->kind == hir::TypeKind::Struct) {
                    auto struct_it = struct_defs_.find(current_type->name);
                    if (struct_it != struct_defs_.end()) {
                        struct_def = struct_it->second;
                    }
                }
                if (struct_def && proj.field_id < struct_def->fields.size()) {
                    name += "." + struct_def->fields[proj.field_id].name;
                } else {
                    name += "[" + std::to_string(proj.field_id) + "]";
                }
            }
        } else if (proj.kind == mir::ProjectionKind::Index) {
            // 配列インデックス: index_localの変数名で添字アクセス
            if (proj.index_local < func.locals.size()) {
                std::string idx_name;
                // 添字が単一定義テンポラリなら式ツリーをスプライスする（Phase 2）。
                // 文字列スライスの算術式に埋め込まれる場合があるため、原子でない式は括弧で囲む
                auto idx_tree = temp_trees_.find(proj.index_local);
                if (idx_tree != temp_trees_.end()) {
                    idx_name = idx_tree->second->to_string();
                    if (idx_tree->second->kind() != SVExpr::Kind::Atom) {
                        idx_name = "(" + idx_name + ")";
                    }
                } else {
                    idx_name = func.locals[proj.index_local].name;
                }
                // self. プレフィックスを除去
                if (idx_name.find("self.") == 0)
                    idx_name = idx_name.substr(5);

                if (current_type && current_type->kind == hir::TypeKind::String) {
                    int L = 0;
                    std::string base_name = name;
                    auto bracket_pos = base_name.find('[');
                    if (bracket_pos != std::string::npos) {
                        base_name = base_name.substr(0, bracket_pos);
                    }
                    auto it = global_string_lengths_.find(base_name);
                    if (it != global_string_lengths_.end()) {
                        L = it->second;
                    }
                    if (L == 0) {
                        L = getBitWidth(current_type) / 8;
                    }
                    if (L > 0) {
                        name =
                            name + "[(" + std::to_string(L - 1) + " - " + idx_name + ") * 8 +: 8]";
                    } else {
                        name += "[" + idx_name + "]";
                    }
                } else {
                    name += "[" + idx_name + "]";
                }
            }
        }
        // 次のイテレーションのために型を更新
        if (current_type) {
            if (proj.kind == mir::ProjectionKind::Index) {
                if (current_type->kind == hir::TypeKind::Array) {
                    current_type = current_type->element_type;
                } else if (current_type->kind == hir::TypeKind::String) {
                    current_type = nullptr;
                }
            } else if (proj.kind == mir::ProjectionKind::Field) {
                // 構造体フィールドの型を追跡する（ネストした構造体のメンバアクセスに必要）
                const mir::MirStruct* struct_def = nullptr;
                if (current_type->kind == hir::TypeKind::Struct) {
                    auto struct_it = struct_defs_.find(current_type->name);
                    if (struct_it != struct_defs_.end()) {
                        struct_def = struct_it->second;
                    }
                }
                if (struct_def && proj.field_id < struct_def->fields.size()) {
                    current_type = struct_def->fields[proj.field_id].type;
                } else {
                    current_type = nullptr;
                }
            }
        }
        first_projection = false;
    }

    return name;
}

// === オペランド生成 ===

std::string SVCodeGen::emitOperand(const mir::MirOperand& operand, const mir::MirFunction& func,
                                   int target_width) {
    switch (operand.kind) {
        case mir::MirOperand::Move:
        case mir::MirOperand::Copy: {
            // data は variant<MirPlace, MirConstant, string>
            const auto& place = std::get<mir::MirPlace>(operand.data);
            std::string result;
            // 単一定義テンポラリは式ツリーをスプライスする（Phase 2）。
            // 本関数の呼び出し元は if条件・配列添字・呼び出し引数などの
            // 自己区切りコンテキスト、または括弧を自前で管理するツリー経路のため、括弧なしの式文字列で安全
            bool spliced = false;
            if (place.projections.empty()) {
                auto it = temp_trees_.find(place.local);
                if (it != temp_trees_.end()) {
                    result = it->second->to_string();
                    spliced = true;
                }
            }
            if (!spliced) {
                result = emitPlace(place, func);
            }
            // target_width が指定されており、変数のビット幅が狭い場合はキャストを挿入
            // (int(32bit) + ushort(16bit) の混合演算での WIDTHEXPAND 警告防止)
            if (target_width > 0 && operand.type) {
                int var_width = getBitWidth(operand.type);
                if (var_width > 0 && var_width < target_width) {
                    result = std::to_string(target_width) + "'(" + result + ")";
                }
            }
            return result;
        }
        case mir::MirOperand::Constant: {
            const auto& constant = std::get<mir::MirConstant>(operand.data);
            return emitConstant(constant, operand.type, target_width);
        }
        default:
            return "/* unknown operand */";
    }
}

// === 右辺値生成 ===

// === 式ツリー構築（式ツリー化 Phase 1）===

// コンパイラ生成テンポラリの命名規約（_tNNN）に一致するか
static bool is_compiler_temp_name(const std::string& name) {
    return name.size() > 2 && name[0] == '_' && name[1] == 't' &&
           std::isdigit(static_cast<unsigned char>(name[2]));
}

// 関数内で1回だけ代入されるコンパイラ生成テンポラリを収集する。
// これらは定義時に式ツリーを記録し、使用箇所で構造的にインライン展開できる
void SVCodeGen::collectSingleDefTemps(const mir::MirFunction& func) {
    single_def_temps_.clear();
    temp_trees_.clear();

    std::unordered_map<mir::LocalId, int> def_counts;
    for (const auto& block : func.basic_blocks) {
        if (!block) {
            continue;
        }
        for (const auto& stmt : block->statements) {
            if (!stmt || stmt->kind != mir::MirStatement::Assign) {
                continue;
            }
            const auto& ad = std::get<mir::MirStatement::AssignData>(stmt->data);
            if (ad.place.projections.empty()) {
                def_counts[ad.place.local]++;
            }
        }
        // Callの戻り先も定義としてカウント（ツリー化対象からは自然に外れる）
        if (block->terminator &&
            std::holds_alternative<mir::MirTerminator::CallData>(block->terminator->data)) {
            const auto& cd = std::get<mir::MirTerminator::CallData>(block->terminator->data);
            if (cd.destination && cd.destination->projections.empty()) {
                def_counts[cd.destination->local]++;
            }
        }
    }

    for (const auto& [local, count] : def_counts) {
        if (count != 1 || local >= func.locals.size()) {
            continue;
        }
        if (is_compiler_temp_name(func.locals[local].name)) {
            single_def_temps_.insert(local);
        }
    }
}

// オペランドを式ツリーに変換する。
// 単一定義テンポラリへの参照は記録済みツリーをスプライスする
SVExprPtr SVCodeGen::buildOperandTree(const mir::MirOperand& op, const mir::MirFunction& func,
                                      int target_width) {
    if (op.kind == mir::MirOperand::Copy || op.kind == mir::MirOperand::Move) {
        if (const auto* place = std::get_if<mir::MirPlace>(&op.data)) {
            if (place->projections.empty()) {
                auto it = temp_trees_.find(place->local);
                if (it != temp_trees_.end()) {
                    // 幅拡張キャストが必要な場合はキャスト構文で原子化する
                    if (target_width > 0 && op.type) {
                        int var_width = getBitWidth(op.type);
                        if (var_width > 0 && var_width < target_width) {
                            return SVExpr::atom(std::to_string(target_width) + "'(" +
                                                it->second->to_string() + ")");
                        }
                    }
                    return it->second;
                }
            }
        }
    }
    // それ以外は既存のテキスト生成を原子として利用する（信号名・リテラル・投影付きplace等の出力ロジックを共有）
    return SVExpr::atom(emitOperand(op, func, target_width));
}

std::string SVCodeGen::emitRvalue(const mir::MirRvalue& rvalue, const mir::MirFunction& func,
                                  int target_width) {
    switch (rvalue.kind) {
        case mir::MirRvalue::Use: {
            const auto& use_data = std::get<mir::MirRvalue::UseData>(rvalue.data);
            if (use_data.operand) {
                return emitOperand(*use_data.operand, func, target_width);
            }
            return "0";
        }

        case mir::MirRvalue::BinaryOp:
        case mir::MirRvalue::UnaryOp:
        case mir::MirRvalue::Cast:
            // 式ツリー経由で生成する（優先順位括弧はプリンタが構造から決定）
            return buildRvalueTree(rvalue, func, target_width)->to_string();

        case mir::MirRvalue::Ref: {
            // 借用はSVでは参照先の信号そのもの（連接lowering等で発生する）
            const auto& ref_data = std::get<mir::MirRvalue::RefData>(rvalue.data);
            return emitPlace(ref_data.place, func);
        }

        default: {
            // 静かなコメント化は「合法だが意味の違うSV」を生むため明示エラーにする（SV007）
            static const char* kRvalueNames[] = {"Use",       "BinaryOp", "UnaryOp",      "Ref",
                                                 "Aggregate", "Cast",     "FormatConvert"};
            int k = static_cast<int>(rvalue.kind);
            std::string kind_name = (k >= 0 && k <= 6) ? kRvalueNames[k] : std::to_string(k);
            throw std::runtime_error(
                i18n::msgf(i18n::MsgId::SvSv007UnsupportedExpressionOnThe, kind_name));
        }
    }
}

// rvalueを式ツリーに変換する。二項・単項演算はノードとして構築し、オペランド位置の単一定義テンポラリは構造的にインライン展開される。
// その他のrvalue（キャスト・Use等）は既存のテキスト生成を原子として扱う
SVExprPtr SVCodeGen::buildRvalueTree(const mir::MirRvalue& rvalue, const mir::MirFunction& func,
                                     int target_width) {
    switch (rvalue.kind) {
        case mir::MirRvalue::BinaryOp: {
            const auto& bin = std::get<mir::MirRvalue::BinaryOpData>(rvalue.data);
            // ビット幅推論はemitRvalueのテキスト経路と同一ロジック
            int lhs_tw = 0, rhs_tw = 0;
            if (bin.lhs && bin.rhs) {
                if (bin.lhs->kind == mir::MirOperand::Constant &&
                    bin.rhs->kind != mir::MirOperand::Constant && bin.rhs->type) {
                    lhs_tw = getBitWidth(bin.rhs->type);
                }
                if (bin.rhs->kind == mir::MirOperand::Constant &&
                    bin.lhs->kind != mir::MirOperand::Constant && bin.lhs->type) {
                    rhs_tw = getBitWidth(bin.lhs->type);
                }
            }
            if (target_width > 0) {
                if (lhs_tw == 0 && bin.lhs && bin.lhs->kind != mir::MirOperand::Constant)
                    lhs_tw = target_width;
                if (rhs_tw == 0 && bin.rhs && bin.rhs->kind != mir::MirOperand::Constant)
                    rhs_tw = target_width;
            }
            SVExprPtr lhs = bin.lhs ? buildOperandTree(*bin.lhs, func, lhs_tw) : SVExpr::atom("0");
            SVExprPtr rhs = bin.rhs ? buildOperandTree(*bin.rhs, func, rhs_tw) : SVExpr::atom("0");

            // 混合ビット幅の拡張キャスト（キャスト構文は自己完結のため原子化）
            int lhs_w = 0, rhs_w = 0;
            if (bin.lhs && bin.lhs->type)
                lhs_w = getBitWidth(bin.lhs->type);
            if (bin.rhs && bin.rhs->type)
                rhs_w = getBitWidth(bin.rhs->type);
            if (lhs_w > 0 && rhs_w > 0 && lhs_w != rhs_w) {
                int wider = std::max(lhs_w, rhs_w);
                if (lhs_w < rhs_w && bin.lhs->kind != mir::MirOperand::Constant) {
                    lhs = SVExpr::atom(std::to_string(wider) + "'(" + lhs->to_string() + ")");
                } else if (rhs_w < lhs_w && bin.rhs->kind != mir::MirOperand::Constant) {
                    rhs = SVExpr::atom(std::to_string(wider) + "'(" + rhs->to_string() + ")");
                }
            }

            std::string op;
            switch (bin.op) {
                case mir::MirBinaryOp::Add:
                    op = "+";
                    break;
                case mir::MirBinaryOp::Sub:
                    op = "-";
                    break;
                case mir::MirBinaryOp::Mul:
                    op = "*";
                    break;
                case mir::MirBinaryOp::Div:
                    op = "/";
                    break;
                case mir::MirBinaryOp::Mod:
                    op = "%";
                    break;
                case mir::MirBinaryOp::BitAnd:
                    op = "&";
                    break;
                case mir::MirBinaryOp::BitOr:
                    op = "|";
                    break;
                case mir::MirBinaryOp::BitXor:
                    op = "^";
                    break;
                case mir::MirBinaryOp::Shl:
                    op = "<<";
                    break;
                case mir::MirBinaryOp::Shr:
                    op = (bin.lhs && is_signed_type(resolve_operand_type(*bin.lhs, func))) ? ">>>"
                                                                                           : ">>";
                    break;
                case mir::MirBinaryOp::Eq:
                    op = "==";
                    break;
                case mir::MirBinaryOp::Ne:
                    op = "!=";
                    break;
                case mir::MirBinaryOp::Lt:
                    op = "<";
                    break;
                case mir::MirBinaryOp::Le:
                    op = "<=";
                    break;
                case mir::MirBinaryOp::Gt:
                    op = ">";
                    break;
                case mir::MirBinaryOp::Ge:
                    op = ">=";
                    break;
                case mir::MirBinaryOp::And:
                    op = "&&";
                    break;
                case mir::MirBinaryOp::Or:
                    op = "||";
                    break;
                default:
                    return SVExpr::atom(emitRvalue(rvalue, func, target_width));
            }
            return SVExpr::binary(op, std::move(lhs), std::move(rhs));
        }

        case mir::MirRvalue::UnaryOp: {
            const auto& unary = std::get<mir::MirRvalue::UnaryOpData>(rvalue.data);
            SVExprPtr operand =
                unary.operand ? buildOperandTree(*unary.operand, func) : SVExpr::atom("0");
            switch (unary.op) {
                case mir::MirUnaryOp::Neg:
                    return SVExpr::unary("-u", std::move(operand));
                case mir::MirUnaryOp::Not:
                case mir::MirUnaryOp::BitNot:
                    return SVExpr::unary("~", std::move(operand));
                default:
                    return operand;
            }
        }

        case mir::MirRvalue::Cast: {
            const auto& cast = std::get<mir::MirRvalue::CastData>(rvalue.data);
            if (!cast.operand) {
                return SVExpr::atom("0");
            }
            SVExprPtr operand_tree = buildOperandTree(*cast.operand, func);
            // 型名キャスト type'(expr)（SV-N8）: enum典型（typedef enum logic）・packed struct への
            // asキャストは、幅キャストでなくSVの型名キャストとして出力する（ビット→enum/構造体の再解釈を明示する）
            if (cast.target_type && !cast.target_type->name.empty()) {
                const std::string& tname = cast.target_type->name;
                const bool is_enum_typedef = enum_typedef_names_.count(tname) > 0;
                const bool is_packed_struct = cast.target_type->kind == hir::TypeKind::Struct &&
                                              struct_defs_.count(tname) > 0;
                if (is_enum_typedef || is_packed_struct) {
                    return SVExpr::atom(tname + "'(" + operand_tree->to_string() + ")");
                }
            }
            // 整数型への幅変更キャストはSVのサイズキャストとして明示的に出力する。
            // 出力しないと式の途中の縮小キャスト（例: (a + 300) as utiny）の切り捨てが失われ、計算結果そのものが変わってしまう
            int cast_w = is_integer_type(cast.target_type) ? getBitWidth(cast.target_type) : 0;
            if (cast_w > 0) {
                hir::TypePtr source_type = resolve_operand_type(*cast.operand, func);
                int source_w = is_integer_type(source_type) ? getBitWidth(source_type) : 0;
                bool need_size_cast = (source_w != cast_w);
                bool need_sign_cast =
                    source_type && is_signed_type(source_type) != is_signed_type(cast.target_type);
                if (need_size_cast || need_sign_cast) {
                    // キャスト構文は自己完結のため原子として扱う
                    std::string result = operand_tree->to_string();
                    if (need_size_cast) {
                        result = std::to_string(cast_w) + "'(" + result + ")";
                    }
                    if (need_sign_cast) {
                        result = (is_signed_type(cast.target_type) ? "$signed(" : "$unsigned(") +
                                 result + ")";
                    }
                    return SVExpr::atom(result);
                }
            }
            // ラッパー不要ならツリーをそのまま返す（構造・括弧情報を保持）
            return operand_tree;
        }

        default:
            // Use等は既存のテキスト生成に委譲する。
            // Useオペランドがツリー化済みテンポラリの場合はスプライスする
            if (rvalue.kind == mir::MirRvalue::Use) {
                const auto& use_data = std::get<mir::MirRvalue::UseData>(rvalue.data);
                if (use_data.operand) {
                    return buildOperandTree(*use_data.operand, func, target_width);
                }
            }
            return SVExpr::atom(emitRvalue(rvalue, func, target_width));
    }
}

// === 代入完全性解析（式ツリー化 Phase 3）===
// 組み合わせ（Auto）ブロックのラッチ推論に使用する。
// must-assignデータフロー: MustIn(B) = ∩ MustOut(pred)、MustOut(B) = MustIn(B) ∪ gen(B)。各returnブロックのMustOutに含まれない書き込み対象信号が「全パスで代入されない信号」= ラッチ要因
std::vector<std::string> SVCodeGen::findIncompletelyAssignedSignals(const mir::MirFunction& func) {
    const size_t nblocks = func.basic_blocks.size();
    if (nblocks == 0) {
        return {};
    }

    // 後続ブロックの列挙
    auto successors = [&](size_t bid) {
        std::vector<size_t> succs;
        const auto& bb = func.basic_blocks[bid];
        if (!bb || !bb->terminator) {
            return succs;
        }
        const auto& term = *bb->terminator;
        if (std::holds_alternative<mir::MirTerminator::GotoData>(term.data)) {
            succs.push_back(std::get<mir::MirTerminator::GotoData>(term.data).target);
        } else if (std::holds_alternative<mir::MirTerminator::SwitchIntData>(term.data)) {
            const auto& sd = std::get<mir::MirTerminator::SwitchIntData>(term.data);
            for (const auto& [v, t] : sd.targets) {
                succs.push_back(t);
            }
            succs.push_back(sd.otherwise);
        } else if (std::holds_alternative<mir::MirTerminator::CallData>(term.data)) {
            const auto& cd = std::get<mir::MirTerminator::CallData>(term.data);
            succs.push_back(cd.success);
            if (cd.unwind) {
                succs.push_back(*cd.unwind);
            }
        }
        return succs;
    };

    // 到達可能ブロックと先行ブロックマップ
    std::vector<bool> reachable(nblocks, false);
    std::vector<std::vector<size_t>> preds(nblocks);
    {
        std::vector<size_t> work = {0};
        while (!work.empty()) {
            size_t bid = work.back();
            work.pop_back();
            if (bid >= nblocks || !func.basic_blocks[bid] || reachable[bid]) {
                continue;
            }
            reachable[bid] = true;
            for (size_t s : successors(bid)) {
                if (s < nblocks) {
                    preds[s].push_back(bid);
                    work.push_back(s);
                }
            }
        }
    }

    // gen集合: 各ブロックで（投影なしで）代入されるモジュールレベル信号。
    // 配列要素・フィールドへの部分書き込みは全体の代入とみなさない
    auto is_target_signal = [&](mir::LocalId local) {
        return local < func.locals.size() && func.locals[local].is_global;
    };
    std::vector<std::set<mir::LocalId>> gen(nblocks);
    std::set<mir::LocalId> universe;
    for (size_t bid = 0; bid < nblocks; ++bid) {
        if (!reachable[bid] || !func.basic_blocks[bid]) {
            continue;
        }
        for (const auto& stmt : func.basic_blocks[bid]->statements) {
            if (!stmt || stmt->kind != mir::MirStatement::Assign) {
                continue;
            }
            const auto& ad = std::get<mir::MirStatement::AssignData>(stmt->data);
            if (!is_target_signal(ad.place.local)) {
                continue;
            }
            universe.insert(ad.place.local);
            if (ad.place.projections.empty()) {
                gen[bid].insert(ad.place.local);
            }
        }
        // Call戻り先への代入もdefとして扱う
        const auto& bb = func.basic_blocks[bid];
        if (bb->terminator &&
            std::holds_alternative<mir::MirTerminator::CallData>(bb->terminator->data)) {
            const auto& cd = std::get<mir::MirTerminator::CallData>(bb->terminator->data);
            if (cd.destination && is_target_signal(cd.destination->local)) {
                universe.insert(cd.destination->local);
                if (cd.destination->projections.empty()) {
                    gen[bid].insert(cd.destination->local);
                }
            }
        }
    }
    if (universe.empty()) {
        return {};
    }

    // must-assign 固定点反復（entry以外はuniverseで初期化する標準的なmust解析）
    std::vector<std::set<mir::LocalId>> must_out(nblocks, universe);
    {
        bool changed = true;
        int iterations = 0;
        while (changed && iterations < 1000) {
            changed = false;
            ++iterations;
            for (size_t bid = 0; bid < nblocks; ++bid) {
                if (!reachable[bid]) {
                    continue;
                }
                std::set<mir::LocalId> must_in;
                if (bid == 0) {
                    // entry: 何も代入されていない
                } else if (!preds[bid].empty()) {
                    bool first = true;
                    for (size_t p : preds[bid]) {
                        if (first) {
                            must_in = must_out[p];
                            first = false;
                        } else {
                            std::set<mir::LocalId> tmp;
                            std::set_intersection(must_in.begin(), must_in.end(),
                                                  must_out[p].begin(), must_out[p].end(),
                                                  std::inserter(tmp, tmp.begin()));
                            must_in = std::move(tmp);
                        }
                    }
                }
                std::set<mir::LocalId> out = must_in;
                out.insert(gen[bid].begin(), gen[bid].end());
                if (out != must_out[bid]) {
                    must_out[bid] = std::move(out);
                    changed = true;
                }
            }
        }
    }

    // 各returnブロックで未代入の信号を収集
    std::set<mir::LocalId> incomplete;
    for (size_t bid = 0; bid < nblocks; ++bid) {
        if (!reachable[bid] || !func.basic_blocks[bid] || !func.basic_blocks[bid]->terminator) {
            continue;
        }
        if (func.basic_blocks[bid]->terminator->kind != mir::MirTerminator::Return) {
            continue;
        }
        for (mir::LocalId g : universe) {
            if (must_out[bid].count(g) == 0) {
                incomplete.insert(g);
            }
        }
    }

    std::vector<std::string> names;
    for (mir::LocalId g : incomplete) {
        std::string name = func.locals[g].name;
        name = strip_namespace(name);
        names.push_back(name);
    }
    return names;
}

// === 文の生成 ===

std::string SVCodeGen::emitStatement(const mir::MirStatement& stmt, const mir::MirFunction& func) {
    switch (stmt.kind) {
        case mir::MirStatement::Assign: {
            const auto& assign = std::get<mir::MirStatement::AssignData>(stmt.data);
            std::string lhs = emitPlace(assign.place, func);
            // 代入先の型からビット幅を推論し、定数リテラルの幅を合わせる
            int target_w = 0;
            // Placeの型情報を優先使用
            if (assign.place.type) {
                target_w = getBitWidth(assign.place.type);
            } else if (assign.place.local < func.locals.size()) {
                const auto& local_type = func.locals[assign.place.local].type;
                if (local_type) {
                    target_w = getBitWidth(local_type);
                }
            }
            // 32bit(intデフォルト)の場合は定数リテラル幅の調整不要
            // (インライン展開後のコンテキストでは型情報が失われるため、混合幅の解決はCmソース側で型を統一して行う)
            if (target_w == 32)
                target_w = 0;
            // 式ツリーとして構築（単一定義テンポラリは構造的にインライン展開され、優先順位括弧はプリンタが構造から決定する）
            SVExprPtr rhs_tree =
                assign.rvalue ? buildRvalueTree(*assign.rvalue, func, target_w) : SVExpr::atom("0");
            // 単一定義テンポラリへの代入はツリーを記録し、以後の使用箇所で構造的にスプライスする。行自体は出力しない（Phase 2:
            // 従来のテキストベースのインライン展開パスを置き換える）
            if (assign.place.projections.empty() &&
                single_def_temps_.count(assign.place.local) > 0) {
                temp_trees_[assign.place.local] = rhs_tree;
                return "";
            }
            std::string rhs = rhs_tree->to_string();
            // always_ff、async
            // func、またはposedge/negedge型パラメータを持つ関数はノンブロッキング代入
            bool use_nonblocking =
                func.is_async || func.always_kind == mir::MirFunction::AlwaysKind::FF;
            if (use_nonblocking && assign.place.local < func.locals.size()) {
                if (!func.locals[assign.place.local].is_global) {
                    use_nonblocking = false;
                }
            }
            if (!use_nonblocking) {
                bool is_dest_global = true;
                if (assign.place.local < func.locals.size()) {
                    is_dest_global = func.locals[assign.place.local].is_global;
                }
                if (is_dest_global) {
                    for (const auto& local : func.locals) {
                        if (local.is_global)
                            continue;
                        if (local.type && (local.type->kind == hir::TypeKind::Posedge ||
                                           local.type->kind == hir::TypeKind::Negedge)) {
                            use_nonblocking = true;
                            break;
                        }
                    }
                }
            }
            if (use_nonblocking) {
                return lhs + " <= " + rhs + ";";
            } else {
                return lhs + " = " + rhs + ";";
            }
        }
        case mir::MirStatement::StorageLive:
        case mir::MirStatement::StorageDead:
        case mir::MirStatement::Nop:
            return "";  // SVでは不要
        case mir::MirStatement::Asm:
            throw std::runtime_error(i18n::msg(i18n::MsgId::SvSv007InlineAssemblyAsmIs));
        default:
            throw std::runtime_error(i18n::msgf(i18n::MsgId::SvSv007UnsupportedStatementOnThe,
                                                std::to_string(static_cast<int>(stmt.kind))));
    }
}

// === 基本ブロック生成 ===

std::string SVCodeGen::emitBlock(const mir::BasicBlock& block, const mir::MirFunction& func) {
    std::ostringstream ss;
    for (const auto& stmt : block.statements) {
        if (!stmt)
            continue;
        std::string line = emitStatement(*stmt, func);
        if (!line.empty()) {
            ss << indent() << line << "\n";
        }
    }
    return ss.str();
}

// === MIR解析: 関数 → always ブロック ===

// === 合流ブロック探索 ===
// 2つの分岐先から辿って最初に共通する後続ブロックIDを探す
size_t SVCodeGen::findMergeBlock(const mir::MirFunction& func, size_t then_block,
                                 size_t else_block) {
    // 各ブランチから到達可能なブロックを収集
    std::set<size_t> then_reachable;
    std::vector<size_t> work = {then_block};
    while (!work.empty()) {
        size_t bid = work.back();
        work.pop_back();
        if (bid >= func.basic_blocks.size() || !func.basic_blocks[bid])
            continue;
        if (!then_reachable.insert(bid).second)
            continue;
        const auto& bb = *func.basic_blocks[bid];
        if (bb.terminator) {
            if (bb.terminator->kind == mir::MirTerminator::Goto) {
                auto& gd = std::get<mir::MirTerminator::GotoData>(bb.terminator->data);
                work.push_back(gd.target);
            } else if (bb.terminator->kind == mir::MirTerminator::SwitchInt) {
                auto& sd = std::get<mir::MirTerminator::SwitchIntData>(bb.terminator->data);
                for (const auto& [val, target] : sd.targets) {
                    work.push_back(target);
                }
                work.push_back(sd.otherwise);
            } else if (bb.terminator->kind == mir::MirTerminator::Call) {
                auto& cd = std::get<mir::MirTerminator::CallData>(bb.terminator->data);
                work.push_back(cd.success);
            }
        }
    }

    // elseブランチから辿って最初にthen_reachableに含まれるブロックを探す
    work = {else_block};
    std::set<size_t> visited;
    while (!work.empty()) {
        size_t bid = work.back();
        work.pop_back();
        if (bid >= func.basic_blocks.size() || !func.basic_blocks[bid])
            continue;
        if (!visited.insert(bid).second)
            continue;
        if (then_reachable.count(bid) && bid != then_block && bid != else_block) {
            return bid;  // 合流ブロック発見
        }
        const auto& bb = *func.basic_blocks[bid];
        if (bb.terminator) {
            if (bb.terminator->kind == mir::MirTerminator::Goto) {
                auto& gd = std::get<mir::MirTerminator::GotoData>(bb.terminator->data);
                work.push_back(gd.target);
            } else if (bb.terminator->kind == mir::MirTerminator::SwitchInt) {
                // thenブランチ側と同様にSwitchIntの全分岐先を追跡
                auto& sd = std::get<mir::MirTerminator::SwitchIntData>(bb.terminator->data);
                for (const auto& [val, target] : sd.targets) {
                    work.push_back(target);
                }
                work.push_back(sd.otherwise);
            } else if (bb.terminator->kind == mir::MirTerminator::Call) {
                // Call ターミネータの後続ブロックも追跡
                auto& cd = std::get<mir::MirTerminator::CallData>(bb.terminator->data);
                work.push_back(cd.success);
            }
        }
    }

    return SIZE_MAX;  // 合流ブロックなし
}

// === MIR解析: プログラム全体 ===

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

// === テストベンチ自動生成 ===

// === XDC制約ファイル生成 ===

// === 非合成型チェック ===

}  // namespace cm::codegen::sv
