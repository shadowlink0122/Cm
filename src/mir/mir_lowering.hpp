#pragma once

#include "../hir/hir_nodes.hpp"
#include "mir_nodes.hpp"

#include <stack>
#include <unordered_map>

namespace cm::mir {

// ============================================================
// HIR → MIR 変換
// ============================================================
class MirLowering {
   public:
    MirProgram lower(const hir::HirProgram& hir_program) {
        MirProgram mir_program;
        mir_program.filename = hir_program.filename;

        for (const auto& decl : hir_program.declarations) {
            if (auto* func = std::get_if<std::unique_ptr<hir::HirFunction>>(&decl->kind)) {
                if (auto mir_func = lower_function(**func)) {
                    mir_program.functions.push_back(std::move(mir_func));
                }
            }
            // TODO: 構造体、インターフェースなどの処理
        }

        return mir_program;
    }

   private:
    // 現在の関数コンテキスト
    struct FunctionContext {
        MirFunction* func;
        BlockId current_block;
        std::unordered_map<std::string, LocalId> var_map;  // 変数名 → ローカルID
        LocalId next_temp_id;                              // 次の一時変数ID

        FunctionContext(MirFunction* f) : func(f), current_block(ENTRY_BLOCK), next_temp_id(0) {}

        // 新しい一時変数を生成
        LocalId new_temp(hir::TypePtr type) {
            std::string name = "_tmp" + std::to_string(next_temp_id++);
            return func->add_local(name, std::move(type), true, false);
        }

        // 現在のブロックを取得
        BasicBlock* get_current_block() { return func->get_block(current_block); }

        // 新しいブロックを作成して切り替え
        BlockId new_block() {
            BlockId id = func->add_block();
            current_block = id;
            return id;
        }

        // ブロックを切り替え
        void switch_to_block(BlockId id) { current_block = id; }

        // 文を現在のブロックに追加
        void push_statement(MirStatementPtr stmt) {
            if (auto* block = get_current_block()) {
                block->add_statement(std::move(stmt));
            }
        }

        // 終端命令を設定
        void set_terminator(MirTerminatorPtr term) {
            if (auto* block = get_current_block()) {
                block->set_terminator(std::move(term));
            }
        }
    };

    // 関数のlowering
    MirFunctionPtr lower_function(const hir::HirFunction& hir_func) {
        auto mir_func = std::make_unique<MirFunction>();
        mir_func->name = hir_func.name;

        FunctionContext ctx(mir_func.get());

        // 戻り値用のローカル変数 _0 を作成
        ctx.func->return_local = ctx.func->add_local("_0", hir_func.return_type, true, false);

        // パラメータをローカル変数として登録
        for (const auto& param : hir_func.params) {
            LocalId param_id = ctx.func->add_local(param.name, param.type, true, true);
            ctx.func->arg_locals.push_back(param_id);
            ctx.var_map[param.name] = param_id;
        }

        // エントリーブロックを作成
        ctx.func->add_block();  // bb0

        // 関数本体を変換
        for (const auto& stmt : hir_func.body) {
            lower_statement(ctx, *stmt);
        }

        // 現在のブロックに終端がない場合、returnを追加
        if (auto* block = ctx.get_current_block()) {
            if (!block->terminator) {
                block->set_terminator(MirTerminator::return_value());
            }
        }

        // CFGを構築
        mir_func->build_cfg();

        return mir_func;
    }

