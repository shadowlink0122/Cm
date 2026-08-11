// ============================================================
// CFG走査ベースのSV出力 - if/else・ループの構造化
// ============================================================
#include "internal/base/i18n.hpp"
#include "internal/codegen/sv/codegen.hpp"
#include "internal/codegen/sv/internal.hpp"

#include <algorithm>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cm::codegen::sv {

// ============================================================
// カウントループのfor形再構成の事前解析
// ============================================================

// for形再構成: ヘッダの全文が単一定義テンポラリへの代入（=出力行なし）であることの判定
bool SVCodeGen::forHeaderClean(const mir::MirFunction& func, size_t header) const {
    for (const auto& stmt : func.basic_blocks[header]->statements) {
        if (!stmt || stmt->kind != mir::MirStatement::Assign) {
            if (stmt && stmt->kind != mir::MirStatement::Nop &&
                stmt->kind != mir::MirStatement::StorageLive &&
                stmt->kind != mir::MirStatement::StorageDead) {
                return false;
            }
            continue;
        }
        const auto& ad = std::get<mir::MirStatement::AssignData>(stmt->data);
        if (!ad.place.projections.empty() || single_def_temps_.count(ad.place.local) == 0) {
            return false;
        }
    }
    return true;
}

// for形再構成: ラッチ末尾側から増分文 var = var ± 定数 を検出する（見つかればloop_var/step_idx/step_rhsへ書いてtrue）
bool SVCodeGen::findForStep(const mir::MirFunction& func, size_t latch, mir::LocalId& loop_var,
                            size_t& step_idx, std::string& step_rhs) {
    const auto& latch_stmts = func.basic_blocks[latch]->statements;
    step_idx = SIZE_MAX;
    loop_var = 0;
    step_rhs.clear();
    // rvalueが「var ± 定数」のBinaryOpか判定する（増分パターン）。
    // MIRでは値が単一定義テンポラリの多段連鎖（t1=copy(i); t2=t1+1; i=copy(t2)等）になるため、
    // Use(Copy(テンポラリ))の連鎖を定義rvalueまで辿って照合する
    auto find_temp_def = [&](mir::LocalId local) -> const mir::MirRvalue* {
        for (const auto& def_stmt : latch_stmts) {
            if (!def_stmt || def_stmt->kind != mir::MirStatement::Assign) {
                continue;
            }
            const auto& dad = std::get<mir::MirStatement::AssignData>(def_stmt->data);
            if (dad.place.projections.empty() && dad.place.local == local && dad.rvalue) {
                return dad.rvalue.get();
            }
        }
        return nullptr;
    };
    // Use(Copy(temp))連鎖を実体のrvalueまで解決する（最大8段）
    auto resolve_rvalue = [&](const mir::MirRvalue* rv) -> const mir::MirRvalue* {
        for (int hop = 0; rv && hop < 8; ++hop) {
            if (rv->kind != mir::MirRvalue::Use) {
                return rv;
            }
            const auto& ud = std::get<mir::MirRvalue::UseData>(rv->data);
            if (!ud.operand || ud.operand->kind != mir::MirOperand::Copy) {
                return rv;
            }
            const auto& tp = std::get<mir::MirPlace>(ud.operand->data);
            if (!tp.projections.empty() || single_def_temps_.count(tp.local) == 0) {
                return rv;
            }
            const auto* def = find_temp_def(tp.local);
            if (!def) {
                return rv;
            }
            rv = def;
        }
        return rv;
    };
    // オペランドのローカルをテンポラリ連鎖の根の非テンポラリlocalまで解決する
    auto resolve_operand_local = [&](const mir::MirOperand& op, mir::LocalId& out_local) -> bool {
        if (op.kind != mir::MirOperand::Copy) {
            return false;
        }
        const auto* place = &std::get<mir::MirPlace>(op.data);
        for (int hop = 0; hop < 8; ++hop) {
            if (!place->projections.empty()) {
                return false;
            }
            if (single_def_temps_.count(place->local) == 0) {
                out_local = place->local;
                return true;
            }
            const auto* def = find_temp_def(place->local);
            if (!def || def->kind != mir::MirRvalue::Use) {
                return false;
            }
            const auto& ud = std::get<mir::MirRvalue::UseData>(def->data);
            if (!ud.operand || ud.operand->kind != mir::MirOperand::Copy) {
                return false;
            }
            place = &std::get<mir::MirPlace>(ud.operand->data);
        }
        return false;
    };
    auto match_step = [&](const mir::MirRvalue& rv, mir::LocalId var,
                          std::string& out_rhs) -> bool {
        const mir::MirRvalue* target = resolve_rvalue(&rv);
        if (!target || target->kind != mir::MirRvalue::BinaryOp) {
            return false;
        }
        const auto& bd = std::get<mir::MirRvalue::BinaryOpData>(target->data);
        if (bd.op != mir::MirBinaryOp::Add && bd.op != mir::MirBinaryOp::Sub) {
            return false;
        }
        if (!bd.lhs || !bd.rhs) {
            return false;
        }
        mir::LocalId lhs_root = 0;
        if (!resolve_operand_local(*bd.lhs, lhs_root) || lhs_root != var) {
            return false;
        }
        // 増分は定数のみfor形にする（定数がテンポラリ経由=Copy(t); t=Use(定数) の場合も解決する）
        const mir::MirOperand* rhs_const = nullptr;
        if (bd.rhs->kind == mir::MirOperand::Constant) {
            rhs_const = bd.rhs.get();
        } else if (bd.rhs->kind == mir::MirOperand::Copy) {
            const auto& rp = std::get<mir::MirPlace>(bd.rhs->data);
            if (rp.projections.empty() && single_def_temps_.count(rp.local) > 0) {
                if (const auto* def = find_temp_def(rp.local)) {
                    const auto* resolved = resolve_rvalue(def);
                    if (resolved && resolved->kind == mir::MirRvalue::Use) {
                        const auto& rud = std::get<mir::MirRvalue::UseData>(resolved->data);
                        if (rud.operand && rud.operand->kind == mir::MirOperand::Constant) {
                            rhs_const = rud.operand.get();
                        }
                    }
                }
            }
        }
        if (!rhs_const) {
            return false;
        }
        std::string rhs_str = emitOperand(*rhs_const, func);
        mir::MirPlace vp{var};
        const std::string op_str = (bd.op == mir::MirBinaryOp::Add) ? " + " : " - ";
        out_rhs = emitPlace(vp, func) + op_str + rhs_str;
        return true;
    };
    for (size_t i = latch_stmts.size(); i-- > 0;) {
        const auto& stmt = latch_stmts[i];
        if (!stmt || stmt->kind != mir::MirStatement::Assign) {
            continue;
        }
        const auto& ad = std::get<mir::MirStatement::AssignData>(stmt->data);
        if (!ad.place.projections.empty() || !ad.rvalue) {
            continue;
        }
        // 出力行を持つ代入（テンポラリ代入は行を出さない）を末尾から走査し、最後のものを増分候補とする
        if (single_def_temps_.count(ad.place.local) > 0) {
            continue;
        }
        std::string rhs;
        if (match_step(*ad.rvalue, ad.place.local, rhs)) {
            loop_var = ad.place.local;
            step_idx = i;
            step_rhs = rhs;
        }
        break;
    }
    return step_idx != SIZE_MAX;
}

