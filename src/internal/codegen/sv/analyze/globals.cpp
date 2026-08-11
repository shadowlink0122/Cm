// ============================================================
// MIR解析フェーズ: 事前収集・モジュール名決定・インスタンス事前パス
// ============================================================
#include "internal/codegen/sv/codegen.hpp"
#include "internal/codegen/sv/sv_internal.hpp"

#include <set>
#include <string>
#include <utility>
#include <vector>

namespace cm::codegen::sv {

// 事前収集フェーズ: #[sv::parameter]付きconst名・グローバル文字列定数長・モジュールスコープ信号名をメンバへ集める
void SVCodeGen::analyzeCollectGlobals(const mir::MirProgram& program) {
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
    io_instance_fields_.clear();
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

    // モジュールスコープ信号名を収集（テストベンチのdut.階層参照判定に使用）
    module_signal_names_.clear();
    for (const auto& gv : program.global_vars) {
        if (gv && !gv->name.empty()) {
            module_signal_names_.insert(gv->name);
            module_signal_names_.insert(strip_namespace(gv->name));
        }
    }
}

// モジュール名決定フェーズ: `module NAME;`宣言・ソースファイル名・出力ファイル名の優先順でモジュール名を決める
void SVCodeGen::analyzeModuleName(SVModule& mod) {
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
        mod.name = is_sv_reserved_word(options_.topModule) ? options_.topModule + "_mod"
                                                           : options_.topModule;
    } else if (!options_.sourceFile.empty()) {
        std::string base = extractBaseName(options_.sourceFile);
        if (!base.empty() && !is_sv_reserved_word(base)) {
            mod.name = base;
        } else if (!base.empty()) {
            mod.name = base + "_mod";
        }
    } else if (!options_.outputFile.empty()) {
        std::string base = extractBaseName(options_.outputFile);
        if (!base.empty() && !is_sv_reserved_word(base)) {
            mod.name = base;
        } else if (!base.empty()) {
            mod.name = base + "_mod";
        }
    }
}

// 事前パスフェーズ: IOインスタンス写像・extern structインスタンス駆動信号・配列信号名を宣言順に依存せず収集する
void SVCodeGen::analyzePrepassInstances(const mir::MirProgram& program,
                                        std::set<std::string>& instance_driven_signals,
                                        std::set<std::string>& array_signal_names) {
    // 事前パス: IOインスタンス（#[input]/#[output]フィールドを持つ構造体のグローバル変数）の一覧を先に構築する。インスタンス接続（a: io.x 等）の写像で宣言順に依存しないようにするため
    for (const auto& gv : program.global_vars) {
        if (!gv || !gv->type) {
            continue;
        }
        std::string io_type_name = strip_namespace(gv->type->name);
        for (const auto& st : program.structs) {
            if (!st || st->is_extern || strip_namespace(st->name) != io_type_name) {
                continue;
            }
            bool has_io = false;
            std::vector<std::string> field_names;
            for (const auto& f : st->fields) {
                field_names.push_back(f.name);
                for (const auto& a : f.attributes) {
                    if (a == "input" || a == "output" || a == "inout") {
                        has_io = true;
                    }
                }
            }
            if (has_io) {
                io_instance_fields_[strip_namespace(gv->name)] = std::move(field_names);
            }
            break;
        }
    }

    // 事前パス: extern structインスタンスの出力ポートに接続された信号を収集する。
    // これらは内部レジスタ宣言で初期値を出力しない（初期値付き変数への連続代入はiverilog等でエラーになるため）
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

    // モジュールスコープの配列信号名（非bit要素の配列）: インスタンス配列のポート結線で
    // 信号が配列ならgenvar添字を付けるための判定に使う（generate-for出力）
    for (const auto& gv : program.global_vars) {
        if (gv && gv->type && gv->type->kind == hir::TypeKind::Array && gv->type->element_type &&
            gv->type->element_type->kind != hir::TypeKind::Bit) {
            array_signal_names.insert(strip_namespace(gv->name));
        }
    }
}

}  // namespace cm::codegen::sv