    // 文のlowering
    void lower_statement(FunctionContext& ctx, const hir::HirStmt& stmt) {
        if (auto* let_stmt = std::get_if<std::unique_ptr<hir::HirLet>>(&stmt.kind)) {
            lower_let_stmt(ctx, **let_stmt);
        } else if (auto* assign = std::get_if<std::unique_ptr<hir::HirAssign>>(&stmt.kind)) {
            lower_assign_stmt(ctx, **assign);
        } else if (auto* ret = std::get_if<std::unique_ptr<hir::HirReturn>>(&stmt.kind)) {
            lower_return_stmt(ctx, **ret);
        } else if (auto* if_stmt = std::get_if<std::unique_ptr<hir::HirIf>>(&stmt.kind)) {
            lower_if_stmt(ctx, **if_stmt);
        } else if (auto* loop_stmt = std::get_if<std::unique_ptr<hir::HirLoop>>(&stmt.kind)) {
            lower_loop_stmt(ctx, **loop_stmt);
        } else if (auto* expr_stmt = std::get_if<std::unique_ptr<hir::HirExprStmt>>(&stmt.kind)) {
            lower_expr(ctx, *(*expr_stmt)->expr);
        } else if (auto* block = std::get_if<std::unique_ptr<hir::HirBlock>>(&stmt.kind)) {
            lower_block_stmt(ctx, **block);
        } else if (std::get_if<std::unique_ptr<hir::HirBreak>>(&stmt.kind)) {
            // TODO: break文の処理（ループコンテキストが必要）
        } else if (std::get_if<std::unique_ptr<hir::HirContinue>>(&stmt.kind)) {
            // TODO: continue文の処理（ループコンテキストが必要）
        }
    }

    // let文のlowering
    void lower_let_stmt(FunctionContext& ctx, const hir::HirLet& let_stmt) {
        // 新しいローカル変数を作成
        LocalId local_id =
            ctx.func->add_local(let_stmt.name, let_stmt.type, !let_stmt.is_const, true);
        ctx.var_map[let_stmt.name] = local_id;

        // StorageLive
        ctx.push_statement(MirStatement::storage_live(local_id));

        // 初期値がある場合は代入
        if (let_stmt.init) {
            LocalId init_local = lower_expr(ctx, *let_stmt.init);

            // place = use(init_local)
            auto rvalue = MirRvalue::use(MirOperand::copy(MirPlace(init_local)));
            ctx.push_statement(MirStatement::assign(MirPlace(local_id), std::move(rvalue)));
        }
    }

    // 代入文のlowering
    void lower_assign_stmt(FunctionContext& ctx, const hir::HirAssign& assign) {
        LocalId value_local = lower_expr(ctx, *assign.value);

        // 変数を探す
        auto it = ctx.var_map.find(assign.target);
        if (it != ctx.var_map.end()) {
            auto rvalue = MirRvalue::use(MirOperand::copy(MirPlace(value_local)));
            ctx.push_statement(MirStatement::assign(MirPlace(it->second), std::move(rvalue)));
        }
    }

    // return文のlowering
    void lower_return_stmt(FunctionContext& ctx, const hir::HirReturn& ret) {
        if (ret.value) {
            // 戻り値を_0に代入
            LocalId value_local = lower_expr(ctx, *ret.value);
            auto rvalue = MirRvalue::use(MirOperand::copy(MirPlace(value_local)));
            ctx.push_statement(
                MirStatement::assign(MirPlace(ctx.func->return_local), std::move(rvalue)));
        }

        // return終端
        ctx.set_terminator(MirTerminator::return_value());
    }

    // if文のlowering
    void lower_if_stmt(FunctionContext& ctx, const hir::HirIf& if_stmt) {
        // 条件式を評価
        LocalId cond_local = lower_expr(ctx, *if_stmt.cond);

        // ブロックを作成
        BlockId then_block = ctx.func->add_block();
        BlockId else_block = ctx.func->add_block();
        BlockId merge_block = ctx.func->add_block();

        // 分岐終端を設定
        auto discriminant = MirOperand::copy(MirPlace(cond_local));
        std::vector<std::pair<int64_t, BlockId>> targets = {{1, then_block}};  // true -> then_block
        ctx.set_terminator(MirTerminator::switch_int(std::move(discriminant), targets, else_block));

        // thenブロックを処理
        ctx.switch_to_block(then_block);
        for (const auto& stmt : if_stmt.then_block) {
            lower_statement(ctx, *stmt);
        }
        // 終端がなければmergeブロックへジャンプ
        if (auto* block = ctx.get_current_block()) {
            if (!block->terminator) {
                ctx.set_terminator(MirTerminator::goto_block(merge_block));
            }
        }

        // elseブロックを処理
        ctx.switch_to_block(else_block);
        for (const auto& stmt : if_stmt.else_block) {
            lower_statement(ctx, *stmt);
        }
        // 終端がなければmergeブロックへジャンプ
        if (auto* block = ctx.get_current_block()) {
            if (!block->terminator) {
                ctx.set_terminator(MirTerminator::goto_block(merge_block));
            }
        }

        // mergeブロックに切り替え
        ctx.switch_to_block(merge_block);
    }

