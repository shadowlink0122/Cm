#include "const_unroll.hpp"

#include "../core/effects.hpp"
#include "internal/mir/analysis/dominators.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace cm::mir::opt {

namespace {

// ============================================================
// MIRノードの複製ヘルパー
// ============================================================

MirOperandPtr clone_operand(const MirOperand& src) {
    auto op = std::make_unique<MirOperand>();
    op->kind = src.kind;
    op->type = src.type;
    if (std::holds_alternative<MirPlace>(src.data)) {
        op->data = std::get<MirPlace>(src.data);
    } else if (std::holds_alternative<MirConstant>(src.data)) {
        op->data = std::get<MirConstant>(src.data);
    } else if (std::holds_alternative<std::string>(src.data)) {
        op->data = std::get<std::string>(src.data);
    }
    return op;
}

MirRvaluePtr clone_rvalue(const MirRvalue& src) {
    auto rv = std::make_unique<MirRvalue>();
    rv->kind = src.kind;
    if (std::holds_alternative<MirRvalue::UseData>(src.data)) {
        rv->data =
            MirRvalue::UseData{clone_operand(*std::get<MirRvalue::UseData>(src.data).operand)};
    } else if (std::holds_alternative<MirRvalue::BinaryOpData>(src.data)) {
        const auto& d = std::get<MirRvalue::BinaryOpData>(src.data);
        rv->data = MirRvalue::BinaryOpData{d.op, clone_operand(*d.lhs), clone_operand(*d.rhs),
                                           d.result_type};
    } else if (std::holds_alternative<MirRvalue::UnaryOpData>(src.data)) {
        const auto& d = std::get<MirRvalue::UnaryOpData>(src.data);
        rv->data = MirRvalue::UnaryOpData{d.op, clone_operand(*d.operand)};
    } else if (std::holds_alternative<MirRvalue::CastData>(src.data)) {
        const auto& d = std::get<MirRvalue::CastData>(src.data);
        rv->data = MirRvalue::CastData{clone_operand(*d.operand), d.target_type};
    } else if (std::holds_alternative<MirRvalue::RefData>(src.data)) {
        const auto& d = std::get<MirRvalue::RefData>(src.data);
        rv->data = MirRvalue::RefData{d.borrow, d.place};
    } else if (std::holds_alternative<MirRvalue::AggregateData>(src.data)) {
        const auto& d = std::get<MirRvalue::AggregateData>(src.data);
        MirRvalue::AggregateData nd;
        nd.kind = d.kind;
        for (const auto& op : d.operands) {
            nd.operands.push_back(clone_operand(*op));
        }
        rv->data = std::move(nd);
    } else if (std::holds_alternative<MirRvalue::FormatConvertData>(src.data)) {
        const auto& d = std::get<MirRvalue::FormatConvertData>(src.data);
        rv->data = MirRvalue::FormatConvertData{clone_operand(*d.operand), d.format_spec};
    }
    return rv;
}

MirStatementPtr clone_statement(const MirStatement& src) {
    auto stmt = std::make_unique<MirStatement>();
    stmt->kind = src.kind;
    stmt->span = src.span;
    stmt->no_opt = src.no_opt;
    if (std::holds_alternative<MirStatement::AssignData>(src.data)) {
        const auto& d = std::get<MirStatement::AssignData>(src.data);
        stmt->data = MirStatement::AssignData{d.place, clone_rvalue(*d.rvalue)};
    } else if (std::holds_alternative<MirStatement::StorageData>(src.data)) {
        stmt->data = std::get<MirStatement::StorageData>(src.data);
    } else if (std::holds_alternative<MirStatement::AsmData>(src.data)) {
        const auto& d = std::get<MirStatement::AsmData>(src.data);
        MirStatement::AsmData nd;
        nd.code = d.code;
        nd.is_must = d.is_must;
        nd.clobbers = d.clobbers;
        nd.operands = d.operands;
        stmt->data = std::move(nd);
    }
    return stmt;
}

MirTerminatorPtr clone_terminator(const MirTerminator& src) {
    auto term = std::make_unique<MirTerminator>();
    term->kind = src.kind;
    term->span = src.span;
    if (std::holds_alternative<MirTerminator::GotoData>(src.data)) {
        term->data = std::get<MirTerminator::GotoData>(src.data);
    } else if (std::holds_alternative<MirTerminator::SwitchIntData>(src.data)) {
        const auto& d = std::get<MirTerminator::SwitchIntData>(src.data);
        MirTerminator::SwitchIntData nd;
        nd.discriminant = clone_operand(*d.discriminant);
        nd.targets = d.targets;
        nd.otherwise = d.otherwise;
        term->data = std::move(nd);
    } else if (std::holds_alternative<MirTerminator::CallData>(src.data)) {
        const auto& d = std::get<MirTerminator::CallData>(src.data);
        MirTerminator::CallData nd;
        nd.func = clone_operand(*d.func);
        for (const auto& a : d.args) {
            nd.args.push_back(clone_operand(*a));
        }
        if (d.destination) {
            nd.destination = *d.destination;
        }
        nd.success = d.success;
        nd.unwind = d.unwind;
        nd.interface_name = d.interface_name;
        nd.method_name = d.method_name;
        nd.is_virtual = d.is_virtual;
        term->data = std::move(nd);
    }
    return term;
}

// ============================================================
// ブロック内の単純な値解決
// ============================================================

// ブロック内代入の解決結果
struct BlockValue {
    enum Kind { Const, Alias, Binary, Opaque } kind = Opaque;
    int64_t const_value = 0;                // Const用
    LocalId alias = 0;                      // Alias用
    MirBinaryOp bin_op = MirBinaryOp::Add;  // Binary用
    LocalId bin_lhs = 0;  // Binary用（オペランドが定数なら *_is_const）
    LocalId bin_rhs = 0;
    bool bin_lhs_is_const = false;
    int64_t bin_lhs_const = 0;
    bool bin_rhs_is_const = false;
    int64_t bin_rhs_const = 0;
};

// オペランドを BlockValue のオペランド表現に変換
bool operand_to_value(const MirOperand& op, bool& is_const, int64_t& const_val, LocalId& local) {
    if (op.kind == MirOperand::Constant) {
        const auto& c = std::get<MirConstant>(op.data);
        if (const auto* iv = std::get_if<int64_t>(&c.value)) {
            is_const = true;
            const_val = *iv;
            return true;
        }
        if (const auto* bv = std::get_if<bool>(&c.value)) {
            is_const = true;
            const_val = *bv ? 1 : 0;
            return true;
        }
        return false;
    }
    if (op.kind == MirOperand::Copy || op.kind == MirOperand::Move) {
        const auto& place = std::get<MirPlace>(op.data);
        if (!place.projections.empty()) {
            return false;
        }
        is_const = false;
        local = place.local;
        return true;
    }
    return false;
}

// ブロック内の単純代入マップを構築する（最後の定義が有効）
std::unordered_map<LocalId, BlockValue> build_block_values(const BasicBlock& block) {
    std::unordered_map<LocalId, BlockValue> values;
    for (const auto& stmt : block.statements) {
        if (!stmt || stmt->kind != MirStatement::Assign) {
            continue;
        }
        const auto& ad = std::get<MirStatement::AssignData>(stmt->data);
        if (!ad.place.projections.empty() || !ad.rvalue) {
            continue;
        }
        BlockValue v;
        if (ad.rvalue->kind == MirRvalue::Use) {
            const auto& ud = std::get<MirRvalue::UseData>(ad.rvalue->data);
            if (!ud.operand) {
                v.kind = BlockValue::Opaque;
            } else {
                bool is_const = false;
                int64_t cval = 0;
                LocalId src = 0;
                if (operand_to_value(*ud.operand, is_const, cval, src)) {
                    if (is_const) {
                        v.kind = BlockValue::Const;
                        v.const_value = cval;
                    } else {
                        v.kind = BlockValue::Alias;
                        v.alias = src;
                    }
                } else {
                    v.kind = BlockValue::Opaque;
                }
            }
        } else if (ad.rvalue->kind == MirRvalue::BinaryOp) {
            const auto& bd = std::get<MirRvalue::BinaryOpData>(ad.rvalue->data);
            v.kind = BlockValue::Binary;
            v.bin_op = bd.op;
            if (!bd.lhs ||
                !operand_to_value(*bd.lhs, v.bin_lhs_is_const, v.bin_lhs_const, v.bin_lhs) ||
                !bd.rhs ||
                !operand_to_value(*bd.rhs, v.bin_rhs_is_const, v.bin_rhs_const, v.bin_rhs)) {
                v.kind = BlockValue::Opaque;
            }
        } else {
            v.kind = BlockValue::Opaque;
        }
        values[ad.place.local] = v;
    }
    return values;
}

// エイリアス連鎖を辿って根本の値を解決する。
// 戻り値: Const（定数）/ Alias（ブロック外で定義されたローカル、aliasフィールドが根本）
// / それ以外（解決不能）
BlockValue resolve_chain(const std::unordered_map<LocalId, BlockValue>& values, LocalId local) {
    LocalId cur = local;
    for (int depth = 0; depth < 64; ++depth) {
        auto it = values.find(cur);
        if (it == values.end()) {
            // このブロックでは定義されていない → 根本のローカル
            BlockValue v;
            v.kind = BlockValue::Alias;
            v.alias = cur;
            return v;
        }
        if (it->second.kind == BlockValue::Alias) {
            cur = it->second.alias;
            continue;
        }
        return it->second;
    }
    BlockValue v;
    v.kind = BlockValue::Opaque;
    return v;
}

// ターミネータの遷移先を列挙する
std::vector<BlockId> terminator_targets(const MirTerminator& term) {
    std::vector<BlockId> targets;
    if (std::holds_alternative<MirTerminator::GotoData>(term.data)) {
        targets.push_back(std::get<MirTerminator::GotoData>(term.data).target);
    } else if (std::holds_alternative<MirTerminator::SwitchIntData>(term.data)) {
        const auto& d = std::get<MirTerminator::SwitchIntData>(term.data);
        for (const auto& [v, t] : d.targets) {
            targets.push_back(t);
        }
        targets.push_back(d.otherwise);
    } else if (std::holds_alternative<MirTerminator::CallData>(term.data)) {
        const auto& d = std::get<MirTerminator::CallData>(term.data);
        targets.push_back(d.success);
        if (d.unwind) {
            targets.push_back(*d.unwind);
        }
    }
    return targets;
}

// ターミネータの遷移先を書き換える
void remap_terminator(MirTerminator& term, const std::function<BlockId(BlockId)>& remap) {
    if (std::holds_alternative<MirTerminator::GotoData>(term.data)) {
        auto& d = std::get<MirTerminator::GotoData>(term.data);
        d.target = remap(d.target);
    } else if (std::holds_alternative<MirTerminator::SwitchIntData>(term.data)) {
        auto& d = std::get<MirTerminator::SwitchIntData>(term.data);
        for (auto& [v, t] : d.targets) {
            t = remap(t);
        }
        d.otherwise = remap(d.otherwise);
    } else if (std::holds_alternative<MirTerminator::CallData>(term.data)) {
        auto& d = std::get<MirTerminator::CallData>(term.data);
        d.success = remap(d.success);
        if (d.unwind) {
            d.unwind = remap(*d.unwind);
        }
    }
}

// エントリから到達可能なブロック集合を計算する
std::unordered_set<BlockId> compute_reachable(const MirFunction& func) {
    std::unordered_set<BlockId> reachable;
    std::vector<BlockId> work = {func.entry_block};
    while (!work.empty()) {
        BlockId id = work.back();
        work.pop_back();
        if (id >= func.basic_blocks.size() || !func.basic_blocks[id]) {
            continue;
        }
        if (!reachable.insert(id).second) {
            continue;
        }
        if (func.basic_blocks[id]->terminator) {
            for (BlockId t : terminator_targets(*func.basic_blocks[id]->terminator)) {
                work.push_back(t);
            }
        }
    }
    return reachable;
}

// 比較演算の評価（トリップカウント計算用）
bool eval_loop_cond(MirBinaryOp op, int64_t iv, int64_t bound) {
    switch (op) {
        case MirBinaryOp::Lt:
            return iv < bound;
        case MirBinaryOp::Le:
            return iv <= bound;
        case MirBinaryOp::Gt:
            return iv > bound;
        case MirBinaryOp::Ge:
            return iv >= bound;
        case MirBinaryOp::Ne:
            return iv != bound;
        default:
            return false;
    }
}

}  // namespace

