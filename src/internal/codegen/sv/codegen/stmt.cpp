// ============================================================
// SVコード生成: 文出力（代入・基本ブロック）と代入完全性解析・合流ブロック探索
// ============================================================
#include "internal/base/i18n.hpp"
#include "internal/codegen/sv/codegen.hpp"
#include "internal/codegen/sv/internal.hpp"

#include <algorithm>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace cm::codegen::sv {

// === 代入完全性解析（式ツリー化 Phase 3）===
// 組み合わせ（Auto）ブロックのラッチ推論に使用する。
// must-assignデータフロー: MustIn(B) = ∩ MustOut(pred)、MustOut(B) = MustIn(B) ∪ gen(B)。各returnブロックのMustOutに含まれない書き込み対象信号が「全パスで代入されない信号」= ラッチ要因
std::vector<std::string> SVCodeGen::findIncompletelyAssignedSignals(const mir::MirFunction& func) {
    const size_t nblocks = func.basic_blocks.size();
    if (nblocks == 0) {
        return {};
    }

    // 後続ブロックの列挙
    auto successors = [&](size_t bid) {
        std::vector<size_t> succs;
        const auto& bb = func.basic_blocks[bid];
        if (!bb || !bb->terminator) {
            return succs;
        }
        const auto& term = *bb->terminator;
        if (std::holds_alternative<mir::MirTerminator::GotoData>(term.data)) {
            succs.push_back(std::get<mir::MirTerminator::GotoData>(term.data).target);
        } else if (std::holds_alternative<mir::MirTerminator::SwitchIntData>(term.data)) {
            const auto& sd = std::get<mir::MirTerminator::SwitchIntData>(term.data);
            for (const auto& [v, t] : sd.targets) {
                succs.push_back(t);
            }
            succs.push_back(sd.otherwise);
        } else if (std::holds_alternative<mir::MirTerminator::CallData>(term.data)) {
            const auto& cd = std::get<mir::MirTerminator::CallData>(term.data);
            succs.push_back(cd.success);
            if (cd.unwind) {
                succs.push_back(*cd.unwind);
            }
        }
        return succs;
    };

    // 到達可能ブロックと先行ブロックマップ
    std::vector<bool> reachable(nblocks, false);
    std::vector<std::vector<size_t>> preds(nblocks);
    {
        std::vector<size_t> work = {0};
        while (!work.empty()) {
            size_t bid = work.back();
            work.pop_back();
            if (bid >= nblocks || !func.basic_blocks[bid] || reachable[bid]) {
                continue;
            }
            reachable[bid] = true;
            for (size_t s : successors(bid)) {
                if (s < nblocks) {
                    preds[s].push_back(bid);
                    work.push_back(s);
                }
            }
        }
    }

    // gen集合: 各ブロックで（投影なしで）代入されるモジュールレベル信号。
    // 配列要素・フィールドへの部分書き込みは全体の代入とみなさない
    auto is_target_signal = [&](mir::LocalId local) {
        return local < func.locals.size() && func.locals[local].is_global;
    };
    std::vector<std::set<mir::LocalId>> gen(nblocks);
    std::set<mir::LocalId> universe;
    for (size_t bid = 0; bid < nblocks; ++bid) {
        if (!reachable[bid] || !func.basic_blocks[bid]) {
            continue;
        }
        for (const auto& stmt : func.basic_blocks[bid]->statements) {
            if (!stmt || stmt->kind != mir::MirStatement::Assign) {
                continue;
            }
            const auto& ad = std::get<mir::MirStatement::AssignData>(stmt->data);
            if (!is_target_signal(ad.place.local)) {
                continue;
            }
            universe.insert(ad.place.local);
            if (ad.place.projections.empty()) {
                gen[bid].insert(ad.place.local);
            }
        }
        // Call戻り先への代入もdefとして扱う
        const auto& bb = func.basic_blocks[bid];
        if (bb->terminator &&
            std::holds_alternative<mir::MirTerminator::CallData>(bb->terminator->data)) {
            const auto& cd = std::get<mir::MirTerminator::CallData>(bb->terminator->data);
            if (cd.destination && is_target_signal(cd.destination->local)) {
                universe.insert(cd.destination->local);
                if (cd.destination->projections.empty()) {
                    gen[bid].insert(cd.destination->local);
                }
            }
        }
    }
    if (universe.empty()) {
        return {};
    }

    // must-assign 固定点反復（entry以外はuniverseで初期化する標準的なmust解析）
    std::vector<std::set<mir::LocalId>> must_out(nblocks, universe);
    {
        bool changed = true;
        int iterations = 0;
        while (changed && iterations < 1000) {
            changed = false;
            ++iterations;
            for (size_t bid = 0; bid < nblocks; ++bid) {
                if (!reachable[bid]) {
                    continue;
                }
                std::set<mir::LocalId> must_in;
                if (bid == 0) {
                    // entry: 何も代入されていない
                } else if (!preds[bid].empty()) {
                    bool first = true;
                    for (size_t p : preds[bid]) {
                        if (first) {
                            must_in = must_out[p];
                            first = false;
                        } else {
                            std::set<mir::LocalId> tmp;
                            std::set_intersection(must_in.begin(), must_in.end(),
                                                  must_out[p].begin(), must_out[p].end(),
                                                  std::inserter(tmp, tmp.begin()));
                            must_in = std::move(tmp);
                        }
                    }
                }
                std::set<mir::LocalId> out = must_in;
                out.insert(gen[bid].begin(), gen[bid].end());
                if (out != must_out[bid]) {
                    must_out[bid] = std::move(out);
                    changed = true;
                }
            }
        }
    }

    // 各returnブロックで未代入の信号を収集
    std::set<mir::LocalId> incomplete;
    for (size_t bid = 0; bid < nblocks; ++bid) {
        if (!reachable[bid] || !func.basic_blocks[bid] || !func.basic_blocks[bid]->terminator) {
            continue;
        }
        if (func.basic_blocks[bid]->terminator->kind != mir::MirTerminator::Return) {
            continue;
        }
        for (mir::LocalId g : universe) {
            if (must_out[bid].count(g) == 0) {
                incomplete.insert(g);
            }
        }
    }

    std::vector<std::string> names;
    for (mir::LocalId g : incomplete) {
        std::string name = func.locals[g].name;
        name = strip_namespace(name);
        names.push_back(name);
    }
    return names;
}

