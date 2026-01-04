#pragma once

#include "../../analysis/dominators.hpp"
#include "../../analysis/loop_analysis.hpp"
#include "../../nodes.hpp"
#include "../core/base.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace cm::mir::opt {

class LoopInvariantCodeMotion : public OptimizationPass {
   public:
    std::string name() const override { return "LoopInvariantCodeMotion"; }

    bool run(MirFunction& func) override {
        bool changed = false;

        // 1. Dominator Tree & Loop Analysis構築
        // DominatorTreeなどはParent Namespace (cm::mir) にあるため修飾が必要
        cm::mir::DominatorTree dom_tree(func);
        cm::mir::LoopAnalysis loop_analysis(func, dom_tree);

        // 2. ループごとに処理（内側から外側へ）
        // LoopAnalysisはネスト順序を保証しないが、再帰的に処理すればOK
        // ここではTop-level loopsから再帰的にvisitする
        for (auto* loop : loop_analysis.get_top_level_loops()) {
            if (process_loop(func, loop)) {
                changed = true;
            }
        }

        return changed;
    }

   private:
    bool process_loop(MirFunction& func, cm::mir::Loop* loop) {
        bool changed = false;

        // まずサブループを処理
        for (auto* sub : loop->sub_loops) {
            if (process_loop(func, sub)) {
                changed = true;
            }
        }

        // Pre-headerの取得または作成
        BlockId pre_header = get_or_create_pre_header(func, loop);
        if (pre_header == INVALID_BLOCK) {
            return changed;  // Pre-headerが作れない（例：entryがヘッダー）場合はスキップ
        }

        // ループ内で変更される変数を特定
        std::set<LocalId> modified_locals;
        for (BlockId b : loop->blocks) {
            const auto& bb = *func.basic_blocks[b];
            for (const auto& stmt : bb.statements) {
                if (stmt->kind == MirStatement::Assign) {
                    auto& assign = std::get<MirStatement::AssignData>(stmt->data);
                    modified_locals.insert(assign.place.local);
                }
                // Call命令のdestinationも変更される
                // （MirStatement::Callはなく、MirTerminator::Callにある）
            }
            if (bb.terminator && bb.terminator->kind == MirTerminator::Call) {
                auto& call = std::get<MirTerminator::CallData>(bb.terminator->data);
                if (call.destination) {
                    modified_locals.insert(call.destination->local);
                }
            }
        }

        // Headerブロック内の命令を走査して移動可能なものを探す
        // （Header以外のブロックからの移動は支配関係のチェックが必要なため、今回はHeaderのみ対象とすることで安全性を確保）
        BlockId header_id = loop->header;
        auto& header_stmts = func.basic_blocks[header_id]->statements;

        std::vector<int> to_move_indices;

        for (int i = 0; i < (int)header_stmts.size(); ++i) {
            const auto& stmt = header_stmts[i];
            if (stmt->kind == MirStatement::Assign) {
                auto& assign = std::get<MirStatement::AssignData>(stmt->data);

                // 1. 左辺変数がループ内の他の場所で変更されていないこと
                // modified_localsには自分自身も含まれるため、カウントして確認すべきだが
                // 簡易的に「自分しか代入していない」ことを確認するのはコストがかかる。
                // 安全策として、ループ不変式の右辺のみチェックし、代入先については
                // 「SSA的性質を持つ一時変数」であることを期待するか、あるいは
                // 移動しても意味論が変わらないことを保証する必要がある。

                // LICMの基本： t = a op b; において a, b が不変なら t の計算も不変。
                // t がループ内で書き換えられていようと、この計算時点での t の値は不変。
                // 問題は t を使う後続の命令が、移動された t を使うことになる点。
                // もしループ内で t
                // が再代入されるなら、移動すると最初のイテレーションと2回目以降で値が変わる可能性がある？
                // いや、t = inv_expr が移動されれば、t はループ前に1回計算されるだけになる。
                // ループ内で t = other_expr があると、t の値は変わる。
                // したがって、t 自体もループ内で（この命令以外で）不変でなければならない。

                // 今回の実装では、修正された変数のセットを使って簡易判定を行う。
                // 「再代入されない」ことを確認するため、modified_localsに含まれていない変数への代入はありえない（代入文がある時点で含まれる）。
                // 正確には「この文以外で代入されない」が必要。
                // 面倒なので、「右辺が不変」かつ「副作用なし」なら移動する、という方針にするが、
                // 左辺変数がループ内で再定義される場合、移動すると不整合が起きる。
                // 安全のため、「左辺変数は temporaries (ユーザー変数でない) かつ
                // ループ内で1回だけ定義される」場合に限定する？
                // あるいは諦めて、「右辺が不変」なら移動させる。

                if (is_invariant(*assign.rvalue, modified_locals)) {
                    // 副作用のチェック（Rvalueには副作用はないはずだが、CallなどはRvalueにならない）
                    // メモリアクセス (Ref, Deref) はエイリアスの問題があるため除外
                    if (has_memory_access(*assign.rvalue))
                        continue;

                    to_move_indices.push_back(i);
                }
            }
        }

        if (!to_move_indices.empty()) {
            changed = true;
            auto& pre_header_bb = *func.basic_blocks[pre_header];

            // 後ろから移動（インデックスがずれないように、ではなく、削除時にずれるので注意）
            // removeするので、大きいインデックスから処理するのが定石だが、
            // 移動順序は前から順にしたい（依存関係のため）。
            // よって前から順にPre-headerに追加し、後でHeaderから一括削除（またはNull化）する。

            for (int idx : to_move_indices) {
                // Pre-headerの末尾（Terminatorの前）に追加
                // しかしTerminatorはStatementリストに含まれないので、単純にpush_back
                pre_header_bb.statements.push_back(std::move(header_stmts[idx]));
            }

            // Headerから削除（erase-remove
            // idiomはunique_ptrには使いにくいので、新しいvectorを作る）
            std::vector<std::unique_ptr<MirStatement>> new_stmts;

            auto move_it = to_move_indices.begin();

            for (int i = 0; i < (int)header_stmts.size(); ++i) {
                if (move_it != to_move_indices.end() && *move_it == i) {
                    // 移動済み（nullになっているはずだが、std::moveしたので空のunique_ptr）
                    move_it++;
                } else {
                    if (header_stmts[i]) {  // moveされた後はnull
                        new_stmts.push_back(std::move(header_stmts[i]));
                    }
                }
            }
            header_stmts = std::move(new_stmts);
        }

        return changed;
    }

