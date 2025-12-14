#pragma once

#include "builtins.hpp"
#include "eval.hpp"
#include "types.hpp"

namespace cm::mir::interp {

/// MIRインタープリタのメインクラス
class Interpreter {
   public:
    Interpreter() { builtin_manager_.initialize(); }

    /// MIRプログラムを実行
    ExecutionResult execute(const MirProgram& program, const std::string& entry_point = "main") {
        debug::interp::log(debug::interp::Id::Start, "Starting interpreter execution",
                           debug::Level::Info);

        current_program_ = &program;

        // エントリポイント関数を探す
        const MirFunction* main_func = find_function(entry_point);
        if (!main_func) {
            return ExecutionResult::error("Entry point '" + entry_point + "' not found");
        }

        try {
            Value result = execute_function(*main_func, {});
            return ExecutionResult::ok(result);
        } catch (const std::exception& e) {
            return ExecutionResult::error(e.what());
        }
    }

   private:
    const MirProgram* current_program_ = nullptr;
    BuiltinManager builtin_manager_;

    // static変数のストレージ（関数名::変数名 → 値）
    std::unordered_map<std::string, Value> static_variables_;
    // static変数の初期化フラグ（関数名::変数名 → 初期化済みか）
    std::unordered_map<std::string, bool> static_initialized_;

    /// 関数を名前で検索
    const MirFunction* find_function(const std::string& name) const {
        if (!current_program_)
            return nullptr;
        for (const auto& func : current_program_->functions) {
            if (func && func->name == name) {
                return func.get();
            }
        }
        return nullptr;
    }

    /// 関数を実行
    Value execute_function(const MirFunction& func, std::vector<Value> args) {
        debug::interp::log(debug::interp::Id::ExecuteStart, "Executing: " + func.name,
                           debug::Level::Debug);

        ExecutionContext ctx(&func, &builtin_manager_.registry());

        // 引数を設定
        for (size_t i = 0; i < args.size() && i < func.arg_locals.size(); ++i) {
            ctx.locals[func.arg_locals[i]] = args[i];

            // デバッグ: 引数の型を出力
            if (args[i].type() == typeid(StructValue)) {
                auto& sv = std::any_cast<StructValue&>(args[i]);
                debug::interp::log(debug::interp::Id::LocalInit,
                                   "Set arg " + std::to_string(i) + " (local " +
                                       std::to_string(func.arg_locals[i]) +
                                       ") as StructValue: " + sv.type_name,
                                   debug::Level::Debug);
            } else {
                debug::interp::log(debug::interp::Id::LocalInit,
                                   "Set arg " + std::to_string(i) + " (local " +
                                       std::to_string(func.arg_locals[i]) +
                                       ") as type: " + std::string(args[i].type().name()),
                                   debug::Level::Debug);
            }
        }

        // static変数の初期化（スキップすべき変数をコンテキストに設定）
        ctx.skip_static_init = initialize_static_locals(ctx, func);

        // エントリブロックから実行
        execute_block(ctx, func.entry_block);

        // static変数の保存
        save_static_locals(ctx, func);

        // 戻り値を取得
        auto it = ctx.locals.find(func.return_local);
        return it != ctx.locals.end() ? it->second : Value{};
    }

    /// static変数を初期化（初回のみ初期値を使用、2回目以降は保存された値を復元）
    /// 戻り値: 空のセット（MIRに初期化代入がないため、スキップ不要）
    std::unordered_set<LocalId> initialize_static_locals(ExecutionContext& ctx,
                                                         const MirFunction& func) {
        for (const auto& local : func.locals) {
            if (local.is_static) {
                std::string key = func.name + "::" + local.name;
                auto it = static_variables_.find(key);
                if (it != static_variables_.end()) {
                    // 既に値がある場合は復元
                    ctx.locals[local.id] = it->second;
                    debug::interp::log(
                        debug::interp::Id::LocalInit,
                        "Restored static " + key + " = " + value_to_string(it->second),
                        debug::Level::Debug);
                } else {
                    // 初回呼び出し時はデフォルト値で初期化
                    Value default_value = get_default_value(local.type);
                    ctx.locals[local.id] = default_value;
                    static_variables_[key] = default_value;
                    debug::interp::log(
                        debug::interp::Id::LocalInit,
                        "Initialized static " + key + " = " + value_to_string(default_value),
                        debug::Level::Debug);
                }
            }
        }
        // MIRに初期化代入がないため、スキップすべき変数はない
        return {};
    }

