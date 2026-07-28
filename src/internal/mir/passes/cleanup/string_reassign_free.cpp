// 文字列再代入時の旧バッファ解放パス（C12の変数上書き）の実装。
// 対象ローカルの選別（fresh所有・非エイリアス）と到達定義解析、free呼び出し挿入のためのブロック分割を行う

#include "string_reassign_free.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace cm::mir::opt {

namespace {

// 新規バッファを返すことが既知のランタイム関数（この結果を受けたローカルはバッファを所有する）
bool is_fresh_buffer_callee(const std::string& name) {
    return name == "cm_string_concat" || name.find("_to_string") != std::string::npos ||
           name.rfind("cm_format_", 0) == 0;
}

// 引数ポインタを保持しない（読み取りのみ）ことが既知のランタイム関数。
// これらへ渡ってもエイリアスは生じない（temp_dropの非保持ホワイトリストと同趣旨の文字列系サブセット）
bool is_non_retaining_callee(const std::string& name) {
    return name.rfind("cm_println_", 0) == 0 || name.rfind("cm_print_", 0) == 0 ||
           name.rfind("cm_format_", 0) == 0 || name.rfind("__builtin_string_", 0) == 0 ||
           name == "cm_string_concat" || name == "cm_strlen" || name == "cm_strcmp" ||
           name == "strlen" || name == "strcmp" || name == "cm_string_free" ||
           name.find("_to_string") != std::string::npos;
}

// 定義サイト（代入文またはCall終端の格納先）
struct DefSite {
    BlockId block;
    // ブロック内のstatementインデックス。Call終端の格納先の場合はstatements.size()（末尾扱い）
    size_t stmt_index;
    bool is_terminator_dest;
    bool fresh = false;  // fresh所有バッファの定義か
};

// 終端の後続ブロックを列挙する
std::vector<BlockId> successors(const MirTerminator& term) {
    std::vector<BlockId> succ;
    switch (term.kind) {
        case MirTerminator::Goto:
            succ.push_back(std::get<MirTerminator::GotoData>(term.data).target);
            break;
        case MirTerminator::SwitchInt: {
            const auto& sw = std::get<MirTerminator::SwitchIntData>(term.data);
            for (const auto& [value, target] : sw.targets) {
                succ.push_back(target);
            }
            succ.push_back(sw.otherwise);
            break;
        }
        case MirTerminator::Call: {
            const auto& call = std::get<MirTerminator::CallData>(term.data);
            succ.push_back(call.success);
            if (call.unwind) {
                succ.push_back(*call.unwind);
            }
            break;
        }
        default:
            break;
    }
    return succ;
}

}  // namespace

