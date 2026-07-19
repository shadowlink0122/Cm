// MIR lowering - インラインアセンブリ文

#include "internal/base/debug.hpp"
#include "internal/mir/lowering/stmt.hpp"
#include "internal/mir/passes/scalar/const_eval.hpp"

#include <cinttypes>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cm::mir {

// インラインアセンブリのlowering
void StmtLowering::lower_asm(const hir::HirAsm& asm_stmt, LoweringContext& ctx) {
    debug_msg("mir_asm", "[MIR] lower_asm: " + asm_stmt.code +
                             " operands=" + std::to_string(asm_stmt.operands.size()));

    // asm code文字列中の ${CONST_NAME} をLLVM即値リテラルに展開
    // 既存のオペランド記法 ${+r:var} は ':' を含むためスキップ
    //
    // cmソースでは ${CONST_NAME} と書くだけでよい。
    // 展開時に $$ プレフィクス (LLVM即値エスケープ) を自動付与する。
    //
    // 例: ${MOUSE_PHASE_ADDR} → $$0x93310
    //   LLVM: $$ → '$', 0x93310 はそのまま
    //   GAS:  $0x93310 (即値)
    std::string expanded_code = asm_stmt.code;
    {
        size_t pos = 0;
        while ((pos = expanded_code.find("${", pos)) != std::string::npos) {
            size_t end = expanded_code.find('}', pos);
            if (end == std::string::npos)
                break;
            std::string name = expanded_code.substr(pos + 2, end - pos - 2);
            // オペランド記法 (${+r:var} 等) はスキップ
            if (name.find(':') != std::string::npos) {
                pos = end + 1;
                continue;
            }
            // const定数テーブルから検索
            auto const_val_opt = ctx.get_const_value(name);
            if (const_val_opt) {
                int64_t val = 0;
                if (std::holds_alternative<int64_t>(const_val_opt->value)) {
                    val = std::get<int64_t>(const_val_opt->value);
                } else if (std::holds_alternative<double>(const_val_opt->value)) {
                    val = static_cast<int64_t>(std::get<double>(const_val_opt->value));
                }
                // LLVM即値リテラル ($$0x<hex>) に変換
                char buf[32];
                snprintf(buf, sizeof(buf), "$$0x%" PRIx64, static_cast<uint64_t>(val));
                std::string hex_str(buf);

                // ${CONST_NAME} を値に置換
                expanded_code.replace(pos, end - pos + 1, hex_str);
                pos += hex_str.size();
                debug_msg("mir_asm", "[MIR] const expand: ${" + name + "} -> " + hex_str);
            } else {
                debug_msg("mir_asm", "[MIR] WARNING: const not found for ${" + name + "}");
                pos = end + 1;
            }
        }
    }

    // オペランドを変換: 変数名 → LocalId、またはmacro/const → 定数値
    std::vector<MirStatement::MirAsmOperand> mir_operands;
    for (const auto& operand : asm_stmt.operands) {
        // HIRレベルで既に定数として解決されている場合
        if (operand.is_constant) {
            mir_operands.push_back(
                MirStatement::MirAsmOperand(operand.constraint, operand.const_value));
            debug_msg("mir_asm", "[MIR] operand: " + operand.constraint +
                                     " -> const_value=" + std::to_string(operand.const_value));
            continue;
        }

        // i/n制約の場合は優先的にconst_valueを検索
        bool isImmediateConstraint = (operand.constraint.find('i') != std::string::npos ||
                                      operand.constraint.find('n') != std::string::npos);

        if (isImmediateConstraint) {
            // i/n制約: 定数値が必要なので優先的にconst_valueを検索
            auto const_val_opt = ctx.get_const_value(operand.var_name);
            if (const_val_opt) {
                // 定数値を取得（整数のみサポート）
                int64_t val = 0;
                if (std::holds_alternative<int64_t>(const_val_opt->value)) {
                    val = std::get<int64_t>(const_val_opt->value);
                } else if (std::holds_alternative<double>(const_val_opt->value)) {
                    val = static_cast<int64_t>(std::get<double>(const_val_opt->value));
                }
                mir_operands.push_back(MirStatement::MirAsmOperand(operand.constraint, val));
                debug_msg("mir_asm", "[MIR] operand: " + operand.constraint + ":" +
                                         operand.var_name +
                                         " -> const_value=" + std::to_string(val));
                continue;  // 次のオペランドへ
            }
            // const_valueが見つからない場合はエラー（i/n制約には定数が必要）
            debug_msg("mir_asm",
                      "[MIR] WARNING: i/n constraint requires constant: " + operand.var_name);
        }

        // 変数名をローカル変数テーブルから検索
        auto local_id_opt = ctx.resolve_variable(operand.var_name);
        if (local_id_opt) {
            mir_operands.push_back(MirStatement::MirAsmOperand(operand.constraint, *local_id_opt));
            debug_msg("mir_asm", "[MIR] operand: " + operand.constraint + ":" + operand.var_name +
                                     " -> local_id=" + std::to_string(*local_id_opt));
        } else {
            // 変数が見つからない場合、macro/const定数として検索
            auto const_val_opt = ctx.get_const_value(operand.var_name);
            if (const_val_opt) {
                // 定数値を取得（整数のみサポート）
                int64_t val = 0;
                if (std::holds_alternative<int64_t>(const_val_opt->value)) {
                    val = std::get<int64_t>(const_val_opt->value);
                } else if (std::holds_alternative<double>(const_val_opt->value)) {
                    val = static_cast<int64_t>(std::get<double>(const_val_opt->value));
                }
                mir_operands.push_back(MirStatement::MirAsmOperand(operand.constraint, val));
                debug_msg("mir_asm", "[MIR] operand: " + operand.constraint + ":" +
                                         operand.var_name +
                                         " -> const_value=" + std::to_string(val));
            } else {
                debug_msg("mir_asm",
                          "[MIR] WARNING: variable or constant not found: " + operand.var_name);
            }
        }
    }

    ctx.push_statement(MirStatement::asm_stmt(expanded_code, asm_stmt.is_must,
                                              std::move(mir_operands), asm_stmt.clobbers));
}

}  // namespace cm::mir