    /// 型に応じたデフォルト値を取得
    Value get_default_value(const hir::TypePtr& type) {
        if (!type)
            return Value{int64_t(0)};

        switch (type->kind) {
            case hir::TypeKind::Int:
            case hir::TypeKind::Long:
            case hir::TypeKind::Short:
            case hir::TypeKind::Tiny:
            case hir::TypeKind::Char:
                return Value{int64_t(0)};
            case hir::TypeKind::UInt:
            case hir::TypeKind::ULong:
            case hir::TypeKind::UShort:
            case hir::TypeKind::UTiny:
                return Value{uint64_t(0)};
            case hir::TypeKind::Float:
            case hir::TypeKind::Double:
                return Value{double(0.0)};
            case hir::TypeKind::Bool:
                return Value{false};
            case hir::TypeKind::String:
                return Value{std::string("")};
            default:
                return Value{int64_t(0)};
        }
    }

    /// static変数を保存
    void save_static_locals(ExecutionContext& ctx, const MirFunction& func) {
        for (const auto& local : func.locals) {
            if (local.is_static) {
                std::string key = func.name + "::" + local.name;
                auto it = ctx.locals.find(local.id);
                if (it != ctx.locals.end()) {
                    static_variables_[key] = it->second;
                    debug::interp::log(debug::interp::Id::Store,
                                       "Saved static " + key + " = " + value_to_string(it->second),
                                       debug::Level::Debug);
                }
            }
        }
    }

    /// コンストラクタを実行（selfを更新）
    void execute_constructor(const MirFunction& func, std::vector<Value>& args) {
        debug::interp::log(debug::interp::Id::ExecuteStart, "Executing constructor: " + func.name,
                           debug::Level::Debug);

        ExecutionContext ctx(&func, &builtin_manager_.registry());

        // 引数を設定（argsへの参照を保持）
        for (size_t i = 0; i < args.size() && i < func.arg_locals.size(); ++i) {
            ctx.locals[func.arg_locals[i]] = args[i];
        }

        // エントリブロックから実行
        execute_block(ctx, func.entry_block);

        // selfを呼び出し元にコピーバック（第一引数）
        if (!func.arg_locals.empty()) {
            auto it = ctx.locals.find(func.arg_locals[0]);
            if (it != ctx.locals.end()) {
                args[0] = it->second;
            }
        }
    }

    /// ブロックを実行（再帰的）
    void execute_block(ExecutionContext& ctx, BlockId block_id) {
        if (block_id == INVALID_BLOCK)
            return;
        if (block_id >= ctx.function->basic_blocks.size())
            return;

        const auto& block = *ctx.function->basic_blocks[block_id];

        // 文を実行
        for (const auto& stmt : block.statements) {
            execute_statement(ctx, *stmt);
        }

        // ターミネータを実行
        if (block.terminator) {
            execute_terminator(ctx, *block.terminator);
        }
    }