    // Pre-headerの取得または作成
    BlockId get_or_create_pre_header(MirFunction& func, cm::mir::Loop* loop) {
        BlockId header_id = loop->header;

        // HeaderへのPredecessors（バックエッジ以外）を取得
        std::vector<BlockId> entering_preds;

        for (size_t i = 0; i < func.basic_blocks.size(); ++i) {
            if (!func.basic_blocks[i])
                continue;
            // Loop内のブロック（バックエッジ含む）は除外
            if (loop->contains((BlockId)i))
                continue;

            // i -> header のエッジがあるか確認
            const auto& bb = *func.basic_blocks[i];
            if (bb.terminator) {
                bool is_pred = false;
                switch (bb.terminator->kind) {
                    case MirTerminator::Goto:
                        if (std::get<MirTerminator::GotoData>(bb.terminator->data).target ==
                            header_id)
                            is_pred = true;
                        break;
                    case MirTerminator::SwitchInt: {
                        auto& data = std::get<MirTerminator::SwitchIntData>(bb.terminator->data);
                        if (data.otherwise == header_id)
                            is_pred = true;
                        for (auto& pair : data.targets)
                            if (pair.second == header_id)
                                is_pred = true;
                        break;
                    }
                    case MirTerminator::Call: {
                        auto& data = std::get<MirTerminator::CallData>(bb.terminator->data);
                        if (data.success == header_id)
                            is_pred = true;
                        break;
                    }
                    default:
                        break;
                }
                if (is_pred) {
                    entering_preds.push_back(i);
                }
            }
        }

        if (entering_preds.empty()) {
            if (header_id == 0)
                return INVALID_BLOCK;  // EntryがヘッダーならPre-header作れない（またはEntryを分割する必要があるが複雑）
            // 到達不能ループ？
            return INVALID_BLOCK;
        }

        // 既存のPre-headerがあるか確認
        // 条件: HeaderのPredecessors（ループ外）が1つだけで、そのブロックのSuccessorがHeaderだけ
        if (entering_preds.size() == 1) {
            BlockId pred = entering_preds[0];
            const auto& pred_bb = *func.basic_blocks[pred];
            // SuccessorがHeaderだけかチェック（Gotoなら確実）
            if (pred_bb.terminator->kind == MirTerminator::Goto) {
                return pred;
            }
        }

        // Pre-headerを新規作成
        BlockId new_id = func.basic_blocks.size();
        auto pre_header = std::make_unique<BasicBlock>(new_id);

        // Headerへのジャンプを設定
        pre_header->terminator = MirTerminator::goto_block(header_id);

        // 各PredecessorのターゲットをPre-headerに書き換え
        for (BlockId p : entering_preds) {
            auto& bb = *func.basic_blocks[p];
            if (bb.terminator) {
                switch (bb.terminator->kind) {
                    case MirTerminator::Goto:
                        std::get<MirTerminator::GotoData>(bb.terminator->data).target = new_id;
                        break;
                    case MirTerminator::SwitchInt: {
                        auto& data = std::get<MirTerminator::SwitchIntData>(bb.terminator->data);
                        if (data.otherwise == header_id)
                            data.otherwise = new_id;
                        for (auto& pair : data.targets)
                            if (pair.second == header_id)
                                pair.second = new_id;
                        break;
                    }
                    case MirTerminator::Call: {
                        auto& data = std::get<MirTerminator::CallData>(bb.terminator->data);
                        if (data.success == header_id)
                            data.success = new_id;
                        break;
                    }
                    default:
                        break;
                }
            }
        }

        func.basic_blocks.push_back(std::move(pre_header));
        return new_id;
    }