// for形再構成の初期値検出: ヘッダへ入るループ外の先行ブロックで、末尾側の var = 定数 を探す（見つかればinit_block/init_idx/init_exprへ書いてtrue）
bool SVCodeGen::findForInit(const mir::MirFunction& func, size_t header, size_t latch,
                            const std::vector<size_t>& latches, mir::LocalId loop_var,
                            size_t& init_block, size_t& init_idx, std::string& init_expr) {
    init_block = SIZE_MAX;
    init_idx = SIZE_MAX;
    init_expr.clear();
    for (size_t b = 0; b < func.basic_blocks.size(); ++b) {
        if (b == header || !func.basic_blocks[b] || !func.basic_blocks[b]->terminator) {
            continue;
        }
        // ヘッダへの辺を持つか（Goto/SwitchIntの行き先）
        bool goes_to_header = false;
        const auto& term = *func.basic_blocks[b]->terminator;
        if (term.kind == mir::MirTerminator::Goto) {
            goes_to_header = std::get<mir::MirTerminator::GotoData>(term.data).target == header;
        } else if (term.kind == mir::MirTerminator::SwitchInt) {
            const auto& sd = std::get<mir::MirTerminator::SwitchIntData>(term.data);
            for (const auto& t : sd.targets) {
                goes_to_header = goes_to_header || t.second == header;
            }
            goes_to_header = goes_to_header || sd.otherwise == header;
        }
        if (!goes_to_header) {
            continue;
        }
        // ラッチ（ループ内後方辺）は初期値の供給元ではない
        if (b == latch || in_natural_loop(func, b, header, latches)) {
            continue;
        }
        const auto& stmts = func.basic_blocks[b]->statements;
        // ブロック内のUse(Copy(テンポラリ))連鎖を定数まで解決する（初期値もi = copy(t); t = 定数 の形になる）
        auto resolve_const_in_block = [&](const mir::MirOperand& op) -> const mir::MirOperand* {
            if (op.kind == mir::MirOperand::Constant) {
                return &op;
            }
            const mir::MirOperand* cur = &op;
            for (int hop = 0; hop < 8; ++hop) {
                if (cur->kind != mir::MirOperand::Copy) {
                    return nullptr;
                }
                const auto& cp = std::get<mir::MirPlace>(cur->data);
                if (!cp.projections.empty() || single_def_temps_.count(cp.local) == 0) {
                    return nullptr;
                }
                const mir::MirRvalue* def = nullptr;
                for (const auto& ds : stmts) {
                    if (!ds || ds->kind != mir::MirStatement::Assign) {
                        continue;
                    }
                    const auto& dad = std::get<mir::MirStatement::AssignData>(ds->data);
                    if (dad.place.projections.empty() && dad.place.local == cp.local &&
                        dad.rvalue) {
                        def = dad.rvalue.get();
                        break;
                    }
                }
                if (!def || def->kind != mir::MirRvalue::Use) {
                    return nullptr;
                }
                const auto& dud = std::get<mir::MirRvalue::UseData>(def->data);
                if (!dud.operand) {
                    return nullptr;
                }
                if (dud.operand->kind == mir::MirOperand::Constant) {
                    return dud.operand.get();
                }
                cur = dud.operand.get();
            }
            return nullptr;
        };
        for (size_t i = stmts.size(); i-- > 0;) {
            const auto& stmt = stmts[i];
            if (!stmt || stmt->kind != mir::MirStatement::Assign) {
                continue;
            }
            const auto& ad = std::get<mir::MirStatement::AssignData>(stmt->data);
            if (!ad.place.projections.empty() || ad.place.local != loop_var || !ad.rvalue ||
                ad.rvalue->kind != mir::MirRvalue::Use) {
                continue;
            }
            const auto& ud = std::get<mir::MirRvalue::UseData>(ad.rvalue->data);
            const auto* cval = ud.operand ? resolve_const_in_block(*ud.operand) : nullptr;
            if (!cval) {
                break;  // 最後の代入が定数でなければfor形にしない
            }
            init_block = b;
            init_idx = i;
            init_expr = emitOperand(*cval, func);
            break;
        }
        if (init_block != SIZE_MAX) {
            break;
        }
    }
    return init_block != SIZE_MAX;
}

