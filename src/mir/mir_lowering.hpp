#pragma once

#include "../common/debug/mir.hpp"
#include "../frontend/ast/types.hpp"
#include "../hir/hir_nodes.hpp"
#include "mir_nodes.hpp"

#include <optional>
#include <stack>
#include <unordered_map>
#include <variant>

namespace cm::mir {

// ============================================================
// HIR → MIR 変換
// ============================================================
class MirLowering {
   public:
    MirProgram lower(const hir::HirProgram& hir_program) {
        cm::debug::mir::log(cm::debug::mir::Id::LowerStart);

        MirProgram mir_program;
        mir_program.filename = hir_program.filename;

        // まず構造体定義を収集
        for (const auto& decl : hir_program.declarations) {
            if (auto* st = std::get_if<std::unique_ptr<hir::HirStruct>>(&decl->kind)) {
                register_struct(**st);
            }
        }

        for (const auto& decl : hir_program.declarations) {
            if (auto* func = std::get_if<std::unique_ptr<hir::HirFunction>>(&decl->kind)) {
                if (auto mir_func = lower_function(**func)) {
                    mir_program.functions.push_back(std::move(mir_func));
                }
            }
        }

        cm::debug::mir::log(cm::debug::mir::Id::LowerEnd,
                            std::to_string(mir_program.functions.size()) + " functions");
        return mir_program;
    }

   private:
    // 構造体定義情報
    struct StructInfo {
        std::string name;
        std::vector<std::pair<std::string, hir::TypePtr>> fields;  // (field_name, type)
    };
    std::unordered_map<std::string, StructInfo> struct_defs;

    // 構造体定義を登録
    void register_struct(const hir::HirStruct& st) {
        StructInfo info;
        info.name = st.name;
        for (const auto& field : st.fields) {
            info.fields.push_back({field.name, field.type});
        }
        struct_defs[st.name] = std::move(info);
    }

    // フィールドインデックスを取得
    std::optional<FieldId> get_field_index(const std::string& struct_name,
                                           const std::string& field_name) {
        auto it = struct_defs.find(struct_name);
        if (it == struct_defs.end())
            return std::nullopt;
        for (size_t i = 0; i < it->second.fields.size(); ++i) {
            if (it->second.fields[i].first == field_name) {
                return static_cast<FieldId>(i);
            }
        }
        return std::nullopt;
    }

    // ループコンテキスト
    struct LoopContext {
        BlockId header;  // ループヘッダ（continue先）
        BlockId exit;    // ループ出口（break先）
    };

    // 現在の関数コンテキスト
    struct FunctionContext {
        MirFunction* func;
        BlockId current_block;
        std::unordered_map<std::string, LocalId> var_map;  // 変数名 → ローカルID
        LocalId next_temp_id;                              // 次の一時変数ID
        std::vector<LoopContext> loop_stack;  // ループコンテキストのスタック

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
        cm::debug::mir::log(cm::debug::mir::Id::FunctionLower, hir_func.name,
                            cm::debug::Level::Debug);
        cm::debug::mir::log(cm::debug::mir::Id::FunctionAnalyze,
                            "params=" + std::to_string(hir_func.params.size()) +
                                ", stmts=" + std::to_string(hir_func.body.size()),
                            cm::debug::Level::Trace);

        auto mir_func = std::make_unique<MirFunction>();
        mir_func->name = hir_func.name;

        FunctionContext ctx(mir_func.get());

        // 戻り値用のローカル変数 _0 を作成
        ctx.func->return_local = ctx.func->add_local("_0", hir_func.return_type, true, false);
        cm::debug::mir::log(
            cm::debug::mir::Id::LocalAlloc,
            "_0 (return value) : " +
                (hir_func.return_type ? hir::type_to_string(*hir_func.return_type) : "void"),
            cm::debug::Level::Trace);

        // パラメータをローカル変数として登録
        for (const auto& param : hir_func.params) {
            LocalId param_id = ctx.func->add_local(param.name, param.type, true, true);
            ctx.func->arg_locals.push_back(param_id);
            ctx.var_map[param.name] = param_id;
            cm::debug::mir::log(cm::debug::mir::Id::LocalAlloc,
                                param.name + " (param) : " +
                                    (param.type ? hir::type_to_string(*param.type) : "auto") +
                                    " -> _" + std::to_string(param_id),
                                cm::debug::Level::Trace);
        }

        // エントリーブロックを作成
        ctx.func->add_block();  // bb0
        cm::debug::mir::log(cm::debug::mir::Id::BasicBlockCreate, "bb0 (entry)",
                            cm::debug::Level::Trace);

        // 関数本体を変換
        for (size_t i = 0; i < hir_func.body.size(); i++) {
            cm::debug::mir::log(cm::debug::mir::Id::StatementLower,
                                "stmt[" + std::to_string(i) + "]", cm::debug::Level::Trace);
            lower_statement(ctx, *hir_func.body[i]);
        }

        // 現在のブロックに終端がない場合、returnを追加
        if (auto* block = ctx.get_current_block()) {
            if (!block->terminator) {
                cm::debug::mir::log(cm::debug::mir::Id::InstReturn, "Adding implicit return",
                                    cm::debug::Level::Trace);
                block->set_terminator(MirTerminator::return_value());
            }
        }

        // CFGを構築
        cm::debug::mir::log(cm::debug::mir::Id::CFGBuild,
                            "blocks=" + std::to_string(mir_func->basic_blocks.size()),
                            cm::debug::Level::Trace);
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
        } else if (auto* while_stmt = std::get_if<std::unique_ptr<hir::HirWhile>>(&stmt.kind)) {
            lower_while_stmt(ctx, **while_stmt);
        } else if (auto* for_stmt = std::get_if<std::unique_ptr<hir::HirFor>>(&stmt.kind)) {
            lower_for_stmt(ctx, **for_stmt);
        } else if (auto* switch_stmt = std::get_if<std::unique_ptr<hir::HirSwitch>>(&stmt.kind)) {
            lower_switch_stmt(ctx, **switch_stmt);
        } else if (auto* expr_stmt = std::get_if<std::unique_ptr<hir::HirExprStmt>>(&stmt.kind)) {
            lower_expr(ctx, *(*expr_stmt)->expr);
        } else if (auto* block = std::get_if<std::unique_ptr<hir::HirBlock>>(&stmt.kind)) {
            lower_block_stmt(ctx, **block);
        } else if (std::get_if<std::unique_ptr<hir::HirBreak>>(&stmt.kind)) {
            // break文の処理
            if (!ctx.loop_stack.empty()) {
                BlockId exit = ctx.loop_stack.back().exit;
                ctx.set_terminator(MirTerminator::goto_block(exit));
                // 新しいブロックを作成（到達不可能だが必要）
                ctx.switch_to_block(ctx.func->add_block());
            }
        } else if (std::get_if<std::unique_ptr<hir::HirContinue>>(&stmt.kind)) {
            // continue文の処理
            if (!ctx.loop_stack.empty()) {
                BlockId header = ctx.loop_stack.back().header;
                ctx.set_terminator(MirTerminator::goto_block(header));
                // 新しいブロックを作成（到達不可能だが必要）
                ctx.switch_to_block(ctx.func->add_block());
            }
        }
    }