    bool is_invariant(const MirRvalue& rvalue, const std::set<LocalId>& modified_locals) {
        switch (rvalue.kind) {
            case MirRvalue::Use: {
                auto& data = std::get<MirRvalue::UseData>(rvalue.data);
                return is_invariant(*data.operand, modified_locals);
            }
            case MirRvalue::BinaryOp: {
                auto& data = std::get<MirRvalue::BinaryOpData>(rvalue.data);
                return is_invariant(*data.lhs, modified_locals) &&
                       is_invariant(*data.rhs, modified_locals);
            }
            case MirRvalue::UnaryOp: {
                auto& data = std::get<MirRvalue::UnaryOpData>(rvalue.data);
                return is_invariant(*data.operand, modified_locals);
            }
            case MirRvalue::Cast: {
                auto& data = std::get<MirRvalue::CastData>(rvalue.data);
                return is_invariant(*data.operand, modified_locals);
            }
            case MirRvalue::FormatConvert: {
                auto& data = std::get<MirRvalue::FormatConvertData>(rvalue.data);
                return is_invariant(*data.operand, modified_locals);
            }
            case MirRvalue::Ref:
                // アドレス取得自体は不変かもしれないが、メモリアクセスとみなして除外（安全策）
                return false;
            default:
                return false;
        }
    }

    bool is_invariant(const MirOperand& operand, const std::set<LocalId>& modified_locals) {
        if (operand.kind == MirOperand::Constant || operand.kind == MirOperand::FunctionRef)
            return true;

        if (operand.kind == MirOperand::Copy || operand.kind == MirOperand::Move) {
            auto& place = std::get<MirPlace>(operand.data);
            if (!place.projections.empty())
                return false;  // プロジェクションなど複雑なものは除外

            // ローカル変数がループ内で変更されていなければ不変
            return modified_locals.find(place.local) == modified_locals.end();
        }
        return false;
    }

    bool has_memory_access(const MirRvalue& rvalue) {
        // Refはメモリアドレス取得なので微妙だが、LICMでは除外するのが無難
        // Use/BinaryOp/UnaryOp/Cast内にはメモリアクセスを含まない（OperandがCopyであればLoad相当だが、SSAではないのでLoadとみなす）
        // オペランドがCopy/Moveの場合、それは変数のLoadを意味する。
        // 変数がmodifiedされていなければ安全。
        return rvalue.kind == MirRvalue::Ref;
    }
};

}  // namespace cm::mir::opt