// === カウントループのfor形再構成の事前解析 ===
// 各自然ループについて「ラッチ末尾の var = var ± 定数（増分）」と「ループ外先行ブロック末尾の
// var = 定数（初期値）」の単純パターンを検出し、for文として出力できるループを登録する。
// ヘッダの残余文（条件計算）が全て単一定義テンポラリ（インライン展開されSV行を出さない）で
// あることを条件とする（for文は条件を自動再評価するため、残余文があると意味が変わる）
void SVCodeGen::computeForLoops(const mir::MirFunction& func) {
    for_loops_.clear();
    suppressed_stmts_.clear();
    for (const auto& [header, latches] : current_loop_latches_) {
        if (latches.empty() || header >= func.basic_blocks.size() || !func.basic_blocks[header]) {
            continue;
        }
        // ヘッダの全文が単一定義テンポラリへの代入（=出力行なし）であること
        if (!forHeaderClean(func, header)) {
            continue;
        }
        // ラッチ末尾側から増分文 var = var ± 定数 を探す（単一ラッチのみ対象）
        if (latches.size() != 1) {
            continue;
        }
        const size_t latch = latches[0];
        if (latch >= func.basic_blocks.size() || !func.basic_blocks[latch]) {
            continue;
        }
        size_t step_idx = SIZE_MAX;
        mir::LocalId loop_var = 0;
        std::string step_rhs;
        if (!findForStep(func, latch, loop_var, step_idx, step_rhs)) {
            continue;
        }
        // 初期値: ヘッダへ入るループ外の先行ブロックで、末尾側の var = 定数
        size_t init_block = SIZE_MAX;
        size_t init_idx = SIZE_MAX;
        std::string init_expr;
        if (!findForInit(func, header, latch, latches, loop_var, init_block, init_idx, init_expr)) {
            continue;
        }
        mir::MirPlace var_place{loop_var};
        ForLoopInfo info;
        info.var = emitPlace(var_place, func);
        info.init_expr = init_expr;
        info.step_expr = step_rhs;
        for_loops_[header] = info;
        suppressed_stmts_.insert({init_block, init_idx});
        suppressed_stmts_.insert({latch, step_idx});
    }
}