    // ループのlowering
    void lower_loop_stmt(FunctionContext& ctx, const hir::HirLoop& loop) {
        // ループヘッダとループ本体ブロックを作成
        BlockId loop_header = ctx.func->add_block();
        BlockId loop_exit = ctx.func->add_block();

        // ループヘッダへジャンプ
        ctx.set_terminator(MirTerminator::goto_block(loop_header));
        ctx.switch_to_block(loop_header);

        // TODO: ループコンテキストの管理（break/continue用）

        // ループ本体を処理
        for (const auto& stmt : loop.body) {
            lower_statement(ctx, *stmt);
        }

        // ループヘッダへ戻る
        if (auto* block = ctx.get_current_block()) {
            if (!block->terminator) {
                ctx.set_terminator(MirTerminator::goto_block(loop_header));
            }
        }

        // ループ出口に切り替え
        ctx.switch_to_block(loop_exit);
    }

    // ブロック文のlowering
    void lower_block_stmt(FunctionContext& ctx, const hir::HirBlock& block) {
        // そのまま文を処理
        for (const auto& stmt : block.stmts) {
            lower_statement(ctx, *stmt);
        }
    }

    // 式のlowering（結果を一時変数に格納し、そのLocalIdを返す）
    LocalId lower_expr(FunctionContext& ctx, const hir::HirExpr& expr) {
        if (auto* lit = std::get_if<std::unique_ptr<hir::HirLiteral>>(&expr.kind)) {
            return lower_literal(ctx, **lit, expr.type);
        } else if (auto* var = std::get_if<std::unique_ptr<hir::HirVarRef>>(&expr.kind)) {
            return lower_var_ref(ctx, **var, expr.type);
        } else if (auto* binary = std::get_if<std::unique_ptr<hir::HirBinary>>(&expr.kind)) {
            return lower_binary(ctx, **binary, expr.type);
        } else if (auto* unary = std::get_if<std::unique_ptr<hir::HirUnary>>(&expr.kind)) {
            return lower_unary(ctx, **unary, expr.type);
        } else if (auto* call = std::get_if<std::unique_ptr<hir::HirCall>>(&expr.kind)) {
            return lower_call(ctx, **call, expr.type);
        } else if (auto* ternary = std::get_if<std::unique_ptr<hir::HirTernary>>(&expr.kind)) {
            return lower_ternary(ctx, **ternary, expr.type);
        }
        // TODO: Index, Member

        // デフォルトは一時変数を返す
        return ctx.new_temp(expr.type);
    }

    // リテラルのlowering
    LocalId lower_literal(FunctionContext& ctx, const hir::HirLiteral& lit, hir::TypePtr type) {
        LocalId temp = ctx.new_temp(type);

        MirConstant constant;
        constant.value = lit.value;
        constant.type = type;

        auto rvalue = MirRvalue::use(MirOperand::constant(std::move(constant)));
        ctx.push_statement(MirStatement::assign(MirPlace(temp), std::move(rvalue)));

        return temp;
    }

    // 変数参照のlowering
    LocalId lower_var_ref(FunctionContext& ctx, const hir::HirVarRef& var, hir::TypePtr type) {
        auto it = ctx.var_map.find(var.name);
        if (it != ctx.var_map.end()) {
            return it->second;
        }

        // 変数が見つからない場合は一時変数を返す
        return ctx.new_temp(type);
    }