    /// 文を実行
    void execute_statement(ExecutionContext& ctx, const MirStatement& stmt) {
        switch (stmt.kind) {
            case MirStatement::Assign: {
                auto& data = std::get<MirStatement::AssignData>(stmt.data);

                // static変数への初期化代入かチェック
                if (data.place.projections.empty() &&
                    ctx.should_skip_static_init(data.place.local)) {
                    // static変数への初期化代入はスキップ（値は既に復元されている）
                    ctx.mark_static_initialized(data.place.local);
                    debug::interp::log(
                        debug::interp::Id::Assign,
                        "Skipping static init for _" + std::to_string(data.place.local),
                        debug::Level::Debug);
                    break;
                }

                Value val = Evaluator::evaluate_rvalue(ctx, *data.rvalue);
                Evaluator::store_to_place(ctx, data.place, val);
                break;
            }
            case MirStatement::StorageLive:
            case MirStatement::StorageDead:
            case MirStatement::Nop:
                // 何もしない
                break;
        }
    }

    /// ターミネータを実行
    void execute_terminator(ExecutionContext& ctx, const MirTerminator& term) {
        switch (term.kind) {
            case MirTerminator::Goto: {
                auto& data = std::get<MirTerminator::GotoData>(term.data);
                execute_block(ctx, data.target);
                break;
            }
            case MirTerminator::SwitchInt: {
                auto& data = std::get<MirTerminator::SwitchIntData>(term.data);
                Value discr = Evaluator::evaluate_operand(ctx, *data.discriminant);

                int64_t val = 0;
                if (discr.type() == typeid(int64_t)) {
                    val = std::any_cast<int64_t>(discr);
                } else if (discr.type() == typeid(bool)) {
                    val = std::any_cast<bool>(discr) ? 1 : 0;
                } else if (discr.type() == typeid(char)) {
                    val = static_cast<int64_t>(std::any_cast<char>(discr));
                } else if (discr.type() == typeid(int32_t)) {
                    val = static_cast<int64_t>(std::any_cast<int32_t>(discr));
                } else if (discr.type() == typeid(uint64_t)) {
                    val = static_cast<int64_t>(std::any_cast<uint64_t>(discr));
                }

                // マッチするケースを探す
                for (const auto& [target_val, target_block] : data.targets) {
                    if (val == target_val) {
                        execute_block(ctx, target_block);
                        return;
                    }
                }

                // デフォルト
                execute_block(ctx, data.otherwise);
                break;
            }
            case MirTerminator::Return: {
                // 関数から戻る（戻り値は_0に設定済み）
                break;
            }
            case MirTerminator::Unreachable: {
                throw std::runtime_error("Reached unreachable code");
            }
            case MirTerminator::Call: {
                auto& data = std::get<MirTerminator::CallData>(term.data);
                execute_call(ctx, data);
                execute_block(ctx, data.success);
                break;
            }
        }
    }

    /// 関数呼び出しを実行
    void execute_call(ExecutionContext& ctx, const MirTerminator::CallData& data) {
        // 関数名を取得
        std::string func_name = get_function_name(data);
        if (func_name.empty()) {
            debug::interp::log(debug::interp::Id::Error, "Could not determine function name",
                               debug::Level::Error);
            return;
        }

        debug::interp::log(
            debug::interp::Id::Call,
            "Calling: " + func_name + " with " + std::to_string(data.args.size()) + " MIR args",
            debug::Level::Debug);

        // 引数を評価
        std::vector<Value> args;
        for (const auto& arg : data.args) {
            args.push_back(Evaluator::evaluate_operand(ctx, *arg));
        }

        // 組み込み関数を探す
        auto& builtins = *ctx.builtins;
        auto builtin_it = builtins.find(func_name);
        if (builtin_it != builtins.end()) {
            Value result = builtin_it->second(args, ctx.locals);
            if (data.destination) {
                Evaluator::store_to_place(ctx, *data.destination, result);
            }
            return;
        }

        // ユーザー定義関数を探す
        const MirFunction* callee = find_function(func_name);

        // 見つからない場合、コンストラクタとして探す
        if (!callee) {
            std::string ctor_name = func_name + "__ctor";
            callee = find_function(ctor_name);
            if (callee) {
                func_name = ctor_name;  // 関数名を更新
            }
        }

        if (callee) {
            // コンストラクタかどうかを判定
            bool is_constructor = func_name.find("__ctor") != std::string::npos;

            if (is_constructor && !args.empty()) {
                // コンストラクタ: selfへの変更を呼び出し元に反映
                constexpr LocalId invalid_local = static_cast<LocalId>(-1);
                LocalId self_local = invalid_local;
                if (!data.args.empty() && data.args[0]->kind == MirOperand::Copy) {
                    auto& place = std::get<MirPlace>(data.args[0]->data);
                    self_local = place.local;
                }

                execute_constructor(*callee, args);

                // selfをコピーバック
                if (self_local != invalid_local) {
                    ctx.locals[self_local] = args[0];
                }

                // destinationがある場合（変数への代入）、コンストラクタの結果を格納
                if (data.destination) {
                    Evaluator::store_to_place(ctx, *data.destination, args[0]);
                }
            } else {
                // 通常の関数呼び出し
                Value result = execute_function(*callee, args);
                if (data.destination) {
                    Evaluator::store_to_place(ctx, *data.destination, result);
                }
            }
            return;
        }

        // 動的ディスパッチを試みる（インターフェースメソッド）
        if (try_dynamic_dispatch(ctx, data, func_name, args)) {
            return;
        }

        debug::interp::log(debug::interp::Id::Error, "Function not found: " + func_name,
                           debug::Level::Warn);
    }