bool ConstantLoopUnroll::run(MirFunction& func) {
    bool changed_any = false;
    // ネストループは外側の展開で複製された内側ループを次の周回で展開する
    for (int round = 0; round < 16; ++round) {
        if (!try_unroll_one(func)) {
            break;
        }
        changed_any = true;
    }
    if (changed_any) {
        for (auto& block : func.basic_blocks) {
            if (block) {
                block->update_successors();
            }
        }
        func.build_cfg();
    }
    return changed_any;
}

bool ConstantLoopUnroll::try_unroll_one(MirFunction& func) {
    if (func.basic_blocks.empty()) {
        return false;
    }

    auto reachable = compute_reachable(func);

    // 先行ブロックマップを構築（到達可能ブロックのみ）
    std::unordered_map<BlockId, std::vector<BlockId>> preds;
    for (BlockId id : reachable) {
        const auto& block = func.basic_blocks[id];
        if (!block->terminator) {
            continue;
        }
        for (BlockId t : terminator_targets(*block->terminator)) {
            preds[t].push_back(id);
        }
    }

    DominatorTree domtree(func);

    for (BlockId header : reachable) {
        auto& hblock = func.basic_blocks[header];
        if (!hblock->terminator ||
            !std::holds_alternative<MirTerminator::SwitchIntData>(hblock->terminator->data)) {
            continue;
        }

        // ラッチ（ヘッダへ戻る後方エッジの始点）を検出
        std::vector<BlockId> latches;
        for (BlockId p : preds[header]) {
            if (domtree.dominates(header, p)) {
                latches.push_back(p);
            }
        }
        if (latches.empty()) {
            continue;
        }

        // 自然ループの集合をラッチから後方に辿って構築
        std::unordered_set<BlockId> loop_set = {header};
        {
            std::vector<BlockId> work(latches.begin(), latches.end());
            bool irreducible = false;
            while (!work.empty()) {
                BlockId b = work.back();
                work.pop_back();
                if (loop_set.count(b)) {
                    continue;
                }
                if (!reachable.count(b) || !domtree.dominates(header, b)) {
                    irreducible = true;
                    break;
                }
                loop_set.insert(b);
                for (BlockId p : preds[b]) {
                    work.push_back(p);
                }
            }
            if (irreducible) {
                continue;
            }
        }

        // ループへの横入りが無いこと（ヘッダ以外のループブロックの先行は全てループ内）
        bool side_entry = false;
        for (BlockId b : loop_set) {
            if (b == header) {
                continue;
            }
            for (BlockId p : preds[b]) {
                if (!loop_set.count(p)) {
                    side_entry = true;
                    break;
                }
            }
            if (side_entry) {
                break;
            }
        }
        if (side_entry) {
            continue;
        }

        // ヘッダの遷移先: ループ内（本体入口）と外（出口）が1つずつであること
        BlockId body_entry = INVALID_BLOCK;
        BlockId exit_block = INVALID_BLOCK;
        {
            bool bad = false;
            for (BlockId t : terminator_targets(*hblock->terminator)) {
                if (loop_set.count(t)) {
                    if (body_entry != INVALID_BLOCK && body_entry != t) {
                        bad = true;
                    }
                    body_entry = t;
                } else {
                    if (exit_block != INVALID_BLOCK && exit_block != t) {
                        bad = true;
                    }
                    exit_block = t;
                }
            }
            if (bad || body_entry == INVALID_BLOCK || exit_block == INVALID_BLOCK) {
                continue;
            }
        }

        // ラッチのターミネータはヘッダへの無条件Gotoであること
        {
            bool bad = false;
            for (BlockId latch : latches) {
                const auto& lb = func.basic_blocks[latch];
                if (!lb->terminator ||
                    !std::holds_alternative<MirTerminator::GotoData>(lb->terminator->data) ||
                    std::get<MirTerminator::GotoData>(lb->terminator->data).target != header) {
                    bad = true;
                    break;
                }
            }
            if (bad) {
                continue;
            }
        }

        // ヘッダの条件式から誘導変数と境界定数を特定する
        auto header_values = build_block_values(*hblock);
        const auto& sw = std::get<MirTerminator::SwitchIntData>(hblock->terminator->data);
        bool cond_is_body_on_true;
        LocalId iv = 0;
        int64_t bound = 0;
        MirBinaryOp cmp_op;
        {
            bool ok = false;
            do {
                if (!sw.discriminant) {
                    break;
                }
                bool is_const = false;
                int64_t cval = 0;
                LocalId disc = 0;
                if (!operand_to_value(*sw.discriminant, is_const, cval, disc) || is_const) {
                    break;
                }
                BlockValue cond = resolve_chain(header_values, disc);
                if (cond.kind != BlockValue::Binary) {
                    break;
                }
                cmp_op = cond.bin_op;
                if (cmp_op != MirBinaryOp::Lt && cmp_op != MirBinaryOp::Le &&
                    cmp_op != MirBinaryOp::Gt && cmp_op != MirBinaryOp::Ge &&
                    cmp_op != MirBinaryOp::Ne) {
                    break;
                }
                // lhs=誘導変数（ヘッダ外定義のローカル）、rhs=定数
                BlockValue lhs = cond.bin_lhs_is_const ? BlockValue{}
                                                       : resolve_chain(header_values, cond.bin_lhs);
                if (cond.bin_lhs_is_const || lhs.kind != BlockValue::Alias) {
                    break;
                }
                int64_t rhs_const = 0;
                if (cond.bin_rhs_is_const) {
                    rhs_const = cond.bin_rhs_const;
                } else {
                    BlockValue rhs = resolve_chain(header_values, cond.bin_rhs);
                    if (rhs.kind != BlockValue::Const) {
                        break;
                    }
                    rhs_const = rhs.const_value;
                }
                iv = lhs.alias;
                bound = rhs_const;
                // switchIntは値1（真）で本体側へ遷移するのが通常形。
                // [1: 本体] の形だけを受け付ける
                if (sw.targets.size() != 1 || sw.targets[0].first != 1) {
                    break;
                }
                cond_is_body_on_true = (sw.targets[0].second == body_entry);
                if (!cond_is_body_on_true) {
                    // 条件が偽のとき本体へ入る形は対象外
                    break;
                }
                ok = true;
            } while (false);
            if (!ok) {
                continue;
            }
        }

        // 誘導変数の安全性チェックと初期値・増分の特定
        // グローバル・静的な誘導変数は呼び出し越しに書き換わりうるため展開しない（効果モデルの共有述語）
        if (is_call_clobbered(func, iv)) {
            continue;
        }
        int64_t init_value = 0;
        int64_t step = 0;
        {
            bool ok = true;
            bool found_init = false;
            bool found_step = false;
            for (BlockId bid : reachable) {
                const auto& block = func.basic_blocks[bid];
                for (const auto& stmt : block->statements) {
                    if (!stmt) {
                        continue;
                    }
                    if (stmt->no_opt && loop_set.count(bid)) {
                        ok = false;  // mustブロックを含むループは展開しない
                        break;
                    }
                    if (stmt->kind != MirStatement::Assign) {
                        continue;
                    }
                    const auto& ad = std::get<MirStatement::AssignData>(stmt->data);
                    // 誘導変数のアドレス取得は不可
                    if (ad.rvalue && ad.rvalue->kind == MirRvalue::Ref) {
                        const auto& rd = std::get<MirRvalue::RefData>(ad.rvalue->data);
                        if (rd.place.local == iv) {
                            ok = false;
                            break;
                        }
                    }
                    if (ad.place.local != iv || !ad.place.projections.empty()) {
                        continue;
                    }
                    if (bid == header) {
                        ok = false;  // ヘッダ内での誘導変数書き換えは対象外
                        break;
                    }
                    auto block_values = build_block_values(*block);
                    if (loop_set.count(bid)) {
                        // ループ内: 増分 iv = iv (+|-) 定数
                        if (found_step) {
                            ok = false;
                            break;
                        }
                        BlockValue v = resolve_chain(block_values, iv);
                        if (v.kind != BlockValue::Binary ||
                            (v.bin_op != MirBinaryOp::Add && v.bin_op != MirBinaryOp::Sub)) {
                            ok = false;
                            break;
                        }
                        // lhsがiv、rhsが定数であること
                        if (v.bin_lhs_is_const) {
                            ok = false;
                            break;
                        }
                        BlockValue lhs_root = resolve_chain(block_values, v.bin_lhs);
                        // 増分文の左辺は「増分前のiv」なので、ブロック内解決では
                        // Binary（自分自身の定義）に到達する。根本ローカルがivであることだけを確認する（Alias経由でiv到達 or 直接iv）
                        bool lhs_is_iv = false;
                        if (lhs_root.kind == BlockValue::Alias && lhs_root.alias == iv) {
                            lhs_is_iv = true;
                        } else if (v.bin_lhs == iv) {
                            lhs_is_iv = true;
                        } else {
                            // ivのコピー（_t = copy(iv) の後に iv = _t + 1）
                            auto it = block_values.find(v.bin_lhs);
                            if (it != block_values.end() && it->second.kind == BlockValue::Alias &&
                                it->second.alias == iv) {
                                lhs_is_iv = true;
                            }
                        }
                        if (!lhs_is_iv) {
                            ok = false;
                            break;
                        }
                        int64_t s = 0;
                        if (v.bin_rhs_is_const) {
                            s = v.bin_rhs_const;
                        } else {
                            BlockValue rhs = resolve_chain(block_values, v.bin_rhs);
                            if (rhs.kind != BlockValue::Const) {
                                ok = false;
                                break;
                            }
                            s = rhs.const_value;
                        }
                        step = (v.bin_op == MirBinaryOp::Sub) ? -s : s;
                        found_step = true;
                    } else {
                        // ループ外: 初期値 iv = 定数
                        if (found_init) {
                            ok = false;
                            break;
                        }
                        BlockValue v = resolve_chain(block_values, iv);
                        if (v.kind != BlockValue::Const) {
                            ok = false;
                            break;
                        }
                        init_value = v.const_value;
                        found_init = true;
                    }
                }
                if (!ok) {
                    break;
                }
                // Callターミネータの戻り先が誘導変数の場合は不可
                if (block->terminator &&
                    std::holds_alternative<MirTerminator::CallData>(block->terminator->data)) {
                    const auto& cd = std::get<MirTerminator::CallData>(block->terminator->data);
                    if (cd.destination && cd.destination->local == iv) {
                        ok = false;
                        break;
                    }
                }
            }
            if (!ok || !found_init || !found_step || step == 0) {
                continue;
            }
        }

        // トリップカウントをシミュレーションで求める
        int64_t trip_count = 0;
        {
            int64_t v = init_value;
            bool terminated = false;
            for (int64_t n = 0; n <= max_trips_; ++n) {
                if (!eval_loop_cond(cmp_op, v, bound)) {
                    trip_count = n;
                    terminated = true;
                    break;
                }
                v += step;
            }
            if (!terminated || trip_count == 0) {
                continue;  // 上限超過 or 0回ループ（SCCPで処理される）は対象外
            }
        }

        // 展開後の総ステートメント数の見積もり
        size_t loop_stmts = 0;
        for (BlockId b : loop_set) {
            loop_stmts += func.basic_blocks[b]->statements.size();
        }
        if (loop_stmts * static_cast<size_t>(trip_count + 1) > max_total_statements_) {
            continue;
        }

        // ============================================================
        // 展開本体: H_1..H_N + Body_1..Body_N + H_final を生成
        // ============================================================
        std::vector<BlockId> body_blocks;
        for (BlockId b : loop_set) {
            if (b != header) {
                body_blocks.push_back(b);
            }
        }
        std::sort(body_blocks.begin(), body_blocks.end());

        BlockId next_id = static_cast<BlockId>(func.basic_blocks.size());
        auto alloc_block = [&func, &next_id]() -> BlockId {
            BlockId id = next_id++;
            auto block = std::make_unique<BasicBlock>(id);
            func.basic_blocks.push_back(std::move(block));
            return id;
        };

        // 各イテレーションのブロックIDを先に確保する
        // iter_headers[k] = k回目のヘッダ複製（k=trip_countは最終ヘッダ=出口側）
        std::vector<BlockId> iter_headers;
        std::vector<std::map<BlockId, BlockId>> iter_body_map;
        for (int64_t k = 0; k <= trip_count; ++k) {
            iter_headers.push_back(alloc_block());
            std::map<BlockId, BlockId> m;
            if (k < trip_count) {
                for (BlockId b : body_blocks) {
                    m[b] = alloc_block();
                }
            }
            iter_body_map.push_back(std::move(m));
        }

        // ヘッダ複製の中身を構築
        for (int64_t k = 0; k <= trip_count; ++k) {
            auto& hb = func.basic_blocks[iter_headers[k]];
            for (const auto& stmt : func.basic_blocks[header]->statements) {
                hb->statements.push_back(clone_statement(*stmt));
            }
            BlockId target = (k < trip_count) ? iter_body_map[k].at(body_entry) : exit_block;
            hb->set_terminator(MirTerminator::goto_block(target));
        }

        // 本体複製の中身を構築
        for (int64_t k = 0; k < trip_count; ++k) {
            const auto& m = iter_body_map[k];
            for (BlockId b : body_blocks) {
                auto& nb = func.basic_blocks[m.at(b)];
                for (const auto& stmt : func.basic_blocks[b]->statements) {
                    nb->statements.push_back(clone_statement(*stmt));
                }
                auto term = clone_terminator(*func.basic_blocks[b]->terminator);
                remap_terminator(*term, [&](BlockId t) -> BlockId {
                    if (t == header) {
                        return iter_headers[k + 1];  // 後方エッジ → 次イテレーション
                    }
                    auto it = m.find(t);
                    if (it != m.end()) {
                        return it->second;  // ループ内 → 同イテレーションの複製
                    }
                    return t;  // ループ外（break等）はそのまま
                });
                nb->terminator = std::move(term);
                nb->update_successors();
            }
        }

        // 元のヘッダへの外部からの遷移を最初の複製ヘッダへ付け替える
        for (BlockId bid : reachable) {
            if (loop_set.count(bid)) {
                continue;
            }
            auto& block = func.basic_blocks[bid];
            if (!block->terminator) {
                continue;
            }
            remap_terminator(*block->terminator, [&](BlockId t) -> BlockId {
                return (t == header) ? iter_headers[0] : t;
            });
            block->update_successors();
        }

        // 元のループブロックは到達不能になる（後続のクリーンアップ任せ）
        return true;
    }

    return false;
}

void unroll_constant_loops(MirProgram& program) {
    ConstantLoopUnroll pass;
    for (auto& func : program.functions) {
        if (func) {
            pass.run(*func);
        }
    }
}

}  // namespace cm::mir::opt