    // 二項演算のlowering
    LocalId lower_binary(FunctionContext& ctx, const hir::HirBinary& binary, hir::TypePtr type) {
        // 特別な処理：代入演算
        if (binary.op == hir::HirBinaryOp::Assign) {
            // 右辺を評価
            LocalId rhs_local = lower_expr(ctx, *binary.rhs);

            // 左辺が変数参照の場合
            if (auto* var_ref = std::get_if<std::unique_ptr<hir::HirVarRef>>(&binary.lhs->kind)) {
                auto it = ctx.var_map.find((*var_ref)->name);
                if (it != ctx.var_map.end()) {
                    auto rvalue = MirRvalue::use(MirOperand::copy(MirPlace(rhs_local)));
                    ctx.push_statement(
                        MirStatement::assign(MirPlace(it->second), std::move(rvalue)));
                    return it->second;
                }
            }

            return rhs_local;
        }

        // 通常の二項演算
        LocalId lhs_local = lower_expr(ctx, *binary.lhs);
        LocalId rhs_local = lower_expr(ctx, *binary.rhs);

        LocalId result = ctx.new_temp(type);

        MirBinaryOp mir_op = convert_binary_op(binary.op);
        auto rvalue = MirRvalue::binary(mir_op, MirOperand::copy(MirPlace(lhs_local)),
                                        MirOperand::copy(MirPlace(rhs_local)));
        ctx.push_statement(MirStatement::assign(MirPlace(result), std::move(rvalue)));

        return result;
    }

    // 単項演算のlowering
    LocalId lower_unary(FunctionContext& ctx, const hir::HirUnary& unary, hir::TypePtr type) {
        LocalId operand_local = lower_expr(ctx, *unary.operand);
        LocalId result = ctx.new_temp(type);

        MirUnaryOp mir_op = convert_unary_op(unary.op);
        auto rvalue = MirRvalue::unary(mir_op, MirOperand::copy(MirPlace(operand_local)));
        ctx.push_statement(MirStatement::assign(MirPlace(result), std::move(rvalue)));

        return result;
    }

    // 関数呼び出しのlowering
    LocalId lower_call(FunctionContext& ctx, const hir::HirCall& call, hir::TypePtr type) {
        // 引数を評価
        std::vector<MirOperandPtr> args;
        for (const auto& arg : call.args) {
            LocalId arg_local = lower_expr(ctx, *arg);
            args.push_back(MirOperand::copy(MirPlace(arg_local)));
        }

        // 戻り値用の一時変数
        LocalId result = 0;
        std::optional<MirPlace> destination;

        // 戻り値がvoidでない場合のみ一時変数を作成
        if (type && type->kind != hir::TypeKind::Void) {
            result = ctx.new_temp(type);
            destination = MirPlace(result);
        }

        // 次のブロック
        BlockId next_block = ctx.func->add_block();

        // 関数呼び出しのターミネータを設定
        // 関数名を定数として作成
        MirConstant func_name_const;
        func_name_const.value = call.func_name;
        auto func_operand = MirOperand::constant(std::move(func_name_const));

        // CallDataを作成
        MirTerminator::CallData call_data;
        call_data.func = std::move(func_operand);
        call_data.args = std::move(args);
        call_data.destination = destination;
        call_data.success = next_block;
        call_data.unwind = next_block;  // エラー時も同じブロックへ（簡略化）

        auto terminator = std::make_unique<MirTerminator>();
        terminator->kind = MirTerminator::Call;
        terminator->data = std::move(call_data);
        ctx.set_terminator(std::move(terminator));

        // 次のブロックに切り替え
        ctx.switch_to_block(next_block);

        return result;
    }

