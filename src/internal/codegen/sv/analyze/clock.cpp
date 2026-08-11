// ============================================================
// MIR解析フェーズ: クロック信号の解決
// ============================================================
#include "internal/codegen/sv/codegen.hpp"
#include "internal/codegen/sv/sv_internal.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace cm::codegen::sv {

// クロック解決フェーズ: エッジ型パラメータの不足クロック入力ポート補完と暗黙clk/rstの自動追加を行う
void SVCodeGen::analyzeClockPorts(const mir::MirProgram& program, SVModule& mod, bool has_clk,
                                  bool has_rst) {
    // クロック信号の解決。
    // エッジ型（posedge/negedge）パラメータのクロック名が入力ポートにもグローバル信号（OSC等で駆動される内部クロック）にも
    // 無い場合のみ、その名前で入力ポートを自動生成する。
    // （従来は無条件に `input clk, rst` を注入していたため、内部クロックと重複宣言になる不具合があった）
    auto global_signal_exists = [&program](const std::string& n) {
        for (const auto& gv : program.global_vars) {
            if (gv && gv->name == n) {
                return true;
            }
        }
        return false;
    };
    auto port_exists = [&mod](const std::string& n) {
        for (const auto& port : mod.ports) {
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
            mod.ports.insert(mod.ports.begin(), SVPort{SVPort::Input, *it, "logic", 1, "", ""});
        }
    }

    // エッジ型パラメータを持たないasync関数がある場合は従来どおり暗黙の clk/rst を自動追加する
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
        mod.ports.insert(mod.ports.begin(), SVPort{SVPort::Input, "clk", "logic", 1, "", ""});
    }
    if (has_async && !has_rst && !global_signal_exists("rst")) {
        // clkの実際の位置を検索して直後に挿入
        size_t insert_pos = 0;
        for (size_t i = 0; i < mod.ports.size(); ++i) {
            if (mod.ports[i].name == "clk") {
                insert_pos = i + 1;
                break;
            }
        }
        mod.ports.insert(mod.ports.begin() + static_cast<ptrdiff_t>(insert_pos),
                         SVPort{SVPort::Input, "rst", "logic", 1, "", ""});
    }
}

}  // namespace cm::codegen::sv
