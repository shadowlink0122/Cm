#pragma once

// ============================================================
// 最適化パス共有の効果モデル（optimizer-shared-analysis）
// 「この文は何を書き、何を無効化すべきか」の意味論をここへ一元化し、各パス（folding/propagation/sccp/gvn/dse/dce/licm/const_unroll）は消費するだけにする。
// no_opt・ASM出力・Derefクロバー・Call越しグローバルクロバーの判定を新規に手書きすることは禁止し、必ず本APIを経由すること
// ============================================================

#include "internal/mir/nodes.hpp"

#include <unordered_set>
#include <vector>

namespace cm::mir::opt {

// 文の効果（書き込み先・クロバー範囲・最適化不可属性）
struct StmtEffects {
    // 書き込まれるベースローカル（Assignのplace.local。投影付きでもベースは無効化が必要: B3）
    std::vector<LocalId> writes;
    // 投影なしの単純代入か（値追跡表の更新はこの場合のみ許される）
    bool direct_write = false;
    // Deref書き込み: 任意のローカルへエイリアスしうるため追跡表は全消去が必要
    bool deref_clobber = false;
    // must文: 値の置換・削除・並べ替えの対象外（ただし書き込み効果は有効なまま消費する: B3）
    bool no_opt = false;
    // ASM出力オペランド（=r/+r）: no_optに関わらず実行時に書き換わるため追跡から除外（Bug1）
    std::vector<LocalId> asm_outputs;
};

inline StmtEffects effects_of(const MirStatement& stmt) {
    StmtEffects e;
    e.no_opt = stmt.no_opt;
    if (stmt.kind == MirStatement::Assign) {
        const auto& assign_data = std::get<MirStatement::AssignData>(stmt.data);
        for (const auto& proj : assign_data.place.projections) {
            if (proj.kind == ProjectionKind::Deref) {
                e.deref_clobber = true;
                break;
            }
        }
        e.writes.push_back(assign_data.place.local);
        e.direct_write = assign_data.place.projections.empty();
    } else if (stmt.kind == MirStatement::Asm) {
        const auto& asm_data = std::get<MirStatement::AsmData>(stmt.data);
        for (const auto& operand : asm_data.operands) {
            // 定数オペランド（i/n制約）はlocal_idが無効（0固定）のため出力として扱わない
            if (operand.is_constant) {
                continue;
            }
            if (!operand.constraint.empty() &&
                (operand.constraint[0] == '+' || operand.constraint[0] == '=')) {
                e.asm_outputs.push_back(operand.local_id);
            }
        }
    }
    return e;
}

// Call終端はグローバル・静的ローカルを書き換えうる（W4）。値追跡系パスは該当ローカルを追跡対象から除外する
inline bool is_call_clobbered(const MirFunction& func, LocalId local) {
    return local < func.locals.size() &&
           (func.locals[local].is_global || func.locals[local].is_static);
}

// グローバル・静的ローカルへの書き込みは関数外から観測される（DCE/DSEは削除不可）。
// 集合はis_call_clobberedと同一だが、読み（クロバーされる）と書き（観測される）で意味が異なるため別名で提供する
inline bool is_externally_visible(const MirFunction& func, LocalId local) {
    return is_call_clobbered(func, local);
}

// 複数回代入されるローカルの検出（ASM出力も代入としてカウント）。folding/propagationの追跡除外に使う
inline std::unordered_set<LocalId> detect_multi_assigned(const MirFunction& func) {
    std::unordered_set<LocalId> assigned;
    std::unordered_set<LocalId> multi_assigned;
    auto count_write = [&](LocalId target) {
        if (assigned.count(target) > 0) {
            multi_assigned.insert(target);
        } else {
            assigned.insert(target);
        }
    };
    for (const auto& block : func.basic_blocks) {
        if (!block) {
            continue;
        }
        for (const auto& stmt : block->statements) {
            if (!stmt) {
                continue;
            }
            const StmtEffects e = effects_of(*stmt);
            if (e.direct_write) {
                for (LocalId target : e.writes) {
                    count_write(target);
                }
            }
            for (LocalId target : e.asm_outputs) {
                count_write(target);
            }
        }
    }
    return multi_assigned;
}

}  // namespace cm::mir::opt
