// ============================================================
// CFG走査ベースのSV出力 - if/else・ループの構造化
// ============================================================
#include "codegen.hpp"
#include "internal/base/i18n.hpp"
#include "sv_internal.hpp"

#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cm::codegen::sv {

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

    // ブロック内の文を出力
    for (const auto& stmt : bb.statements) {
        if (!stmt)
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
                // if (cond == val) ... else ...
                // MIRのSwitchIntは: targets=[(val, then_block)], otherwise=else_block
                size_t then_block = sd.targets[0].second;
                size_t else_block = sd.otherwise;

                // bool分岐の場合、val==0なら否定条件
                bool is_negated = (sd.targets[0].first == 0);

                // === ループヘッダ検出とwhileループ再構成 ===
                // このブロックへの後方エッジがあり、かつ片方の分岐だけが
                // 自然ループに属する場合、ループヘッダとみなす。
                // if/elseとして出力するとバックエッジが消えて「ループ本体が最大1回・ループ後コードが到達不能」という
                // 誤ったSVになるため、whileループとして構造を復元する
                auto latch_it = current_loop_latches_.find(current_block);
                if (current_block != SIZE_MAX && latch_it != current_loop_latches_.end()) {
                    const std::vector<size_t>& latches = latch_it->second;
                    if (!latches.empty()) {
                        // 真条件(cond != 0)で実行される分岐
                        size_t true_block = is_negated ? else_block : then_block;
                        size_t false_block = is_negated ? then_block : else_block;
                        bool true_in_loop =
                            in_natural_loop(func, true_block, current_block, latches);
                        bool false_in_loop =
                            in_natural_loop(func, false_block, current_block, latches);
                        if (true_in_loop != false_in_loop) {
                            size_t body = true_in_loop ? true_block : false_block;
                            size_t exit = true_in_loop ? false_block : true_block;
                            std::string loop_cond = true_in_loop ? cond : "!(" + cond + ")";

                            // ループ脱出（disable）用の名前付きブロックで囲む
                            std::string loop_name = "__loop" + std::to_string(loop_name_counter_++);
                            ss << indent() << "begin : " << loop_name << "\n";
                            increaseIndent();
                            ss << indent() << "while (" << loop_cond << ") begin\n";
                            increaseIndent();
                            // ループ本体を出力。ループ脱出（exitへの分岐）を検出できるよう
                            // exitブロックをスタックに積む。ヘッダへの後方エッジはvisited済みのため自然に停止する
                            loop_exit_stack_.push_back(exit);
                            loop_name_stack_.push_back(loop_name);
                            emitBlockRecursive(func, body, visited, ss, exit);
                            loop_name_stack_.pop_back();
                            loop_exit_stack_.pop_back();
                            // ヘッダブロックの文（ループ条件の再計算）を本体末尾で再実行する。条件のテンポラリが2箇所で代入されることになり、インライン展開の対象からも自動的に外れる
                            if (current_block < func.basic_blocks.size() &&
                                func.basic_blocks[current_block]) {
                                for (const auto& stmt :
                                     func.basic_blocks[current_block]->statements) {
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
                            break;
                        }
                    }
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

                // === 三項演算子の構造的判定（式ツリー化 Phase 2b） ===
                // 両分岐が「同一placeへの単一代入行」なら cond ? a : b として出力。
                // 内側の分岐は再帰で先に三項化されるため、else-ifチェーンも自然に入れ子の三項として畳まれる（従来はテキストの5行パターン検出パスで行っていた）
                auto parse_single_assign = [](const std::string& text, std::string& lhs,
                                              std::string& rhs, std::string& op) {
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
                };
                std::string t_lhs, t_rhs, t_op, f_lhs, f_rhs, f_op;
                if (parse_single_assign(true_ss.str(), t_lhs, t_rhs, t_op) &&
                    parse_single_assign(false_ss.str(), f_lhs, f_rhs, f_op) && t_lhs == f_lhs &&
                    t_op == f_op) {
                    visited.insert(false_visited.begin(), false_visited.end());
                    if (t_rhs == f_rhs) {
                        // 両辺同一なら分岐自体が不要（旧・冗長三項除去パス相当）
                        ss << indent() << t_lhs << t_op << t_rhs << ";\n";
                    } else {
                        ss << indent() << t_lhs << t_op << "(" << cond << ") ? " << t_rhs << " : "
                           << f_rhs << ";\n";
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
                if (merge != SIZE_MAX &&
                    (loop_exit_stack_.empty() || merge != loop_exit_stack_.back())) {
                    emitBlockRecursive(func, merge, visited, ss, merge_block);
                }
            } else {
                // 複数ターゲット → case文
                // 全分岐先が合流するブロックを探す（最初の2つから合流点を特定）
                size_t merge = SIZE_MAX;
                if (sd.targets.size() >= 2) {
                    merge = findMergeBlock(func, sd.targets[0].second, sd.targets[1].second);
                }

                // matchは網羅性検査済み・switchはdefault生成があるため、シミュレーション時の重複ヒット検出が得られる unique case を出力する
                ss << indent() << "unique case (" << cond << ")\n";
                increaseIndent();

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
            break;
        }
        case mir::MirTerminator::Return:
        case mir::MirTerminator::Unreachable:
            // SVのalwaysブロック内ではreturnは不要
            break;
        case mir::MirTerminator::Call: {
            const auto& cd = std::get<mir::MirTerminator::CallData>(term.data);
            std::string func_name;
            if (cd.func && cd.func->kind == mir::MirOperand::FunctionRef) {
                func_name = std::get<std::string>(cd.func->data);
            }

            // Ref逆引きマップ構築: テンポラリ(_tXXX) → 元のPlace
            // Use(Constant)逆引きマップ: テンポラリ → 定数値
            // copy逆引きマップ: テンポラリ → コピー元
            std::map<mir::LocalId, mir::MirPlace> ref_map;
            std::map<mir::LocalId, mir::MirPlace> copy_map;
            std::map<mir::LocalId, std::pair<mir::MirConstant, hir::TypePtr>> const_map;
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
                        if (auto* ref_data =
                                std::get_if<mir::MirRvalue::RefData>(&ad.rvalue->data)) {
                            ref_map.insert_or_assign(ad.place.local, ref_data->place);
                        }
                    } else if (ad.rvalue->kind == mir::MirRvalue::Use) {
                        if (auto* use_data =
                                std::get_if<mir::MirRvalue::UseData>(&ad.rvalue->data)) {
                            if (use_data->operand) {
                                if (use_data->operand->kind == mir::MirOperand::Constant) {
                                    const_map.insert_or_assign(
                                        ad.place.local, std::make_pair(std::get<mir::MirConstant>(
                                                                           use_data->operand->data),
                                                                       use_data->operand->type));
                                } else if (use_data->operand->kind == mir::MirOperand::Copy ||
                                           use_data->operand->kind == mir::MirOperand::Move) {
                                    copy_map.insert_or_assign(
                                        ad.place.local,
                                        std::get<mir::MirPlace>(use_data->operand->data));
                                }
                            }
                        }
                    }
                }
            }

            // Call args を解決: テンポラリ → 元のPlace名 or 定数値
            auto resolveArg = [&](const mir::MirOperand& op) -> std::string {
                if (op.kind == mir::MirOperand::Move || op.kind == mir::MirOperand::Copy) {
                    const auto& place = std::get<mir::MirPlace>(op.data);
                    // Ref逆引き: _t → &original → original
                    auto ref_it = ref_map.find(place.local);
                    if (ref_it != ref_map.end()) {
                        return emitPlace(ref_it->second, func);
                    }
                    // Const逆引き: _t → constant
                    auto const_it = const_map.find(place.local);
                    if (const_it != const_map.end()) {
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
            };

            auto traceToOrigin = [&](mir::MirPlace p) -> std::string {
                while (true) {
                    auto copy_it = copy_map.find(p.local);
                    if (copy_it != copy_map.end()) {
                        p = copy_it->second;
                        continue;
                    }
                    auto ref_it = ref_map.find(p.local);
                    if (ref_it != ref_map.end()) {
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

            if (func_name == "assert") {
                // 即時アサーション: assert (条件) else $error(...);
                // シミュレーションで検証され、合成ツールでは無視される
                std::string cond =
                    (!cd.args.empty() && cd.args[0]) ? resolveArg(*cd.args[0]) : "1'b1";
                std::string message = "assertion failed";
                if (cd.args.size() >= 2 && cd.args[1]) {
                    const mir::MirConstant* msg_const = nullptr;
                    if (cd.args[1]->kind == mir::MirOperand::Constant) {
                        msg_const = &std::get<mir::MirConstant>(cd.args[1]->data);
                    } else if (cd.args[1]->kind == mir::MirOperand::Move ||
                               cd.args[1]->kind == mir::MirOperand::Copy) {
                        const auto& place = std::get<mir::MirPlace>(cd.args[1]->data);
                        auto const_it = const_map.find(place.local);
                        if (const_it != const_map.end()) {
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
                emitBlockRecursive(func, cd.success, visited, ss, merge_block);
            } else if (func_name == "__builtin_concat" || func_name == "__builtin_replicate") {
                // ノンブロッキング代入の判定
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

                if (func_name == "__builtin_concat") {
                    // SV連接: {a, b, ...}
                    std::string rhs = "{";
                    for (size_t i = 0; i < cd.args.size(); ++i) {
                        if (i > 0)
                            rhs += ", ";
                        rhs += cd.args[i] ? resolveArg(*cd.args[i]) : "0";
                    }
                    rhs += "}";
                    if (cd.destination) {
                        std::string lhs = emitPlace(*cd.destination, func);
                        ss << indent() << lhs << (use_nb ? " <= " : " = ") << rhs << ";\n";
                    }
                } else {
                    // SV複製: {N{expr}} count を直接整数値として取得
                    std::string count_str = "1";
                    if (cd.args.size() > 0 && cd.args[0]) {
                        if (cd.args[0]->kind == mir::MirOperand::Constant) {
                            const auto& c = std::get<mir::MirConstant>(cd.args[0]->data);
                            if (auto* ival = std::get_if<int64_t>(&c.value)) {
                                count_str = std::to_string(*ival);
                            } else {
                                count_str = resolveArg(*cd.args[0]);
                            }
                        } else if (cd.args[0]->kind == mir::MirOperand::Move ||
                                   cd.args[0]->kind == mir::MirOperand::Copy) {
                            const auto& place = std::get<mir::MirPlace>(cd.args[0]->data);
                            auto const_it = const_map.find(place.local);
                            if (const_it != const_map.end()) {
                                if (auto* ival =
                                        std::get_if<int64_t>(&const_it->second.first.value)) {
                                    count_str = std::to_string(*ival);
                                } else {
                                    count_str = resolveArg(*cd.args[0]);
                                }
                            } else {
                                count_str = resolveArg(*cd.args[0]);
                            }
                        } else {
                            count_str = resolveArg(*cd.args[0]);
                        }
                    }
                    std::string expr =
                        cd.args.size() > 1 && cd.args[1] ? resolveArg(*cd.args[1]) : "0";
                    std::string rhs = "{" + count_str + "{" + expr + "}}";
                    if (cd.destination) {
                        std::string lhs = emitPlace(*cd.destination, func);
                        ss << indent() << lhs << (use_nb ? " <= " : " = ") << rhs << ";\n";
                    }
                }
                // 成功ブロックに続行
                emitBlockRecursive(func, cd.success, visited, ss, merge_block);
            } else if (func_name == "__builtin_string_charAt" ||
                       func_name == "__builtin_string_byte_at") {
                // R2: SVの文字列はASCIIバイト配列でバイト==コードポイントのため、byte_atはcharAtと同一の添字アクセスとして生成する
                // ノンブロッキング代入の判定
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
                    std::string res_name = cleanName(resolveArg(*cd.args[0]));
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
                    cd.args.size() > 0 && cd.args[0] ? resolveArg(*cd.args[0]) : "0";
                std::string idx_val =
                    cd.args.size() > 1 && cd.args[1] ? resolveArg(*cd.args[1]) : "0";

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
                // 成功ブロックに続行
                emitBlockRecursive(func, cd.success, visited, ss, merge_block);
            } else if (func_name == "cm_string_free" || func_name == "cm_slice_free") {
                // C12 dropパスのメモリ解放呼び出しはSVでは意味を持たないため黙ってスキップする
                emitBlockRecursive(func, cd.success, visited, ss, merge_block);
            } else if (func_name == "println" || func_name == "print" ||
                       func_name.rfind("cm_println", 0) == 0 ||
                       func_name.rfind("cm_print", 0) == 0 ||
                       func_name.rfind("cm_format", 0) == 0) {
                // M18: 合成モジュール内のprintln等のI/O組み込みは合成不能。
                // 従来は未定義関数としてそのまま出力され診断が一切なかった（黙殺）。
                // 未定義関数の無言emitをやめ、警告してスキップする（シミュレーション出力は
                // #[test] 関数内でのみ$displayへ変換される。段階導入のためまず警告）
                std::cerr << i18n::msgf(i18n::MsgId::SvSv008UnsynthesizableCallSkipped, func_name);
                emitBlockRecursive(func, cd.success, visited, ss, merge_block);
            } else {
                // 一般的な関数呼び出し: result = func_name(arg1, arg2, ...);
                // ノンブロッキング代入の判定
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
                    ss << indent() << lhs << (use_nb ? " <= " : " = ") << func_name << "("
                       << args_str << ");\n";
                } else {
                    // void関数呼び出し（taskの場合等）
                    ss << indent() << func_name << "(" << args_str << ");\n";
                }
                // 成功ブロックに続行
                emitBlockRecursive(func, cd.success, visited, ss, merge_block);
            }
            // その他の関数呼び出しはスキップ
            break;
        }
    }
}

}  // namespace cm::codegen::sv
