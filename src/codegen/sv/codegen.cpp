#include "codegen.hpp"

#include "../../mir/analysis/dominators.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>

namespace cm::codegen::sv {

namespace {
// 文字列内の特定の部分文字列をすべて別の文字列に置換する
std::string replace_all(std::string str, const std::string& from, const std::string& to) {
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length();
    }
    return str;
}

// 符号付き整数型であるか判定
bool is_signed_type(const hir::TypePtr& type) {
    if (!type)
        return false;
    switch (type->kind) {
        case hir::TypeKind::Tiny:
        case hir::TypeKind::Short:
        case hir::TypeKind::Int:
        case hir::TypeKind::Long:
        case hir::TypeKind::ISize:
            return true;
        case hir::TypeKind::Wire:
        case hir::TypeKind::Reg:
            return type->element_type && is_signed_type(type->element_type);
        default:
            return false;
    }
}

// オペランドの型を解決する
// （operand.typeが未設定の場合はローカル変数宣言の型を参照する）
hir::TypePtr resolve_operand_type(const mir::MirOperand& op, const mir::MirFunction& func) {
    if (op.type)
        return op.type;
    if (op.kind == mir::MirOperand::Copy || op.kind == mir::MirOperand::Move) {
        if (const auto* place = std::get_if<mir::MirPlace>(&op.data)) {
            if (place->projections.empty() && place->local < func.locals.size()) {
                return func.locals[place->local].type;
            }
        }
    }
    return nullptr;
}

// ブロックのターミネータが遷移しうる後続ブロックを列挙する
std::vector<size_t> terminator_targets(const mir::BasicBlock& bb) {
    std::vector<size_t> succs;
    if (!bb.terminator)
        return succs;
    switch (bb.terminator->kind) {
        case mir::MirTerminator::Goto:
            succs.push_back(std::get<mir::MirTerminator::GotoData>(bb.terminator->data).target);
            break;
        case mir::MirTerminator::SwitchInt: {
            const auto& sd = std::get<mir::MirTerminator::SwitchIntData>(bb.terminator->data);
            for (const auto& [val, target] : sd.targets) {
                succs.push_back(target);
            }
            succs.push_back(sd.otherwise);
            break;
        }
        case mir::MirTerminator::Call: {
            const auto& cd = std::get<mir::MirTerminator::CallData>(bb.terminator->data);
            succs.push_back(cd.success);
            break;
        }
        default:
            break;
    }
    return succs;
}

// 関数内の全ループヘッダとそのラッチ（後方エッジの始点）を一括計算する。
// 後方エッジ = ヘッダが支配するブロックからヘッダへ入るエッジ。
// DominatorTreeの構築はO(ブロック数^2)級のため、関数ごとに1回だけ呼ぶこと
std::unordered_map<size_t, std::vector<size_t>> compute_loop_latches(const mir::MirFunction& func) {
    std::unordered_map<size_t, std::vector<size_t>> latches;
    if (func.basic_blocks.empty()) {
        return latches;
    }
    mir::DominatorTree domtree(func);
    for (size_t p = 0; p < func.basic_blocks.size(); ++p) {
        if (!func.basic_blocks[p])
            continue;
        for (size_t succ : terminator_targets(*func.basic_blocks[p])) {
            if (domtree.dominates(succ, p)) {
                latches[succ].push_back(p);
            }
        }
    }
    return latches;
}

// start が header の自然ループに属するか判定する。
// 「header を通らずにいずれかのラッチへ到達できる」ことが条件。
// （単純な到達可能性では、外側ループのバックエッジ経由で
//   ループ外からもヘッダに戻れてしまい誤判定する）
bool in_natural_loop(const mir::MirFunction& func, size_t start, size_t header,
                     const std::vector<size_t>& latches) {
    std::set<size_t> latch_set(latches.begin(), latches.end());
    std::set<size_t> seen;
    std::vector<size_t> work = {start};
    while (!work.empty()) {
        size_t bid = work.back();
        work.pop_back();
        if (bid == header)
            continue;  // ヘッダは通過しない
        if (latch_set.count(bid))
            return true;
        if (bid >= func.basic_blocks.size() || !func.basic_blocks[bid])
            continue;
        if (!seen.insert(bid).second)
            continue;
        for (size_t succ : terminator_targets(*func.basic_blocks[bid])) {
            work.push_back(succ);
        }
    }
    return false;
}

// 固定幅の整数型であるか判定（サイズキャスト出力の対象判定用）
bool is_integer_type(const hir::TypePtr& type) {
    if (!type)
        return false;
    switch (type->kind) {
        case hir::TypeKind::Tiny:
        case hir::TypeKind::UTiny:
        case hir::TypeKind::Short:
        case hir::TypeKind::UShort:
        case hir::TypeKind::Int:
        case hir::TypeKind::UInt:
        case hir::TypeKind::Long:
        case hir::TypeKind::ULong:
        case hir::TypeKind::ISize:
        case hir::TypeKind::USize:
            return true;
        default:
            return false;
    }
}
}  // namespace

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
    if (type->kind == hir::TypeKind::Array && type->array_size && *type->array_size > 0) {
        // bit[N] は packed dimension として mapType で処理済みなのでスキップ
        if (type->element_type && type->element_type->kind == hir::TypeKind::Bit) {
            return "";
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
        if (i < ports.size() - 1) {
            line += ",";
        }
        emitLine(line);
    }
    decreaseIndent();
    emitLine(");");
}