// === 文の生成 ===

std::string SVCodeGen::emitStatement(const mir::MirStatement& stmt, const mir::MirFunction& func) {
    switch (stmt.kind) {
        case mir::MirStatement::Assign: {
            const auto& assign = std::get<mir::MirStatement::AssignData>(stmt.data);
            std::string lhs = emitPlace(assign.place, func);
            // 代入先の型からビット幅を推論し、定数リテラルの幅を合わせる
            int target_w = 0;
            // Placeの型情報を優先使用
            if (assign.place.type) {
                target_w = getBitWidth(assign.place.type);
            } else if (assign.place.local < func.locals.size()) {
                const auto& local_type = func.locals[assign.place.local].type;
                if (local_type) {
                    target_w = getBitWidth(local_type);
                }
            }
            // 32bit(intデフォルト)の場合は定数リテラル幅の調整不要
            // (インライン展開後のコンテキストでは型情報が失われるため、混合幅の解決はCmソース側で型を統一して行う)
            if (target_w == 32)
                target_w = 0;
            // 式ツリーとして構築（単一定義テンポラリは構造的にインライン展開され、優先順位括弧はプリンタが構造から決定する）
            SVExprPtr rhs_tree =
                assign.rvalue ? buildRvalueTree(*assign.rvalue, func, target_w) : SVExpr::atom("0");
            // 単一定義テンポラリへの代入はツリーを記録し、以後の使用箇所で構造的にスプライスする。行自体は出力しない（Phase 2:
            // 従来のテキストベースのインライン展開パスを置き換える）
            if (assign.place.projections.empty() &&
                single_def_temps_.count(assign.place.local) > 0) {
                temp_trees_[assign.place.local] = rhs_tree;
                return "";
            }
            std::string rhs = rhs_tree->to_string();
            // always_ff、async
            // func、またはposedge/negedge型パラメータを持つ関数はノンブロッキング代入
            bool use_nonblocking =
                func.is_async || func.always_kind == mir::MirFunction::AlwaysKind::FF;
            if (use_nonblocking && assign.place.local < func.locals.size()) {
                if (!func.locals[assign.place.local].is_global) {
                    use_nonblocking = false;
                }
            }
            if (!use_nonblocking) {
                bool is_dest_global = true;
                if (assign.place.local < func.locals.size()) {
                    is_dest_global = func.locals[assign.place.local].is_global;
                }
                if (is_dest_global) {
                    for (const auto& local : func.locals) {
                        if (local.is_global)
                            continue;
                        if (local.type && (local.type->kind == hir::TypeKind::Posedge ||
                                           local.type->kind == hir::TypeKind::Negedge)) {
                            use_nonblocking = true;
                            break;
                        }
                    }
                }
            }
            if (use_nonblocking) {
                return lhs + " <= " + rhs + ";";
            } else {
                return lhs + " = " + rhs + ";";
            }
        }
        case mir::MirStatement::StorageLive:
        case mir::MirStatement::StorageDead:
        case mir::MirStatement::Nop:
            return "";  // SVでは不要
        case mir::MirStatement::Asm:
            throw std::runtime_error(i18n::msg(i18n::MsgId::SvSv007InlineAssemblyAsmIs));
        default:
            throw std::runtime_error(i18n::msgf(i18n::MsgId::SvSv007UnsupportedStatementOnThe,
                                                std::to_string(static_cast<int>(stmt.kind))));
    }
}

