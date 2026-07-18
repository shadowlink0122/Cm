// ============================================================
// SVバックエンド検証 - 予約語・非合成型・NBA警告
// ============================================================
#include "../../common/i18n.hpp"
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

// SystemVerilog（IEEE 1800-2017 Annex B）の予約語判定。
// Cm識別子がそのままSVへ出力されるため、衝突すると不正なSVになる
bool is_sv_reserved_word(const std::string& name) {
    // clang-format off
    static const std::unordered_set<std::string> kReserved = {
        // a
        "accept_on", "alias", "always", "always_comb", "always_ff", "always_latch", "and",
        "assert", "assign", "assume", "automatic",
        // b
        "before", "begin", "bind", "bins", "binsof", "bit", "break", "buf", "bufif0", "bufif1",
        "byte",
        // c
        "case", "casex", "casez", "cell", "chandle", "checker", "class", "clocking", "cmos",
        "config", "const", "constraint", "context", "continue", "cover", "covergroup",
        "coverpoint", "cross",
        // d
        "deassign", "default", "defparam", "design", "disable", "dist", "do",
        // e
        "edge", "else", "end", "endcase", "endchecker", "endclass", "endclocking", "endconfig",
        "endfunction", "endgenerate", "endgroup", "endinterface", "endmodule", "endpackage",
        "endprimitive", "endprogram", "endproperty", "endsequence", "endspecify", "endtable",
        "endtask", "enum", "event", "eventually", "expect", "export", "extends", "extern",
        // f
        "final", "first_match", "for", "force", "foreach", "forever", "fork", "forkjoin",
        "function",
        // g
        "generate", "genvar", "global",
        // h
        "highz0", "highz1",
        // i
        "if", "iff", "ifnone", "ignore_bins", "illegal_bins", "implements", "implies", "import",
        "incdir", "include", "initial", "inout", "input", "inside", "instance", "int", "integer",
        "interconnect", "interface", "intersect",
        // j
        "join", "join_any", "join_none",
        // l
        "large", "let", "liblist", "library", "local", "localparam", "logic", "longint",
        // m
        "macromodule", "matches", "medium", "modport", "module",
        // n
        "nand", "negedge", "nettype", "new", "nexttime", "nmos", "nor", "noshowcancelled", "not",
        "notif0", "notif1", "null",
        // o
        "or", "output",
        // p
        "package", "packed", "parameter", "pmos", "posedge", "primitive", "priority", "program",
        "property", "protected", "pull0", "pull1", "pulldown", "pullup", "pulsestyle_ondetect",
        "pulsestyle_onevent", "pure",
        // r
        "rand", "randc", "randcase", "randsequence", "rcmos", "real", "realtime", "ref", "reg",
        "reject_on", "release", "repeat", "restrict", "return", "rnmos", "rpmos", "rtran",
        "rtranif0", "rtranif1",
        // s
        "s_always", "s_eventually", "s_nexttime", "s_until", "s_until_with", "scalared",
        "sequence", "shortint", "shortreal", "showcancelled", "signed", "small", "soft", "solve",
        "specify", "specparam", "static", "string", "strong", "strong0", "strong1", "struct",
        "super", "supply0", "supply1", "sync_accept_on", "sync_reject_on",
        // t
        "table", "tagged", "task", "this", "throughout", "time", "timeprecision", "timeunit",
        "tran", "tranif0", "tranif1", "tri", "tri0", "tri1", "triand", "trior", "trireg", "type",
        "typedef",
        // u
        "union", "unique", "unique0", "unsigned", "until", "until_with", "untyped", "use",
        "uwire",
        // v
        "var", "vectored", "virtual", "void",
        // w
        "wait", "wait_order", "wand", "weak", "weak0", "weak1", "while", "wildcard", "wire",
        "with", "within", "wor",
        // x
        "xnor", "xor",
    };
    // clang-format on
    return kReserved.count(name) > 0;
}

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
                // 非constのstringは logic [23:0]（3文字分）固定のため、3文字を超える初期値はサイレントに切り詰められてしまう → エラーにする
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
                // 従来は警告のみで logic [31:0] として出力され、演算結果が静かに壊れていた。非対応として明示エラーにする
                std::cerr << i18n::msgf(i18n::MsgId::SvSv004FloatingPointTypesAre, gv->name);
                has_error = true;
                break;
            case hir::TypeKind::Array:
                // 動的配列（スライス）は実行時確保が前提のため合成不能。
                // 従来は無警告で未定義のランタイム関数呼び出しを出力していた
                if (!gv->type->array_size.has_value() && gv->type->size_param_name.empty()) {
                    std::cerr << i18n::msgf(i18n::MsgId::SvSv006DynamicArraysSlicesAre, gv->name);
                    has_error = true;
                }
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
                    // String types not synthesizable error is removed to allow local string constants/temporaries
                    break;
                case hir::TypeKind::Float:
                case hir::TypeKind::Double:
                    std::cerr << i18n::msgf(i18n::MsgId::SvSv004FloatingPointTypesAre2, func->name,
                                            local.name);
                    has_error = true;
                    break;
                case hir::TypeKind::Array:
                    if (!local.type->array_size.has_value() &&
                        local.type->size_param_name.empty()) {
                        std::cerr << i18n::msgf(i18n::MsgId::SvSv006DynamicArraysSlicesAre2,
                                                func->name, local.name);
                        has_error = true;
                    }
                    break;
                default:
                    break;
            }
        }
    }
    return !has_error;
}

