// ============================================================
// MIR解析 - モジュール情報の抽出とalwaysブロック組み立て
// ============================================================
#include "codegen.hpp"
#include "sv_internal.hpp"

#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cm::codegen::sv {

void SVCodeGen::analyzeMIR(const mir::MirProgram& program) {
    // #[sv::parameter] 付きconstを事前収集（ポート幅の記号出力で参照）
    sv_param_names_.clear();
    testbench_fns_.clear();
    for (const auto& gv : program.global_vars) {
        if (!gv || !gv->is_const) {
            continue;
        }
        for (const auto& attr : gv->attributes) {
            if (attr == "sv::parameter" || attr == "verilog::parameter") {
                std::string pname = gv->name;
                pname = strip_namespace(pname);
                sv_param_names_.insert(pname);
            }
        }
    }

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
            var_name = strip_namespace(var_name);
            global_string_lengths_[var_name] = L;
        }
    }

    SVModule default_mod;
    default_mod.name = "top";

    // モジュールスコープ信号名を収集（テストベンチのdut.階層参照判定に使用）
    module_signal_names_.clear();
    for (const auto& gv : program.global_vars) {
        if (gv && !gv->name.empty()) {
            module_signal_names_.insert(gv->name);
            module_signal_names_.insert(strip_namespace(gv->name));
        }
    }

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

    // モジュール名の決定: `module NAME;` 宣言 > ソースファイル名 > 出力ファイル名
    if (!options_.topModule.empty()) {
        default_mod.name = is_sv_reserved_word(options_.topModule) ? options_.topModule + "_mod"
                                                                   : options_.topModule;
    } else if (!options_.sourceFile.empty()) {
        std::string base = extractBaseName(options_.sourceFile);
        if (!base.empty() && !is_sv_reserved_word(base)) {
            default_mod.name = base;
        } else if (!base.empty()) {
            default_mod.name = base + "_mod";
        }
    } else if (!options_.outputFile.empty()) {
        std::string base = extractBaseName(options_.outputFile);
        if (!base.empty() && !is_sv_reserved_word(base)) {
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
        var_name = strip_namespace(var_name);

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
                default_mod.header_parameters.push_back(pdecl);
                continue;
            }

            // 配列const: localparamのunpacked配列はiverilogが未対応のため、
            // 信号宣言 + initialブロックでの要素別初期化として出力する
            std::string arr_suffix = getArraySuffix(gv->type);
            if (!arr_suffix.empty() && gv->init_expr) {
                if (auto* arr =
                        std::get_if<std::unique_ptr<hir::HirArrayLiteral>>(&gv->init_expr->kind)) {
                    default_mod.parameters.push_back(type_str + " " + param_name + arr_suffix +
                                                     ";");
                    std::stringstream arr_init;
                    arr_init << "    // const配列 " << param_name << " の初期化\n";
                    arr_init << "    initial begin\n";
                    for (size_t ei = 0; ei < (*arr)->elements.size(); ++ei) {
                        arr_init << "        " << param_name << "[" << ei
                                 << "] = " << emitHirExpr(*(*arr)->elements[ei]) << ";\n";
                    }
                    arr_init << "    end\n";
                    default_mod.initial_blocks.push_back(arr_init.str());
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
                // トライステート属性付きは複数ドライバ可能なnet型（tri）で宣言する
                bool has_tri = false;
                for (const auto& attr : gv->attributes) {
                    if (attr.rfind("sv::tri(", 0) == 0) {
                        has_tri = true;
                    }
                }
                std::string port_type = has_tri ? "tri" : mapType(gv->type);
                default_mod.ports.push_back({SVPort::InOut, var_name, port_type,
                                             getBitWidth(gv->type), getArraySuffix(gv->type)});
                emitted_var_names.insert(var_name);

                // #[sv::tri(oe: "...", out: "...")]: トライステート駆動を生成。
                // oe=1 で out を駆動、oe=0 でハイインピーダンス（'z）
                for (const auto& attr : gv->attributes) {
                    auto kv = parseSvAttrKV(attr, "sv::tri");
                    if (kv.count("oe") && kv.count("out")) {
                        int w = getBitWidth(gv->type);
                        std::string zlit = (w > 1) ? (std::to_string(w) + "'bz") : "1'bz";
                        default_mod.assign_statements.push_back(
                            "assign " + var_name + " = " + kv["oe"] + " ? " + kv["out"] + " : " +
                            zlit + ";  // トライステート（#[sv::tri]）");
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
                default_mod.ports.push_back({SVPort::Output, var_name, mapType(gv->type),
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
                        default_mod.reg_declarations.push_back("(* async_reg = \"true\" *) " +
                                                               type_str + " " + meta + ";");
                        regs.push_back(meta);
                    }
                    default_mod.reg_declarations.push_back("(* async_reg = \"true\" *) " +
                                                           type_str + " " + var_name + ";");
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
                    default_mod.always_ff_blocks.push_back(blk.str());
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

    // クロック信号の解決。
    // エッジ型（posedge/negedge）パラメータのクロック名が
    // 入力ポートにもグローバル信号（OSC等で駆動される内部クロック）にも
    // 無い場合のみ、その名前で入力ポートを自動生成する。
    // （従来は無条件に `input clk, rst` を注入していたため、内部クロックと
    //   重複宣言になる不具合があった）
    auto global_signal_exists = [&program](const std::string& n) {
        for (const auto& gv : program.global_vars) {
            if (gv && gv->name == n) {
                return true;
            }
        }
        return false;
    };
    auto port_exists = [&default_mod](const std::string& n) {
        for (const auto& port : default_mod.ports) {
            if (port.name == n) {
                return true;
            }
        }
        return false;
    };
    {
        std::vector<std::string> missing_clocks;
        for (const auto& func : program.functions) {
            if (!func) {
                continue;
            }
            for (const auto& local : func->locals) {
                if (local.is_global || !local.type) {
                    continue;
                }
                if (local.type->kind != hir::TypeKind::Posedge &&
                    local.type->kind != hir::TypeKind::Negedge) {
                    continue;
                }
                const std::string& cn = local.name;
                if (cn.empty() || global_signal_exists(cn) || port_exists(cn)) {
                    continue;
                }
                if (std::find(missing_clocks.begin(), missing_clocks.end(), cn) ==
                    missing_clocks.end()) {
                    missing_clocks.push_back(cn);
                }
            }
        }
        for (auto it = missing_clocks.rbegin(); it != missing_clocks.rend(); ++it) {
            default_mod.ports.insert(default_mod.ports.begin(),
                                     SVPort{SVPort::Input, *it, "logic", 1, ""});
        }
    }

    // エッジ型パラメータを持たないasync関数がある場合は
    // 従来どおり暗黙の clk/rst を自動追加する
    bool has_async = false;
    for (const auto& func : program.functions) {
        if (!func || !func->is_async) {
            continue;
        }
        bool has_edge_param = false;
        for (const auto& local : func->locals) {
            if (local.is_global) {
                continue;
            }
            if (local.type && (local.type->kind == hir::TypeKind::Posedge ||
                               local.type->kind == hir::TypeKind::Negedge)) {
                has_edge_param = true;
                break;
            }
        }
        if (!has_edge_param) {
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
    if (has_async && !has_clk && !global_signal_exists("clk")) {
        default_mod.ports.insert(default_mod.ports.begin(),
                                 SVPort{SVPort::Input, "clk", "logic", 1, ""});
    }
    if (has_async && !has_rst && !global_signal_exists("rst")) {
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
        flat_name = strip_namespace(flat_name);
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

void SVCodeGen::analyzeFunction(const mir::MirFunction& func, SVModule& mod) {
    // main関数はスキップ（ハードウェアにmainはない）
    if (func.name == "main")
        return;

    // std::debug::assert はイントリンシック（呼び出し箇所で即時アサーションに展開）。
    // 定義本体は出力しない（assertはSVの予約語でもある）
    if (func.name == "assert" || func.name == "panic")
        return;

    // #[test] 関数はテストベンチ生成専用（モジュール本体へは出力しない）
    for (const auto& attr : func.attributes) {
        if (attr == "test") {
            testbench_fns_.push_back(&func);
            return;
        }
    }

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
            flat_func_name = strip_namespace(flat_func_name);

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

    // ローカル変数・一時変数をalwaysブロック内ローカルとして宣言する候補を収集
    // （モジュールスコープへのホイストをやめ、スコープ汚染と
    //   function内ローカルとの名前衝突（VARHIDDEN）を防ぐ。
    //   ポートやモジュール信号と名前が衝突する変数は従来どおり宣言しない＝
    //   モジュールスコープの実体を参照する）
    std::vector<std::pair<std::string, std::string>> block_local_decls;  // {名前, 宣言文}
    std::set<std::string> port_names;
    for (const auto& port : mod.ports) {
        port_names.insert(port.name);
    }
    for (const auto& local : func.locals) {
        std::string name = local.name;
        if (name.empty() || name == "_0")
            continue;  // 戻り値用
        // モジュール信号への参照はローカルではない
        if (local.is_global)
            continue;
        // posedge/negedge型パラメータはセンシティビティ指定であり変数ではない
        if (local.type && (local.type->kind == hir::TypeKind::Posedge ||
                           local.type->kind == hir::TypeKind::Negedge))
            continue;
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
        // モジュールパラメータ（#[sv::parameter]）とも衝突チェック
        if (!is_param_var && sv_param_names_.count(name) > 0) {
            is_param_var = true;
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
            bool dup_candidate = false;
            for (const auto& c : block_local_decls) {
                if (c.first == name) {
                    dup_candidate = true;
                    break;
                }
            }
            if (!dup_candidate) {
                block_local_decls.push_back({name, decl});
            }
        }
    }

    std::ostringstream block_ss;

    // モジュール内のインデントレベルを設定
    indent_level_ = 1;

    // 関数名コメントを追加（namespace::プレフィックスをフラット化）
    std::string display_name = func.name;
    display_name = strip_namespace(display_name);
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
                    // テストベンチのクロック検出用にプロセスクロック名を記録
                    process_clock_names_.insert(local.name);
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

    // ブロック内ローカル宣言のスコープとして名前付きブロックにする
    // （名前付きブロック内の変数宣言はVerilog-2001から全ツールで有効）
    {
        std::string header = block_ss.str();
        const std::string begin_nl = " begin\n";
        if (header.size() >= begin_nl.size() &&
            header.compare(header.size() - begin_nl.size(), begin_nl.size(), begin_nl) == 0) {
            header.replace(header.size() - begin_nl.size(), begin_nl.size(),
                           " begin : " + display_name + "_blk\n");
            block_ss.str("");
            block_ss << header;
        }
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
    const std::string body_text = raw_ss.str();

    // 本文で実際に使用されるローカル変数のみブロック内に宣言する
    // （単一定義テンポラリは式ツリーへインライン済みのため宣言不要）
    {
        auto used_in_body = [&body_text](const std::string& name) {
            return contains_identifier(body_text, name);
        };
        for (const auto& c : block_local_decls) {
            if (used_in_body(c.first)) {
                block_ss << indent() << c.second << "\n";
            }
        }
    }

    block_ss << body_text;

    decreaseIndent();
    block_ss << indent() << "end\n";

    // 未使用テンポラリ宣言の除去はモジュール出力時に全ブロックを対象に行う
    // （テンポラリ名は関数間で衝突するため、関数単位の除去は誤削除の危険がある）
    std::string block_content = block_ss.str();

    // 三項演算子化は emitTerminator の構造的判定（Phase 2b）で実施済み

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

    // 冗長三項（cond ? X : X）は構造的検出が両辺同一時に直接単純代入を
    // 出力するため、テキストベースの除去パスは不要になった

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

}  // namespace cm::codegen::sv