    /// 関数名を取得
    static std::string get_function_name(const MirTerminator::CallData& data) {
        if (!data.func)
            return "";

        if (data.func->kind == MirOperand::FunctionRef) {
            if (auto* name = std::get_if<std::string>(&data.func->data)) {
                return *name;
            }
        } else if (data.func->kind == MirOperand::Constant) {
            auto& constant = std::get<MirConstant>(data.func->data);
            if (auto* name = std::get_if<std::string>(&constant.value)) {
                return *name;
            }
        }
        return "";
    }

    /// 動的ディスパッチを試みる
    bool try_dynamic_dispatch(ExecutionContext& ctx, const MirTerminator::CallData& data,
                              const std::string& func_name, std::vector<Value>& args) {
        size_t sep_pos = func_name.find("__");
        if (sep_pos == std::string::npos || args.empty()) {
            debug::interp::log(debug::interp::Id::Call,
                               "Dynamic dispatch failed: no __ or empty args for " + func_name,
                               debug::Level::Debug);
            return false;
        }

        std::string type_part = func_name.substr(0, sep_pos);
        std::string method_name = func_name.substr(sep_pos + 2);

        // 最初の引数（self）の実際の型を取得
        Value& self_arg = args[0];
        if (self_arg.type() != typeid(StructValue)) {
            debug::interp::log(debug::interp::Id::Call,
                               "Dynamic dispatch failed: arg[0] is not StructValue for " +
                                   func_name + ", type: " + std::string(self_arg.type().name()),
                               debug::Level::Debug);
            return false;
        }

        auto& struct_val = std::any_cast<StructValue&>(self_arg);
        std::string actual_type = struct_val.type_name;

        debug::interp::log(
            debug::interp::Id::Call,
            "Dynamic dispatch: type_part=" + type_part + ", actual_type=" + actual_type,
            debug::Level::Debug);

        if (actual_type.empty()) {
            return false;
        }

        // 実際の関数名を構築
        std::string actual_func_name = actual_type + "__" + method_name;
        const MirFunction* actual_func = find_function(actual_func_name);
        if (!actual_func) {
            debug::interp::log(debug::interp::Id::Call,
                               "Dynamic dispatch failed: function not found: " + actual_func_name,
                               debug::Level::Debug);
            return false;
        }

        debug::interp::log(debug::interp::Id::Call,
                           "Dynamic dispatch: " + func_name + " -> " + actual_func_name,
                           debug::Level::Debug);

        Value result = execute_function(*actual_func, args);
        if (data.destination) {
            Evaluator::store_to_place(ctx, *data.destination, result);
        }
        return true;
    }
};

}  // namespace cm::mir::interp