// === CFG再帰走査ベースのブロック出力 ===
void SVCodeGen::emitBlockRecursive(const mir::MirFunction& func, size_t block_id,
                                   std::set<size_t>& visited, std::ostringstream& ss,
                                   size_t merge_block) {
    // ループ本体の出力中にexitブロックへ到達した場合はループ脱出を出力（exitブロック自体はループ終了後に出力される）。
    // break は SV-2005 キーワードで古いIcarus Verilog等が未対応のため、ループを囲む名前付きブロックへの disable で脱出する（Verilog-1995互換）
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

    // ブロック内の文を出力（for形再構成へ吸収した初期値・増分文は抑止する）
    for (size_t si = 0; si < bb.statements.size(); ++si) {
        const auto& stmt = bb.statements[si];
        if (!stmt)
            continue;
        if (suppressed_stmts_.count({block_id, si}) > 0)
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

// ============================================================
// SwitchIntターミネータの腕別ヘルパー
// ============================================================

namespace {

// === 三項演算子の構造的判定（式ツリー化 Phase 2b） ===
// 両分岐が「同一placeへの単一代入行」なら cond ? a : b として出力。
// 内側の分岐は再帰で先に三項化されるため、else-ifチェーンも自然に入れ子の三項として畳まれる（従来はテキストの5行パターン検出パスで行っていた）
bool parse_single_assign(const std::string& text, std::string& lhs, std::string& rhs,
                         std::string& op) {
    // 「1行のみ + 行末が ;」であること
    size_t nl = text.find('\n');
    if (nl == std::string::npos || nl + 1 != text.size()) {
        return false;
    }
    std::string line = text.substr(0, nl);
    size_t first = line.find_first_not_of(' ');
    if (first == std::string::npos) {
        return false;
    }
    line = line.substr(first);
    if (line.empty() || line.back() != ';') {
        return false;
    }
    // 代入演算子（先に現れた方。右辺の比較 <= と混同しない）
    size_t nb = line.find(" <= ");
    size_t bl = line.find(" = ");
    if (nb != std::string::npos && (bl == std::string::npos || nb < bl)) {
        op = " <= ";
        lhs = line.substr(0, nb);
        rhs = line.substr(nb + 4);
    } else if (bl != std::string::npos) {
        op = " = ";
        lhs = line.substr(0, bl);
        rhs = line.substr(bl + 3);
    } else {
        return false;
    }
    rhs.pop_back();  // 末尾の ;
    return !lhs.empty() && !rhs.empty();
}

// case修飾の決定: 属性指定（#[sv::priority]/#[sv::unique0]）が最優先。
// 指定が無いcasezはパターンの重なりで自動選択する（互いに素→unique・重なりあり→priority＝matchの先勝ち意味論を表明）。
// 通常のcaseは従来どおりunique（matchは網羅性検査済み・switchはdefault生成があるため重複ヒット検出が得られる）
std::string choose_case_modifier(const mir::MirTerminator::SwitchIntData& sd, bool has_masks) {
    std::string case_modifier = "unique";
    if (sd.sv_case_modifier == 1) {
        case_modifier = "priority";
    } else if (sd.sv_case_modifier == 2) {
        case_modifier = "unique0";
    } else if (has_masks) {
        bool overlapping = false;
        for (size_t i = 0; i < sd.targets.size() && !overlapping; ++i) {
            for (size_t j = i + 1; j < sd.targets.size(); ++j) {
                const int64_t mi = i < sd.target_masks.size() ? sd.target_masks[i] : -1;
                const int64_t mj = j < sd.target_masks.size() ? sd.target_masks[j] : -1;
                const int64_t common = mi & mj;
                if ((sd.targets[i].first & common) == (sd.targets[j].first & common)) {
                    overlapping = true;
                    break;
                }
            }
        }
        case_modifier = overlapping ? "priority" : "unique";
    }
    return case_modifier;
}

}  // namespace

// SwitchInt単一ターゲット腕のループ再構成: ループヘッダならwhile/for形で出力してtrueを返す
bool SVCodeGen::tryEmitLoop(const std::string& cond, size_t then_block, size_t else_block,
                            bool is_negated, const mir::MirFunction& func,
                            std::set<size_t>& visited, std::ostringstream& ss, size_t merge_block,
                            size_t current_block) {
    // === ループヘッダ検出とwhileループ再構成 ===
    // このブロックへの後方エッジがあり、かつ片方の分岐だけが
    // 自然ループに属する場合、ループヘッダとみなす。
    // if/elseとして出力するとバックエッジが消えて「ループ本体が最大1回・ループ後コードが到達不能」という
    // 誤ったSVになるため、whileループとして構造を復元する
    auto latch_it = current_loop_latches_.find(current_block);
    if (current_block == SIZE_MAX || latch_it == current_loop_latches_.end()) {
        return false;
    }
    const std::vector<size_t>& latches = latch_it->second;
    if (latches.empty()) {
        return false;
    }
    // 真条件(cond != 0)で実行される分岐
    size_t true_block = is_negated ? else_block : then_block;
    size_t false_block = is_negated ? then_block : else_block;
    bool true_in_loop = in_natural_loop(func, true_block, current_block, latches);
    bool false_in_loop = in_natural_loop(func, false_block, current_block, latches);
    if (true_in_loop == false_in_loop) {
        return false;
    }
    size_t body = true_in_loop ? true_block : false_block;
    size_t exit = true_in_loop ? false_block : true_block;
    std::string loop_cond = true_in_loop ? cond : "!(" + cond + ")";

    // ループ脱出（disable）用の名前付きブロックで囲む。
    // 単純カウントループはfor形で出力する（合成ツールはalways内whileを
    // 拒否するため。パラメータ境界#[sv::parameter]のループも合成可能になる）
    auto for_it = for_loops_.find(current_block);
    const bool as_for = for_it != for_loops_.end();
    std::string loop_name = "__loop" + std::to_string(loop_name_counter_++);
    ss << indent() << "begin : " << loop_name << "\n";
    increaseIndent();
    if (as_for) {
        ss << indent() << "for (" << for_it->second.var << " = " << for_it->second.init_expr << "; "
           << loop_cond << "; " << for_it->second.var << " = " << for_it->second.step_expr
           << ") begin\n";
    } else {
        ss << indent() << "while (" << loop_cond << ") begin\n";
    }
    increaseIndent();
    // ループ本体を出力。ループ脱出（exitへの分岐）を検出できるよう
    // exitブロックをスタックに積む。ヘッダへの後方エッジはvisited済みのため自然に停止する
    loop_exit_stack_.push_back(exit);
    loop_name_stack_.push_back(loop_name);
    emitBlockRecursive(func, body, visited, ss, exit);
    loop_name_stack_.pop_back();
    loop_exit_stack_.pop_back();
    // ヘッダブロックの文（ループ条件の再計算）を本体末尾で再実行する。条件のテンポラリが2箇所で代入されることになり、インライン展開の対象からも自動的に外れる
    // （for形はfor文が条件を再評価するため再実行しない。for形の条件は全て単一定義テンポラリのインライン展開で構成される）
    if (!as_for && current_block < func.basic_blocks.size() && func.basic_blocks[current_block]) {
        for (const auto& stmt : func.basic_blocks[current_block]->statements) {
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
    return true;
}

// SwitchInt単一ターゲット腕: 条件分岐をif/else（両分岐が単一代入なら三項演算子）として出力する
void SVCodeGen::emitTermBranch(const mir::MirTerminator::SwitchIntData& sd, const std::string& cond,
                               const mir::MirFunction& func, std::set<size_t>& visited,
                               std::ostringstream& ss, size_t merge_block, size_t current_block) {
    // if (cond == val) ... else ...
    // MIRのSwitchIntは: targets=[(val, then_block)], otherwise=else_block
    size_t then_block = sd.targets[0].second;
    size_t else_block = sd.otherwise;

    // bool分岐の場合、val==0なら否定条件
    bool is_negated = (sd.targets[0].first == 0);

    // ループヘッダならwhile/for形で再構成して終了
    if (tryEmitLoop(cond, then_block, else_block, is_negated, func, visited, ss, merge_block,
                    current_block)) {
        return;
    }

    // 合流ブロックを探す
    size_t merge = findMergeBlock(func, then_block, else_block);

    // 条件が真のとき実行される分岐（is_negated時は反転）
    size_t true_blk = is_negated ? else_block : then_block;
    size_t false_blk = is_negated ? then_block : else_block;

    // 両分岐をバッファに出力してから if/else か三項演算子かを決める
    std::ostringstream true_ss;
    increaseIndent();
    emitBlockRecursive(func, true_blk, visited, true_ss, merge);
    decreaseIndent();

    std::ostringstream false_ss;
    std::set<size_t> false_visited = visited;
    increaseIndent();
    emitBlockRecursive(func, false_blk, false_visited, false_ss, merge);
    decreaseIndent();

    // 両分岐が単一代入なら三項演算子へ畳む（parse_single_assignの構造的判定）
    std::string t_lhs, t_rhs, t_op, f_lhs, f_rhs, f_op;
    if (parse_single_assign(true_ss.str(), t_lhs, t_rhs, t_op) &&
        parse_single_assign(false_ss.str(), f_lhs, f_rhs, f_op) && t_lhs == f_lhs && t_op == f_op) {
        visited.insert(false_visited.begin(), false_visited.end());
        if (t_rhs == f_rhs) {
            // 両辺同一なら分岐自体が不要（旧・冗長三項除去パス相当）
            ss << indent() << t_lhs << t_op << t_rhs << ";\n";
        } else {
            ss << indent() << t_lhs << t_op << "(" << cond << ") ? " << t_rhs << " : " << f_rhs
               << ";\n";
        }
    } else {
        ss << indent() << "if (" << cond << ") begin\n";
        ss << true_ss.str();
        if (!false_ss.str().empty()) {
            ss << indent() << "end else begin\n";
            ss << false_ss.str();
            visited.insert(false_visited.begin(), false_visited.end());
        }
        ss << indent() << "end\n";
    }

    // 合流ブロックを処理
    // （合流先がループexitの場合はここでは出力しない。
    //   break; は各分岐内で出力済みで、exit本体はループ終了後に出力される）
    if (merge != SIZE_MAX && (loop_exit_stack_.empty() || merge != loop_exit_stack_.back())) {
        emitBlockRecursive(func, merge, visited, ss, merge_block);
    }
}

// case腕: casezのワイルドカードビット付きcase項の出力
void SVCodeGen::emitCasezArms(const mir::MirTerminator::SwitchIntData& sd,
                              const mir::MirFunction& func, std::set<size_t>& visited,
                              std::ostringstream& ss, size_t merge) {
    // casez: スクルーチニの型幅でワイルドカードビット付き2進リテラルを出力する。
    // 先勝ち意味論を保存するためcase項の順序はMIRエントリ順を維持し、連続する同一遷移先のみカンマでまとめる
    int width = getBitWidth(resolve_operand_type(*sd.discriminant, func));
    if (width <= 0 || width > 64) {
        width = 32;
    }
    auto render_masked = [&](int64_t val, int64_t mask) {
        std::string lit = std::to_string(width) + "'b";
        for (int b = width - 1; b >= 0; --b) {
            if ((mask >> b) & 1) {
                lit += ((val >> b) & 1) ? '1' : '0';
            } else {
                lit += '?';
            }
        }
        return lit;
    };
    for (size_t i = 0; i < sd.targets.size();) {
        const size_t target = sd.targets[i].second;
        ss << indent();
        size_t j = i;
        for (; j < sd.targets.size() && sd.targets[j].second == target; ++j) {
            if (j > i) {
                ss << ", ";
            }
            const int64_t mask = j < sd.target_masks.size() ? sd.target_masks[j] : -1;
            ss << render_masked(sd.targets[j].first, mask);
        }
        ss << ": begin\n";
        increaseIndent();
        std::set<size_t> case_visited = visited;
        emitBlockRecursive(func, target, case_visited, ss, merge);
        visited.insert(case_visited.begin(), case_visited.end());
        decreaseIndent();
        ss << indent() << "end\n";
        i = j;
    }
}

// case腕: 同一遷移先ごとに値をまとめた通常case項の出力
void SVCodeGen::emitCaseArms(const mir::MirTerminator::SwitchIntData& sd,
                             const mir::MirFunction& func, std::set<size_t>& visited,
                             std::ostringstream& ss, size_t merge) {
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
}

// SwitchInt複数ターゲット腕: case/casez文として出力する
void SVCodeGen::emitTermCase(const mir::MirTerminator::SwitchIntData& sd, const std::string& cond,
                             const mir::MirFunction& func, std::set<size_t>& visited,
                             std::ostringstream& ss, size_t merge_block) {
    // 全分岐先が合流するブロックを探す（最初の2つから合流点を特定）
    size_t merge = SIZE_MAX;
    if (sd.targets.size() >= 2) {
        merge = findMergeBlock(func, sd.targets[0].second, sd.targets[1].second);
    }

    // don't-careビットマスク付きcase（matchの0b1?00等。SV-N3）は native casez を出力する
    bool has_masks = false;
    for (int64_t m : sd.target_masks) {
        if (m != -1) {
            has_masks = true;
            break;
        }
    }

    // case修飾を決定する（choose_case_modifierの規則）
    std::string case_modifier = choose_case_modifier(sd, has_masks);

    ss << indent() << case_modifier << (has_masks ? " casez (" : " case (") << cond << ")\n";
    increaseIndent();

    if (has_masks) {
        emitCasezArms(sd, func, visited, ss, merge);
    } else {
        emitCaseArms(sd, func, visited, ss, merge);
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

// ============================================================
// Callターミネータの腕別ヘルパー
// ============================================================

// Call腕の逆引きマップ構築: 全ブロックを走査してテンポラリの定義元を収集する
SVCodeGen::CallArgMaps SVCodeGen::buildCallArgMaps(const mir::MirFunction& func) {
    // Ref逆引きマップ構築: テンポラリ(_tXXX) → 元のPlace
    // Use(Constant)逆引きマップ: テンポラリ → 定数値
    // copy逆引きマップ: テンポラリ → コピー元
    CallArgMaps maps;
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
                if (auto* ref_data = std::get_if<mir::MirRvalue::RefData>(&ad.rvalue->data)) {
                    maps.ref_map.insert_or_assign(ad.place.local, ref_data->place);
                }
            } else if (ad.rvalue->kind == mir::MirRvalue::Use) {
                if (auto* use_data = std::get_if<mir::MirRvalue::UseData>(&ad.rvalue->data)) {
                    if (use_data->operand) {
                        if (use_data->operand->kind == mir::MirOperand::Constant) {
                            maps.const_map.insert_or_assign(
                                ad.place.local,
                                std::make_pair(std::get<mir::MirConstant>(use_data->operand->data),
                                               use_data->operand->type));
                        } else if (use_data->operand->kind == mir::MirOperand::Copy ||
                                   use_data->operand->kind == mir::MirOperand::Move) {
                            maps.copy_map.insert_or_assign(
                                ad.place.local, std::get<mir::MirPlace>(use_data->operand->data));
                        }
                    }
                }
            }
        }
    }
    return maps;
}

// Call args を解決: テンポラリ → 元のPlace名 or 定数値
std::string SVCodeGen::resolveCallArg(const mir::MirOperand& op, const CallArgMaps& maps,
                                      const mir::MirFunction& func) {
    if (op.kind == mir::MirOperand::Move || op.kind == mir::MirOperand::Copy) {
        const auto& place = std::get<mir::MirPlace>(op.data);
        // Ref逆引き: _t → &original → original
        auto ref_it = maps.ref_map.find(place.local);
        if (ref_it != maps.ref_map.end()) {
            return emitPlace(ref_it->second, func);
        }
        // Const逆引き: _t → constant
        auto const_it = maps.const_map.find(place.local);
        if (const_it != maps.const_map.end()) {
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
}

// Call腕のノンブロッキング代入判定: 宛先のグローバル性とposedge/negedge型パラメータの規則で <= / = を選ぶ
bool SVCodeGen::useNonblockingForCall(const mir::MirFunction& func,
                                      const mir::MirTerminator::CallData& cd) {
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
    return use_nb;
}

// Call腕: assert組み込みの出力
void SVCodeGen::emitCallAssert(const mir::MirTerminator::CallData& cd, const CallArgMaps& maps,
                               const mir::MirFunction& func, std::ostringstream& ss) {
    // 即時アサーション: assert (条件) else $error(...);
    // シミュレーションで検証され、合成ツールでは無視される
    std::string cond =
        (!cd.args.empty() && cd.args[0]) ? resolveCallArg(*cd.args[0], maps, func) : "1'b1";
    std::string message = "assertion failed";
    if (cd.args.size() >= 2 && cd.args[1]) {
        const mir::MirConstant* msg_const = nullptr;
        if (cd.args[1]->kind == mir::MirOperand::Constant) {
            msg_const = &std::get<mir::MirConstant>(cd.args[1]->data);
        } else if (cd.args[1]->kind == mir::MirOperand::Move ||
                   cd.args[1]->kind == mir::MirOperand::Copy) {
            const auto& place = std::get<mir::MirPlace>(cd.args[1]->data);
            auto const_it = maps.const_map.find(place.local);
            if (const_it != maps.const_map.end()) {
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
}

// Call腕: SV連接 __builtin_concat の出力
void SVCodeGen::emitCallConcat(const mir::MirTerminator::CallData& cd, const CallArgMaps& maps,
                               const mir::MirFunction& func, bool use_nb, std::ostringstream& ss) {
    // SV連接: {a, b, ...}
    std::string rhs = "{";
    for (size_t i = 0; i < cd.args.size(); ++i) {
        if (i > 0)
            rhs += ", ";
        rhs += cd.args[i] ? resolveCallArg(*cd.args[i], maps, func) : "0";
    }
    rhs += "}";
    if (cd.destination) {
        std::string lhs = emitPlace(*cd.destination, func);
        ss << indent() << lhs << (use_nb ? " <= " : " = ") << rhs << ";\n";
    }
}

// Call腕: SV複製 __builtin_replicate の出力
void SVCodeGen::emitCallReplicate(const mir::MirTerminator::CallData& cd, const CallArgMaps& maps,
                                  const mir::MirFunction& func, bool use_nb,
                                  std::ostringstream& ss) {
    // SV複製: {N{expr}} count を直接整数値として取得
    std::string count_str = "1";
    if (cd.args.size() > 0 && cd.args[0]) {
        if (cd.args[0]->kind == mir::MirOperand::Constant) {
            const auto& c = std::get<mir::MirConstant>(cd.args[0]->data);
            if (auto* ival = std::get_if<int64_t>(&c.value)) {
                count_str = std::to_string(*ival);
            } else {
                count_str = resolveCallArg(*cd.args[0], maps, func);
            }
        } else if (cd.args[0]->kind == mir::MirOperand::Move ||
                   cd.args[0]->kind == mir::MirOperand::Copy) {
            const auto& place = std::get<mir::MirPlace>(cd.args[0]->data);
            auto const_it = maps.const_map.find(place.local);
            if (const_it != maps.const_map.end()) {
                if (auto* ival = std::get_if<int64_t>(&const_it->second.first.value)) {
                    count_str = std::to_string(*ival);
                } else {
                    count_str = resolveCallArg(*cd.args[0], maps, func);
                }
            } else {
                count_str = resolveCallArg(*cd.args[0], maps, func);
            }
        } else {
            count_str = resolveCallArg(*cd.args[0], maps, func);
        }
    }
    std::string expr =
        cd.args.size() > 1 && cd.args[1] ? resolveCallArg(*cd.args[1], maps, func) : "0";
    std::string rhs = "{" + count_str + "{" + expr + "}}";
    if (cd.destination) {
        std::string lhs = emitPlace(*cd.destination, func);
        ss << indent() << lhs << (use_nb ? " <= " : " = ") << rhs << ";\n";
    }
}

// Call腕: native part-select __builtin_sv_* の出力
void SVCodeGen::emitCallPartSelect(const std::string& func_name,
                                   const mir::MirTerminator::CallData& cd, const CallArgMaps& maps,
                                   const mir::MirFunction& func, bool use_nb,
                                   std::ostringstream& ss) {
    // native part-select（SV-N1）: ビット範囲の読み書きをshift+maskでなく
    // SVのpart-select構文（x[hi:lo]・x[base +: w]・x[base -: w]）へ写像する

    // 定数int引数の取得（直接定数またはconst_map経由のテンポラリ）
    auto const_int_arg = [&](size_t idx, int64_t fallback) -> int64_t {
        if (idx >= cd.args.size() || !cd.args[idx]) {
            return fallback;
        }
        const auto& op = *cd.args[idx];
        if (op.kind == mir::MirOperand::Constant) {
            if (auto* iv = std::get_if<int64_t>(&std::get<mir::MirConstant>(op.data).value)) {
                return *iv;
            }
        } else if (op.kind == mir::MirOperand::Move || op.kind == mir::MirOperand::Copy) {
            const auto& place = std::get<mir::MirPlace>(op.data);
            auto it = maps.const_map.find(place.local);
            if (it != maps.const_map.end()) {
                if (auto* iv = std::get_if<int64_t>(&it->second.first.value)) {
                    return *iv;
                }
            }
        }
        return fallback;
    };
    // 対象信号のルートlocal（copy/ref逆引きで辿る。ノンブロッキング判定用）
    auto trace_root_local = [&](size_t idx) -> std::optional<mir::LocalId> {
        if (idx >= cd.args.size() || !cd.args[idx]) {
            return std::nullopt;
        }
        const auto& op = *cd.args[idx];
        if (op.kind != mir::MirOperand::Move && op.kind != mir::MirOperand::Copy) {
            return std::nullopt;
        }
        mir::MirPlace p = std::get<mir::MirPlace>(op.data);
        while (true) {
            auto c = maps.copy_map.find(p.local);
            if (c != maps.copy_map.end()) {
                p = c->second;
                continue;
            }
            auto r = maps.ref_map.find(p.local);
            if (r != maps.ref_map.end()) {
                p = r->second;
                continue;
            }
            break;
        }
        return p.local;
    };
    // part-select本体（x[hi:lo] / x[base +: w] / x[base -: w]）の構築
    const std::string target =
        (!cd.args.empty() && cd.args[0]) ? resolveCallArg(*cd.args[0], maps, func) : "0";
    std::string select;
    if (func_name == "__builtin_sv_range_select" || func_name == "__builtin_sv_range_assign") {
        const int64_t hi = const_int_arg(1, 0);
        const int64_t lo = const_int_arg(2, 0);
        select = target + "[" + std::to_string(hi) + ":" + std::to_string(lo) + "]";
    } else {
        // 基点が定数ならサイズ無しの10進で出力する（32'sd7でなく7）
        const int64_t cbase = const_int_arg(1, INT64_MIN);
        const std::string base = cbase != INT64_MIN ? std::to_string(cbase)
                                                    : ((cd.args.size() > 1 && cd.args[1])
                                                           ? resolveCallArg(*cd.args[1], maps, func)
                                                           : "0");
        const int64_t w = const_int_arg(2, 1);
        const bool down = func_name.rfind("_down") != std::string::npos;
        select = target + "[" + base + (down ? " -: " : " +: ") + std::to_string(w) + "]";
    }

    if (func_name == "__builtin_sv_range_select" || func_name == "__builtin_sv_part_select" ||
        func_name == "__builtin_sv_part_select_down") {
        // 読み: dest = x[...];
        if (cd.destination) {
            std::string lhs = emitPlace(*cd.destination, func);
            ss << indent() << lhs << (use_nb ? " <= " : " = ") << select << ";\n";
        }
    } else {
        // 書き（部分代入）: x[...] = v; ノンブロッキング判定は対象信号のルートlocalで行う
        bool assign_nb = func.is_async || func.always_kind == mir::MirFunction::AlwaysKind::FF;
        auto root = trace_root_local(0);
        const bool root_is_global =
            root && *root < func.locals.size() && func.locals[*root].is_global;
        if (assign_nb && root && !root_is_global) {
            assign_nb = false;
        }
        // posedge/negedge型パラメータを持つ関数のグローバル信号書き込みはノンブロッキング（汎用代入経路と同一規則）
        if (!assign_nb && root_is_global) {
            for (const auto& local : func.locals) {
                if (local.is_global) {
                    continue;
                }
                if (local.type && (local.type->kind == hir::TypeKind::Posedge ||
                                   local.type->kind == hir::TypeKind::Negedge)) {
                    assign_nb = true;
                    break;
                }
            }
        }
        const size_t vidx = cd.args.size() - 1;
        const std::string value = (cd.args.size() >= 4 && cd.args[vidx])
                                      ? resolveCallArg(*cd.args[vidx], maps, func)
                                      : "0";
        ss << indent() << select << (assign_nb ? " <= " : " = ") << value << ";\n";
    }
}

// Call腕: SVリダクション演算子 __builtin_reduce_* の出力
void SVCodeGen::emitCallReduce(const std::string& func_name, const mir::MirTerminator::CallData& cd,
                               const CallArgMaps& maps, const mir::MirFunction& func, bool use_nb,
                               std::ostringstream& ss) {
    // SVリダクション演算子（SV-N2）: __builtin_reduce_* をベクタ全ビットを1ビットへ
    // 畳み込む native 前置単項演算子（&x / |x / ^x / ~&x / ~|x / ~^x）へ写像する
    std::string op = "&";
    if (func_name == "__builtin_reduce_or") {
        op = "|";
    } else if (func_name == "__builtin_reduce_xor") {
        op = "^";
    } else if (func_name == "__builtin_reduce_nand") {
        op = "~&";
    } else if (func_name == "__builtin_reduce_nor") {
        op = "~|";
    } else if (func_name == "__builtin_reduce_xnor") {
        op = "~^";
    }
    std::string operand =
        (!cd.args.empty() && cd.args[0]) ? resolveCallArg(*cd.args[0], maps, func) : "0";
    std::string rhs = op + "(" + operand + ")";
    if (cd.destination) {
        std::string lhs = emitPlace(*cd.destination, func);
        ss << indent() << lhs << (use_nb ? " <= " : " = ") << rhs << ";\n";
    }
}

// Call腕: 文字列添字 __builtin_string_charAt/byte_at の出力
void SVCodeGen::emitCallStringCharAt(const mir::MirTerminator::CallData& cd,
                                     const CallArgMaps& maps, const mir::MirFunction& func,
                                     std::ostringstream& ss) {
    // R2: SVの文字列はASCIIバイト配列でバイト==コードポイントのため、byte_atはcharAtと同一の添字アクセスとして生成する
    // ノンブロッキング代入の判定
    const bool use_nb = useNonblockingForCall(func, cd);

    auto traceToOrigin = [&](mir::MirPlace p) -> std::string {
        while (true) {
            auto copy_it = maps.copy_map.find(p.local);
            if (copy_it != maps.copy_map.end()) {
                p = copy_it->second;
                continue;
            }
            auto ref_it = maps.ref_map.find(p.local);
            if (ref_it != maps.ref_map.end()) {
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
        name = strip_namespace(name);
        return name;
    };

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
        std::string res_name = cleanName(resolveCallArg(*cd.args[0], maps, func));
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
        cd.args.size() > 0 && cd.args[0] ? resolveCallArg(*cd.args[0], maps, func) : "0";
    std::string idx_val =
        cd.args.size() > 1 && cd.args[1] ? resolveCallArg(*cd.args[1], maps, func) : "0";

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
}

// Call腕: 一般関数呼び出しの出力
void SVCodeGen::emitCallGeneric(const std::string& func_name,
                                const mir::MirTerminator::CallData& cd,
                                const mir::MirFunction& func, std::ostringstream& ss) {
    // 一般的な関数呼び出し: result = func_name(arg1, arg2, ...);
    // ノンブロッキング代入の判定
    const bool use_nb = useNonblockingForCall(func, cd);

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
        ss << indent() << lhs << (use_nb ? " <= " : " = ") << func_name << "(" << args_str
           << ");\n";
    } else {
        // void関数呼び出し（taskの場合等）
        ss << indent() << func_name << "(" << args_str << ");\n";
    }
}

// Callターミネータ腕: 関数名で各出力ヘルパーへ振り分け、成功ブロックへ続行する
void SVCodeGen::emitTermCall(const mir::MirTerminator::CallData& cd, const mir::MirFunction& func,
                             std::set<size_t>& visited, std::ostringstream& ss,
                             size_t merge_block) {
    std::string func_name;
    if (cd.func && cd.func->kind == mir::MirOperand::FunctionRef) {
        func_name = std::get<std::string>(cd.func->data);
    }

    const CallArgMaps maps = buildCallArgMaps(func);

    if (func_name == "assert") {
        emitCallAssert(cd, maps, func, ss);
        emitBlockRecursive(func, cd.success, visited, ss, merge_block);
    } else if (func_name == "__builtin_concat" || func_name == "__builtin_replicate" ||
               func_name.rfind("__builtin_reduce_", 0) == 0 ||
               func_name.rfind("__builtin_sv_", 0) == 0) {
        // ノンブロッキング代入の判定
        const bool use_nb = useNonblockingForCall(func, cd);
        if (func_name == "__builtin_concat") {
            emitCallConcat(cd, maps, func, use_nb, ss);
        } else if (func_name == "__builtin_replicate") {
            emitCallReplicate(cd, maps, func, use_nb, ss);
        } else if (func_name.rfind("__builtin_sv_", 0) == 0) {
            emitCallPartSelect(func_name, cd, maps, func, use_nb, ss);
        } else {
            emitCallReduce(func_name, cd, maps, func, use_nb, ss);
        }
        // 成功ブロックに続行
        emitBlockRecursive(func, cd.success, visited, ss, merge_block);
    } else if (func_name == "__builtin_string_charAt" || func_name == "__builtin_string_byte_at") {
        emitCallStringCharAt(cd, maps, func, ss);
        // 成功ブロックに続行
        emitBlockRecursive(func, cd.success, visited, ss, merge_block);
    } else if (func_name == "cm_string_free" || func_name == "cm_slice_free") {
        // C12 dropパスのメモリ解放呼び出しはSVでは意味を持たないため黙ってスキップする
        emitBlockRecursive(func, cd.success, visited, ss, merge_block);
    } else if (func_name == "println" || func_name == "print" ||
               func_name.rfind("cm_println", 0) == 0 || func_name.rfind("cm_print", 0) == 0 ||
               func_name.rfind("cm_format", 0) == 0) {
        // M18: 合成モジュール内のprintln等のI/O組み込みは合成不能。
        // 従来は未定義関数としてそのまま出力され診断が一切なかった（黙殺）。
        // 未定義関数の無言emitをやめ、警告してスキップする（シミュレーション出力は
        // #[test] 関数内でのみ$displayへ変換される。段階導入のためまず警告）
        std::cerr << i18n::msgf(i18n::MsgId::SvSv008UnsynthesizableCallSkipped, func_name);
        emitBlockRecursive(func, cd.success, visited, ss, merge_block);
    } else {
        emitCallGeneric(func_name, cd, func, ss);
        // 成功ブロックに続行
        emitBlockRecursive(func, cd.success, visited, ss, merge_block);
    }
    // その他の関数呼び出しはスキップ
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
                emitTermBranch(sd, cond, func, visited, ss, merge_block, current_block);
            } else {
                // 複数ターゲット → case文
                emitTermCase(sd, cond, func, visited, ss, merge_block);
            }
            break;
        }
        case mir::MirTerminator::Return:
        case mir::MirTerminator::Unreachable:
            // SVのalwaysブロック内ではreturnは不要
            break;
        case mir::MirTerminator::Call:
            emitTermCall(std::get<mir::MirTerminator::CallData>(term.data), func, visited, ss,
                         merge_block);
            break;
    }
}

}  // namespace cm::codegen::sv