    // let文のlowering
    void lower_let_stmt(FunctionContext& ctx, const hir::HirLet& let_stmt) {
        cm::debug::mir::log(cm::debug::mir::Id::StatementLower,
                            "let " + let_stmt.name + (let_stmt.is_const ? " (const)" : ""),
                            cm::debug::Level::Debug);

        // 新しいローカル変数を作成
        LocalId local_id =
            ctx.func->add_local(let_stmt.name, let_stmt.type, !let_stmt.is_const, true);
        ctx.var_map[let_stmt.name] = local_id;
        cm::debug::mir::log(cm::debug::mir::Id::LocalAlloc,
                            let_stmt.name + " -> _" + std::to_string(local_id) +
                                (let_stmt.type ? " : " + hir::type_to_string(*let_stmt.type) : ""),
                            cm::debug::Level::Trace);

        // StorageLive
        ctx.push_statement(MirStatement::storage_live(local_id));
        cm::debug::mir::log(cm::debug::mir::Id::StorageLive, "_" + std::to_string(local_id),
                            cm::debug::Level::Trace);

        // 初期値がある場合は代入
        if (let_stmt.init) {
            cm::debug::mir::log(cm::debug::mir::Id::InitExpr,
                                "Evaluating initializer for " + let_stmt.name,
                                cm::debug::Level::Trace);
            LocalId init_local = lower_expr(ctx, *let_stmt.init);

            // place = use(init_local)
            auto rvalue = MirRvalue::use(MirOperand::copy(MirPlace(init_local)));
            ctx.push_statement(MirStatement::assign(MirPlace(local_id), std::move(rvalue)));
            cm::debug::mir::log(
                cm::debug::mir::Id::InstStore,
                "_" + std::to_string(local_id) + " = _" + std::to_string(init_local),
                cm::debug::Level::Trace);
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
        cm::debug::mir::log(cm::debug::mir::Id::BasicBlockCreate, "loop_header",
                            cm::debug::Level::Trace);
        // ループヘッダとループ本体ブロックを作成
        BlockId loop_header = ctx.func->add_block();
        BlockId loop_exit = ctx.func->add_block();

        // ループヘッダへジャンプ
        ctx.set_terminator(MirTerminator::goto_block(loop_header));
        ctx.switch_to_block(loop_header);

        // ループコンテキストをスタックにプッシュ
        ctx.loop_stack.push_back({loop_header, loop_exit});

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

        // ループコンテキストをポップ
        ctx.loop_stack.pop_back();

        // ループ出口に切り替え
        ctx.switch_to_block(loop_exit);
    }

    // while文のlowering
    void lower_while_stmt(FunctionContext& ctx, const hir::HirWhile& while_stmt) {
        // ブロックを作成: ループヘッダ（条件チェック）、ループ本体、ループ出口
        BlockId loop_header = ctx.func->add_block();
        BlockId loop_body = ctx.func->add_block();
        BlockId loop_exit = ctx.func->add_block();

        // ループヘッダへジャンプ
        ctx.set_terminator(MirTerminator::goto_block(loop_header));
        ctx.switch_to_block(loop_header);

        // 条件式を評価
        LocalId cond_local = lower_expr(ctx, *while_stmt.cond);

        // 条件分岐: trueならloop_body、falseならloop_exit
        auto discriminant = MirOperand::copy(MirPlace(cond_local));
        std::vector<std::pair<int64_t, BlockId>> targets = {{1, loop_body}};  // true -> loop_body
        ctx.set_terminator(MirTerminator::switch_int(std::move(discriminant), targets, loop_exit));

        // ループコンテキストをスタックにプッシュ
        ctx.loop_stack.push_back({loop_header, loop_exit});

        // ループ本体を処理
        ctx.switch_to_block(loop_body);
        for (const auto& stmt : while_stmt.body) {
            lower_statement(ctx, *stmt);
        }

        // ループヘッダへ戻る（終端がない場合）
        if (auto* block = ctx.get_current_block()) {
            if (!block->terminator) {
                ctx.set_terminator(MirTerminator::goto_block(loop_header));
            }
        }

        // ループコンテキストをポップ
        ctx.loop_stack.pop_back();

        // ループ出口に切り替え
        ctx.switch_to_block(loop_exit);
    }

    // for文のlowering
    void lower_for_stmt(FunctionContext& ctx, const hir::HirFor& for_stmt) {
        // ブロックを作成: 初期化後、ループヘッダ（条件チェック）、ループ本体、更新部、ループ出口
        BlockId loop_header = ctx.func->add_block();
        BlockId loop_body = ctx.func->add_block();
        BlockId loop_update = ctx.func->add_block();
        BlockId loop_exit = ctx.func->add_block();

        // 初期化を処理
        if (for_stmt.init) {
            lower_statement(ctx, *for_stmt.init);
        }

        // ループヘッダへジャンプ
        ctx.set_terminator(MirTerminator::goto_block(loop_header));
        ctx.switch_to_block(loop_header);

        // 条件式を評価
        if (for_stmt.cond) {
            LocalId cond_local = lower_expr(ctx, *for_stmt.cond);

            // 条件分岐: trueならloop_body、falseならloop_exit
            auto discriminant = MirOperand::copy(MirPlace(cond_local));
            std::vector<std::pair<int64_t, BlockId>> targets = {
                {1, loop_body}};  // true -> loop_body
            ctx.set_terminator(
                MirTerminator::switch_int(std::move(discriminant), targets, loop_exit));
        } else {
            // 条件がない場合は常にloop_bodyへ
            ctx.set_terminator(MirTerminator::goto_block(loop_body));
        }

        // ループコンテキストをスタックにプッシュ（continueはloop_updateへ）
        ctx.loop_stack.push_back({loop_update, loop_exit});

        // ループ本体を処理
        ctx.switch_to_block(loop_body);
        for (const auto& stmt : for_stmt.body) {
            lower_statement(ctx, *stmt);
        }

        // 更新部へジャンプ（終端がない場合）
        if (auto* block = ctx.get_current_block()) {
            if (!block->terminator) {
                ctx.set_terminator(MirTerminator::goto_block(loop_update));
            }
        }

        // 更新部を処理
        ctx.switch_to_block(loop_update);
        if (for_stmt.update) {
            lower_expr(ctx, *for_stmt.update);
        }

        // ループヘッダへ戻る
        ctx.set_terminator(MirTerminator::goto_block(loop_header));

        // ループコンテキストをポップ
        ctx.loop_stack.pop_back();

        // ループ出口に切り替え
        ctx.switch_to_block(loop_exit);
    }

    // switch文のlowering（パターン展開とスコープ管理付き）
    void lower_switch_stmt(FunctionContext& ctx, const hir::HirSwitch& switch_stmt) {
        // switch式を評価
        LocalId expr_local = lower_expr(ctx, *switch_stmt.expr);

        // 終了ブロックを作成
        BlockId merge_block = ctx.func->add_block();

        // 各caseのブロックを作成
        std::vector<BlockId> case_blocks;
        for (size_t i = 0; i < switch_stmt.cases.size(); i++) {
            case_blocks.push_back(ctx.func->add_block());
        }

        // else/defaultブロックを事前に特定
        BlockId else_block = merge_block;  // デフォルトはmergeブロックへ
        for (size_t i = 0; i < switch_stmt.cases.size(); i++) {
            if (!switch_stmt.cases[i].pattern && !switch_stmt.cases[i].value) {
                else_block = case_blocks[i];
                break;
            }
        }

        // 各caseのチェックブロックを作成
        std::vector<BlockId> check_blocks;
        for (size_t i = 0; i < switch_stmt.cases.size(); i++) {
            if (switch_stmt.cases[i].pattern || switch_stmt.cases[i].value) {
                check_blocks.push_back(ctx.func->add_block());
            } else {
                check_blocks.push_back(INVALID_BLOCK);  // elseケースはチェック不要
            }
        }

        // 現在のブロック（エントリーブロック）を保存
        BlockId entry_block = ctx.current_block;

        // パターンマッチングの生成（前から順に処理）
        for (size_t i = 0; i < switch_stmt.cases.size(); i++) {
            const auto& case_ = switch_stmt.cases[i];

            // elseケースの場合はスキップ（既に処理済み）
            if (!case_.pattern && !case_.value) {
                continue;
            }

            // 次のチェックブロックまたはelse_blockを決定
            BlockId next_block = else_block;
            for (size_t j = i + 1; j < switch_stmt.cases.size(); j++) {
                if (check_blocks[j] != INVALID_BLOCK) {
                    next_block = check_blocks[j];
                    break;
                }
            }

            // チェックブロックに切り替え
            ctx.switch_to_block(check_blocks[i]);

            // パターンに応じた条件生成
            if (case_.pattern) {
                generate_pattern_check(ctx, *case_.pattern, expr_local, case_blocks[i], next_block);
            } else if (case_.value) {
                // 後方互換性: 旧式の単一値パターン
                generate_simple_value_check(ctx, *case_.value, expr_local, case_blocks[i],
                                            next_block);
            }
        }

        // エントリーブロックに戻る
        ctx.switch_to_block(entry_block);

        // エントリーから最初のチェックへジャンプ
        BlockId first_check = else_block;
        for (size_t i = 0; i < check_blocks.size(); i++) {
            if (check_blocks[i] != INVALID_BLOCK) {
                first_check = check_blocks[i];
                break;
            }
        }
        ctx.set_terminator(MirTerminator::goto_block(first_check));

        // 各caseブロックの本体を処理
        for (size_t i = 0; i < switch_stmt.cases.size(); i++) {
            ctx.switch_to_block(case_blocks[i]);

            // スコープ開始（各caseは独立したスコープ）
            // TODO: より精密な実装では、このスコープで宣言される変数を追跡
            // size_t scope_start_vars = ctx.var_map.size(); // 将来の実装で使用

            // case内の文を処理
            for (const auto& stmt : switch_stmt.cases[i].stmts) {
                lower_statement(ctx, *stmt);
            }

            // 自動的にbreak（フォールスルーなし）
            if (auto* block = ctx.get_current_block()) {
                if (!block->terminator) {
                    // スコープ終了処理
                    // TODO: スコープ内で宣言された変数にStorageDeadを発行
                    // 現在は簡易実装（将来的にdeferやデストラクタの処理も必要）

                    ctx.set_terminator(MirTerminator::goto_block(merge_block));
                }
            }

            // スコープ終了
            // 注: 完全な実装にはStorageDeadの発行が必要
        }

        // mergeブロックに切り替え
        ctx.switch_to_block(merge_block);
    }

    // 単純な値チェック（後方互換性）
    void generate_simple_value_check(FunctionContext& ctx, const hir::HirExpr& value_expr,
                                     LocalId expr_local, BlockId match_block, BlockId else_block) {
        LocalId value_local = lower_expr(ctx, value_expr);
        LocalId cmp_result = ctx.new_temp(hir::make_bool());

        // 比較演算
        auto cmp_rvalue = MirRvalue::binary(MirBinaryOp::Eq, MirOperand::copy(MirPlace(expr_local)),
                                            MirOperand::copy(MirPlace(value_local)));
        ctx.push_statement(MirStatement::assign(MirPlace(cmp_result), std::move(cmp_rvalue)));

        // 条件分岐
        auto discriminant = MirOperand::copy(MirPlace(cmp_result));
        std::vector<std::pair<int64_t, BlockId>> targets = {{1, match_block}};
        ctx.set_terminator(MirTerminator::switch_int(std::move(discriminant), targets, else_block));
    }

    // パターンチェックの生成
    void generate_pattern_check(FunctionContext& ctx, const hir::HirSwitchPattern& pattern,
                                LocalId expr_local, BlockId match_block, BlockId else_block) {
        switch (pattern.kind) {
            case hir::HirSwitchPattern::SingleValue: {
                // 単一値: expr == value
                if (pattern.value) {
                    LocalId value_local = lower_expr(ctx, *pattern.value);
                    LocalId cmp_result = ctx.new_temp(hir::make_bool());

                    // 比較演算
                    auto cmp_rvalue =
                        MirRvalue::binary(MirBinaryOp::Eq, MirOperand::copy(MirPlace(expr_local)),
                                          MirOperand::copy(MirPlace(value_local)));
                    ctx.push_statement(
                        MirStatement::assign(MirPlace(cmp_result), std::move(cmp_rvalue)));

                    // 条件分岐
                    auto discriminant = MirOperand::copy(MirPlace(cmp_result));
                    std::vector<std::pair<int64_t, BlockId>> targets = {{1, match_block}};
                    ctx.set_terminator(
                        MirTerminator::switch_int(std::move(discriminant), targets, else_block));
                }
                break;
            }

            case hir::HirSwitchPattern::Range: {
                // 範囲: start <= expr && expr <= end
                if (pattern.range_start && pattern.range_end) {
                    LocalId start_local = lower_expr(ctx, *pattern.range_start);
                    LocalId end_local = lower_expr(ctx, *pattern.range_end);

                    // expr >= start
                    LocalId ge_result = ctx.new_temp(hir::make_bool());
                    auto ge_rvalue =
                        MirRvalue::binary(MirBinaryOp::Ge, MirOperand::copy(MirPlace(expr_local)),
                                          MirOperand::copy(MirPlace(start_local)));
                    ctx.push_statement(
                        MirStatement::assign(MirPlace(ge_result), std::move(ge_rvalue)));

                    // expr <= end
                    LocalId le_result = ctx.new_temp(hir::make_bool());
                    auto le_rvalue =
                        MirRvalue::binary(MirBinaryOp::Le, MirOperand::copy(MirPlace(expr_local)),
                                          MirOperand::copy(MirPlace(end_local)));
                    ctx.push_statement(
                        MirStatement::assign(MirPlace(le_result), std::move(le_rvalue)));

                    // ge_result && le_result
                    LocalId and_result = ctx.new_temp(hir::make_bool());
                    auto and_rvalue =
                        MirRvalue::binary(MirBinaryOp::And, MirOperand::copy(MirPlace(ge_result)),
                                          MirOperand::copy(MirPlace(le_result)));
                    ctx.push_statement(
                        MirStatement::assign(MirPlace(and_result), std::move(and_rvalue)));

                    // 条件分岐
                    auto discriminant = MirOperand::copy(MirPlace(and_result));
                    std::vector<std::pair<int64_t, BlockId>> targets = {{1, match_block}};
                    ctx.set_terminator(
                        MirTerminator::switch_int(std::move(discriminant), targets, else_block));
                }
                break;
            }

            case hir::HirSwitchPattern::Or: {
                // OR: pattern1 || pattern2 || ...
                // 各パターンをインラインで展開し、順次チェック

                // 最後のパターン以外を処理
                for (size_t i = 0; i < pattern.or_patterns.size() - 1; i++) {
                    // 単一値パターンのみサポート（簡易実装）
                    if (pattern.or_patterns[i]->kind == hir::HirSwitchPattern::SingleValue) {
                        LocalId value_local = lower_expr(ctx, *pattern.or_patterns[i]->value);
                        LocalId cmp_result = ctx.new_temp(hir::make_bool());

                        // 比較演算
                        auto cmp_rvalue = MirRvalue::binary(
                            MirBinaryOp::Eq, MirOperand::copy(MirPlace(expr_local)),
                            MirOperand::copy(MirPlace(value_local)));
                        ctx.push_statement(
                            MirStatement::assign(MirPlace(cmp_result), std::move(cmp_rvalue)));

                        // 次のチェック用ブロック
                        BlockId next_check_block = ctx.func->add_block();

                        // 条件分岐（マッチしたらmatch_block、しなければ次のチェックへ）
                        auto discriminant = MirOperand::copy(MirPlace(cmp_result));
                        std::vector<std::pair<int64_t, BlockId>> targets = {{1, match_block}};
                        ctx.set_terminator(MirTerminator::switch_int(std::move(discriminant),
                                                                     targets, next_check_block));

                        // 次のチェックブロックに移動
                        ctx.switch_to_block(next_check_block);
                    }
                }

                // 最後のパターンを処理（else_blockへの分岐を含む）
                if (!pattern.or_patterns.empty()) {
                    size_t last_idx = pattern.or_patterns.size() - 1;
                    if (pattern.or_patterns[last_idx]->kind == hir::HirSwitchPattern::SingleValue) {
                        LocalId value_local =
                            lower_expr(ctx, *pattern.or_patterns[last_idx]->value);
                        LocalId cmp_result = ctx.new_temp(hir::make_bool());

                        // 比較演算
                        auto cmp_rvalue = MirRvalue::binary(
                            MirBinaryOp::Eq, MirOperand::copy(MirPlace(expr_local)),
                            MirOperand::copy(MirPlace(value_local)));
                        ctx.push_statement(
                            MirStatement::assign(MirPlace(cmp_result), std::move(cmp_rvalue)));

                        // 最後の条件分岐（マッチしたらmatch_block、しなければelse_block）
                        auto discriminant = MirOperand::copy(MirPlace(cmp_result));
                        std::vector<std::pair<int64_t, BlockId>> targets = {{1, match_block}};
                        ctx.set_terminator(MirTerminator::switch_int(std::move(discriminant),
                                                                     targets, else_block));
                    }
                }
                break;
            }
        }
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
        cm::debug::mir::log(cm::debug::mir::Id::ExprLower, "", cm::debug::Level::Trace);

        if (auto* lit = std::get_if<std::unique_ptr<hir::HirLiteral>>(&expr.kind)) {
            cm::debug::mir::log(cm::debug::mir::Id::LiteralExpr, "Literal",
                                cm::debug::Level::Trace);
            return lower_literal(ctx, **lit, expr.type);
        } else if (auto* var = std::get_if<std::unique_ptr<hir::HirVarRef>>(&expr.kind)) {
            cm::debug::mir::log(cm::debug::mir::Id::VarRef, (*var)->name, cm::debug::Level::Debug);
            return lower_var_ref(ctx, **var, expr.type);
        } else if (auto* binary = std::get_if<std::unique_ptr<hir::HirBinary>>(&expr.kind)) {
            cm::debug::mir::log(cm::debug::mir::Id::InstBinary, "Binary op",
                                cm::debug::Level::Debug);
            return lower_binary(ctx, **binary, expr.type);
        } else if (auto* unary = std::get_if<std::unique_ptr<hir::HirUnary>>(&expr.kind)) {
            cm::debug::mir::log(cm::debug::mir::Id::InstUnary, "Unary op", cm::debug::Level::Debug);
            return lower_unary(ctx, **unary, expr.type);
        } else if (auto* call = std::get_if<std::unique_ptr<hir::HirCall>>(&expr.kind)) {
            cm::debug::mir::log(cm::debug::mir::Id::InstCall, (*call)->func_name,
                                cm::debug::Level::Debug);
            return lower_call(ctx, **call, expr.type);
        } else if (auto* ternary = std::get_if<std::unique_ptr<hir::HirTernary>>(&expr.kind)) {
            cm::debug::mir::log(cm::debug::mir::Id::TernaryExpr, "Ternary",
                                cm::debug::Level::Debug);
            return lower_ternary(ctx, **ternary, expr.type);
        } else if (auto* member = std::get_if<std::unique_ptr<hir::HirMember>>(&expr.kind)) {
            cm::debug::mir::log(cm::debug::mir::Id::FieldAccess, (*member)->member,
                                cm::debug::Level::Debug);
            return lower_member(ctx, **member, expr.type);
        }

        // デフォルトは一時変数を返す
        cm::debug::mir::log(cm::debug::mir::Id::Warning, "Unknown expression type",
                            cm::debug::Level::Warn);
        return ctx.new_temp(expr.type);
    }

    // メンバーアクセスのlowering
    LocalId lower_member(FunctionContext& ctx, const hir::HirMember& member, hir::TypePtr type) {
        // オブジェクトを評価
        LocalId obj_local = lower_expr(ctx, *member.object);

        // オブジェクトの型から構造体名を取得
        std::string struct_name;
        if (member.object->type && member.object->type->kind == ast::TypeKind::Struct) {
            struct_name = member.object->type->name;
        }

        // 変数参照の場合、変数の型を直接確認
        if (struct_name.empty()) {
            if (auto* var_ref =
                    std::get_if<std::unique_ptr<hir::HirVarRef>>(&member.object->kind)) {
                auto it = ctx.var_map.find((*var_ref)->name);
                if (it != ctx.var_map.end()) {
                    LocalId local_id = it->second;
                    if (local_id < ctx.func->locals.size()) {
                        const auto& local = ctx.func->locals[local_id];
                        if (local.type && local.type->kind == ast::TypeKind::Struct) {
                            struct_name = local.type->name;
                        }
                    }
                }
            }
        }

        // フィールドインデックスを取得
        auto field_idx = get_field_index(struct_name, member.member);
        if (!field_idx) {
            // フィールドが見つからない場合はダミーの一時変数を返す
            return ctx.new_temp(type);
        }

        // MirPlaceにフィールドプロジェクションを追加
        MirPlace place(obj_local, {PlaceProjection::field(*field_idx)});

        // 結果を一時変数にコピー
        LocalId result = ctx.new_temp(type);
        auto rvalue = MirRvalue::use(MirOperand::copy(place));
        ctx.push_statement(MirStatement::assign(MirPlace(result), std::move(rvalue)));

        return result;
    }

    // 文字列補間を処理するヘルパー関数
    LocalId process_string_interpolation(FunctionContext& ctx, const std::string& str,
                                         hir::TypePtr type) {
        // エスケープシーケンスを一時的にプレースホルダーに置換
        std::string processed = str;

        // {{ -> \x01LBRACE\x02
        size_t pos = 0;
        while ((pos = processed.find("{{", pos)) != std::string::npos) {
            processed.replace(pos, 2, "\x01LBRACE\x02");
            pos += 8;  // プレースホルダーは8バイト
        }

        // }} -> \x01RBRACE\x02
        pos = 0;
        while ((pos = processed.find("}}", pos)) != std::string::npos) {
            processed.replace(pos, 2, "\x01RBRACE\x02");
            pos += 8;  // プレースホルダーは8バイト
        }

        // 構造体：テキスト、変数名、フォーマット指定子
        struct InterpolationPart {
            std::string text;
            std::string var_name;
            std::string format_spec;
        };
        std::vector<InterpolationPart> parts;
        std::string current_text;
        pos = 0;

        while (pos < processed.length()) {
            size_t brace_start = processed.find('{', pos);

            if (brace_start == std::string::npos) {
                // 残りのテキストを追加
                current_text += processed.substr(pos);
                if (!current_text.empty() || parts.empty()) {
                    parts.push_back({current_text, "", ""});
                }
                break;
            }

            // { の前のテキストを追加
            current_text += processed.substr(pos, brace_start - pos);

            // } を探す
            size_t brace_end = processed.find('}', brace_start);
            if (brace_end == std::string::npos) {
                // } が見つからない場合は、{ を通常の文字として扱う
                current_text += '{';
                pos = brace_start + 1;
                continue;
            }

            // 変数名とフォーマット指定子を抽出
            std::string var_content =
                processed.substr(brace_start + 1, brace_end - brace_start - 1);

            // フォーマット指定子を分離
            size_t colon_pos = var_content.find(':');
            std::string var_name;
            std::string format_spec;

            if (colon_pos != std::string::npos) {
                var_name = var_content.substr(0, colon_pos);
                format_spec = var_content.substr(colon_pos + 1);
            } else {
                var_name = var_content;
                format_spec = "";
            }

            // 変数名が有効かチェック
            // 空の{}や数字で始まる場合は変数埋め込みとして扱わない
            if (!var_content.empty() && !std::isdigit(var_content[0]) &&
                var_name.find_first_not_of(" \t\n\r") != std::string::npos) {
                // テキスト部分があれば追加
                if (!current_text.empty()) {
                    parts.push_back({current_text, "", ""});
                    current_text.clear();
                }
                // 変数部分を追加（フォーマット指定子付き）
                parts.push_back({"", var_name, format_spec});
            } else {
                // 無効な変数名の場合は、そのまま文字列として扱う
                current_text += processed.substr(brace_start, brace_end - brace_start + 1);
            }

            pos = brace_end + 1;
        }

        // プレースホルダーを元の文字に戻す関数
        auto restore_placeholders = [](std::string& text) {
            size_t ph_pos = 0;
            while ((ph_pos = text.find("\x01LBRACE\x02", ph_pos)) != std::string::npos) {
                text.replace(ph_pos, 8, "{");  // 8バイトを1文字に置換
                ph_pos += 1;
            }
            ph_pos = 0;
            while ((ph_pos = text.find("\x01RBRACE\x02", ph_pos)) != std::string::npos) {
                text.replace(ph_pos, 8, "}");  // 8バイトを1文字に置換
                ph_pos += 1;
            }
        };

        // すべてのパーツのプレースホルダーを元に戻す
        for (auto& part : parts) {
            restore_placeholders(part.text);
        }

        // 補間がない場合は通常のリテラルとして処理
        if (parts.size() == 1 && parts[0].var_name.empty()) {
            LocalId temp = ctx.new_temp(type);
            MirConstant constant;
            constant.value = parts[0].text;
            constant.type = type;
            auto rvalue = MirRvalue::use(MirOperand::constant(std::move(constant)));
            ctx.push_statement(MirStatement::assign(MirPlace(temp), std::move(rvalue)));
            return temp;
        }

        // 文字列連結を生成
        LocalId result = 0;
        for (size_t i = 0; i < parts.size(); ++i) {
            LocalId current;

            if (!parts[i].var_name.empty()) {
                // メンバーアクセス（"obj.field"形式）かどうかチェック
                size_t dot_pos = parts[i].var_name.find('.');
                if (dot_pos != std::string::npos) {
                    // メンバーアクセス
                    std::string obj_name = parts[i].var_name.substr(0, dot_pos);
                    std::string field_name = parts[i].var_name.substr(dot_pos + 1);

                    auto it = ctx.var_map.find(obj_name);
                    if (it != ctx.var_map.end()) {
                        LocalId obj_local = it->second;

                        // オブジェクトの型を取得
                        std::string struct_name;
                        if (obj_local < ctx.func->locals.size()) {
                            const auto& local = ctx.func->locals[obj_local];
                            if (local.type && local.type->kind == ast::TypeKind::Struct) {
                                struct_name = local.type->name;
                            }
                        }

                        // フィールドインデックスを取得
                        auto field_idx = get_field_index(struct_name, field_name);
                        if (field_idx) {
                            // フィールドから値を取得
                            MirPlace place(obj_local, {PlaceProjection::field(*field_idx)});
                            current = ctx.new_temp(type);

                            // フォーマット指定子がある場合
                            if (!parts[i].format_spec.empty()) {
                                LocalId field_val = ctx.new_temp(type);
                                auto use_rvalue = MirRvalue::use(MirOperand::copy(place));
                                ctx.push_statement(MirStatement::assign(MirPlace(field_val),
                                                                        std::move(use_rvalue)));

                                auto format_rvalue = MirRvalue::format_convert(
                                    MirOperand::copy(MirPlace(field_val)), parts[i].format_spec);
                                ctx.push_statement(MirStatement::assign(MirPlace(current),
                                                                        std::move(format_rvalue)));
                            } else {
                                auto use_rvalue = MirRvalue::use(MirOperand::copy(place));
                                ctx.push_statement(
                                    MirStatement::assign(MirPlace(current), std::move(use_rvalue)));
                            }
                        } else {
                            // フィールドが見つからない
                            current = ctx.new_temp(type);
                            MirConstant constant;
                            constant.value = std::string("{missing}");
                            constant.type = type;
                            auto rvalue = MirRvalue::use(MirOperand::constant(std::move(constant)));
                            ctx.push_statement(
                                MirStatement::assign(MirPlace(current), std::move(rvalue)));
                        }
                    } else {
                        // オブジェクトが見つからない
                        current = ctx.new_temp(type);
                        MirConstant constant;
                        constant.value = std::string("{missing}");
                        constant.type = type;
                        auto rvalue = MirRvalue::use(MirOperand::constant(std::move(constant)));
                        ctx.push_statement(
                            MirStatement::assign(MirPlace(current), std::move(rvalue)));
                    }
                } else {
                    // 通常の変数参照
                    auto it = ctx.var_map.find(parts[i].var_name);
                    if (it != ctx.var_map.end()) {
                        // フォーマット指定子がある場合はFormatConvert演算を生成
                        if (!parts[i].format_spec.empty()) {
                            current = ctx.new_temp(type);
                            auto format_rvalue = MirRvalue::format_convert(
                                MirOperand::copy(MirPlace(it->second)), parts[i].format_spec);
                            ctx.push_statement(
                                MirStatement::assign(MirPlace(current), std::move(format_rvalue)));
                        } else {
                            current = it->second;
                        }
                    } else {
                        // 変数が見つからない場合、{missing}
                        current = ctx.new_temp(type);
                        MirConstant constant;
                        constant.value = std::string("{missing}");
                        constant.type = type;
                        auto rvalue = MirRvalue::use(MirOperand::constant(std::move(constant)));
                        ctx.push_statement(
                            MirStatement::assign(MirPlace(current), std::move(rvalue)));
                    }
                }
            } else {
                // テキストリテラル
                current = ctx.new_temp(type);
                MirConstant constant;
                constant.value = parts[i].text;
                constant.type = type;
                auto rvalue = MirRvalue::use(MirOperand::constant(std::move(constant)));
                ctx.push_statement(MirStatement::assign(MirPlace(current), std::move(rvalue)));
            }

            if (i == 0) {
                result = current;
            } else {
                // 文字列連結
                LocalId concat_result = ctx.new_temp(type);
                auto concat_rvalue =
                    MirRvalue::binary(MirBinaryOp::Add, MirOperand::copy(MirPlace(result)),
                                      MirOperand::copy(MirPlace(current)));
                ctx.push_statement(
                    MirStatement::assign(MirPlace(concat_result), std::move(concat_rvalue)));
                result = concat_result;
            }
        }

        return result;
    }

    // リテラルのlowering
    LocalId lower_literal(FunctionContext& ctx, const hir::HirLiteral& lit, hir::TypePtr type) {
        // 文字列リテラルの場合、変数埋め込みがあるかチェック
        if (auto* str_val = std::get_if<std::string>(&lit.value)) {
            // 変数埋め込みパターン {varname} があり、その変数が存在する場合のみ処理
            bool has_valid_interpolation = false;
            size_t pos = 0;
            while ((pos = str_val->find('{', pos)) != std::string::npos) {
                // {{ はエスケープなのでスキップ
                if (pos + 1 < str_val->size() && (*str_val)[pos + 1] == '{') {
                    pos += 2;
                    continue;
                }

                size_t end = str_val->find('}', pos);
                if (end != std::string::npos) {
                    std::string content = str_val->substr(pos + 1, end - pos - 1);
                    // :がある場合は前の部分を変数名とする
                    size_t colon = content.find(':');
                    std::string var_name =
                        (colon != std::string::npos) ? content.substr(0, colon) : content;

                    // メンバーアクセスの場合はオブジェクト名を抽出
                    size_t dot_pos = var_name.find('.');
                    std::string lookup_name =
                        (dot_pos != std::string::npos) ? var_name.substr(0, dot_pos) : var_name;

                    // 空でない変数名で、数字で始まらず、実際に変数が存在する場合
                    if (!lookup_name.empty() && !std::isdigit(lookup_name[0]) &&
                        ctx.var_map.find(lookup_name) != ctx.var_map.end()) {
                        has_valid_interpolation = true;
                        break;
                    }
                }
                pos++;
            }

            // 有効な変数埋め込みがある場合のみ処理
            if (has_valid_interpolation) {
                return process_string_interpolation(ctx, *str_val, type);
            }
        }

        // 通常のリテラル処理
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
            // 左辺がメンバーアクセスの場合
            else if (auto* member =
                         std::get_if<std::unique_ptr<hir::HirMember>>(&binary.lhs->kind)) {
                // オブジェクトを評価
                LocalId obj_local = lower_expr(ctx, *(*member)->object);

                // オブジェクトの型から構造体名を取得
                std::string struct_name;
                if ((*member)->object->type &&
                    (*member)->object->type->kind == ast::TypeKind::Struct) {
                    struct_name = (*member)->object->type->name;
                }

                // 変数参照の場合、変数の型を直接確認
                if (struct_name.empty()) {
                    if (auto* var_ref = std::get_if<std::unique_ptr<hir::HirVarRef>>(
                            &(*member)->object->kind)) {
                        auto it = ctx.var_map.find((*var_ref)->name);
                        if (it != ctx.var_map.end()) {
                            LocalId local_id = it->second;
                            if (local_id < ctx.func->locals.size()) {
                                const auto& local = ctx.func->locals[local_id];
                                if (local.type && local.type->kind == ast::TypeKind::Struct) {
                                    struct_name = local.type->name;
                                }
                            }
                        }
                    }
                }

                // フィールドインデックスを取得
                auto field_idx = get_field_index(struct_name, (*member)->member);
                if (field_idx) {
                    // MirPlaceにフィールドプロジェクションを追加して代入
                    MirPlace place(obj_local, {PlaceProjection::field(*field_idx)});
                    auto rvalue = MirRvalue::use(MirOperand::copy(MirPlace(rhs_local)));
                    ctx.push_statement(MirStatement::assign(place, std::move(rvalue)));
                    return rhs_local;
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
        // インクリメント/デクリメント演算子の脱糖
        if (unary.op == hir::HirUnaryOp::PreInc || unary.op == hir::HirUnaryOp::PreDec ||
            unary.op == hir::HirUnaryOp::PostInc || unary.op == hir::HirUnaryOp::PostDec) {
            // 変数への参照を取得（現在は変数のみサポート）
            if (auto* var_ref =
                    std::get_if<std::unique_ptr<hir::HirVarRef>>(&unary.operand->kind)) {
                auto it = ctx.var_map.find((*var_ref)->name);
                if (it != ctx.var_map.end()) {
                    LocalId var_local = it->second;
                    LocalId result = ctx.new_temp(type);

                    // 前置か後置かで処理が異なる
                    bool is_post = (unary.op == hir::HirUnaryOp::PostInc ||
                                    unary.op == hir::HirUnaryOp::PostDec);
                    bool is_inc = (unary.op == hir::HirUnaryOp::PreInc ||
                                   unary.op == hir::HirUnaryOp::PostInc);

                    if (is_post) {
                        // 後置: 元の値を保存してから更新
                        auto save_rvalue = MirRvalue::use(MirOperand::copy(MirPlace(var_local)));
                        ctx.push_statement(
                            MirStatement::assign(MirPlace(result), std::move(save_rvalue)));
                    }

                    // 1を表すリテラル
                    LocalId one = ctx.new_temp(type);
                    MirConstant one_const;
                    one_const.value = 1;
                    one_const.type = type;
                    auto one_rvalue = MirRvalue::use(MirOperand::constant(std::move(one_const)));
                    ctx.push_statement(MirStatement::assign(MirPlace(one), std::move(one_rvalue)));

                    // インクリメント/デクリメント実行
                    MirBinaryOp op = is_inc ? MirBinaryOp::Add : MirBinaryOp::Sub;
                    auto update_rvalue = MirRvalue::binary(
                        op, MirOperand::copy(MirPlace(var_local)), MirOperand::copy(MirPlace(one)));
                    ctx.push_statement(
                        MirStatement::assign(MirPlace(var_local), std::move(update_rvalue)));

                    if (!is_post) {
                        // 前置: 更新後の値を返す
                        auto ret_rvalue = MirRvalue::use(MirOperand::copy(MirPlace(var_local)));
                        ctx.push_statement(
                            MirStatement::assign(MirPlace(result), std::move(ret_rvalue)));
                    }

                    return result;
                }
            }

            // 変数以外のインクリメント/デクリメントはエラー
            // TODO: エラー処理
            return ctx.new_temp(type);
        }

        // 通常の単項演算
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
        std::vector<LocalId> arg_locals;  // 引数のLocalIdを保存

        // println/printの文字列補間処理
        if ((call.func_name == "println" || call.func_name == "print") && !call.args.empty()) {
            // 第1引数が文字列リテラルかチェック
            if (auto* lit = std::get_if<std::unique_ptr<hir::HirLiteral>>(&call.args[0]->kind)) {
                if (auto* str_val = std::get_if<std::string>(&(*lit)->value)) {
                    // 文字列補間のプレースホルダーを解析
                    std::string str = *str_val;
                    std::vector<std::string> interpolated_vars;

                    // {変数名}パターンを検出
                    size_t pos = 0;
                    while ((pos = str.find('{', pos)) != std::string::npos) {
                        size_t end = str.find('}', pos);
                        if (end != std::string::npos) {
                            // {{や}}はエスケープなのでスキップ
                            if (pos + 1 < str.size() && str[pos + 1] == '{') {
                                pos += 2;
                                continue;
                            }

                            std::string var_name = str.substr(pos + 1, end - pos - 1);
                            // 変数名が空でなく、コロンを含まない（フォーマット指定がない）場合
                            if (!var_name.empty() && var_name.find(':') == std::string::npos &&
                                !std::isdigit(
                                    var_name[0])) {  // 数字で始まらない（位置引数ではない）
                                interpolated_vars.push_back(var_name);
                            }
                            pos = end + 1;
                        } else {
                            break;
                        }
                    }

                    // 補間変数がある場合は、文字列を直接定数として渡す
                    if (!interpolated_vars.empty()) {
                        // 文字列リテラルを定数として直接引数リストに追加
                        MirConstant str_constant;
                        str_constant.value = str;
                        str_constant.type = call.args[0]->type;
                        args.clear();  // 既存の引数をクリア
                        args.push_back(MirOperand::constant(std::move(str_constant)));

                        // 補間変数を引数として追加
                        for (const auto& var_name : interpolated_vars) {
                            // メンバーアクセス（"obj.field"形式）かどうかチェック
                            size_t dot_pos = var_name.find('.');
                            if (dot_pos != std::string::npos) {
                                // メンバーアクセス
                                std::string obj_name = var_name.substr(0, dot_pos);
                                std::string field_name = var_name.substr(dot_pos + 1);

                                auto it = ctx.var_map.find(obj_name);
                                if (it != ctx.var_map.end()) {
                                    LocalId obj_local = it->second;

                                    // オブジェクトの型を取得
                                    std::string struct_name_for_field;
                                    if (obj_local < ctx.func->locals.size()) {
                                        const auto& local = ctx.func->locals[obj_local];
                                        if (local.type &&
                                            local.type->kind == ast::TypeKind::Struct) {
                                            struct_name_for_field = local.type->name;
                                        }
                                    }

                                    // フィールドインデックスを取得
                                    auto field_idx =
                                        get_field_index(struct_name_for_field, field_name);
                                    if (field_idx) {
                                        // フィールドの値を取得
                                        MirPlace place(obj_local,
                                                       {PlaceProjection::field(*field_idx)});
                                        auto string_type = ast::make_string();
                                        LocalId temp = ctx.new_temp(string_type);
                                        auto use_rvalue = MirRvalue::use(MirOperand::copy(place));
                                        ctx.push_statement(MirStatement::assign(
                                            MirPlace(temp), std::move(use_rvalue)));
                                        args.push_back(MirOperand::copy(MirPlace(temp)));
                                    } else {
                                        // フィールドが見つからない
                                        auto string_type = ast::make_string();
                                        LocalId temp = ctx.new_temp(string_type);
                                        MirConstant constant;
                                        constant.value = std::string("{missing}");
                                        constant.type = string_type;
                                        auto rvalue = MirRvalue::use(
                                            MirOperand::constant(std::move(constant)));
                                        ctx.push_statement(MirStatement::assign(MirPlace(temp),
                                                                                std::move(rvalue)));
                                        args.push_back(MirOperand::copy(MirPlace(temp)));
                                    }
                                } else {
                                    // オブジェクトが見つからない
                                    auto string_type = ast::make_string();
                                    LocalId temp = ctx.new_temp(string_type);
                                    MirConstant constant;
                                    constant.value = std::string("{missing}");
                                    constant.type = string_type;
                                    auto rvalue =
                                        MirRvalue::use(MirOperand::constant(std::move(constant)));
                                    ctx.push_statement(
                                        MirStatement::assign(MirPlace(temp), std::move(rvalue)));
                                    args.push_back(MirOperand::copy(MirPlace(temp)));
                                }
                            } else {
                                // 通常の変数参照
                                auto it = ctx.var_map.find(var_name);
                                if (it != ctx.var_map.end()) {
                                    // 変数が存在する場合、引数として追加
                                    args.push_back(MirOperand::copy(MirPlace(it->second)));
                                } else {
                                    // 変数が見つからない場合、デフォルト値を追加
                                    auto string_type = ast::make_string();
                                    LocalId temp = ctx.new_temp(string_type);
                                    MirConstant constant;
                                    constant.value = std::string("{missing}");
                                    constant.type = string_type;
                                    auto rvalue =
                                        MirRvalue::use(MirOperand::constant(std::move(constant)));
                                    ctx.push_statement(
                                        MirStatement::assign(MirPlace(temp), std::move(rvalue)));
                                    args.push_back(MirOperand::copy(MirPlace(temp)));
                                }
                            }
                        }
                    } else {
                        // 補間がない場合は通常通り処理
                        LocalId arg_local = lower_expr(ctx, *call.args[0]);
                        args.push_back(MirOperand::copy(MirPlace(arg_local)));
                    }

                    // 残りの引数を処理（もしあれば）
                    for (size_t i = 1; i < call.args.size(); ++i) {
                        LocalId arg_local = lower_expr(ctx, *call.args[i]);
                        args.push_back(MirOperand::copy(MirPlace(arg_local)));
                    }

                    // ここで処理完了なので、後続の通常処理をスキップ
                    goto skip_normal_args;
                }
            }
        }

        // 通常の引数処理（文字列補間がない場合）
        for (const auto& arg : call.args) {
            LocalId arg_local = lower_expr(ctx, *arg);
            arg_locals.push_back(arg_local);
            args.push_back(MirOperand::copy(MirPlace(arg_local)));
        }

    skip_normal_args:

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
        // 関数名を関数参照として作成
        // println/printはstd::ioモジュールからインポートされた関数として扱う
        std::string func_name = call.func_name;
        if (func_name == "println") {
            func_name = "std::io::println";
        } else if (func_name == "print") {
            func_name = "std::io::print";
        }
        auto func_operand = MirOperand::function_ref(func_name);

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