bool StringReassignFree::run(MirFunction& func) {
    // 冪等性ガード: 本パスが挿入する退避ローカル（_c12_old）が既にあれば処理済み。
    // 収束マネージャが変化ありとみなして再実行しても、二重挿入（二重free）と循環を起こさない
    for (const auto& local : func.locals) {
        if (local.name == "_c12_old") {
            return false;
        }
    }

    // 文字列型ローカルの収集（引数は対象外: 呼び出し元がバッファを所有する）
    std::unordered_set<LocalId> args(func.arg_locals.begin(), func.arg_locals.end());
    std::vector<LocalId> candidates;
    for (LocalId i = 0; i < static_cast<LocalId>(func.locals.size()); ++i) {
        const auto& local = func.locals[i];
        if (!local.type || local.type->kind != hir::TypeKind::String || args.count(i) > 0) {
            continue;
        }
        candidates.push_back(i);
    }
    if (candidates.empty()) {
        return false;
    }

    // ローカルごとの定義サイトと使用レコードを1パスで収集する。
    // loweringは呼び出し引数等で必ず temp = copy(X) の一時を作るため、コピーを一律エイリアスにすると
    // 全候補が除外される。コピー先一時のプロファイル（単一定義・読み取り/非保持呼び出しのみで消費）を
    // 1段だけ追跡し、引数一時経由の読み取りをエイリアスから除外する
    struct UseRec {
        enum Kind { Read, Retaining, CopyTo, Hard } kind;
        LocalId dest = 0;  // CopyToのコピー先
    };
    std::unordered_map<LocalId, std::vector<DefSite>> defs;
    std::unordered_map<LocalId, std::vector<UseRec>> uses;

    // ローカルXの定義rvalueがfreshか（Use(copy(T))でTがfresh呼び出しの単独消費一時、を含む）を
    // 後段で判定するため、Call終端の格納先→calleeの対応も記録する
    std::unordered_map<LocalId, std::vector<std::string>> call_dest_callees;

    auto record_read = [&](const MirOperandPtr& op) {
        if (!op || (op->kind != MirOperand::Copy && op->kind != MirOperand::Move)) {
            return;
        }
        const auto& place = std::get<MirPlace>(op->data);
        uses[place.local].push_back(UseRec{UseRec::Read, 0});
        for (const auto& proj : place.projections) {
            if (proj.kind == ProjectionKind::Index) {
                uses[proj.index_local].push_back(UseRec{UseRec::Read, 0});
            }
        }
    };
    auto record_hard = [&](const MirOperandPtr& op) {
        if (!op || (op->kind != MirOperand::Copy && op->kind != MirOperand::Move)) {
            return;
        }
        const auto& place = std::get<MirPlace>(op->data);
        uses[place.local].push_back(UseRec{UseRec::Hard, 0});
    };

    for (BlockId b = 0; b < static_cast<BlockId>(func.basic_blocks.size()); ++b) {
        const auto* block = func.basic_blocks[b].get();
        if (!block) {
            continue;
        }
        for (size_t i = 0; i < block->statements.size(); ++i) {
            const auto& stmt = block->statements[i];
            if (!stmt) {
                continue;
            }
            if (stmt->kind == MirStatement::Asm) {
                // Asmに関わるローカルは全て保守的に除外
                const auto& asm_data = std::get<MirStatement::AsmData>(stmt->data);
                for (const auto& operand : asm_data.operands) {
                    uses[operand.local_id].push_back(UseRec{UseRec::Hard, 0});
                }
                continue;
            }
            if (stmt->kind != MirStatement::Assign) {
                continue;
            }
            const auto& assign = std::get<MirStatement::AssignData>(stmt->data);

            // 定義の記録（投影なしの直接代入のみを定義とみなす。投影付きは保守的にHard扱い）
            if (assign.place.projections.empty()) {
                defs[assign.place.local].push_back(DefSite{b, i, false});
            } else {
                uses[assign.place.local].push_back(UseRec{UseRec::Hard, 0});
                for (const auto& proj : assign.place.projections) {
                    if (proj.kind == ProjectionKind::Index) {
                        uses[proj.index_local].push_back(UseRec{UseRec::Read, 0});
                    }
                }
            }

            const auto& rv = assign.rvalue;
            if (!rv) {
                continue;
            }
            switch (rv->kind) {
                case MirRvalue::Use: {
                    const auto& use = std::get<MirRvalue::UseData>(rv->data);
                    if (use.operand && (use.operand->kind == MirOperand::Copy ||
                                        use.operand->kind == MirOperand::Move)) {
                        const auto& src = std::get<MirPlace>(use.operand->data);
                        if (src.projections.empty() && assign.place.projections.empty()) {
                            if (src.local == assign.place.local) {
                                uses[src.local].push_back(UseRec{UseRec::Read, 0});
                            } else {
                                uses[src.local].push_back(
                                    UseRec{UseRec::CopyTo, assign.place.local});
                            }
                        } else {
                            record_hard(use.operand);
                        }
                    }
                    break;
                }
                case MirRvalue::BinaryOp: {
                    const auto& bin = std::get<MirRvalue::BinaryOpData>(rv->data);
                    record_read(bin.lhs);
                    record_read(bin.rhs);
                    break;
                }
                case MirRvalue::UnaryOp:
                    record_read(std::get<MirRvalue::UnaryOpData>(rv->data).operand);
                    break;
                case MirRvalue::FormatConvert:
                    record_read(std::get<MirRvalue::FormatConvertData>(rv->data).operand);
                    break;
                case MirRvalue::Ref:
                    uses[std::get<MirRvalue::RefData>(rv->data).place.local].push_back(
                        UseRec{UseRec::Hard, 0});
                    break;
                case MirRvalue::Aggregate:
                    for (const auto& op : std::get<MirRvalue::AggregateData>(rv->data).operands) {
                        record_hard(op);
                    }
                    break;
                case MirRvalue::Cast:
                    record_hard(std::get<MirRvalue::CastData>(rv->data).operand);
                    break;
            }
        }

        if (!block->terminator) {
            continue;
        }
        const auto& term = *block->terminator;
        if (term.kind == MirTerminator::Call) {
            const auto& call = std::get<MirTerminator::CallData>(term.data);
            std::string callee;
            if (call.func && call.func->kind == MirOperand::FunctionRef) {
                callee = std::get<std::string>(call.func->data);
            }
            // 仮想呼び出し・関数ポインタ経由は保持されうるものとして扱う
            bool retains = callee.empty() || call.is_virtual || !is_non_retaining_callee(callee);
            for (const auto& arg : call.args) {
                if (!arg || (arg->kind != MirOperand::Copy && arg->kind != MirOperand::Move)) {
                    continue;
                }
                const auto& place = std::get<MirPlace>(arg->data);
                uses[place.local].push_back(UseRec{retains ? UseRec::Retaining : UseRec::Read, 0});
            }
            if (call.destination && call.destination->projections.empty()) {
                defs[call.destination->local].push_back(DefSite{b, block->statements.size(), true});
                call_dest_callees[call.destination->local].push_back(callee);
            } else if (call.destination) {
                uses[call.destination->local].push_back(UseRec{UseRec::Hard, 0});
            }
        } else if (term.kind == MirTerminator::SwitchInt) {
            const auto& sw = std::get<MirTerminator::SwitchIntData>(term.data);
            record_read(sw.discriminant);
        } else if (term.kind == MirTerminator::Return) {
            // returnは最終値が呼び出し元へ渡るのみで、過去のバッファ解放の妥当性へは影響しない
        }
    }

    // コピー先一時の安全性: 単一定義で、消費が読み取り/非保持呼び出しのみ（さらなるコピー・保持・Hardなし）
    auto copy_dest_is_transparent = [&](LocalId dest) {
        auto d_it = defs.find(dest);
        if (d_it == defs.end() || d_it->second.size() != 1) {
            return false;
        }
        auto u_it = uses.find(dest);
        if (u_it == uses.end()) {
            return true;  // 未使用の一時（デッドコピー）
        }
        for (const auto& u : u_it->second) {
            if (u.kind != UseRec::Read) {
                return false;
            }
        }
        return true;
    };

    // ローカルXがエイリアスされないか（全使用が読み取りまたは透明なコピー先経由）
    auto is_unaliased = [&](LocalId x) {
        auto u_it = uses.find(x);
        if (u_it == uses.end()) {
            return true;
        }
        for (const auto& u : u_it->second) {
            if (u.kind == UseRec::Read) {
                continue;
            }
            if (u.kind == UseRec::CopyTo && copy_dest_is_transparent(u.dest)) {
                continue;
            }
            return false;
        }
        return true;
    };

    // ローカルXの消費使用回数（fresh一時の単独消費判定用）
    auto consuming_use_count = [&](LocalId x) {
        auto u_it = uses.find(x);
        return u_it == uses.end() ? size_t{0} : u_it->second.size();
    };

    // 各定義のfresh判定。
    // (a) Call終端の格納先でcalleeがfresh、(b) Use(copy(T))でTの定義が唯一のfresh呼び出し格納先かつ
    //     Tの消費使用がこのコピー1回のみ（複数消費だとバッファが他所へも渡っている）
    auto def_index = [&](LocalId x) -> const std::vector<DefSite>* {
        auto it = defs.find(x);
        return it == defs.end() ? nullptr : &it->second;
    };
    auto classify_fresh = [&](LocalId x, DefSite& d) {
        const auto* block = func.basic_blocks[d.block].get();
        if (d.is_terminator_dest) {
            const auto& callees = call_dest_callees[x];
            // 同一ローカルへの複数call格納がある場合の対応付けは順序保存できないため、
            // 全てのcall格納がfreshな場合のみfreshとみなす（保守的）
            d.fresh = !callees.empty() &&
                      std::all_of(callees.begin(), callees.end(), is_fresh_buffer_callee);
            return;
        }
        const auto& stmt = block->statements[d.stmt_index];
        const auto& assign = std::get<MirStatement::AssignData>(stmt->data);
        if (!assign.rvalue || assign.rvalue->kind != MirRvalue::Use) {
            d.fresh = false;
            return;
        }
        const auto& use = std::get<MirRvalue::UseData>(assign.rvalue->data);
        if (!use.operand ||
            (use.operand->kind != MirOperand::Copy && use.operand->kind != MirOperand::Move)) {
            d.fresh = false;
            return;
        }
        const auto& src = std::get<MirPlace>(use.operand->data);
        if (!src.projections.empty()) {
            d.fresh = false;
            return;
        }
        LocalId t = src.local;
        // Tの定義が唯一で、fresh呼び出しの格納先であること
        const auto* t_defs = def_index(t);
        if (!t_defs || t_defs->size() != 1 || !(*t_defs)[0].is_terminator_dest) {
            d.fresh = false;
            return;
        }
        const auto& t_callees = call_dest_callees[t];
        if (t_callees.size() != 1 || !is_fresh_buffer_callee(t_callees[0])) {
            d.fresh = false;
            return;
        }
        // Tの消費がこのコピー1回のみであること（他所へ渡っていればバッファは共有されている）
        d.fresh = consuming_use_count(t) == 1;
    };

    // 先行ブロックの逆引きを構築
    std::unordered_map<BlockId, std::vector<BlockId>> preds;
    for (BlockId b = 0; b < static_cast<BlockId>(func.basic_blocks.size()); ++b) {
        const auto* block = func.basic_blocks[b].get();
        if (!block || !block->terminator) {
            continue;
        }
        for (BlockId s : successors(*block->terminator)) {
            preds[s].push_back(b);
        }
    }

    // 挿入計画: (ブロック, 定義文インデックス, 対象ローカル)
    struct Insertion {
        BlockId block;
        size_t stmt_index;
        LocalId local;
    };
    std::vector<Insertion> insertions;

    for (LocalId x : candidates) {
        auto it = defs.find(x);
        if (it == defs.end() || !is_unaliased(x)) {
            continue;
        }
        auto& x_defs = it->second;
        if (x_defs.size() < 2) {
            continue;  // 再代入が存在しない
        }
        bool all_fresh = true;
        for (auto& d : x_defs) {
            classify_fresh(x, d);
            all_fresh = all_fresh && d.fresh;
        }
        if (!all_fresh) {
            continue;
        }

        // 到達定義解析（対象ローカル単体・ブロック単位）。
        // 値ドメイン: {UNINIT} ∪ 定義サイト。ブロック内に定義があれば出口はその最後の定義のみ
        constexpr int kUninit = -1;
        std::unordered_map<BlockId, int>
            last_def_in_block;  // ブロック内最後の定義のdefsインデックス
        for (size_t di = 0; di < x_defs.size(); ++di) {
            auto found = last_def_in_block.find(x_defs[di].block);
            if (found == last_def_in_block.end() ||
                x_defs[found->second].stmt_index < x_defs[di].stmt_index) {
                last_def_in_block[x_defs[di].block] = static_cast<int>(di);
            }
        }
        std::unordered_map<BlockId, std::set<int>> in_sets, out_sets;
        // 初期化: entryのINはUNINIT
        in_sets[0].insert(kUninit);
        bool changed_flow = true;
        while (changed_flow) {
            changed_flow = false;
            for (BlockId b = 0; b < static_cast<BlockId>(func.basic_blocks.size()); ++b) {
                if (!func.basic_blocks[b]) {
                    continue;
                }
                std::set<int> in = in_sets[b];
                for (BlockId p : preds[b]) {
                    for (int v : out_sets[p]) {
                        in.insert(v);
                    }
                }
                std::set<int> out;
                auto ld = last_def_in_block.find(b);
                if (ld != last_def_in_block.end()) {
                    out.insert(ld->second);
                } else {
                    out = in;
                }
                if (in != in_sets[b] || out != out_sets[b]) {
                    in_sets[b] = std::move(in);
                    out_sets[b] = std::move(out);
                    changed_flow = true;
                }
            }
        }

        // 各定義（Call終端格納先は挿入位置の特定が複雑なため代入文の定義のみ対象）について、
        // その地点への到達定義が全てfresh定義（UNINITなし）なら旧値解放を挿入する
        for (size_t di = 0; di < x_defs.size(); ++di) {
            const auto& d = x_defs[di];
            if (d.is_terminator_dest) {
                continue;
            }
            // 定義地点への到達集合: ブロックINから始め、同一ブロック内でこの定義より前の定義があればそれが上書き
            std::set<int> reaching = in_sets[d.block];
            int prior_in_block = -2;
            for (size_t dj = 0; dj < x_defs.size(); ++dj) {
                if (dj == di || x_defs[dj].block != d.block ||
                    x_defs[dj].stmt_index >= d.stmt_index) {
                    continue;
                }
                if (prior_in_block == -2 ||
                    x_defs[dj].stmt_index >
                        x_defs[static_cast<size_t>(prior_in_block)].stmt_index) {
                    prior_in_block = static_cast<int>(dj);
                }
            }
            if (prior_in_block != -2) {
                reaching.clear();
                reaching.insert(prior_in_block);
            }
            if (reaching.empty() || reaching.count(kUninit) > 0) {
                continue;  // 未初期化が到達しうる（最初の代入等）
            }
            // 到達定義は全てx_defsのメンバでありall_fresh確認済み
            insertions.push_back(Insertion{d.block, d.stmt_index, x});
        }
    }

    if (insertions.empty()) {
        return false;
    }

    // 挿入の適用。定義文の直前に旧値の退避（O = copy(X)）を置き、その直後でブロックを分割して
    // cm_string_free(O)のCall終端を挿む。後方の挿入から処理してインデックスを保つ
    std::sort(insertions.begin(), insertions.end(), [](const Insertion& a, const Insertion& b) {
        return a.block != b.block ? a.block > b.block : a.stmt_index > b.stmt_index;
    });

    for (const auto& ins : insertions) {
        auto* block = func.basic_blocks[ins.block].get();
        if (!block || ins.stmt_index >= block->statements.size()) {
            continue;
        }

        // 旧値退避ローカル
        LocalId old_val = func.add_local("_c12_old", hir::make_string());

        // 分割: [0, stmt_index) + 退避 | free呼び出し | 新ブロック[stmt_index, end) + 元の終端
        BlockId cont = func.basic_blocks.size();
        auto cont_block = std::make_unique<BasicBlock>();
        cont_block->id = cont;
        for (size_t i = ins.stmt_index; i < block->statements.size(); ++i) {
            cont_block->statements.push_back(std::move(block->statements[i]));
        }
        block->statements.resize(ins.stmt_index);
        cont_block->terminator = std::move(block->terminator);

        block->statements.push_back(MirStatement::assign(
            MirPlace{old_val}, MirRvalue::use(MirOperand::copy(MirPlace{ins.local}))));

        std::vector<MirOperandPtr> free_args;
        free_args.push_back(MirOperand::copy(MirPlace{old_val}, hir::make_string()));
        auto free_term = std::make_unique<MirTerminator>();
        free_term->kind = MirTerminator::Call;
        free_term->data = MirTerminator::CallData{MirOperand::function_ref("cm_string_free"),
                                                  std::move(free_args),
                                                  std::nullopt,
                                                  cont,
                                                  std::nullopt,
                                                  "",
                                                  "",
                                                  false};
        block->terminator = std::move(free_term);

        // CFGキャッシュ（predecessors/successors）を分割に合わせて更新する
        cont_block->successors = std::move(block->successors);
        block->successors = {cont};
        cont_block->predecessors = {ins.block};
        for (BlockId s2 : cont_block->successors) {
            if (s2 < func.basic_blocks.size() && func.basic_blocks[s2]) {
                for (auto& p2 : func.basic_blocks[s2]->predecessors) {
                    if (p2 == ins.block) {
                        p2 = cont;
                    }
                }
            }
        }

        func.basic_blocks.push_back(std::move(cont_block));
    }

    return true;
}

}  // namespace cm::mir::opt
