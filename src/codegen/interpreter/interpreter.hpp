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
        }

        // エントリブロックから実行
        execute_block(ctx, func.entry_block);

        // 戻り値を取得
        auto it = ctx.locals.find(func.return_local);
        return it != ctx.locals.end() ? it->second : Value{};
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

        debug::interp::log(debug::interp::Id::Call, "Calling: " + func_name, debug::Level::Debug);

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
        if (callee) {
            // コンストラクタかどうかを判定
            bool is_constructor = func_name.find("__ctor") != std::string::npos;

            if (is_constructor && !args.empty()) {
                // コンストラクタ: selfへの変更を呼び出し元に反映
                LocalId self_local = 0;
                if (!data.args.empty() && data.args[0]->kind == MirOperand::Copy) {
                    auto& place = std::get<MirPlace>(data.args[0]->data);
                    self_local = place.local;
                }

                execute_constructor(*callee, args);

                // selfをコピーバック
                if (self_local != 0) {
                    ctx.locals[self_local] = args[0];
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
            return false;
        }

        std::string method_name = func_name.substr(sep_pos + 2);

        // 最初の引数（self）の実際の型を取得
        Value& self_arg = args[0];
        if (self_arg.type() != typeid(StructValue)) {
            return false;
        }

        auto& struct_val = std::any_cast<StructValue&>(self_arg);
        std::string actual_type = struct_val.type_name;

        if (actual_type.empty()) {
            return false;
        }

        // 実際の関数名を構築
        std::string actual_func_name = actual_type + "__" + method_name;
        const MirFunction* actual_func = find_function(actual_func_name);
        if (!actual_func) {
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