    // 三項演算子のlowering
    LocalId lower_ternary(FunctionContext& ctx, const hir::HirTernary& ternary, hir::TypePtr type) {
        // 条件を評価
        LocalId cond_local = lower_expr(ctx, *ternary.condition);

        // 結果用の一時変数
        LocalId result = ctx.new_temp(type);

        // ブロックを作成
        BlockId then_block = ctx.func->add_block();
        BlockId else_block = ctx.func->add_block();
        BlockId merge_block = ctx.func->add_block();

        // 分岐
        auto discriminant = MirOperand::copy(MirPlace(cond_local));
        std::vector<std::pair<int64_t, BlockId>> targets = {{1, then_block}};
        ctx.set_terminator(MirTerminator::switch_int(std::move(discriminant), targets, else_block));

        // thenブロック
        ctx.switch_to_block(then_block);
        LocalId then_val = lower_expr(ctx, *ternary.then_expr);
        auto then_rvalue = MirRvalue::use(MirOperand::copy(MirPlace(then_val)));
        ctx.push_statement(MirStatement::assign(MirPlace(result), std::move(then_rvalue)));
        ctx.set_terminator(MirTerminator::goto_block(merge_block));

        // elseブロック
        ctx.switch_to_block(else_block);
        LocalId else_val = lower_expr(ctx, *ternary.else_expr);
        auto else_rvalue = MirRvalue::use(MirOperand::copy(MirPlace(else_val)));
        ctx.push_statement(MirStatement::assign(MirPlace(result), std::move(else_rvalue)));
        ctx.set_terminator(MirTerminator::goto_block(merge_block));

        // mergeブロック
        ctx.switch_to_block(merge_block);

        return result;
    }

    // HIR二項演算子をMIRに変換
    MirBinaryOp convert_binary_op(hir::HirBinaryOp op) {
        switch (op) {
            case hir::HirBinaryOp::Add:
                return MirBinaryOp::Add;
            case hir::HirBinaryOp::Sub:
                return MirBinaryOp::Sub;
            case hir::HirBinaryOp::Mul:
                return MirBinaryOp::Mul;
            case hir::HirBinaryOp::Div:
                return MirBinaryOp::Div;
            case hir::HirBinaryOp::Mod:
                return MirBinaryOp::Mod;
            case hir::HirBinaryOp::BitAnd:
                return MirBinaryOp::BitAnd;
            case hir::HirBinaryOp::BitOr:
                return MirBinaryOp::BitOr;
            case hir::HirBinaryOp::BitXor:
                return MirBinaryOp::BitXor;
            case hir::HirBinaryOp::Shl:
                return MirBinaryOp::Shl;
            case hir::HirBinaryOp::Shr:
                return MirBinaryOp::Shr;
            case hir::HirBinaryOp::And:
                return MirBinaryOp::And;
            case hir::HirBinaryOp::Or:
                return MirBinaryOp::Or;
            case hir::HirBinaryOp::Eq:
                return MirBinaryOp::Eq;
            case hir::HirBinaryOp::Ne:
                return MirBinaryOp::Ne;
            case hir::HirBinaryOp::Lt:
                return MirBinaryOp::Lt;
            case hir::HirBinaryOp::Gt:
                return MirBinaryOp::Gt;
            case hir::HirBinaryOp::Le:
                return MirBinaryOp::Le;
            case hir::HirBinaryOp::Ge:
                return MirBinaryOp::Ge;
            default:
                return MirBinaryOp::Add;  // デフォルト
        }
    }

    // HIR単項演算子をMIRに変換
    MirUnaryOp convert_unary_op(hir::HirUnaryOp op) {
        switch (op) {
            case hir::HirUnaryOp::Neg:
                return MirUnaryOp::Neg;
            case hir::HirUnaryOp::Not:
                return MirUnaryOp::Not;
            case hir::HirUnaryOp::BitNot:
                return MirUnaryOp::BitNot;
            default:
                return MirUnaryOp::Neg;  // デフォルト
        }
    }
};

}  // namespace cm::mir