// SV予約語と衝突する識別子のチェック。
// そのまま出力すると不正なSV（iverilog等で構文エラー）になるため、明確なエラーとしてコンパイルを停止する（監査08 A-4対応）
void SVCodeGen::validateReservedIdentifiers(const mir::MirProgram& program) const {
    std::vector<std::string> collisions;
    auto check_name = [&](const std::string& raw, const std::string& kind) {
        std::string name = strip_namespace(raw);
        if (is_sv_reserved_word(name)) {
            collisions.push_back(kind + " '" + name + "'");
        }
    };
    for (const auto& gv : program.global_vars) {
        if (gv) {
            check_name(gv->name, i18n::msg(i18n::MsgId::SvVariable));
        }
    }
    for (const auto& func : program.functions) {
        if (!func) {
            continue;
        }
        // モジュール本体へ出力されない関数は対象外（analyzeFunctionのスキップ条件と対応: main / assert・panic
        //   イントリンシック / #[test] テスト関数）
        std::string fn_name = strip_namespace(func->name);
        if (fn_name == "main" || fn_name == "assert" || fn_name == "panic") {
            continue;
        }
        bool is_test_fn = false;
        for (const auto& attr : func->attributes) {
            if (attr == "test") {
                is_test_fn = true;
                break;
            }
        }
        if (is_test_fn) {
            continue;
        }
        check_name(func->name, i18n::msg(i18n::MsgId::SvFunction));
        for (const auto& local : func->locals) {
            // ユーザー定義のローカル変数のみ（コンパイラ生成の一時変数は対象外）
            if (local.is_user_variable && !local.is_global && !local.name.empty()) {
                check_name(local.name, i18n::msg(i18n::MsgId::SvVariable));
            }
        }
    }
    if (!collisions.empty()) {
        std::string msg = i18n::msg(i18n::MsgId::SvIdentifiersConflictWithSystemverilogReserved);
        for (size_t i = 0; i < collisions.size(); ++i) {
            if (i > 0) {
                msg += ", ";
            }
            msg += collisions[i];
        }
        msg += i18n::msg(i18n::MsgId::SvRenameThem);
        throw std::runtime_error(msg);
    }
}

// --sv-warn-nba: posedge/negedge関数内で「代入した状態変数をその後で参照」を警告する。
// 状態変数への代入は次サイクル反映（ノンブロッキング代入）のため、直後の参照は前サイクル値を読む。ソフトウェア的な逐次実行とは結果が異なるため、意図の確認を促す（基本ブロック内の保守的な検査）
void SVCodeGen::warnNbaReadback(const mir::MirProgram& program) const {
    if (!options_.warnNba) {
        return;
    }
    for (const auto& func : program.functions) {
        if (!func) {
            continue;
        }
        bool has_edge = false;
        for (const auto& local : func->locals) {
            if (!local.is_global && local.type &&
                (local.type->kind == hir::TypeKind::Posedge ||
                 local.type->kind == hir::TypeKind::Negedge)) {
                has_edge = true;
                break;
            }
        }
        if (!has_edge) {
            continue;
        }
        auto is_state_var = [&func](mir::LocalId id) {
            return id < func->locals.size() && func->locals[id].is_global;
        };
        std::set<std::string> warned;
        auto check_operand = [&](const mir::MirOperandPtr& op,
                                 const std::set<mir::LocalId>& assigned) {
            if (!op || (op->kind != mir::MirOperand::Move && op->kind != mir::MirOperand::Copy)) {
                return;
            }
            auto* pl = std::get_if<mir::MirPlace>(&op->data);
            if (!pl || assigned.count(pl->local) == 0) {
                return;
            }
            const std::string& nm = func->locals[pl->local].name;
            if (warned.insert(nm).second) {
                std::cerr << i18n::msgf(i18n::MsgId::SvSvTargetFunctionAssignsState,
                                        strip_namespace(func->name), nm);
            }
        };
        for (const auto& bb : func->basic_blocks) {
            if (!bb) {
                continue;
            }
            std::set<mir::LocalId> assigned;
            for (const auto& stmt : bb->statements) {
                if (!stmt || stmt->kind != mir::MirStatement::Assign) {
                    continue;
                }
                const auto& ad = std::get<mir::MirStatement::AssignData>(stmt->data);
                // 右辺の読み取りを先に検査（自己更新 x = x + 1 は対象外になる）
                if (ad.rvalue) {
                    switch (ad.rvalue->kind) {
                        case mir::MirRvalue::Use:
                            check_operand(
                                std::get<mir::MirRvalue::UseData>(ad.rvalue->data).operand,
                                assigned);
                            break;
                        case mir::MirRvalue::BinaryOp: {
                            const auto& bd =
                                std::get<mir::MirRvalue::BinaryOpData>(ad.rvalue->data);
                            check_operand(bd.lhs, assigned);
                            check_operand(bd.rhs, assigned);
                            break;
                        }
                        case mir::MirRvalue::UnaryOp:
                            check_operand(
                                std::get<mir::MirRvalue::UnaryOpData>(ad.rvalue->data).operand,
                                assigned);
                            break;
                        case mir::MirRvalue::Cast:
                            check_operand(
                                std::get<mir::MirRvalue::CastData>(ad.rvalue->data).operand,
                                assigned);
                            break;
                        default:
                            break;
                    }
                }
                // 左辺: 状態変数への代入を記録
                if (is_state_var(ad.place.local)) {
                    assigned.insert(ad.place.local);
                }
            }
        }
    }
}

}  // namespace cm::codegen::sv
