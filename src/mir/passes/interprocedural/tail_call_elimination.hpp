#pragma once

#include "../../nodes.hpp"
#include "../core/base.hpp"

#include <string>
#include <vector>

namespace cm::mir::opt {

// ============================================================
// 末尾再帰最適化（Tail Call Elimination）
// ============================================================
// 自己再帰呼び出しが末尾位置にある場合、ループに変換してスタックを節約
//
// 変換前:
//   bb0:
//     if (n <= 1) goto bb_ret else goto bb_recurse
//   bb_recurse:
//     _temp = factorial(n-1, n*acc)
//     goto bb_ret
//   bb_ret:
//     return _temp
//
// 変換後:
//   bb0:
//     if (n <= 1) goto bb_ret else goto bb_update
//   bb_update:
//     acc = n * acc
//     n = n - 1
//     goto bb0 (ループ)
//   bb_ret:
//     return acc
// ============================================================

class TailCallElimination : public OptimizationPass {
   public:
    std::string name() const override { return "TailCallElimination"; }

    bool run(MirFunction& func) override {
        bool changed = false;

        // 各基本ブロックを検査して末尾呼び出しをマーク
        for (auto& block : func.basic_blocks) {
            if (!block || !block->terminator)
                continue;

            // Call終端子を探す
            if (block->terminator->kind != MirTerminator::Call)
                continue;

            auto& call_data = std::get<MirTerminator::CallData>(block->terminator->data);

            // 既にマーク済みならスキップ
            if (call_data.is_tail_call)
                continue;

            // 自己呼び出しかチェック
            if (!is_self_call(func, call_data))
                continue;

            // 末尾位置かチェック（successブロックがReturnのみ）
            if (!is_tail_position(func, call_data))
                continue;

            // 末尾呼び出しとしてマーク（LLVMで tail call 属性を設定するため）
            call_data.is_tail_call = true;
            changed = true;
        }

        return changed;
    }

   private:
    // 自己呼び出しかどうかチェック
    bool is_self_call(const MirFunction& func, const MirTerminator::CallData& call_data) {
        if (!call_data.func)
            return false;

        // 関数参照を取得
        if (call_data.func->kind != MirOperand::FunctionRef)
            return false;

        const auto* func_name = std::get_if<std::string>(&call_data.func->data);
        if (!func_name)
            return false;

        // 自己呼び出しか確認
        return *func_name == func.name;
    }

    // 末尾位置かどうかチェック
    // successブロックがReturn終端子を持ち、呼び出し結果をそのまま返すか確認
    bool is_tail_position(const MirFunction& func, const MirTerminator::CallData& call_data) {
        if (call_data.success >= func.basic_blocks.size())
            return false;

        const auto* success_block = func.basic_blocks[call_data.success].get();
        if (!success_block || !success_block->terminator)
            return false;

        // successブロックがReturnで終わっていること
        if (success_block->terminator->kind != MirTerminator::Return)
            return false;

        // successブロックに追加の文がある場合は末尾位置ではない
        // （戻り値の格納以外の処理がある）
        // ただし、戻り値の格納（_0への代入）は許可
        for (const auto& stmt : success_block->statements) {
            if (!stmt)
                continue;
            if (stmt->kind == MirStatement::Assign) {
                auto& assign_data = std::get<MirStatement::AssignData>(stmt->data);
                // _0（戻り値）への代入以外があれば末尾位置ではない
                if (assign_data.place.local != func.return_local) {
                    return false;
                }
            }
        }

        return true;
    }

    // 末尾再帰をループに変換
    // 新しいループヘッダーブロックを作成し、CFG整合性を維持
    bool transform_to_loop(MirFunction& func, BasicBlock& call_block,
                           MirTerminator::CallData& call_data) {
        // 引数の数が一致しているか確認
        if (call_data.args.size() != func.arg_locals.size()) {
            return false;
        }

        // エントリブロックを取得
        if (func.entry_block >= func.basic_blocks.size() || !func.basic_blocks[func.entry_block]) {
            return false;
        }
        BasicBlock* entry = func.basic_blocks[func.entry_block].get();
        if (!entry->terminator) {
            return false;  // エントリブロックに終端子がない場合はスキップ
        }

        // ループヘッダー用の新しいブロックを作成
        BlockId loop_header_id = func.add_block();
        BasicBlock* loop_header = func.basic_blocks[loop_header_id].get();

        // エントリブロックの内容をループヘッダーに移動
        loop_header->statements = std::move(entry->statements);
        loop_header->terminator = std::move(entry->terminator);
        entry->statements.clear();

        // エントリブロックからループヘッダーへのGotoを設定
        entry->terminator = MirTerminator::goto_block(loop_header_id);

        // 引数を更新する文を生成
        std::vector<MirStatementPtr> update_stmts;

        // 一時変数を使って引数を更新（循環依存を避けるため）
        std::vector<LocalId> temp_locals;

        for (size_t i = 0; i < call_data.args.size(); ++i) {
            auto& arg = call_data.args[i];
            if (!arg)
                continue;

            LocalId arg_local = func.arg_locals[i];

            // 一時変数を作成
            LocalId temp_local = func.add_local("_tce_temp_" + std::to_string(i),
                                                func.locals[arg_local].type, true, false);
            temp_locals.push_back(temp_local);

            // temp = arg_expression
            auto temp_stmt =
                MirStatement::assign(MirPlace(temp_local), MirRvalue::use(clone_operand(*arg)));
            update_stmts.push_back(std::move(temp_stmt));
        }

        // 一時変数から引数に代入
        for (size_t i = 0; i < temp_locals.size(); ++i) {
            LocalId arg_local = func.arg_locals[i];

            // arg = temp
            auto assign_stmt = MirStatement::assign(
                MirPlace(arg_local), MirRvalue::use(MirOperand::copy(MirPlace(temp_locals[i]))));
            update_stmts.push_back(std::move(assign_stmt));
        }

        // call_blockに引数更新文を追加
        for (auto& stmt : update_stmts) {
            call_block.statements.push_back(std::move(stmt));
        }

        // call_blockの終端子をループヘッダーへのGotoに変更（エントリではなく）
        call_block.terminator = MirTerminator::goto_block(loop_header_id);

        return true;
    }

    // オペランドをクローン
    MirOperandPtr clone_operand(const MirOperand& src) {
        auto op = std::make_unique<MirOperand>();
        op->kind = src.kind;
        op->type = src.type;

        if (auto* place = std::get_if<MirPlace>(&src.data)) {
            op->data = *place;
        } else if (auto* constant = std::get_if<MirConstant>(&src.data)) {
            op->data = *constant;
        } else if (auto* func_name = std::get_if<std::string>(&src.data)) {
            op->data = *func_name;
        }

        return op;
    }
};

}  // namespace cm::mir::opt