void SVCodeGen::emitModule(const SVModule& mod) {
    // module宣言
    emitIndented("module " + mod.name + " ");

    // ポートリスト
    emitPortList(mod.ports);
    append_line("");

    // Verilator リント警告の一括無視メタコメントをモジュール内に挿入
    emitLine("/* verilator lint_off UNUSED */");
    emitLine("/* verilator lint_off WIDTHTRUNC */");
    emitLine("/* verilator lint_off WIDTHEXPAND */");
    emitLine("/* verilator lint_off UNDRIVEN */");
    append_line("");

    increaseIndent();

    // parameter宣言
    for (const auto& param : mod.parameters) {
        emitLine(param);
    }
    if (!mod.parameters.empty()) {
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
    // どのブロックでも使用されない _tNNN テンポラリ宣言はここで除去する
    // （式ツリー化によりインライン展開されたテンポラリ。テンポラリ名は
    //   関数間で衝突するため、全ブロックのテキストを対象に判定する）
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
        size_t pos = 0;
        while ((pos = all_blocks_text.find(name, pos)) != std::string::npos) {
            bool at_start = (pos == 0 || (!std::isalnum(all_blocks_text[pos - 1]) &&
                                          all_blocks_text[pos - 1] != '_'));
            size_t after = pos + name.size();
            bool at_end =
                (after >= all_blocks_text.size() ||
                 (!std::isalnum(all_blocks_text[after]) && all_blocks_text[after] != '_'));
            if (at_start && at_end) {
                return true;
            }
            pos += name.size();
        }
        return false;
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
        // Gowin EDA 互換のため always_ff @ を always @ に置換
        std::string modified = replace_all(block, "always_ff @", "always @");
        emit(modified);
        append_line("");
    }

    // always_comb ブロック
    for (const auto& block : mod.always_comb_blocks) {
        // Gowin EDA 互換のため always_comb を always @(*) に置換
        std::string modified = replace_all(block, "always_comb begin", "always @(*) begin");
        emit(modified);
        append_line("");
    }

    // always_latch ブロック
    for (const auto& block : mod.always_latch_blocks) {
        // Gowin EDA 互換のため always_latch を always @(*) に置換
        std::string modified = replace_all(block, "always_latch begin", "always @(*) begin");
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

    // initial ブロック（シミュレーション用）
    for (const auto& init : mod.initial_blocks) {
        append_line("");
        emit(init);
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
        // 以前はtarget_width指定時にunsigned扱いにしていたが、SVでは片方が
        // unsignedだと比較全体がunsignedになり、s < 32'd0 のような符号付き
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
// FPGA合成ツールはROM/RAMの初期内容として扱い、
// シミュレーションでは時刻0に初期化される
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

// memfile属性があれば $readmemh を、無ければ要素代入のinitialブロックを生成する
std::string SVCodeGen::buildArrayInitialOrReadmem(const mir::MirGlobalVar& gv,
                                                  const std::string& var_name) {
    std::string memfile = getMemfilePath(gv);
    if (memfile.empty()) {
        return buildArrayInitial(gv, var_name);
    }
    emitMemfileIfRequested(gv, memfile);
    return "initial $readmemh(\"" + memfile + "\", " + var_name + ");\n";
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
        std::cerr << "警告: memfileを書き出せません: " << out_path.string() << "\n";
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
        // 要素幅に合わせた16進値を1行1要素で出力（$readmemh形式）
        int width = getBitWidth(gv.type ? gv.type->element_type : nullptr);
        int hex_digits = (width + 3) / 4;
        if (hex_digits <= 0) {
            hex_digits = 1;
        }
        uint64_t mask = (width >= 64) ? ~0ULL : ((1ULL << width) - 1);
        file << std::hex << std::setw(hex_digits) << std::setfill('0') << (value & mask) << "\n";
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
    for (const auto& proj : place.projections) {
        if (proj.kind == mir::ProjectionKind::Field) {
            name += "[" + std::to_string(proj.field_id) + "]";
        } else if (proj.kind == mir::ProjectionKind::Index) {
            // 配列インデックス: index_localの変数名で添字アクセス
            if (proj.index_local < func.locals.size()) {
                std::string idx_name;
                // 添字が単一定義テンポラリなら式ツリーをスプライスする（Phase 2）。
                // 文字列スライスの算術式に埋め込まれる場合があるため、
                // 原子でない式は括弧で囲む
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
                current_type = nullptr;
            }
        }
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
            // 自己区切りコンテキスト、または括弧を自前で管理するツリー経路のため、
            // 括弧なしの式文字列で安全
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
    // それ以外は既存のテキスト生成を原子として利用する
    // （信号名・リテラル・投影付きplace等の出力ロジックを共有）
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

        default:
            return "/* unsupported rvalue */";
    }
}

// rvalueを式ツリーに変換する。二項・単項演算はノードとして構築し、
// オペランド位置の単一定義テンポラリは構造的にインライン展開される。
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
            // 整数型への幅変更キャストはSVのサイズキャストとして明示的に出力する。
            // 出力しないと式の途中の縮小キャスト（例: (a + 300) as utiny）の
            // 切り捨てが失われ、計算結果そのものが変わってしまう
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
// must-assignデータフロー: MustIn(B) = ∩ MustOut(pred)、
// MustOut(B) = MustIn(B) ∪ gen(B)。各returnブロックのMustOutに
// 含まれない書き込み対象信号が「全パスで代入されない信号」= ラッチ要因
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
        auto ns_pos = name.rfind("::");
        if (ns_pos != std::string::npos) {
            name = name.substr(ns_pos + 2);
        }
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
            // (インライン展開後のコンテキストでは型情報が失われるため、
            //  混合幅の解決はCmソース側で型を統一して行う)
            if (target_w == 32)
                target_w = 0;
            // 式ツリーとして構築（単一定義テンポラリは構造的にインライン展開され、
            // 優先順位括弧はプリンタが構造から決定する）
            SVExprPtr rhs_tree =
                assign.rvalue ? buildRvalueTree(*assign.rvalue, func, target_w) : SVExpr::atom("0");
            // 単一定義テンポラリへの代入はツリーを記録し、以後の使用箇所で
            // 構造的にスプライスする。行自体は出力しない（Phase 2:
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
        default:
            return "// unsupported statement";
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

void SVCodeGen::analyzeFunction(const mir::MirFunction& func, SVModule& mod) {
    // main関数はスキップ（ハードウェアにmainはない）
    if (func.name == "main")
        return;

    // std::debug::assert はイントリンシック（呼び出し箇所で即時アサーションに展開）。
    // 定義本体は出力しない（assertはSVの予約語でもある）
    if (func.name == "assert")
        return;

    // 非always/非async関数で、非void（戻り値あり）の場合 → SV function automatic
    // void関数は always_comb / always_ff にフォールスルー
    if (!func.is_always && !func.is_async &&
        func.always_kind == mir::MirFunction::AlwaysKind::None) {
        // edgeパラメータの有無を確認
        bool has_edge_param = false;
        for (auto arg_id : func.arg_locals) {
            if (arg_id < func.locals.size()) {
                auto& local = func.locals[arg_id];
                if (local.type && (local.type->kind == hir::TypeKind::Posedge ||
                                   local.type->kind == hir::TypeKind::Negedge)) {
                    has_edge_param = true;
                }
            }
        }

        // 非void関数（戻り値あり）→ SV function automatic
        bool is_void = true;
        std::string ret_type_str = "void";
        if (func.return_local < func.locals.size()) {
            auto& ret_local = func.locals[func.return_local];
            if (ret_local.type && ret_local.type->kind != hir::TypeKind::Void) {
                is_void = false;
                ret_type_str = mapType(ret_local.type);
            }
        }

        if (!is_void && !has_edge_param) {
            std::ostringstream fn_ss;
            indent_level_ = 1;

            // 関数名のnamespace::フラット化（import時の alu_lib::add → add）
            std::string flat_func_name = func.name;
            auto fn_ns = flat_func_name.rfind("::");
            if (fn_ns != std::string::npos) {
                flat_func_name = flat_func_name.substr(fn_ns + 2);
            }

            if (flat_func_name == "stringToUint") {
                std::ostringstream fn_ss;
                fn_ss
                    << "    function automatic logic [31:0] stringToUint(input logic [23:0] s);\n";
                fn_ss << "        return {8'd0, s};\n";
                fn_ss << "    endfunction\n";
                mod.function_blocks.push_back(fn_ss.str());
                return;
            }

            // 引数リスト構築（posedge/negedge型を除外）
            std::vector<std::string> args;
            std::set<std::string> arg_names;  // 引数名の重複チェック用
            for (auto arg_id : func.arg_locals) {
                if (arg_id < func.locals.size()) {
                    auto& local = func.locals[arg_id];
                    if (local.type && (local.type->kind == hir::TypeKind::Posedge ||
                                       local.type->kind == hir::TypeKind::Negedge))
                        continue;
                    args.push_back("input " + mapType(local.type) + " " + local.name);
                    arg_names.insert(local.name);
                }
            }

            fn_ss << indent() << "function automatic " << ret_type_str << " " << flat_func_name
                  << "(";
            for (size_t i = 0; i < args.size(); ++i) {
                if (i > 0)
                    fn_ss << ", ";
                fn_ss << args[i];
            }
            fn_ss << ");\n";

            // ローカル変数宣言（引数と戻り値を除く、テンポラリ変数は後で除去）
            increaseIndent();
            std::set<mir::LocalId> arg_set(func.arg_locals.begin(), func.arg_locals.end());
            // 一旦全ローカル変数を記録（テンポラリは後でスキップ判定）
            std::vector<std::pair<size_t, std::string>> local_decls;
            for (size_t i = 0; i < func.locals.size(); ++i) {
                if (i == func.return_local)
                    continue;
                if (arg_set.count(static_cast<mir::LocalId>(i)))
                    continue;
                auto& local = func.locals[i];
                if (local.name.empty() || local.name.find('@') != std::string::npos)
                    continue;
                // import/export時にグローバル定数がローカルとして混入するのを防止
                if (local.is_global)
                    continue;
                // 引数と同名のローカル変数はスキップ（関数引数の重複宣言防止）
                if (arg_names.count(local.name))
                    continue;
                // ポインタ型テンポラリはスキップ
                if (local.name.find("_t") == 0 && local.type &&
                    local.type->kind == hir::TypeKind::Pointer)
                    continue;
                local_decls.push_back({i, mapType(local.type) + " " + local.name + ";"});
            }

            // 関数本体 — テンポラリ変数のインライン展開
            std::string body_content;
            if (!func.basic_blocks.empty() && func.basic_blocks[0]) {
                // ループヘッダ情報とテンポラリ情報を関数ごとに1回だけ計算する
                collectSingleDefTemps(func);
                current_loop_latches_ = compute_loop_latches(func);
                std::set<size_t> visited;
                std::ostringstream body_ss;
                emitBlockRecursive(func, 0, visited, body_ss);
                std::string raw_body = body_ss.str();

                // @return → 関数名 に置換（フラット化済み名前を使用）
                size_t pos = 0;
                while ((pos = raw_body.find("@return", pos)) != std::string::npos) {
                    raw_body.replace(pos, 7, flat_func_name);
                    pos += flat_func_name.size();
                }

                // 式ツリー化（Phase 2）により単一定義テンポラリは
                // 出力時に構造的へインライン展開済み。
                // テキストベースの再展開パスは不要になった
                body_content = raw_body;

                // 本体で使用されなくなったテンポラリ宣言を除去
                auto is_word_used = [&](const std::string& name) {
                    size_t p = 0;
                    while ((p = body_content.find(name, p)) != std::string::npos) {
                        bool at_start = (p == 0 || (!std::isalnum(body_content[p - 1]) &&
                                                    body_content[p - 1] != '_'));
                        size_t after = p + name.size();
                        bool at_end =
                            (after >= body_content.size() ||
                             (!std::isalnum(body_content[after]) && body_content[after] != '_'));
                        if (at_start && at_end) {
                            return true;
                        }
                        p += name.size();
                    }
                    return false;
                };
                auto decl_it = local_decls.begin();
                while (decl_it != local_decls.end()) {
                    auto& local = func.locals[decl_it->first];
                    if (local.name.size() > 2 && local.name[0] == '_' && local.name[1] == 't' &&
                        std::isdigit(local.name[2]) && !is_word_used(local.name)) {
                        decl_it = local_decls.erase(decl_it);
                    } else {
                        ++decl_it;
                    }
                }
            }

            // ローカル変数宣言を出力
            for (const auto& decl : local_decls) {
                fn_ss << indent() << decl.second << "\n";
            }

            // 展開済みの関数本体を出力
            fn_ss << body_content;

            decreaseIndent();
            fn_ss << indent() << "endfunction\n";

            mod.function_blocks.push_back(fn_ss.str());
            return;
        }  // if (!is_void && !has_edge_param)
    }

    // ローカル変数を内部ワイヤ/レジスタとして宣言
    // （ポートと名前が衝突する変数は除外）
    std::set<std::string> port_names;
    for (const auto& port : mod.ports) {
        port_names.insert(port.name);
    }
    for (const auto& local : func.locals) {
        std::string name = local.name;
        if (name.empty() || name == "_0")
            continue;  // 戻り値用
        // 不正なSV識別子をスキップ（@return等）
        if (name.find('@') != std::string::npos)
            continue;
        // self.プレフィックスを除去
        if (name.find("self.") == 0)
            name = name.substr(5);
        // ポートと名前が衝突する場合はスキップ
        if (port_names.count(name))
            continue;
        // extern struct インスタンスと同名の変数はスキップ
        bool is_instance_var = false;
        for (const auto& inst : mod.instance_blocks) {
            if (inst.find(" " + name + " ") != std::string::npos ||
                inst.find(" " + name + ";") != std::string::npos) {
                is_instance_var = true;
                break;
            }
        }
        if (is_instance_var)
            continue;
        // parameter宣言と名前が衝突する場合はスキップ
        bool is_param_var = false;
        for (const auto& param : mod.parameters) {
            if (param.find(" " + name + " ") != std::string::npos ||
                param.find(" " + name + ";") != std::string::npos) {
                is_param_var = true;
                break;
            }
        }
        if (is_param_var)
            continue;
        // 既に登録済みの宣言もスキップ（変数名の部分一致で検出）
        std::string decl = mapType(local.type) + " " + name + getArraySuffix(local.type) + ";";
        bool already_declared = false;
        for (const auto& existing : mod.reg_declarations) {
            // 完全一致またはBRAM/LutRAM属性付き宣言で同名変数がある場合もスキップ
            if (existing == decl || existing.find(" " + name + " ") != std::string::npos ||
                existing.find(" " + name + ";") != std::string::npos) {
                already_declared = true;
                break;
            }
        }
        if (!already_declared) {
            for (const auto& existing : mod.wire_declarations) {
                if (existing.find(" " + name + " ") != std::string::npos ||
                    existing.find(" " + name + ";") != std::string::npos) {
                    already_declared = true;
                    break;
                }
            }
        }
        if (!already_declared) {
            mod.reg_declarations.push_back(decl);
        }
    }

    std::ostringstream block_ss;

    // モジュール内のインデントレベルを設定
    indent_level_ = 1;

    // 関数名コメントを追加（namespace::プレフィックスをフラット化）
    std::string display_name = func.name;
    auto dn_ns = display_name.rfind("::");
    if (dn_ns != std::string::npos) {
        display_name = display_name.substr(dn_ns + 2);
    }
    block_ss << indent() << "// " << display_name << "\n";

    // SV固有型: posedge/negedge型パラメータの検出
    std::string edge_type;   // "posedge" or "negedge"
    std::string edge_clock;  // クロック信号名
    bool has_explicit_edge = false;

    // 複数エッジ: 非同期リセット用 (always void f(posedge clk, negedge rst_n))
    std::vector<std::pair<std::string, std::string>> all_edges;  // {edge_type, signal_name}

    for (const auto& local : func.locals) {
        if (local.is_global)
            continue;
        if (local.type && local.type->kind == hir::TypeKind::Posedge) {
            // 重複排除: 同名信号が既にある場合はスキップ
            bool dup = false;
            for (const auto& e : all_edges) {
                if (e.second == local.name) {
                    dup = true;
                    break;
                }
            }
            if (!dup) {
                if (!has_explicit_edge) {
                    edge_type = "posedge";
                    edge_clock = local.name;
                    has_explicit_edge = true;
                }
                all_edges.push_back({"posedge", local.name});
            }
        }
        if (local.type && local.type->kind == hir::TypeKind::Negedge) {
            // 重複排除: 同名信号が既にある場合はスキップ
            bool dup = false;
            for (const auto& e : all_edges) {
                if (e.second == local.name) {
                    dup = true;
                    break;
                }
            }
            if (!dup) {
                if (!has_explicit_edge) {
                    edge_type = "negedge";
                    edge_clock = local.name;
                    has_explicit_edge = true;
                }
                all_edges.push_back({"negedge", local.name});
            }
        }
    }

    if (has_explicit_edge) {
        // 明示的なposedge/negedge型パラメータ → always_ff
        if (all_edges.size() > 1) {
            // 複数エッジ: always_ff @(posedge clk or negedge rst_n)
            block_ss << indent() << "always_ff @(";
            for (size_t i = 0; i < all_edges.size(); ++i) {
                if (i > 0)
                    block_ss << " or ";
                block_ss << all_edges[i].first << " " << all_edges[i].second;
            }
            block_ss << ") begin\n";
        } else {
            block_ss << indent() << "always_ff @(" << edge_type << " " << edge_clock << ") begin\n";
        }
    } else if (func.is_always && !has_explicit_edge) {
        // always修飾子 + エッジパラメータなし
        using AK = mir::MirFunction::AlwaysKind;
        if (func.always_kind == AK::Comb) {
            // always_comb 明示指定
            block_ss << indent() << "always_comb begin\n";
        } else if (func.always_kind == AK::Latch) {
            // always_latch 明示指定
            block_ss << indent() << "always_latch begin\n";
        } else {
            // AutoまたはNone: 後でCFG解析で判別（一旦always_combとして出力し後で置換）
            block_ss << indent() << "always_comb begin\n";
        }
    } else if (func.always_kind == mir::MirFunction::AlwaysKind::FF) {
        // always_ff 明示指定（エッジパラメータなし）→ デフォルト posedge clk
        std::string clock_name = "clk";
        for (const auto& attr : func.attributes) {
            std::string prefix1 = "sv::clock_domain(";
            std::string prefix2 = "verilog::clock_domain(";
            if (attr.find(prefix1) == 0 && attr.back() == ')') {
                clock_name = attr.substr(prefix1.size(), attr.size() - prefix1.size() - 1);
            } else if (attr.find(prefix2) == 0 && attr.back() == ')') {
                clock_name = attr.substr(prefix2.size(), attr.size() - prefix2.size() - 1);
            }
        }
        block_ss << indent() << "always_ff @(posedge " << clock_name << ") begin\n";
    } else if (func.is_always || func.is_async) {
        // always修飾子+エッジあり、またはasync修飾子（後方互換）→ always_ff @(posedge clk)
        // Phase 4: マルチクロックドメイン対応
        std::string clock_name = "clk";
        for (const auto& attr : func.attributes) {
            std::string prefix1 = "sv::clock_domain(";
            std::string prefix2 = "verilog::clock_domain(";
            if (attr.find(prefix1) == 0 && attr.back() == ')') {
                clock_name = attr.substr(prefix1.size(), attr.size() - prefix1.size() - 1);
            } else if (attr.find(prefix2) == 0 && attr.back() == ')') {
                clock_name = attr.substr(prefix2.size(), attr.size() - prefix2.size() - 1);
            }
        }

        for (const auto& attr : func.attributes) {
            if (attr.find("sv::pipeline") != std::string::npos ||
                attr.find("verilog::pipeline") != std::string::npos) {
                block_ss << indent() << "// synthesis attribute: " << attr << "\n";
            }
            if (attr.find("sv::share") != std::string::npos ||
                attr.find("verilog::share") != std::string::npos) {
                block_ss << indent() << "// synthesis attribute: resource sharing enabled\n";
            }
        }

        block_ss << indent() << "always_ff @(posedge " << clock_name << ") begin\n";
    } else {
        block_ss << indent() << "always_comb begin\n";
    }

    increaseIndent();

    // CFG再帰走査でブロックを構造化出力
    std::ostringstream raw_ss;
    if (!func.basic_blocks.empty() && func.basic_blocks[0]) {
        // ループヘッダ情報とテンポラリ情報を関数ごとに1回だけ計算する
        collectSingleDefTemps(func);
        current_loop_latches_ = compute_loop_latches(func);
        std::set<size_t> visited;
        emitBlockRecursive(func, 0, visited, raw_ss);
    }

    // 式ツリー化（Phase 2）により単一定義テンポラリは出力時に
    // 構造的へインライン展開済み。テキストベースのインライン展開パス
    // （Pass1/Pass2）は不要になった
    block_ss << raw_ss.str();

    decreaseIndent();
    block_ss << indent() << "end\n";

    // 未使用テンポラリ宣言の除去はモジュール出力時に全ブロックを対象に行う
    // （テンポラリ名は関数間で衝突するため、関数単位の除去は誤削除の危険がある）
    std::string block_content = block_ss.str();

    // 三項演算子最適化: if/elseが同一変数への単一代入のみなら cond ? a : b に変換
    {
        std::istringstream opt_stream(block_content);
        std::vector<std::string> opt_lines;
        std::string opt_line;
        while (std::getline(opt_stream, opt_line)) {
            opt_lines.push_back(opt_line);
        }

        // パターン検出: 連続する行で以下の形式を探す
        // [i]   if (COND) begin
        // [i+1]     VAR = A;  (or VAR <= A;)
        // [i+2] end else begin
        // [i+3]     VAR = B;  (or VAR <= B;)
        // [i+4] end
        std::vector<std::string> optimized;
        for (size_t i = 0; i < opt_lines.size(); ++i) {
            std::string trimmed_if = opt_lines[i];
            auto if_start = trimmed_if.find_first_not_of(' ');
            if (if_start == std::string::npos || i + 4 >= opt_lines.size()) {
                optimized.push_back(opt_lines[i]);
                continue;
            }
            std::string if_content = trimmed_if.substr(if_start);

            // "if (...) begin" パターンチェック
            if (if_content.substr(0, 4) != "if (" || if_content.back() != 'n' ||
                if_content.find(") begin") == std::string::npos) {
                optimized.push_back(opt_lines[i]);
                continue;
            }

            // 条件式を抽出
            auto cond_start_pos = if_content.find('(');
            auto cond_end_pos = if_content.rfind(") begin");
            if (cond_start_pos == std::string::npos || cond_end_pos == std::string::npos) {
                optimized.push_back(opt_lines[i]);
                continue;
            }
            std::string cond_expr =
                if_content.substr(cond_start_pos + 1, cond_end_pos - cond_start_pos - 1);

            // then代入行を解析
            std::string then_line = opt_lines[i + 1];
            auto then_start = then_line.find_first_not_of(' ');
            if (then_start == std::string::npos) {
                optimized.push_back(opt_lines[i]);
                continue;
            }
            std::string then_content = then_line.substr(then_start);

            // "end else begin" チェック
            std::string else_line = opt_lines[i + 2];
            auto else_start = else_line.find_first_not_of(' ');
            if (else_start == std::string::npos) {
                optimized.push_back(opt_lines[i]);
                continue;
            }
            std::string else_content = else_line.substr(else_start);
            if (else_content != "end else begin") {
                optimized.push_back(opt_lines[i]);
                continue;
            }

            // else代入行を解析
            std::string else_assign_line = opt_lines[i + 3];
            auto ea_start = else_assign_line.find_first_not_of(' ');
            if (ea_start == std::string::npos) {
                optimized.push_back(opt_lines[i]);
                continue;
            }
            std::string ea_content = else_assign_line.substr(ea_start);

            // "end" チェック
            std::string end_line = opt_lines[i + 4];
            auto end_start = end_line.find_first_not_of(' ');
            if (end_start == std::string::npos) {
                optimized.push_back(opt_lines[i]);
                continue;
            }
            std::string end_content = end_line.substr(end_start);
            if (end_content != "end") {
                optimized.push_back(opt_lines[i]);
                continue;
            }

            // 代入演算子を検出（= または <=）
            std::string assign_op = " = ";
            auto then_eq = then_content.find(" = ");
            auto then_nbeq = then_content.find(" <= ");
            auto ea_eq = ea_content.find(" = ");
            auto ea_nbeq = ea_content.find(" <= ");

            std::string then_lhs, then_rhs, else_lhs, else_rhs;

            if (then_nbeq != std::string::npos && ea_nbeq != std::string::npos) {
                assign_op = " <= ";
                then_lhs = then_content.substr(0, then_nbeq);
                then_rhs = then_content.substr(then_nbeq + 4);
                else_lhs = ea_content.substr(0, ea_nbeq);
                else_rhs = ea_content.substr(ea_nbeq + 4);
            } else if (then_eq != std::string::npos && ea_eq != std::string::npos) {
                then_lhs = then_content.substr(0, then_eq);
                then_rhs = then_content.substr(then_eq + 3);
                else_lhs = ea_content.substr(0, ea_eq);
                else_rhs = ea_content.substr(ea_eq + 3);
            } else {
                optimized.push_back(opt_lines[i]);
                continue;
            }

            // セミコロン除去
            if (!then_rhs.empty() && then_rhs.back() == ';')
                then_rhs.pop_back();
            if (!else_rhs.empty() && else_rhs.back() == ';')
                else_rhs.pop_back();

            // 同一変数チェック
            if (then_lhs != else_lhs || then_lhs.empty()) {
                optimized.push_back(opt_lines[i]);
                continue;
            }

            // 三項演算子に変換 (条件式を括弧で囲み演算子優先順位の問題を回避)
            std::string indent_str = trimmed_if.substr(0, if_start);
            optimized.push_back(indent_str + then_lhs + assign_op + "(" + cond_expr + ")" + " ? " +
                                then_rhs + " : " + else_rhs + ";");
            i += 4;  // 5行消費
        }

        // 最適化結果を再構築
        std::ostringstream opt_ss;
        for (size_t i = 0; i < optimized.size(); ++i) {
            opt_ss << optimized[i];
            if (i + 1 < optimized.size())
                opt_ss << "\n";
        }
        block_content = opt_ss.str();
    }

    // else if 正規化: "end else begin\n    if (...) begin" → "end else if (...) begin"
    // 結合時にブロック内容のインデントを1レベル浅く調整し、余分なendも除去
    {
        std::istringstream elif_stream(block_content);
        std::vector<std::string> elif_lines;
        std::string elif_line;
        while (std::getline(elif_stream, elif_line)) {
            elif_lines.push_back(elif_line);
        }

        std::ostringstream elif_ss;
        bool first = true;
        // インデント調整量のスタック: 結合されたelse ifの中で4スペース浅くする
        int indent_adjust = 0;
        std::vector<int> adjust_stack;  // begin/endの対応でadjustを追跡

        for (size_t i = 0; i < elif_lines.size(); ++i) {
            auto trim_start = elif_lines[i].find_first_not_of(' ');
            if (trim_start == std::string::npos) {
                if (!first)
                    elif_ss << "\n";
                elif_ss << elif_lines[i];
                first = false;
                continue;
            }
            std::string trimmed = elif_lines[i].substr(trim_start);
            std::string indent_str = elif_lines[i].substr(0, trim_start);

            // "end else begin" + 次行 "if (...)" パターン検出
            if (trimmed == "end else begin" && i + 1 < elif_lines.size()) {
                auto next_trim = elif_lines[i + 1].find_first_not_of(' ');
                if (next_trim != std::string::npos &&
                    elif_lines[i + 1].substr(next_trim, 4) == "if (") {
                    // 結合: "end else if (...) begin"
                    if (!first)
                        elif_ss << "\n";
                    elif_ss << indent_str << "end else " << elif_lines[i + 1].substr(next_trim);
                    first = false;
                    ++i;  // if行をスキップ
                    // 次行以降のインデントを4スペース浅く調整
                    indent_adjust += 4;
                    // 対応するendを見つけるためにdepthカウンタを初期化
                    adjust_stack.push_back(0);
                    continue;
                }
            }

            // インデント調整中: begin/endの深さを追跡
            if (indent_adjust > 0 && !adjust_stack.empty()) {
                // beginを含む行でdepth++
                if (trimmed.size() >= 5 && trimmed.substr(trimmed.size() - 5) == "begin") {
                    adjust_stack.back()++;
                }
                // "end"で始まる行でdepth--
                if (trimmed == "end" || (trimmed.size() >= 4 && trimmed.substr(0, 4) == "end ")) {
                    if (adjust_stack.back() > 0) {
                        adjust_stack.back()--;
                    } else {
                        // この"end"は余分（結合されたelse ifの対応end）→ スキップ
                        indent_adjust -= 4;
                        adjust_stack.pop_back();
                        continue;
                    }
                }
            }

            // インデント調整を適用
            if (!first)
                elif_ss << "\n";
            if (indent_adjust > 0 && static_cast<int>(trim_start) > indent_adjust) {
                elif_ss << indent_str.substr(indent_adjust) << trimmed;
            } else {
                elif_ss << elif_lines[i];
            }
            first = false;
        }
        block_content = elif_ss.str();
    }

    // 冗長三項演算子除去: "cond ? X : X" → "X"
    {
        // 単純な文字列探索で "? X : X" パターンを検出（X が同一値）
        std::istringstream tern_stream(block_content);
        std::ostringstream tern_out;
        std::string tern_line;
        while (std::getline(tern_stream, tern_line)) {
            // "expr ? val : val;" パターンを検出
            auto q_pos = tern_line.find(" ? ");
            auto c_pos =
                (q_pos != std::string::npos) ? tern_line.find(" : ", q_pos + 3) : std::string::npos;
            if (q_pos != std::string::npos && c_pos != std::string::npos) {
                std::string then_val = tern_line.substr(q_pos + 3, c_pos - q_pos - 3);
                std::string else_val = tern_line.substr(c_pos + 3);
                // セミコロンを含む場合は除去して比較
                std::string else_val_clean = else_val;
                if (!else_val_clean.empty() && else_val_clean.back() == ';')
                    else_val_clean.pop_back();
                if (then_val == else_val_clean && !then_val.empty()) {
                    // 三項演算子を除去して直接値を使用
                    // "var = cond ? X : X;" → "var = X;"
                    auto assign_pos = tern_line.rfind(" = ", q_pos);
                    auto nb_assign_pos = tern_line.rfind(" <= ", q_pos);
                    if (assign_pos != std::string::npos || nb_assign_pos != std::string::npos) {
                        size_t a_pos =
                            (nb_assign_pos != std::string::npos &&
                             (assign_pos == std::string::npos || nb_assign_pos > assign_pos))
                                ? nb_assign_pos
                                : assign_pos;
                        std::string a_op =
                            (a_pos == nb_assign_pos && nb_assign_pos != std::string::npos) ? " <= "
                                                                                           : " = ";
                        tern_out << tern_line.substr(0, a_pos) << a_op << then_val << ";\n";
                        continue;
                    }
                }
            }
            tern_out << tern_line << "\n";
        }
        // 末尾の余分な改行を除去
        block_content = tern_out.str();
        if (!block_content.empty() && block_content.back() == '\n')
            block_content.pop_back();
    }

    if (has_explicit_edge || func.is_async ||
        func.always_kind == mir::MirFunction::AlwaysKind::FF) {
        mod.always_ff_blocks.push_back(block_content);
    } else {
        using AK = mir::MirFunction::AlwaysKind;
        if (func.always_kind == AK::Latch) {
            // always_latch 明示指定
            mod.always_latch_blocks.push_back(block_content);
        } else if (func.always_kind == AK::Comb) {
            // always_comb 明示指定
            mod.always_comb_blocks.push_back(block_content);
        } else {
            // Auto: MIRの代入完全性解析で判別（式ツリー化 Phase 3）。
            // entryから各returnまでの全制御パスで代入されない
            // モジュールレベル信号があればラッチ推論となる。
            // （従来は「if行数 vs else行数」のテキストヒューリスティックで、
            //   if前のデフォルト代入を見落とし、片側代入のif/elseを見逃していた）
            auto incomplete_signals = findIncompletelyAssignedSignals(func);
            if (!incomplete_signals.empty()) {
                // ブロックヘッダを always_latch に置換し、要因の信号を注記する
                size_t pos = block_content.find("always_comb begin");
                if (pos != std::string::npos) {
                    block_content.replace(pos, 17, "always_latch begin");
                }
                std::string note = "    // ラッチ推論: ";
                for (size_t i = 0; i < incomplete_signals.size(); ++i) {
                    if (i > 0) {
                        note += ", ";
                    }
                    note += incomplete_signals[i];
                }
                note += " が全パスで代入されません\n";
                block_content = note + block_content;
                mod.always_latch_blocks.push_back(block_content);
            } else {
                mod.always_comb_blocks.push_back(block_content);
            }
        }
    }

    // インデントレベルをリセット
    indent_level_ = 0;
}

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

// === CFG再帰走査ベースのブロック出力 ===
void SVCodeGen::emitBlockRecursive(const mir::MirFunction& func, size_t block_id,
                                   std::set<size_t>& visited, std::ostringstream& ss,
                                   size_t merge_block) {
    // ループ本体の出力中にexitブロックへ到達した場合はループ脱出を出力
    // （exitブロック自体はループ終了後に出力される）。
    // break は SV-2005 キーワードで古いIcarus Verilog等が未対応のため、
    // ループを囲む名前付きブロックへの disable で脱出する（Verilog-1995互換）
    if (!loop_exit_stack_.empty() && block_id == loop_exit_stack_.back()) {
        ss << indent() << "disable " << loop_name_stack_.back() << ";\n";
        return;
    }
    // 既に訪問済み、または合流ブロックに到達した場合は停止
    if (block_id >= func.basic_blocks.size() || !func.basic_blocks[block_id])
        return;
    if (visited.count(block_id))
        return;
    if (block_id == merge_block)
        return;

    visited.insert(block_id);
    const auto& bb = *func.basic_blocks[block_id];

    // ブロック内の文を出力
    for (const auto& stmt : bb.statements) {
        if (!stmt)
            continue;
        std::string line = emitStatement(*stmt, func);
        if (!line.empty()) {
            ss << indent() << line << "\n";
        }
    }

    // ターミネータを処理
    if (bb.terminator) {
        emitTerminator(*bb.terminator, func, visited, ss, merge_block, block_id);
    }
}

// === ターミネータのSV変換 ===
void SVCodeGen::emitTerminator(const mir::MirTerminator& term, const mir::MirFunction& func,
                               std::set<size_t>& visited, std::ostringstream& ss,
                               size_t merge_block, size_t current_block) {
    switch (term.kind) {
        case mir::MirTerminator::Goto: {
            // 無条件ジャンプ → 後続ブロックをインライン出力
            const auto& gd = std::get<mir::MirTerminator::GotoData>(term.data);
            emitBlockRecursive(func, gd.target, visited, ss, merge_block);
            break;
        }
        case mir::MirTerminator::SwitchInt: {
            // 条件分岐 → if/else begin...end
            const auto& sd = std::get<mir::MirTerminator::SwitchIntData>(term.data);
            std::string cond = sd.discriminant ? emitOperand(*sd.discriminant, func) : "0";

            if (sd.targets.size() == 1) {
                // if (cond == val) ... else ...
                // MIRのSwitchIntは: targets=[(val, then_block)], otherwise=else_block
                size_t then_block = sd.targets[0].second;
                size_t else_block = sd.otherwise;

                // bool分岐の場合、val==0なら否定条件
                bool is_negated = (sd.targets[0].first == 0);

                // === ループヘッダ検出とwhileループ再構成 ===
                // このブロックへの後方エッジがあり、かつ片方の分岐だけが
                // 自然ループに属する場合、ループヘッダとみなす。
                // if/elseとして出力するとバックエッジが消えて
                // 「ループ本体が最大1回・ループ後コードが到達不能」という
                // 誤ったSVになるため、whileループとして構造を復元する
                auto latch_it = current_loop_latches_.find(current_block);
                if (current_block != SIZE_MAX && latch_it != current_loop_latches_.end()) {
                    const std::vector<size_t>& latches = latch_it->second;
                    if (!latches.empty()) {
                        // 真条件(cond != 0)で実行される分岐
                        size_t true_block = is_negated ? else_block : then_block;
                        size_t false_block = is_negated ? then_block : else_block;
                        bool true_in_loop =
                            in_natural_loop(func, true_block, current_block, latches);
                        bool false_in_loop =
                            in_natural_loop(func, false_block, current_block, latches);
                        if (true_in_loop != false_in_loop) {
                            size_t body = true_in_loop ? true_block : false_block;
                            size_t exit = true_in_loop ? false_block : true_block;
                            std::string loop_cond = true_in_loop ? cond : "!(" + cond + ")";

                            // ループ脱出（disable）用の名前付きブロックで囲む
                            std::string loop_name = "__loop" + std::to_string(loop_name_counter_++);
                            ss << indent() << "begin : " << loop_name << "\n";
                            increaseIndent();
                            ss << indent() << "while (" << loop_cond << ") begin\n";
                            increaseIndent();
                            // ループ本体を出力。ループ脱出（exitへの分岐）を検出できるよう
                            // exitブロックをスタックに積む。ヘッダへの後方エッジは
                            // visited済みのため自然に停止する
                            loop_exit_stack_.push_back(exit);
                            loop_name_stack_.push_back(loop_name);
                            emitBlockRecursive(func, body, visited, ss, exit);
                            loop_name_stack_.pop_back();
                            loop_exit_stack_.pop_back();
                            // ヘッダブロックの文（ループ条件の再計算）を本体末尾で
                            // 再実行する。条件のテンポラリが2箇所で代入されることに
                            // なり、インライン展開の対象からも自動的に外れる
                            if (current_block < func.basic_blocks.size() &&
                                func.basic_blocks[current_block]) {
                                for (const auto& stmt :
                                     func.basic_blocks[current_block]->statements) {
                                    if (!stmt)
                                        continue;
                                    std::string line = emitStatement(*stmt, func);
                                    if (!line.empty()) {
                                        ss << indent() << line << "\n";
                                    }
                                }
                            }
                            decreaseIndent();
                            ss << indent() << "end\n";
                            decreaseIndent();
                            ss << indent() << "end\n";

                            // ループ後（exit）ブロックを出力
                            emitBlockRecursive(func, exit, visited, ss, merge_block);
                            break;
                        }
                    }
                }

                // 合流ブロックを探す
                size_t merge = findMergeBlock(func, then_block, else_block);

                if (is_negated) {
                    // SwitchInt(cond, [(0, then_block)], otherwise=else_block)
                    // → if (!cond) then_block else else_block
                    // → if (cond) else_block else then_block (反転)
                    ss << indent() << "if (" << cond << ") begin\n";
                    increaseIndent();
                    emitBlockRecursive(func, else_block, visited, ss, merge);
                    decreaseIndent();
                    // else ブロック（空でなければ出力）
                    std::ostringstream else_ss;
                    std::set<size_t> else_visited = visited;
                    increaseIndent();
                    emitBlockRecursive(func, then_block, else_visited, else_ss, merge);
                    decreaseIndent();
                    if (!else_ss.str().empty()) {
                        ss << indent() << "end else begin\n";
                        ss << else_ss.str();
                        visited.insert(else_visited.begin(), else_visited.end());
                    }
                } else {
                    ss << indent() << "if (" << cond << ") begin\n";
                    increaseIndent();
                    emitBlockRecursive(func, then_block, visited, ss, merge);
                    decreaseIndent();
                    std::ostringstream else_ss;
                    std::set<size_t> else_visited = visited;
                    increaseIndent();
                    emitBlockRecursive(func, else_block, else_visited, else_ss, merge);
                    decreaseIndent();
                    if (!else_ss.str().empty()) {
                        ss << indent() << "end else begin\n";
                        ss << else_ss.str();
                        visited.insert(else_visited.begin(), else_visited.end());
                    }
                }
                ss << indent() << "end\n";

                // 合流ブロックを処理
                // （合流先がループexitの場合はここでは出力しない。
                //   break; は各分岐内で出力済みで、exit本体はループ終了後に出力される）
                if (merge != SIZE_MAX &&
                    (loop_exit_stack_.empty() || merge != loop_exit_stack_.back())) {
                    emitBlockRecursive(func, merge, visited, ss, merge_block);
                }
            } else {
                // 複数ターゲット → case文
                // 全分岐先が合流するブロックを探す（最初の2つから合流点を特定）
                size_t merge = SIZE_MAX;
                if (sd.targets.size() >= 2) {
                    merge = findMergeBlock(func, sd.targets[0].second, sd.targets[1].second);
                }

                ss << indent() << "case (" << cond << ")\n";
                increaseIndent();

                // 各ターゲットのケース（同じ遷移先ブロックごとに値をカンマ区切りでグループ化）
                std::map<size_t, std::vector<int64_t>> target_groups;
                std::vector<size_t> target_order;
                for (const auto& [val, target] : sd.targets) {
                    if (target_groups.find(target) == target_groups.end()) {
                        target_order.push_back(target);
                    }
                    target_groups[target].push_back(val);
                }

                for (size_t target : target_order) {
                    const auto& vals = target_groups[target];
                    ss << indent();
                    for (size_t i = 0; i < vals.size(); ++i) {
                        ss << vals[i];
                        if (i + 1 < vals.size()) {
                            ss << ", ";
                        }
                    }
                    ss << ": begin\n";
                    increaseIndent();
                    std::set<size_t> case_visited = visited;
                    emitBlockRecursive(func, target, case_visited, ss, merge);
                    visited.insert(case_visited.begin(), case_visited.end());
                    decreaseIndent();
                    ss << indent() << "end\n";
                }

                // defaultケース (otherwise)
                ss << indent() << "default: begin\n";
                increaseIndent();
                std::set<size_t> default_visited = visited;
                emitBlockRecursive(func, sd.otherwise, default_visited, ss, merge);
                visited.insert(default_visited.begin(), default_visited.end());
                decreaseIndent();
                ss << indent() << "end\n";

                decreaseIndent();
                ss << indent() << "endcase\n";

                // 合流ブロックを処理
                if (merge != SIZE_MAX) {
                    emitBlockRecursive(func, merge, visited, ss, merge_block);
                }
            }
            break;
        }
        case mir::MirTerminator::Return:
        case mir::MirTerminator::Unreachable:
            // SVのalwaysブロック内ではreturnは不要
            break;
        case mir::MirTerminator::Call: {
            const auto& cd = std::get<mir::MirTerminator::CallData>(term.data);
            std::string func_name;
            if (cd.func && cd.func->kind == mir::MirOperand::FunctionRef) {
                func_name = std::get<std::string>(cd.func->data);
            }

            // Ref逆引きマップ構築: テンポラリ(_tXXX) → 元のPlace
            // Use(Constant)逆引きマップ: テンポラリ → 定数値
            // copy逆引きマップ: テンポラリ → コピー元
            std::map<mir::LocalId, mir::MirPlace> ref_map;
            std::map<mir::LocalId, mir::MirPlace> copy_map;
            std::map<mir::LocalId, std::pair<mir::MirConstant, hir::TypePtr>> const_map;
            for (const auto& block : func.basic_blocks) {
                if (!block)
                    continue;
                for (const auto& s : block->statements) {
                    if (!s || s->kind != mir::MirStatement::Assign)
                        continue;
                    const auto& ad = std::get<mir::MirStatement::AssignData>(s->data);
                    if (!ad.rvalue)
                        continue;
                    if (ad.rvalue->kind == mir::MirRvalue::Ref) {
                        if (auto* ref_data =
                                std::get_if<mir::MirRvalue::RefData>(&ad.rvalue->data)) {
                            ref_map.insert_or_assign(ad.place.local, ref_data->place);
                        }
                    } else if (ad.rvalue->kind == mir::MirRvalue::Use) {
                        if (auto* use_data =
                                std::get_if<mir::MirRvalue::UseData>(&ad.rvalue->data)) {
                            if (use_data->operand) {
                                if (use_data->operand->kind == mir::MirOperand::Constant) {
                                    const_map.insert_or_assign(
                                        ad.place.local, std::make_pair(std::get<mir::MirConstant>(
                                                                           use_data->operand->data),
                                                                       use_data->operand->type));
                                } else if (use_data->operand->kind == mir::MirOperand::Copy ||
                                           use_data->operand->kind == mir::MirOperand::Move) {
                                    copy_map.insert_or_assign(
                                        ad.place.local,
                                        std::get<mir::MirPlace>(use_data->operand->data));
                                }
                            }
                        }
                    }
                }
            }

            // Call args を解決: テンポラリ → 元のPlace名 or 定数値
            auto resolveArg = [&](const mir::MirOperand& op) -> std::string {
                if (op.kind == mir::MirOperand::Move || op.kind == mir::MirOperand::Copy) {
                    const auto& place = std::get<mir::MirPlace>(op.data);
                    // Ref逆引き: _t → &original → original
                    auto ref_it = ref_map.find(place.local);
                    if (ref_it != ref_map.end()) {
                        return emitPlace(ref_it->second, func);
                    }
                    // Const逆引き: _t → constant
                    auto const_it = const_map.find(place.local);
                    if (const_it != const_map.end()) {
                        return emitConstant(const_it->second.first, const_it->second.second);
                    }
                    // ツリー化済みテンポラリはスプライスする（Phase 2:
                    // 定義行が出力されないため、名前参照のままだと未定義になる）
                    if (place.projections.empty()) {
                        auto tree_it = temp_trees_.find(place.local);
                        if (tree_it != temp_trees_.end()) {
                            return tree_it->second->to_string();
                        }
                    }
                    return emitPlace(place, func);
                } else if (op.kind == mir::MirOperand::Constant) {
                    return emitConstant(std::get<mir::MirConstant>(op.data), op.type);
                }
                return "0";
            };

            auto traceToOrigin = [&](mir::MirPlace p) -> std::string {
                while (true) {
                    auto copy_it = copy_map.find(p.local);
                    if (copy_it != copy_map.end()) {
                        p = copy_it->second;
                        continue;
                    }
                    auto ref_it = ref_map.find(p.local);
                    if (ref_it != ref_map.end()) {
                        p = ref_it->second;
                        continue;
                    }
                    break;
                }
                std::string name;
                if (p.local < func.locals.size()) {
                    name = func.locals[p.local].name;
                    if (name.empty()) {
                        name = "_" + std::to_string(p.local);
                    }
                } else {
                    name = "_" + std::to_string(p.local);
                }
                if (name.find("self.") == 0) {
                    name = name.substr(5);
                }
                return name;
            };

            auto cleanName = [](std::string name) -> std::string {
                auto ns_pos = name.rfind("::");
                if (ns_pos != std::string::npos) {
                    name = name.substr(ns_pos + 2);
                }
                return name;
            };

            if (func_name == "assert") {
                // 即時アサーション: assert (条件) else $error(...);
                // シミュレーションで検証され、合成ツールでは無視される
                std::string cond =
                    (!cd.args.empty() && cd.args[0]) ? resolveArg(*cd.args[0]) : "1'b1";
                std::string message = "assertion failed";
                if (cd.args.size() >= 2 && cd.args[1]) {
                    const mir::MirConstant* msg_const = nullptr;
                    if (cd.args[1]->kind == mir::MirOperand::Constant) {
                        msg_const = &std::get<mir::MirConstant>(cd.args[1]->data);
                    } else if (cd.args[1]->kind == mir::MirOperand::Move ||
                               cd.args[1]->kind == mir::MirOperand::Copy) {
                        const auto& place = std::get<mir::MirPlace>(cd.args[1]->data);
                        auto const_it = const_map.find(place.local);
                        if (const_it != const_map.end()) {
                            msg_const = &const_it->second.first;
                        }
                    }
                    if (msg_const) {
                        if (const auto* s = std::get_if<std::string>(&msg_const->value)) {
                            message += ": " + *s;
                        }
                    }
                }
                ss << indent() << "assert (" << cond << ") else $error(\"" << message << "\");\n";
                emitBlockRecursive(func, cd.success, visited, ss, merge_block);
            } else if (func_name == "__builtin_concat" || func_name == "__builtin_replicate") {
                // ノンブロッキング代入の判定
                bool use_nb = func.is_async || func.always_kind == mir::MirFunction::AlwaysKind::FF;
                if (use_nb && cd.destination && cd.destination->local < func.locals.size()) {
                    if (!func.locals[cd.destination->local].is_global) {
                        use_nb = false;
                    }
                }
                if (!use_nb) {
                    bool is_dest_global = true;
                    if (cd.destination && cd.destination->local < func.locals.size()) {
                        is_dest_global = func.locals[cd.destination->local].is_global;
                    }
                    if (is_dest_global) {
                        for (const auto& local : func.locals) {
                            if (local.is_global)
                                continue;
                            if (local.type && (local.type->kind == hir::TypeKind::Posedge ||
                                               local.type->kind == hir::TypeKind::Negedge)) {
                                use_nb = true;
                                break;
                            }
                        }
                    }
                }

                if (func_name == "__builtin_concat") {
                    // SV連接: {a, b, ...}
                    std::string rhs = "{";
                    for (size_t i = 0; i < cd.args.size(); ++i) {
                        if (i > 0)
                            rhs += ", ";
                        rhs += cd.args[i] ? resolveArg(*cd.args[i]) : "0";
                    }
                    rhs += "}";
                    if (cd.destination) {
                        std::string lhs = emitPlace(*cd.destination, func);
                        ss << indent() << lhs << (use_nb ? " <= " : " = ") << rhs << ";\n";
                    }
                } else {
                    // SV複製: {N{expr}}
                    // count を直接整数値として取得
                    std::string count_str = "1";
                    if (cd.args.size() > 0 && cd.args[0]) {
                        if (cd.args[0]->kind == mir::MirOperand::Constant) {
                            const auto& c = std::get<mir::MirConstant>(cd.args[0]->data);
                            if (auto* ival = std::get_if<int64_t>(&c.value)) {
                                count_str = std::to_string(*ival);
                            } else {
                                count_str = resolveArg(*cd.args[0]);
                            }
                        } else if (cd.args[0]->kind == mir::MirOperand::Move ||
                                   cd.args[0]->kind == mir::MirOperand::Copy) {
                            const auto& place = std::get<mir::MirPlace>(cd.args[0]->data);
                            auto const_it = const_map.find(place.local);
                            if (const_it != const_map.end()) {
                                if (auto* ival =
                                        std::get_if<int64_t>(&const_it->second.first.value)) {
                                    count_str = std::to_string(*ival);
                                } else {
                                    count_str = resolveArg(*cd.args[0]);
                                }
                            } else {
                                count_str = resolveArg(*cd.args[0]);
                            }
                        } else {
                            count_str = resolveArg(*cd.args[0]);
                        }
                    }
                    std::string expr =
                        cd.args.size() > 1 && cd.args[1] ? resolveArg(*cd.args[1]) : "0";
                    std::string rhs = "{" + count_str + "{" + expr + "}}";
                    if (cd.destination) {
                        std::string lhs = emitPlace(*cd.destination, func);
                        ss << indent() << lhs << (use_nb ? " <= " : " = ") << rhs << ";\n";
                    }
                }
                // 成功ブロックに続行
                emitBlockRecursive(func, cd.success, visited, ss, merge_block);
            } else if (func_name == "__builtin_string_charAt") {
                // ノンブロッキング代入の判定
                bool use_nb = func.is_async || func.always_kind == mir::MirFunction::AlwaysKind::FF;
                if (use_nb && cd.destination && cd.destination->local < func.locals.size()) {
                    if (!func.locals[cd.destination->local].is_global) {
                        use_nb = false;
                    }
                }
                if (!use_nb) {
                    bool is_dest_global = true;
                    if (cd.destination && cd.destination->local < func.locals.size()) {
                        is_dest_global = func.locals[cd.destination->local].is_global;
                    }
                    if (is_dest_global) {
                        for (const auto& local : func.locals) {
                            if (local.is_global)
                                continue;
                            if (local.type && (local.type->kind == hir::TypeKind::Posedge ||
                                               local.type->kind == hir::TypeKind::Negedge)) {
                                use_nb = true;
                                break;
                            }
                        }
                    }
                }

                std::string orig_name = "";
                int L = 0;
                if (cd.args.size() > 0 && cd.args[0]) {
                    if (cd.args[0]->kind == mir::MirOperand::Move ||
                        cd.args[0]->kind == mir::MirOperand::Copy) {
                        const auto& place = std::get<mir::MirPlace>(cd.args[0]->data);
                        orig_name = cleanName(traceToOrigin(place));
                    } else if (cd.args[0]->kind == mir::MirOperand::Constant) {
                        const auto& c = std::get<mir::MirConstant>(cd.args[0]->data);
                        if (auto* sval = std::get_if<std::string>(&c.value)) {
                            L = sval->length();
                        }
                    }
                }

                if (!orig_name.empty()) {
                    std::string base_name = orig_name;
                    auto bracket_pos = base_name.find('[');
                    if (bracket_pos != std::string::npos) {
                        base_name = base_name.substr(0, bracket_pos);
                    }
                    auto it = global_string_lengths_.find(base_name);
                    if (it != global_string_lengths_.end()) {
                        L = it->second;
                    }
                }
                if (L == 0 && cd.args.size() > 0 && cd.args[0]) {
                    std::string res_name = cleanName(resolveArg(*cd.args[0]));
                    std::string base_name = res_name;
                    auto bracket_pos = base_name.find('[');
                    if (bracket_pos != std::string::npos) {
                        base_name = base_name.substr(0, bracket_pos);
                    }
                    auto it = global_string_lengths_.find(base_name);
                    if (it != global_string_lengths_.end()) {
                        L = it->second;
                    }
                }
                if (L == 0 && cd.args.size() > 0 && cd.args[0] && cd.args[0]->type) {
                    L = getBitWidth(cd.args[0]->type) / 8;
                }

                std::string str_val =
                    cd.args.size() > 0 && cd.args[0] ? resolveArg(*cd.args[0]) : "0";
                std::string idx_val =
                    cd.args.size() > 1 && cd.args[1] ? resolveArg(*cd.args[1]) : "0";

                std::string rhs;
                if (L > 0) {
                    rhs = str_val + "[(" + std::to_string(L - 1) + " - " + idx_val + ") * 8 +: 8]";
                } else {
                    rhs = str_val + "[" + idx_val + "]";
                }

                if (cd.destination) {
                    std::string lhs = emitPlace(*cd.destination, func);
                    ss << indent() << lhs << (use_nb ? " <= " : " = ") << rhs << ";\n";
                }
                // 成功ブロックに続行
                emitBlockRecursive(func, cd.success, visited, ss, merge_block);
            } else {
                // 一般的な関数呼び出し: result = func_name(arg1, arg2, ...);
                // ノンブロッキング代入の判定
                bool use_nb = func.is_async || func.always_kind == mir::MirFunction::AlwaysKind::FF;
                if (use_nb && cd.destination && cd.destination->local < func.locals.size()) {
                    if (!func.locals[cd.destination->local].is_global) {
                        use_nb = false;
                    }
                }
                if (!use_nb) {
                    bool is_dest_global = true;
                    if (cd.destination && cd.destination->local < func.locals.size()) {
                        is_dest_global = func.locals[cd.destination->local].is_global;
                    }
                    if (is_dest_global) {
                        for (const auto& local : func.locals) {
                            if (local.is_global)
                                continue;
                            if (local.type && (local.type->kind == hir::TypeKind::Posedge ||
                                               local.type->kind == hir::TypeKind::Negedge)) {
                                use_nb = true;
                                break;
                            }
                        }
                    }
                }

                // 引数リスト構築（emitOperandがツリー化済みテンポラリをスプライスする）
                std::string args_str;
                for (size_t i = 0; i < cd.args.size(); ++i) {
                    if (i > 0)
                        args_str += ", ";
                    if (cd.args[i]) {
                        args_str += emitOperand(*cd.args[i], func);
                    }
                }

                // 戻り値がある場合は代入文として出力
                if (cd.destination) {
                    std::string lhs = emitPlace(*cd.destination, func);
                    ss << indent() << lhs << (use_nb ? " <= " : " = ") << func_name << "("
                       << args_str << ");\n";
                } else {
                    // void関数呼び出し（taskの場合等）
                    ss << indent() << func_name << "(" << args_str << ");\n";
                }
                // 成功ブロックに続行
                emitBlockRecursive(func, cd.success, visited, ss, merge_block);
            }
            // その他の関数呼び出しはスキップ
            break;
        }
    }
}

// === MIR解析: プログラム全体 ===

void SVCodeGen::analyzeMIR(const mir::MirProgram& program) {
    global_string_lengths_.clear();
    for (const auto& gv : program.global_vars) {
        if (gv && gv->is_const && gv->type && gv->type->kind == hir::TypeKind::String) {
            int L = 0;
            if (gv->init_value) {
                if (auto* sval = std::get_if<std::string>(&gv->init_value->value)) {
                    L = sval->length();
                }
            }
            std::string var_name = gv->name;
            auto ns_pos = var_name.rfind("::");
            if (ns_pos != std::string::npos) {
                var_name = var_name.substr(ns_pos + 2);
            }
            global_string_lengths_[var_name] = L;
        }
    }

    SVModule default_mod;
    default_mod.name = "top";

    // ソースファイル名からモジュール名を推定（SV予約語を回避）
    auto extractBaseName = [](const std::string& path) -> std::string {
        std::string base = path;
        auto slash = base.rfind('/');
        if (slash != std::string::npos)
            base = base.substr(slash + 1);
        auto dot = base.rfind('.');
        if (dot != std::string::npos)
            base = base.substr(0, dot);
        return base;
    };

    // SV予約語リスト（モジュール名として使用不可）
    static const std::set<std::string> sv_reserved = {
        "output",   "input",     "inout",   "module",  "wire",    "reg",     "logic",
        "begin",    "end",       "if",      "else",    "for",     "while",   "case",
        "default",  "assign",    "always",  "initial", "posedge", "negedge", "task",
        "function", "parameter", "integer", "real",    "time",    "event"};

    // ソースファイル名を優先してモジュール名を決定
    if (!options_.sourceFile.empty()) {
        std::string base = extractBaseName(options_.sourceFile);
        if (!base.empty() && sv_reserved.find(base) == sv_reserved.end()) {
            default_mod.name = base;
        } else if (!base.empty()) {
            default_mod.name = base + "_mod";
        }
    } else if (!options_.outputFile.empty()) {
        std::string base = extractBaseName(options_.outputFile);
        if (!base.empty() && sv_reserved.find(base) == sv_reserved.end()) {
            default_mod.name = base;
        } else if (!base.empty()) {
            default_mod.name = base + "_mod";
        }
    }

    // 事前パス: extern structインスタンスの出力ポートに接続された信号を収集する。
    // これらは内部レジスタ宣言で初期値を出力しない
    // （初期値付き変数への連続代入はiverilog等でエラーになるため）
    std::set<std::string> instance_driven_signals;
    for (const auto& gv : program.global_vars) {
        if (!gv || !gv->type) {
            continue;
        }
        const mir::MirStruct* extern_st = nullptr;
        for (const auto& st : program.structs) {
            if (st && st->name == gv->type->name && st->is_extern) {
                extern_st = st.get();
                break;
            }
        }
        if (!extern_st) {
            continue;
        }
        for (const auto& field : extern_st->fields) {
            bool is_output = false;
            for (const auto& attr : field.attributes) {
                if (attr == "output" || attr == "inout") {
                    is_output = true;
                }
            }
            if (!is_output) {
                continue;
            }
            std::string sig = field.name;
            if (!field.default_value_str.empty()) {
                sig = field.default_value_str;
            } else {
                for (const auto& [fname, fconst] : gv->struct_field_inits) {
                    if (fname == field.name) {
                        if (const auto* sval = std::get_if<std::string>(&fconst.value)) {
                            sig = *sval;
                        }
                        break;
                    }
                }
            }
            instance_driven_signals.insert(sig);
        }
    }

    // グローバル変数からポートと内部シグナルを生成
    bool has_clk = false;
    bool has_rst = false;
    // import/export時のlocalparam重複排除用セット
    std::set<std::string> emitted_param_names;
    // import/export時のグローバル変数/ポート重複排除用セット
    std::set<std::string> emitted_var_names;
    for (const auto& gv : program.global_vars) {
        if (!gv)
            continue;

        // 変数名のフラット化 (namespace:: を除去)
        std::string var_name = gv->name;
        auto ns_pos = var_name.rfind("::");
        if (ns_pos != std::string::npos) {
            var_name = var_name.substr(ns_pos + 2);
        }

        // extern struct インスタンスの検出（型名ベース）
        if (gv->type) {
            const mir::MirStruct* extern_st = nullptr;
            for (const auto& st : program.structs) {
                if (st && st->name == gv->type->name && st->is_extern) {
                    extern_st = st.get();
                    break;
                }
            }
            if (extern_st) {
                if (emitted_var_names.count(var_name) == 0) {
                    // インスタンス化文を生成
                    std::string inst;
                    std::string module_name = extern_st->name;
                    // #[sv::module_name] アトリビュートを探索
                    for (const auto& field : extern_st->fields) {
                        for (const auto& attr : field.attributes) {
                            if (attr == "sv::module_name") {
                                if (!field.default_value_str.empty()) {
                                    std::string val = field.default_value_str;
                                    if (val.front() == '"' && val.back() == '"') {
                                        val = val.substr(1, val.length() - 2);
                                    }
                                    module_name = val;
                                }
                                break;
                            }
                        }
                    }
                    inst += module_name;

                    // パラメータ部（#[sv::param]属性）
                    std::vector<std::string> params;
                    std::vector<std::string> ports;

                    for (const auto& field : extern_st->fields) {
                        bool is_sv_param = false;
                        bool is_port = false;
                        for (const auto& attr : field.attributes) {
                            if (attr == "sv::param")
                                is_sv_param = true;
                            if (attr == "input" || attr == "output" || attr == "inout")
                                is_port = true;
                        }

                        if (is_sv_param) {
                            // デフォルト値: フィールドの default_value_str → struct_field_inits →
                            // "0"
                            std::string val = "0";
                            if (!field.default_value_str.empty()) {
                                val = field.default_value_str;
                            } else {
                                for (const auto& [fname, fconst] : gv->struct_field_inits) {
                                    if (fname == field.name) {
                                        if (auto* ival = std::get_if<int64_t>(&fconst.value)) {
                                            val = std::to_string(*ival);
                                        } else if (auto* bval = std::get_if<bool>(&fconst.value)) {
                                            val = *bval ? "1" : "0";
                                        }
                                        break;
                                    }
                                }
                            }
                            params.push_back("." + field.name + "(" + val + ")");
                        } else if (is_port) {
                            // ポート接続: フィールドの default_value_str → struct_field_inits →
                            // フィールド名
                            std::string sig = field.name;
                            if (!field.default_value_str.empty()) {
                                sig = field.default_value_str;
                            } else {
                                for (const auto& [fname, fconst] : gv->struct_field_inits) {
                                    if (fname == field.name) {
                                        if (auto* sval = std::get_if<std::string>(&fconst.value)) {
                                            sig = *sval;
                                        }
                                        break;
                                    }
                                }
                            }
                            ports.push_back("." + field.name + "(" + sig + ")");
                        }
                    }

                    if (!params.empty()) {
                        inst += " #(\n";
                        for (size_t i = 0; i < params.size(); ++i) {
                            inst += "    " + params[i];
                            if (i + 1 < params.size())
                                inst += ",";
                            inst += "\n";
                        }
                        inst += ")";
                    }

                    inst += " " + var_name;

                    if (!ports.empty()) {
                        inst += " (\n";
                        for (size_t i = 0; i < ports.size(); ++i) {
                            inst += "    " + ports[i];
                            if (i + 1 < ports.size())
                                inst += ",";
                            inst += "\n";
                        }
                        inst += ")";
                    }

                    inst += ";";
                    default_mod.instance_blocks.push_back(inst);
                    emitted_var_names.insert(var_name);
                }
                continue;
            }
        }

        // 属性からポート方向を判定
        bool is_input = false;
        bool is_output = false;
        bool is_inout = false;
        [[maybe_unused]] bool is_param = false;
        for (const auto& attr : gv->attributes) {
            if (attr == "input")
                is_input = true;
            if (attr == "output")
                is_output = true;
            if (attr == "inout")
                is_inout = true;
            if (attr == "sv::param" || attr == "verilog::param")
                is_param = true;
        }

        // const変数 → 常にlocalparam
        if (gv->is_const) {
            // import/export時の重複排除: namespace::付き名前はフラット化
            std::string param_name = gv->name;
            auto ns_pos = param_name.rfind("::");
            if (ns_pos != std::string::npos) {
                param_name = param_name.substr(ns_pos + 2);
            }
            // 同名のlocalparamが既に出力済みならスキップ
            if (emitted_param_names.count(param_name)) {
                continue;
            }
            emitted_param_names.insert(param_name);
            std::string type_str;
            if (gv->type->kind == hir::TypeKind::String) {
                int L = 0;
                if (gv->init_value) {
                    if (auto* sval = std::get_if<std::string>(&gv->init_value->value)) {
                        L = sval->length();
                    }
                }
                if (L > 0) {
                    type_str = "logic [" + std::to_string(8 * L - 1) + ":0]";
                } else {
                    type_str = "logic [7:0]";
                }
            } else {
                type_str = mapType(gv->type);
            }
            std::string localparam_decl =
                "localparam " + type_str + " " + param_name + getArraySuffix(gv->type);
            if (gv->init_value) {
                localparam_decl += " = " + emitConstant(*gv->init_value, gv->type);
            } else if (gv->init_expr) {
                localparam_decl += " = " + emitHirExpr(*gv->init_expr);
            }
            localparam_decl += ";";
            default_mod.parameters.push_back(localparam_decl);
            continue;
        }

        // assign文 → wire宣言 + assign name = expr;
        if (gv->is_assign) {
            if (emitted_var_names.count(var_name) == 0) {
                // 属性からポート方向を判定
                bool is_input = false;
                bool is_output = false;
                bool is_inout = false;
                for (const auto& attr : gv->attributes) {
                    if (attr == "input")
                        is_input = true;
                    if (attr == "output")
                        is_output = true;
                    if (attr == "inout")
                        is_inout = true;
                }

                if (is_input) {
                    default_mod.ports.push_back({SVPort::Input, var_name, mapType(gv->type),
                                                 getBitWidth(gv->type), getArraySuffix(gv->type)});
                } else if (is_inout) {
                    default_mod.ports.push_back({SVPort::InOut, var_name, mapType(gv->type),
                                                 getBitWidth(gv->type), getArraySuffix(gv->type)});
                } else if (is_output) {
                    default_mod.ports.push_back({SVPort::Output, var_name, mapType(gv->type),
                                                 getBitWidth(gv->type), getArraySuffix(gv->type)});
                } else {
                    // wire宣言を追加（連続代入の左辺はnet型が必要）
                    default_mod.wire_declarations.push_back("wire " + mapType(gv->type) + " " +
                                                            var_name + ";");
                }

                // assign文を追加
                std::string assign_stmt = "assign " + var_name;
                if (gv->init_value) {
                    assign_stmt += " = " + emitConstant(*gv->init_value, gv->type);
                } else if (gv->init_expr) {
                    // 非定数式: HIR式をSVに変換
                    assign_stmt += " = " + emitHirExpr(*gv->init_expr);
                } else {
                    // 初期化式なし: エラー回避のため 0 を使用
                    assign_stmt += " = 0";
                }
                assign_stmt += ";";
                default_mod.assign_statements.push_back(assign_stmt);
                emitted_var_names.insert(var_name);
            }
            continue;
        }

        // Phase 3: BRAM/LutRAM推論
        bool is_bram = false;
        bool is_lutram = false;
        for (const auto& attr : gv->attributes) {
            if (attr == "sv::bram" || attr == "verilog::bram")
                is_bram = true;
            if (attr == "sv::lutram" || attr == "verilog::lutram")
                is_lutram = true;
        }
        if (is_bram || is_lutram) {
            if (emitted_var_names.count(var_name) == 0) {
                std::string ram_attr =
                    is_bram ? "(* ram_style = \"block\" *) " : "(* ram_style = \"distributed\" *) ";
                std::string ram_decl =
                    ram_attr + mapType(gv->type) + " " + var_name + getArraySuffix(gv->type) + ";";
                default_mod.reg_declarations.push_back(ram_decl);
                // 配列初期値をinitialブロックとして出力（memfile属性なら$readmemh）
                std::string ram_init = buildArrayInitialOrReadmem(*gv, var_name);
                if (!ram_init.empty()) {
                    default_mod.initial_blocks.push_back(ram_init);
                }
                emitted_var_names.insert(var_name);
            }
            continue;
        }

        if (is_input) {
            if (emitted_var_names.count(var_name) == 0) {
                // 配列型ポートはアンパックド次元も保持する
                default_mod.ports.push_back({SVPort::Input, var_name, mapType(gv->type),
                                             getBitWidth(gv->type), getArraySuffix(gv->type)});
                emitted_var_names.insert(var_name);
            }
            if (var_name == "clk")
                has_clk = true;
            if (var_name == "rst")
                has_rst = true;
        } else if (is_inout) {
            if (emitted_var_names.count(var_name) == 0) {
                default_mod.ports.push_back({SVPort::InOut, var_name, mapType(gv->type),
                                             getBitWidth(gv->type), getArraySuffix(gv->type)});
                emitted_var_names.insert(var_name);
            }
        } else if (is_output) {
            if (emitted_var_names.count(var_name) == 0) {
                default_mod.ports.push_back({SVPort::Output, var_name, mapType(gv->type),
                                             getBitWidth(gv->type), getArraySuffix(gv->type)});
                emitted_var_names.insert(var_name);
            }
        } else {
            // 属性なし → 内部レジスタ/ワイヤとして宣言
            if (emitted_var_names.count(var_name) == 0) {
                std::string array_suffix = getArraySuffix(gv->type);
                std::string reg_decl = mapType(gv->type) + " " + var_name + array_suffix;
                // 宣言初期値を電源投入時初期値として出力する。
                // 出力しないとシミュレーションでXのままFSMが進まない
                // （FPGA合成でもレジスタの初期値として扱われる）。
                // ただしインスタンス出力に接続された信号は連続駆動されるため
                // 初期値を付けない（iverilog等で二重駆動エラーになる）
                if (gv->init_value && array_suffix.empty() &&
                    instance_driven_signals.count(var_name) == 0) {
                    reg_decl +=
                        " = " + emitConstant(*gv->init_value, gv->type, getBitWidth(gv->type));
                }
                default_mod.reg_declarations.push_back(reg_decl + ";");
                // 配列初期値はinitialブロックとして出力（memfile属性なら$readmemh）
                if (!array_suffix.empty()) {
                    std::string arr_init = buildArrayInitialOrReadmem(*gv, var_name);
                    if (!arr_init.empty()) {
                        default_mod.initial_blocks.push_back(arr_init);
                    }
                }
                emitted_var_names.insert(var_name);
            }
        }
    }

    // ポートにclk/rstが含まれていない場合、自動追加（async funcがある場合のみ）
    bool has_async = false;
    for (const auto& func : program.functions) {
        if (func && func->is_async) {
            has_async = true;
            break;
        }
    }
    // 明示的なエッジトリガー入力ポート（posedge/negedge）がある場合は自動追加しない
    bool has_edge_trigger = false;
    for (const auto& gv : program.global_vars) {
        if (!gv)
            continue;
        bool is_input = false;
        for (const auto& attr : gv->attributes) {
            if (attr == "input") {
                is_input = true;
                break;
            }
        }
        bool is_edge = false;
        if (gv->type && (gv->type->kind == ast::TypeKind::Posedge ||
                         gv->type->kind == ast::TypeKind::Negedge)) {
            is_edge = true;
        }
        if (is_input && is_edge) {
            has_edge_trigger = true;
            break;
        }
    }
    if (has_edge_trigger) {
        has_clk = true;
        has_rst = true;
    }
    if (has_async && !has_clk) {
        default_mod.ports.insert(default_mod.ports.begin(),
                                 SVPort{SVPort::Input, "clk", "logic", 1, ""});
    }
    if (has_async && !has_rst) {
        // clkの実際の位置を検索して直後に挿入
        size_t insert_pos = 0;
        for (size_t i = 0; i < default_mod.ports.size(); ++i) {
            if (default_mod.ports[i].name == "clk") {
                insert_pos = i + 1;
                break;
            }
        }
        default_mod.ports.insert(default_mod.ports.begin() + static_cast<ptrdiff_t>(insert_pos),
                                 SVPort{SVPort::Input, "rst", "logic", 1, ""});
    }

    // 各関数を解析（import/export時の重複排除）
    std::set<std::string> emitted_function_names;
    for (const auto& func : program.functions) {
        if (!func)
            continue;
        // 関数名のnamespace::フラット化
        std::string flat_name = func->name;
        auto fn_ns_pos = flat_name.rfind("::");
        if (fn_ns_pos != std::string::npos) {
            flat_name = flat_name.substr(fn_ns_pos + 2);
        }
        // 同名関数が既に出力済みならスキップ
        if (emitted_function_names.count(flat_name)) {
            continue;
        }
        emitted_function_names.insert(flat_name);
        analyzeFunction(*func, default_mod);
    }

    // enum → typedef enum logic 出力
    for (const auto& e : program.enums) {
        if (!e)
            continue;
        // Tagged Union（ペイロード付きenum）はSVでは直接変換しない
        if (e->is_tagged_union())
            continue;

        std::ostringstream ss;
        // ビット幅計算: 最大タグ値を表現できるビット数を算出
        // （明示的なタグ値はメンバー数-1 より大きい場合があるため、
        //   メンバー数ではなく実際の値の最大から求める）
        int64_t max_tag = static_cast<int64_t>(e->members.size()) - 1;
        for (const auto& m : e->members) {
            if (m.tag_value > max_tag) {
                max_tag = m.tag_value;
            }
        }
        int bit_width = 1;
        int64_t val = max_tag;
        while (val > 1) {
            bit_width++;
            val >>= 1;
        }

        ss << "typedef enum logic";
        if (bit_width > 1) {
            ss << " [" << (bit_width - 1) << ":0]";
        }
        ss << " {\n";
        for (size_t i = 0; i < e->members.size(); ++i) {
            ss << "    " << e->members[i].name << " = " << bit_width << "'d"
               << e->members[i].tag_value;
            if (i + 1 < e->members.size())
                ss << ",";
            ss << "\n";
        }
        ss << "} " << e->name << ";";
        default_mod.type_declarations.push_back(ss.str());
    }

    // struct → typedef struct packed 出力（#[sv::packed]属性付きのみ）
    // extern struct はモジュール定義なので除外
    for (const auto& st : program.structs) {
        if (!st)
            continue;
        if (st->is_extern)
            continue;  // extern struct はtypedef出力しない
        // TODO: sv::packed属性チェック（現状は全structをpacked出力）
        std::ostringstream ss;
        ss << "typedef struct packed {\n";
        for (const auto& f : st->fields) {
            ss << "    " << mapType(f.type) << " " << f.name << ";\n";
        }
        ss << "} " << st->name << ";";
        default_mod.type_declarations.push_back(ss.str());
    }

    // initial ブロックを処理
    for (const auto& init : program.initial_blocks) {
        if (!init)
            continue;
        std::ostringstream ss;
        ss << "initial begin\n";

        // HIR文をSVに変換
        for (const auto* stmt : init->hir_stmts) {
            if (stmt) {
                std::string sv_stmt = emitHirStmt(*stmt);
                if (!sv_stmt.empty()) {
                    ss << "    " << sv_stmt << "\n";
                }
            }
        }

        ss << "end\n";
        default_mod.initial_blocks.push_back(ss.str());
    }

    modules_.push_back(default_mod);
}

// === メインコンパイル処理 ===

void SVCodeGen::compile(const mir::MirProgram& program) {
    // 非合成型チェック（エラーがあればコンパイル停止）
    if (!validateSynthesizableTypes(program)) {
        throw std::runtime_error("SVターゲットで非合成型が検出されました");
    }

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
        std::cout << "✓ SystemVerilog 生成完了: " << options_.outputFile << "\n";
        std::cout << "  行数: " << get_stats().total_lines << "\n";
        std::cout << "  サイズ: " << get_stats().total_bytes << " bytes\n";
    }

    // テストベンチ自動生成（モジュールがあれば）
    if (!modules_.empty()) {
        std::string tb_code = generateTestbench(modules_[0]);
        std::string tb_path = options_.outputFile;
        auto dot = tb_path.rfind('.');
        if (dot != std::string::npos) {
            tb_path = tb_path.substr(0, dot) + "_tb.sv";
        } else {
            tb_path += "_tb.sv";
        }
        writeToFile(tb_code, tb_path);
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
        std::cerr << "エラー: ファイル '" << path << "' を開けません\n";
        return;
    }
    ofs << content;
}

// === テストベンチ自動生成 ===

std::string SVCodeGen::generateTestbench(const SVModule& mod) {
    std::ostringstream ss;

    // ソースファイルから//! test:コメントをパース
    struct TestCase {
        std::vector<std::pair<std::string, std::string>> inputs;    // {name, value}
        std::vector<std::pair<std::string, std::string>> expected;  // {name, value}
        int cycles = 0;  // async用: クロックサイクル数
    };
    std::vector<TestCase> test_cases;

    if (!options_.sourceFile.empty()) {
        std::ifstream src(options_.sourceFile);
        if (src.is_open()) {
            std::string line;
            while (std::getline(src, line)) {
                // "//! test:" プレフィックスを検出
                auto pos = line.find("//! test:");
                if (pos == std::string::npos)
                    continue;
                std::string test_spec = line.substr(pos + 9);

                TestCase tc;
                // "->" で入力と期待出力を分割
                auto arrow = test_spec.find("->");
                std::string input_part, output_part;
                if (arrow != std::string::npos) {
                    input_part = test_spec.substr(0, arrow);
                    output_part = test_spec.substr(arrow + 2);
                } else {
                    input_part = test_spec;
                }

                // 入力パース: "a=3, b=5" or "cycles=5"
                std::istringstream iss(input_part);
                std::string token;
                while (std::getline(iss, token, ',')) {
                    // 空白除去
                    size_t s = token.find_first_not_of(' ');
                    size_t e = token.find_last_not_of(' ');
                    if (s == std::string::npos)
                        continue;
                    token = token.substr(s, e - s + 1);

                    auto eq = token.find('=');
                    if (eq != std::string::npos) {
                        std::string name = token.substr(0, eq);
                        std::string val = token.substr(eq + 1);
                        // 名前と値の空白除去
                        size_t ns = name.find_first_not_of(' ');
                        size_t ne = name.find_last_not_of(' ');
                        if (ns != std::string::npos)
                            name = name.substr(ns, ne - ns + 1);
                        size_t vs = val.find_first_not_of(' ');
                        size_t ve = val.find_last_not_of(' ');
                        if (vs != std::string::npos)
                            val = val.substr(vs, ve - vs + 1);

                        if (name == "cycles") {
                            tc.cycles = std::stoi(val);
                        } else {
                            tc.inputs.push_back({name, val});
                        }
                    }
                }

                // 期待出力パース: "result=8, out=0"
                if (!output_part.empty()) {
                    std::istringstream oss(output_part);
                    while (std::getline(oss, token, ',')) {
                        size_t s = token.find_first_not_of(' ');
                        size_t e = token.find_last_not_of(' ');
                        if (s == std::string::npos)
                            continue;
                        token = token.substr(s, e - s + 1);

                        auto eq = token.find('=');
                        if (eq != std::string::npos) {
                            std::string name = token.substr(0, eq);
                            std::string val = token.substr(eq + 1);
                            size_t ns = name.find_first_not_of(' ');
                            size_t ne = name.find_last_not_of(' ');
                            if (ns != std::string::npos)
                                name = name.substr(ns, ne - ns + 1);
                            size_t vs = val.find_first_not_of(' ');
                            size_t ve = val.find_last_not_of(' ');
                            if (vs != std::string::npos)
                                val = val.substr(vs, ve - vs + 1);
                            tc.expected.push_back({name, val});
                        }
                    }
                }

                test_cases.push_back(tc);
            }
        }
    }

    ss << "// 自動生成テストベンチ - Cm compiler\n";
    ss << "`timescale 1ns / 1ps\n\n";

    ss << "module " << mod.name << "_tb;\n\n";

    // ポートに対応する信号宣言
    for (const auto& port : mod.ports) {
        ss << "    " << port.sv_type << " " << port.name << port.array_suffix << ";\n";
    }
    ss << "\n";

    // DUTインスタンス化
    ss << "    // DUTインスタンス\n";
    ss << "    " << mod.name << " dut (\n";
    for (size_t i = 0; i < mod.ports.size(); ++i) {
        ss << "        ." << mod.ports[i].name << "(" << mod.ports[i].name << ")";
        if (i + 1 < mod.ports.size())
            ss << ",";
        ss << "\n";
    }
    ss << "    );\n\n";

    // クロック生成（clkポートがある場合）
    bool has_clk = false;
    std::string rst_name;         // リセット信号の実際のポート名
    bool rst_active_low = false;  // アクティブLowリセットかどうか
    for (const auto& port : mod.ports) {
        if (port.name == "clk" && port.direction == SVPort::Input)
            has_clk = true;
        if (port.direction == SVPort::Input) {
            if (port.name == "rst") {
                rst_name = "rst";
                rst_active_low = false;
            } else if (port.name == "rst_n") {
                rst_name = "rst_n";
                rst_active_low = true;
            }
        }
    }
    bool has_rst = !rst_name.empty();

    if (has_clk) {
        ss << "    // クロック生成 (10ns周期 = 100MHz)\n";
        ss << "    initial clk = 0;\n";
        ss << "    always #5 clk = ~clk;\n\n";
    }

    // テストシーケンス
    // VCDはシミュレーション実行時のカレントディレクトリに出力する。
    // コンパイル時の-oの相対パスを埋め込むと、シミュレータを別の
    // ディレクトリから実行したときに開けず異常終了するため
    ss << "    // テストシーケンス\n";
    ss << "    initial begin\n";
    ss << "        $dumpfile(\"" << mod.name << "_tb.vcd\");\n";
    ss << "        $dumpvars(0, " << mod.name << "_tb);\n\n";

    // 入力ポート初期化
    for (const auto& port : mod.ports) {
        if (port.direction == SVPort::Input && port.name != "clk") {
            ss << "        " << port.name << " = 0;\n";
        }
    }
    ss << "\n";

    // リセットシーケンス（実際のポート名を使用）
    if (has_rst) {
        ss << "        // リセット\n";
        if (rst_active_low) {
            // アクティブLow: 0→1（リセット解除）
            ss << "        " << rst_name << " = 0;\n";
            ss << "        #20;\n";
            ss << "        " << rst_name << " = 1;\n";
        } else {
            // アクティブHigh: 1→0（リセット解除）
            ss << "        " << rst_name << " = 1;\n";
            ss << "        #20;\n";
            ss << "        " << rst_name << " = 0;\n";
        }
        ss << "        #10;\n\n";
    }

    if (!test_cases.empty()) {
        // テストシナリオベースの検証
        int test_num = 1;
        for (const auto& tc : test_cases) {
            ss << "        // テスト " << test_num << "\n";

            // 入力値設定
            for (const auto& [name, val] : tc.inputs) {
                ss << "        " << name << " = " << val << ";\n";
            }

            // 評価待ち
            if (tc.cycles > 0) {
                // async: 指定サイクル分クロックを待つ
                ss << "        repeat(" << tc.cycles << ") @(posedge clk);\n";
                ss << "        #1; // 安定化\n";
            } else if (has_clk) {
                // クロック付きだがcycles未指定: 1サイクル待ち
                ss << "        @(posedge clk);\n";
                ss << "        #1;\n";
            } else {
                // 組み合わせ回路: 伝搬遅延待ち
                ss << "        #10;\n";
            }

            // 出力値の表示と検証
            for (const auto& [name, val] : tc.expected) {
                ss << "        $display(\"TEST " << test_num << ": " << name << "=%0d\", " << name
                   << ");\n";
            }
            ss << "\n";
            test_num++;
        }
    } else {
        // テストシナリオなし: 基本的な動作確認
        ss << "        // テスト実行\n";
        ss << "        #100;\n\n";
    }

    ss << "        $display(\"=== Test Complete ===\");\n";
    ss << "        $finish;\n";
    ss << "    end\n\n";

    ss << "endmodule\n";

    return ss.str();
}

// === XDC制約ファイル生成 ===

std::string SVCodeGen::generateXDC(const mir::MirProgram& program) {
    std::ostringstream ss;
    bool has_pins = false;

    for (const auto& gv : program.global_vars) {
        if (!gv)
            continue;

        std::string pin_name;
        std::string iostandard = "LVCMOS33";  // デフォルト

        for (const auto& attr : gv->attributes) {
            // sv::pin("XX") 形式の解析
            std::string prefix1 = "sv::pin(\"";
            std::string prefix2 = "verilog::pin(\"";
            if (attr.find(prefix1) == 0 && attr.size() > prefix1.size() + 2) {
                pin_name = attr.substr(prefix1.size(), attr.size() - prefix1.size() - 2);
            } else if (attr.find(prefix2) == 0 && attr.size() > prefix2.size() + 2) {
                pin_name = attr.substr(prefix2.size(), attr.size() - prefix2.size() - 2);
            }

            // sv::iostandard("XX") 形式の解析
            std::string io_prefix1 = "sv::iostandard(\"";
            std::string io_prefix2 = "verilog::iostandard(\"";
            if (attr.find(io_prefix1) == 0 && attr.size() > io_prefix1.size() + 2) {
                iostandard = attr.substr(io_prefix1.size(), attr.size() - io_prefix1.size() - 2);
            } else if (attr.find(io_prefix2) == 0 && attr.size() > io_prefix2.size() + 2) {
                iostandard = attr.substr(io_prefix2.size(), attr.size() - io_prefix2.size() - 2);
            }
        }

        if (!pin_name.empty()) {
            if (!has_pins) {
                ss << "## Cm compiler 自動生成 XDC 制約ファイル\n\n";
                has_pins = true;
            }
            ss << "set_property -dict { PACKAGE_PIN " << pin_name << "  IOSTANDARD " << iostandard
               << " } [get_ports {" << gv->name << "}]\n";

            // clk ポートにはクロック制約を追加
            if (gv->name.find("clk") != std::string::npos) {
                ss << "create_clock -add -name " << gv->name << "_pin -period 10.00 [get_ports {"
                   << gv->name << "}]\n";
            }
        }
    }

    return ss.str();
}

// === 非合成型チェック ===

bool SVCodeGen::validateSynthesizableTypes(const mir::MirProgram& program) {
    bool has_error = false;
    for (const auto& gv : program.global_vars) {
        if (!gv || !gv->type)
            continue;

        switch (gv->type->kind) {
            case hir::TypeKind::Pointer:
                std::cerr << "error[SV002]: Pointer types are not supported in SV target: "
                          << gv->name << "\n";
                has_error = true;
                break;
            case hir::TypeKind::String:
                // const文字列は実長のlocalparamとして合成可能。
                // 非constのstringは logic [23:0]（3文字分）固定のため、
                // 3文字を超える初期値はサイレントに切り詰められてしまう → エラーにする
                if (!gv->is_const && gv->init_value) {
                    if (const auto* sval = std::get_if<std::string>(&gv->init_value->value)) {
                        if (sval->length() > 3) {
                            std::cerr << "error[SV005]: Non-const string longer than 3 "
                                         "characters is not synthesizable (would be truncated "
                                         "to logic [23:0]): "
                                      << gv->name << " = \"" << *sval << "\"\n";
                            has_error = true;
                        }
                    }
                }
                break;
            case hir::TypeKind::Float:
            case hir::TypeKind::Double:
                std::cerr << "warning[SV004]: Floating-point requires IP core: " << gv->name
                          << "\n";
                break;
            default:
                break;
        }
    }

    for (const auto& func : program.functions) {
        if (!func)
            continue;
        for (const auto& local : func->locals) {
            if (!local.type)
                continue;
            switch (local.type->kind) {
                case hir::TypeKind::Pointer:
                    // MIR生成テンポラリ変数（_tXXX）はスキップ
                    // __builtin_concat等のCall引数用アドレステンポラリ
                    if (local.name.size() > 2 && local.name[0] == '_' && local.name[1] == 't' &&
                        std::isdigit(static_cast<unsigned char>(local.name[2]))) {
                        break;
                    }
                    std::cerr << "error[SV002]: Pointer types are not supported in SV target: "
                              << func->name << "::" << local.name << "\n";
                    has_error = true;
                    break;
                case hir::TypeKind::String:
                    // String types not synthesizable error is removed to allow local string
                    // constants/temporaries
                    break;
                default:
                    break;
            }
        }
    }
    return !has_error;
}

// HIR式をSVに変換（assign文の非定数式用）
std::string SVCodeGen::emitHirExpr(const hir::HirExpr& expr) {
    // リテラル
    if (auto* lit = std::get_if<std::unique_ptr<hir::HirLiteral>>(&expr.kind)) {
        if (*lit) {
            const auto& value = (*lit)->value;
            if (std::holds_alternative<int64_t>(value)) {
                return std::to_string(std::get<int64_t>(value));
            } else if (std::holds_alternative<double>(value)) {
                return std::to_string(std::get<double>(value));
            } else if (std::holds_alternative<bool>(value)) {
                return std::get<bool>(value) ? "1'b1" : "1'b0";
            } else if (std::holds_alternative<char>(value)) {
                return std::to_string(static_cast<int64_t>(std::get<char>(value)));
            } else if (std::holds_alternative<std::string>(value)) {
                return "\"" + std::get<std::string>(value) + "\"";
            }
        }
    }
    // 配列リテラル
    if (auto* arr = std::get_if<std::unique_ptr<hir::HirArrayLiteral>>(&expr.kind)) {
        if (*arr) {
            std::string res = "'{";
            for (size_t i = 0; i < (*arr)->elements.size(); ++i) {
                if (i > 0)
                    res += ", ";
                res += emitHirExpr(*(*arr)->elements[i]);
            }
            res += "}";
            return res;
        }
    }

    // 識別子（変数参照）
    if (auto* var = std::get_if<std::unique_ptr<hir::HirVarRef>>(&expr.kind)) {
        if (*var) {
            return (*var)->name;
        }
    }

    // 二項演算
    if (auto* binary = std::get_if<std::unique_ptr<hir::HirBinary>>(&expr.kind)) {
        if (*binary && (*binary)->lhs && (*binary)->rhs) {
            std::string lhs = emitHirExpr(*(*binary)->lhs);
            std::string rhs = emitHirExpr(*(*binary)->rhs);
            std::string op;
            switch ((*binary)->op) {
                case hir::HirBinaryOp::Add:
                    op = "+";
                    break;
                case hir::HirBinaryOp::Sub:
                    op = "-";
                    break;
                case hir::HirBinaryOp::Mul:
                    op = "*";
                    break;
                case hir::HirBinaryOp::Div:
                    op = "/";
                    break;
                case hir::HirBinaryOp::Mod:
                    op = "%";
                    break;
                case hir::HirBinaryOp::BitAnd:
                    op = "&";
                    break;
                case hir::HirBinaryOp::BitOr:
                    op = "|";
                    break;
                case hir::HirBinaryOp::BitXor:
                    op = "^";
                    break;
                case hir::HirBinaryOp::Shl:
                    op = "<<";
                    break;
                case hir::HirBinaryOp::Shr:
                    op = ">>";
                    break;
                case hir::HirBinaryOp::And:
                    op = "&&";
                    break;
                case hir::HirBinaryOp::Or:
                    op = "||";
                    break;
                case hir::HirBinaryOp::Eq:
                    op = "==";
                    break;
                case hir::HirBinaryOp::Ne:
                    op = "!=";
                    break;
                case hir::HirBinaryOp::Lt:
                    op = "<";
                    break;
                case hir::HirBinaryOp::Le:
                    op = "<=";
                    break;
                case hir::HirBinaryOp::Gt:
                    op = ">";
                    break;
                case hir::HirBinaryOp::Ge:
                    op = ">=";
                    break;
                case hir::HirBinaryOp::Assign:
                    // 代入式: SVでは式として括弧に包めないため
                    // 素の代入形式で返す（式文経由で "lhs = rhs;" になる）
                    return lhs + " = " + rhs;
                default:
                    op = "?";
                    break;
            }
            return "(" + lhs + " " + op + " " + rhs + ")";
        }
    }

    // 単項演算
    if (auto* unary = std::get_if<std::unique_ptr<hir::HirUnary>>(&expr.kind)) {
        if (*unary && (*unary)->operand) {
            std::string operand = emitHirExpr(*(*unary)->operand);
            std::string op;
            switch ((*unary)->op) {
                case hir::HirUnaryOp::Neg:
                    op = "-";
                    break;
                case hir::HirUnaryOp::Not:
                    op = "!";
                    break;
                case hir::HirUnaryOp::BitNot:
                    op = "~";
                    break;
                default:
                    op = "?";
                    break;
            }
            return op + operand;
        }
    }

    // メンバアクセス
    if (auto* member = std::get_if<std::unique_ptr<hir::HirMember>>(&expr.kind)) {
        if (*member && (*member)->object) {
            std::string obj = emitHirExpr(*(*member)->object);
            return obj + "." + (*member)->member;
        }
    }

    // 三項演算子
    if (auto* ternary = std::get_if<std::unique_ptr<hir::HirTernary>>(&expr.kind)) {
        if (*ternary && (*ternary)->condition && (*ternary)->then_expr && (*ternary)->else_expr) {
            std::string cond = emitHirExpr(*(*ternary)->condition);
            std::string then_e = emitHirExpr(*(*ternary)->then_expr);
            std::string else_e = emitHirExpr(*(*ternary)->else_expr);
            return "(" + cond + " ? " + then_e + " : " + else_e + ")";
        }
    }

    // キャスト
    if (auto* cast = std::get_if<std::unique_ptr<hir::HirCast>>(&expr.kind)) {
        if (*cast && (*cast)->operand) {
            return emitHirExpr(*(*cast)->operand);
        }
    }

    // 未対応の式: 0を返す
    return "0 /* unsupported expr */";
}

// HIR文をSVに変換（initial block用）
std::string SVCodeGen::emitHirStmt(const hir::HirStmt& stmt) {
    // 代入文
    if (auto* assign = std::get_if<std::unique_ptr<hir::HirAssign>>(&stmt.kind)) {
        if (*assign && (*assign)->target && (*assign)->value) {
            std::string lhs = emitHirExpr(*(*assign)->target);
            std::string rhs = emitHirExpr(*(*assign)->value);
            return lhs + " = " + rhs + ";";
        }
    }

    // 変数宣言（let文）
    if (auto* let = std::get_if<std::unique_ptr<hir::HirLet>>(&stmt.kind)) {
        if (*let) {
            std::string sv_type = mapType((*let)->type);
            std::string init_val = (*let)->init ? emitHirExpr(*(*let)->init) : "0";
            return sv_type + " " + (*let)->name + " = " + init_val + ";";
        }
    }

    // 式文
    if (auto* expr_stmt = std::get_if<std::unique_ptr<hir::HirExprStmt>>(&stmt.kind)) {
        if (*expr_stmt && (*expr_stmt)->expr) {
            return emitHirExpr(*(*expr_stmt)->expr) + ";";
        }
    }

    // ブロック文
    if (auto* block = std::get_if<std::unique_ptr<hir::HirBlock>>(&stmt.kind)) {
        if (*block) {
            std::ostringstream ss;
            ss << "begin\n";
            for (const auto& s : (*block)->stmts) {
                if (s) {
                    std::string sv_stmt = emitHirStmt(*s);
                    if (!sv_stmt.empty()) {
                        ss << "    " << sv_stmt << "\n";
                    }
                }
            }
            ss << "end";
            return ss.str();
        }
    }

    // if文
    if (auto* if_stmt = std::get_if<std::unique_ptr<hir::HirIf>>(&stmt.kind)) {
        if (*if_stmt && (*if_stmt)->cond) {
            std::ostringstream ss;
            std::string cond = emitHirExpr(*(*if_stmt)->cond);
            ss << "if (" << cond << ") begin\n";
            for (const auto& s : (*if_stmt)->then_block) {
                if (s) {
                    std::string sv_stmt = emitHirStmt(*s);
                    if (!sv_stmt.empty()) {
                        ss << "    " << sv_stmt << "\n";
                    }
                }
            }
            ss << "end";
            if (!(*if_stmt)->else_block.empty()) {
                ss << " else begin\n";
                for (const auto& s : (*if_stmt)->else_block) {
                    if (s) {
                        std::string sv_stmt = emitHirStmt(*s);
                        if (!sv_stmt.empty()) {
                            ss << "    " << sv_stmt << "\n";
                        }
                    }
                }
                ss << "end";
            }
            return ss.str();
        }
    }

    // 未対応の文
    return "/* unsupported stmt */";
}

}  // namespace cm::codegen::sv