// === 基本ブロック生成 ===

std::string SVCodeGen::emitBlock(const mir::BasicBlock& block, const mir::MirFunction& func) {
    std::ostringstream ss;
    for (const auto& stmt : block.statements) {
        if (!stmt)
            continue;
        std::string line = emitStatement(*stmt, func);
        if (!line.empty()) {
            ss << indent() << line << "\n";
        }
    }
    return ss.str();
}

// === 合流ブロック探索 ===
// 2つの分岐先から辿って最初に共通する後続ブロックIDを探す
size_t SVCodeGen::findMergeBlock(const mir::MirFunction& func, size_t then_block,
                                 size_t else_block) {
    // 各ブランチから到達可能なブロックを収集
    std::set<size_t> then_reachable;
    std::vector<size_t> work = {then_block};
    while (!work.empty()) {
        size_t bid = work.back();
        work.pop_back();
        if (bid >= func.basic_blocks.size() || !func.basic_blocks[bid])
            continue;
        if (!then_reachable.insert(bid).second)
            continue;
        const auto& bb = *func.basic_blocks[bid];
        if (bb.terminator) {
            if (bb.terminator->kind == mir::MirTerminator::Goto) {
                auto& gd = std::get<mir::MirTerminator::GotoData>(bb.terminator->data);
                work.push_back(gd.target);
            } else if (bb.terminator->kind == mir::MirTerminator::SwitchInt) {
                auto& sd = std::get<mir::MirTerminator::SwitchIntData>(bb.terminator->data);
                for (const auto& [val, target] : sd.targets) {
                    work.push_back(target);
                }
                work.push_back(sd.otherwise);
            } else if (bb.terminator->kind == mir::MirTerminator::Call) {
                auto& cd = std::get<mir::MirTerminator::CallData>(bb.terminator->data);
                work.push_back(cd.success);
            }
        }
    }

    // elseブランチから辿って最初にthen_reachableに含まれるブロックを探す
    work = {else_block};
    std::set<size_t> visited;
    while (!work.empty()) {
        size_t bid = work.back();
        work.pop_back();
        if (bid >= func.basic_blocks.size() || !func.basic_blocks[bid])
            continue;
        if (!visited.insert(bid).second)
            continue;
        if (then_reachable.count(bid) && bid != then_block && bid != else_block) {
            return bid;  // 合流ブロック発見
        }
        const auto& bb = *func.basic_blocks[bid];
        if (bb.terminator) {
            if (bb.terminator->kind == mir::MirTerminator::Goto) {
                auto& gd = std::get<mir::MirTerminator::GotoData>(bb.terminator->data);
                work.push_back(gd.target);
            } else if (bb.terminator->kind == mir::MirTerminator::SwitchInt) {
                // thenブランチ側と同様にSwitchIntの全分岐先を追跡
                auto& sd = std::get<mir::MirTerminator::SwitchIntData>(bb.terminator->data);
                for (const auto& [val, target] : sd.targets) {
                    work.push_back(target);
                }
                work.push_back(sd.otherwise);
            } else if (bb.terminator->kind == mir::MirTerminator::Call) {
                // Call ターミネータの後続ブロックも追跡
                auto& cd = std::get<mir::MirTerminator::CallData>(bb.terminator->data);
                work.push_back(cd.success);
            }
        }
    }

    return SIZE_MAX;  // 合流ブロックなし
}

}  // namespace cm::codegen::sv
