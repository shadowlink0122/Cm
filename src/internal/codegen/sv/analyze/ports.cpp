// ============================================================
// MIR解析フェーズ: グローバル変数からのポート・内部シグナル生成
// ============================================================
#include "internal/codegen/sv/codegen.hpp"
#include "internal/codegen/sv/sv_internal.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace cm::codegen::sv {

// ポート生成フェーズ: グローバル変数からポート・localparam・内部シグナル・インスタンス化文を生成する
void SVCodeGen::analyzeGlobalPorts(const mir::MirProgram& program, SVModule& mod,
                                   const std::set<std::string>& instance_driven_signals,
                                   const std::set<std::string>& array_signal_names, bool& has_clk,
                                   bool& has_rst) {
    // import/export時のlocalparam重複排除用セット
    std::set<std::string> emitted_param_names;
    // import/export時のグローバル変数/ポート重複排除用セット
    std::set<std::string> emitted_var_names;
    for (const auto& gv : program.global_vars) {
        if (!gv)
            continue;

        // 変数名のフラット化 (namespace:: を除去)
        std::string var_name = gv->name;
        var_name = strip_namespace(var_name);

        // IOインスタンスの検出: #[input]/#[output]/#[inout] フィールドを持つ
        // 構造体のグローバル変数は、フィールドをモジュールポートへ展開する（個別のポート宣言は不要。io.field アクセスはポート名へ写像される）
        if (gv->type) {
            const mir::MirStruct* io_st = nullptr;
            std::string io_type_name = strip_namespace(gv->type->name);
            for (const auto& st : program.structs) {
                if (!st || st->is_extern || strip_namespace(st->name) != io_type_name) {
                    continue;
                }
                for (const auto& f : st->fields) {
                    for (const auto& a : f.attributes) {
                        if (a == "input" || a == "output" || a == "inout") {
                            io_st = st.get();
                            break;
                        }
                    }
                    if (io_st)
                        break;
                }
                break;
            }
            if (io_st) {
                if (emitted_var_names.count(var_name) == 0) {
                    std::vector<std::string> field_names;
                    for (const auto& f : io_st->fields) {
                        field_names.push_back(f.name);
                        std::string dir;
                        for (const auto& a : f.attributes) {
                            if (a == "input" || a == "output" || a == "inout") {
                                dir = a;
                            }
                        }
                        if (dir.empty()) {
                            continue;  // 方向属性のないフィールドはポートにしない
                        }
                        std::string array_suffix = getArraySuffix(f.type);
                        std::string init_value;
                        if (dir == "output" && array_suffix.empty() &&
                            !f.default_value_str.empty()) {
                            init_value = f.default_value_str;
                        }
                        SVPort::Direction pdir = SVPort::Input;
                        if (dir == "output") {
                            pdir = SVPort::Output;
                        } else if (dir == "inout") {
                            pdir = SVPort::InOut;
                        }
                        mod.ports.push_back({pdir, f.name, mapType(f.type), getBitWidth(f.type),
                                             array_suffix, init_value});
                        if (pdir == SVPort::Input && f.name == "clk")
                            has_clk = true;
                        if (pdir == SVPort::Input && f.name == "rst")
                            has_rst = true;
                    }
                    io_instance_fields_[var_name] = std::move(field_names);
                    emitted_var_names.insert(var_name);
                }
                continue;
            }
        }

        // extern struct インスタンスの検出（型名ベース）。
        // importされたモジュール内の宣言は名前空間修飾付きになるため、修飾を除去した名前で照合する
        if (gv->type) {
            const mir::MirStruct* extern_st = nullptr;
            std::string inst_type_name = strip_namespace(gv->type->name);
            for (const auto& st : program.structs) {
                if (st && st->is_extern && strip_namespace(st->name) == inst_type_name) {
                    extern_st = st.get();
                    break;
                }
            }
            if (extern_st) {
                if (emitted_var_names.count(var_name) == 0) {
                    // #[sv::instance_array(N)]: 同一サブモジュールをN個並べるgenerate-for出力（SV-N5）。
                    // Nはリテラルまたは#[sv::parameter]のパラメータ名
                    std::string inst_count;
                    for (const auto& attr : gv->attributes) {
                        for (const char* prefix :
                             {"sv::instance_array(", "verilog::instance_array("}) {
                            if (attr.rfind(prefix, 0) == 0 && attr.back() == ')') {
                                inst_count =
                                    attr.substr(strlen(prefix), attr.size() - strlen(prefix) - 1);
                                // 属性引数の文字列化で付く引用符を除去する（リテラル・識別子とも）
                                if (inst_count.size() >= 2 && inst_count.front() == '"' &&
                                    inst_count.back() == '"') {
                                    inst_count = inst_count.substr(1, inst_count.size() - 2);
                                }
                            }
                        }
                    }
                    // インスタンス化文を生成。
                    // importされたモジュール内で宣言されたインスタンスは型名が名前空間修飾付き（mod::PLL等）になるため除去する
                    std::string inst;
                    std::string module_name = strip_namespace(extern_st->name);
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
                            // 値の優先順位: インスタンスのstructリテラル（上書き）→
                            // フィールドのデフォルト値 → "0"
                            std::string val = "0";
                            bool overridden = false;
                            for (const auto& [fname, fconst] : gv->struct_field_inits) {
                                if (fname == field.name) {
                                    if (auto* ival = std::get_if<int64_t>(&fconst.value)) {
                                        val = std::to_string(*ival);
                                        overridden = true;
                                    } else if (auto* bval = std::get_if<bool>(&fconst.value)) {
                                        val = *bval ? "1" : "0";
                                        overridden = true;
                                    }
                                    break;
                                }
                            }
                            if (!overridden && !field.default_value_str.empty()) {
                                val = field.default_value_str;
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
                            // IOインスタンスのフィールド参照（io.x）はポート名へ写像する
                            auto dot = sig.find('.');
                            if (dot != std::string::npos &&
                                io_instance_fields_.count(sig.substr(0, dot)) != 0) {
                                sig = sig.substr(dot + 1);
                            }
                            // インスタンス配列: 結線先が配列信号ならgenvarで各レーンへ分配し、
                            // スカラ信号はそのまま全レーンへブロードキャストする
                            if (!inst_count.empty() && array_signal_names.count(sig) > 0) {
                                sig += "[__gi_" + var_name + "]";
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
                    if (!inst_count.empty()) {
                        // generate-forで包む（genvar添字は結線側で付与済み）
                        std::string gi = "__gi_" + var_name;
                        std::string gen;
                        gen += "genvar " + gi + ";\n";
                        gen += "generate\n";
                        gen += "    for (" + gi + " = 0; " + gi + " < " + inst_count + "; " + gi +
                               " = " + gi + " + 1) begin : " + var_name + "_gen\n";
                        {
                            std::istringstream inst_lines(inst);
                            std::string line;
                            while (std::getline(inst_lines, line)) {
                                gen += "        " + line + "\n";
                            }
                        }
                        gen += "    end\n";
                        gen += "endgenerate";
                        mod.instance_blocks.push_back(gen);
                    } else {
                        mod.instance_blocks.push_back(inst);
                    }
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
            param_name = strip_namespace(param_name);
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
            // #[sv::parameter]: localparamではなくモジュールパラメータとして出力
            bool is_module_param = false;
            for (const auto& attr : gv->attributes) {
                if (attr == "sv::parameter" || attr == "verilog::parameter") {
                    is_module_param = true;
                }
            }
            if (is_module_param) {
                std::string pdecl = "parameter " + param_name;
                if (gv->init_value) {
                    // パラメータ既定値は上書き可能にするためサイズなしで出力
                    if (auto* iv = std::get_if<int64_t>(&gv->init_value->value)) {
                        pdecl += " = " + std::to_string(*iv);
                    } else {
                        pdecl += " = " + emitConstant(*gv->init_value, gv->type);
                    }
                } else if (gv->init_expr) {
                    pdecl += " = " + emitHirExpr(*gv->init_expr);
                }
                mod.header_parameters.push_back(pdecl);
                continue;
            }

            // 配列const: localparamのunpacked配列はiverilogが未対応のため、信号宣言 + initialブロックでの要素別初期化として出力する
            std::string arr_suffix = getArraySuffix(gv->type);
            if (!arr_suffix.empty() && gv->init_expr) {
                if (auto* arr =
                        std::get_if<std::unique_ptr<hir::HirArrayLiteral>>(&gv->init_expr->kind)) {
                    mod.parameters.push_back(type_str + " " + param_name + arr_suffix + ";");
                    // 無インデントで生成する（モジュールインデントは出力時に一律付与）
                    std::stringstream arr_init;
                    arr_init << "// const配列 " << param_name << " の初期化\n";
                    arr_init << "initial begin\n";
                    for (size_t ei = 0; ei < (*arr)->elements.size(); ++ei) {
                        arr_init << "    " << param_name << "[" << ei
                                 << "] = " << emitHirExpr(*(*arr)->elements[ei]) << ";\n";
                    }
                    arr_init << "end\n";
                    mod.initial_blocks.push_back(arr_init.str());
                    emitted_var_names.insert(var_name);
                    continue;
                }
            }

            std::string localparam_decl = "localparam " + type_str + " " + param_name + arr_suffix;
            if (gv->init_value) {
                localparam_decl += " = " + emitConstant(*gv->init_value, gv->type);
            } else if (gv->init_expr) {
                localparam_decl += " = " + emitHirExpr(*gv->init_expr);
            }
            localparam_decl += ";";
            mod.parameters.push_back(localparam_decl);
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
                    mod.ports.push_back({SVPort::Input, var_name, mapType(gv->type),
                                         getBitWidth(gv->type), getArraySuffix(gv->type), ""});
                } else if (is_inout) {
                    mod.ports.push_back({SVPort::InOut, var_name, mapType(gv->type),
                                         getBitWidth(gv->type), getArraySuffix(gv->type), ""});
                } else if (is_output) {
                    // assign駆動の出力ポートは連続代入されるため初期化子を付けない
                    mod.ports.push_back({SVPort::Output, var_name, mapType(gv->type),
                                         getBitWidth(gv->type), getArraySuffix(gv->type), ""});
                } else {
                    // wire宣言を追加（連続代入の左辺はnet型が必要）
                    mod.wire_declarations.push_back("wire " + mapType(gv->type) + " " + var_name +
                                                    ";");
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
                mod.assign_statements.push_back(assign_stmt);
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
                mod.reg_declarations.push_back(ram_decl);
                // 配列初期値をinitialブロックとして出力（memfile属性なら$readmemh）
                std::string ram_init = buildArrayInitialOrReadmem(*gv, var_name);
                if (!ram_init.empty()) {
                    mod.initial_blocks.push_back(ram_init);
                }
                emitted_var_names.insert(var_name);
            }
            continue;
        }

        if (is_input) {
            if (emitted_var_names.count(var_name) == 0) {
                // 配列型ポートはアンパックド次元も保持する
                mod.ports.push_back({SVPort::Input, var_name, mapType(gv->type),
                                     getBitWidth(gv->type), getArraySuffix(gv->type), ""});
                emitted_var_names.insert(var_name);
            }
            if (var_name == "clk")
                has_clk = true;
            if (var_name == "rst")
                has_rst = true;
        } else if (is_inout) {
            if (emitted_var_names.count(var_name) == 0) {
                // トライステート属性付きは複数ドライバ可能なnet型（tri）で宣言する
                bool has_tri = false;
                for (const auto& attr : gv->attributes) {
                    if (attr.rfind("sv::tri(", 0) == 0) {
                        has_tri = true;
                    }
                }
                std::string port_type = has_tri ? "tri" : mapType(gv->type);
                mod.ports.push_back({SVPort::InOut, var_name, port_type, getBitWidth(gv->type),
                                     getArraySuffix(gv->type), ""});
                emitted_var_names.insert(var_name);

                // #[sv::tri(oe: "...", out: "...")]: トライステート駆動を生成。
                // oe=1 で out を駆動、oe=0 でハイインピーダンス（'z）
                for (const auto& attr : gv->attributes) {
                    auto kv = parseSvAttrKV(attr, "sv::tri");
                    if (kv.count("oe") && kv.count("out")) {
                        int w = getBitWidth(gv->type);
                        std::string zlit = (w > 1) ? (std::to_string(w) + "'bz") : "1'bz";
                        mod.assign_statements.push_back("assign " + var_name + " = " + kv["oe"] +
                                                        " ? " + kv["out"] + " : " + zlit +
                                                        ";  // トライステート（#[sv::tri]）");
                    }
                }
            }
        } else if (is_output) {
            if (emitted_var_names.count(var_name) == 0) {
                std::string array_suffix = getArraySuffix(gv->type);
                // 内部レジスタと同様に宣言初期値を電源投入時初期値として出力する。
                // 出力しないと条件付き代入のみの出力ポートがシミュレーションでXのまま残る。
                // インスタンス出力に接続されたポートは連続駆動されるため初期値を付けない
                std::string init_value;
                if (gv->init_value && array_suffix.empty() &&
                    instance_driven_signals.count(var_name) == 0) {
                    init_value = emitConstant(*gv->init_value, gv->type, getBitWidth(gv->type));
                }
                mod.ports.push_back({SVPort::Output, var_name, mapType(gv->type),
                                     getBitWidth(gv->type), array_suffix, init_value});
                emitted_var_names.insert(var_name);
            }
        } else {
            // #[sv::sync(clk: "...", src: "...", stages: N)]: CDC 2FF同期段を生成
            bool handled_sync = false;
            for (const auto& attr : gv->attributes) {
                auto kv = parseSvAttrKV(attr, "sv::sync");
                if (kv.count("clk") && kv.count("src")) {
                    int stages = 2;
                    if (kv.count("stages")) {
                        stages = std::max(1, std::atoi(kv["stages"].c_str()));
                    }
                    std::string type_str = mapType(gv->type);
                    // メタステーブル段の宣言（合成属性 async_reg 付き）
                    std::vector<std::string> regs;
                    for (int i = 1; i < stages; ++i) {
                        std::string meta = var_name + "_meta" + std::to_string(i);
                        mod.reg_declarations.push_back("(* async_reg = \"true\" *) " + type_str +
                                                       " " + meta + ";");
                        regs.push_back(meta);
                    }
                    mod.reg_declarations.push_back("(* async_reg = \"true\" *) " + type_str + " " +
                                                   var_name + ";");
                    // 同期段のalwaysブロック
                    std::ostringstream blk;
                    blk << "    // CDC同期（#[sv::sync] stages=" << stages << "）\n";
                    blk << "    always @(posedge " << kv["clk"] << ") begin\n";
                    std::string prev = kv["src"];
                    for (const auto& r : regs) {
                        blk << "        " << r << " <= " << prev << ";\n";
                        prev = r;
                    }
                    blk << "        " << var_name << " <= " << prev << ";\n";
                    blk << "    end";
                    mod.always_ff_blocks.push_back(blk.str());
                    emitted_var_names.insert(var_name);
                    handled_sync = true;
                    break;
                }
            }
            if (handled_sync) {
                continue;
            }

            // 属性なし → 内部レジスタ/ワイヤとして宣言
            if (emitted_var_names.count(var_name) == 0) {
                std::string array_suffix = getArraySuffix(gv->type);
                std::string reg_decl = mapType(gv->type) + " " + var_name + array_suffix;
                // 宣言初期値を電源投入時初期値として出力する。
                // 出力しないとシミュレーションでXのままFSMが進まない（FPGA合成でもレジスタの初期値として扱われる）。
                // ただしインスタンス出力に接続された信号は連続駆動されるため
                // 初期値を付けない（iverilog等で二重駆動エラーになる）
                if (gv->init_value && array_suffix.empty() &&
                    instance_driven_signals.count(var_name) == 0) {
                    reg_decl +=
                        " = " + emitConstant(*gv->init_value, gv->type, getBitWidth(gv->type));
                }
                mod.reg_declarations.push_back(reg_decl + ";");
                // 配列初期値はinitialブロックとして出力（memfile属性なら$readmemh）
                if (!array_suffix.empty()) {
                    std::string arr_init = buildArrayInitialOrReadmem(*gv, var_name);
                    if (!arr_init.empty()) {
                        mod.initial_blocks.push_back(arr_init);
                    }
                }
                emitted_var_names.insert(var_name);
            }
        }
    }
}

}  // namespace cm::codegen::sv
