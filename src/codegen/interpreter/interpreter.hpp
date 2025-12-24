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

                // PointerValueの場合、ターゲット変数の型情報でelement_typeを設定
                if (val.type() == typeid(PointerValue) && data.place.projections.empty()) {
                    // ローカル変数の型を取得
                    for (const auto& local : ctx.function->locals) {
                        if (local.id == data.place.local && local.type) {
                            if (local.type->kind == hir::TypeKind::Pointer) {
                                auto& pv = std::any_cast<PointerValue&>(val);
                                pv.element_type = local.type->element_type;
                            }
                            break;
                        }
                    }
                }

                // クロージャ変数への代入をチェック
                if (data.place.projections.empty() && ctx.function) {
                    const LocalId dest_local = data.place.local;
                    if (dest_local < ctx.function->locals.size()) {
                        const auto& local_decl = ctx.function->locals[dest_local];
                        if (local_decl.is_closure && !local_decl.captured_locals.empty()) {
                            // クロージャ: ClosureValueを作成
                            ClosureValue cv;
                            cv.func_name = local_decl.closure_func_name;
                            for (LocalId cap_local : local_decl.captured_locals) {
                                auto it = ctx.locals.find(cap_local);
                                if (it != ctx.locals.end()) {
                                    cv.captured_values.push_back(it->second);
                                }
                            }
                            val = Value(cv);
                            debug::interp::log(
                                debug::interp::Id::Assign,
                                "Created ClosureValue for " + cv.func_name + " with " +
                                    std::to_string(cv.captured_values.size()) + " captures",
                                debug::Level::Debug);
                        }
                    }
                }

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
        // 関数名を取得（関数ポインタ変数からの呼び出しも対応）
        std::string func_name = get_function_name_from_operand(ctx, data);
        if (func_name.empty()) {
            debug::interp::log(debug::interp::Id::Error, "Could not determine function name",
                               debug::Level::Error);
            return;
        }

        // クロージャのキャプチャ引数を追加
        std::vector<Value> captured_args;
        if (data.func &&
            (data.func->kind == MirOperand::Copy || data.func->kind == MirOperand::Move)) {
            auto& place = std::get<MirPlace>(data.func->data);

            // まず、値がClosureValueかどうかチェック
            auto it = ctx.locals.find(place.local);
            if (it != ctx.locals.end() && it->second.type() == typeid(ClosureValue)) {
                const auto& cv = std::any_cast<ClosureValue>(it->second);
                func_name = cv.func_name;
                captured_args = cv.captured_values;
            }
            // 次に、LocalDeclのクロージャ情報をチェック（フォールバック）
            else if (ctx.function && place.local < ctx.function->locals.size()) {
                const auto& local_decl = ctx.function->locals[place.local];
                if (local_decl.is_closure && !local_decl.captured_locals.empty()) {
                    // クロージャ: 実際の関数名を使用
                    func_name = local_decl.closure_func_name;
                    // キャプチャされた変数を引数の先頭に追加
                    for (LocalId cap_local : local_decl.captured_locals) {
                        auto cap_it = ctx.locals.find(cap_local);
                        if (cap_it != ctx.locals.end()) {
                            captured_args.push_back(cap_it->second);
                        }
                    }
                }
            }
        }

        debug::interp::log(debug::interp::Id::Call,
                           "Calling: " + func_name + " with " + std::to_string(data.args.size()) +
                               " MIR args" +
                               (captured_args.empty()
                                    ? ""
                                    : " + " + std::to_string(captured_args.size()) + " captured"),
                           debug::Level::Debug);

        // 引数を評価
        std::vector<Value> args;
        // キャプチャ引数を先頭に追加
        for (const auto& cap_arg : captured_args) {
            args.push_back(cap_arg);
        }
        for (const auto& arg : data.args) {
            args.push_back(Evaluator::evaluate_operand(ctx, *arg));
        }

        // スライス操作の特別処理（スライスを直接変更する必要があるため）
        if (func_name.find("cm_slice_push") == 0 || func_name == "cm_slice_pop_i32" ||
            func_name == "cm_slice_pop_i64" || func_name == "cm_slice_pop_f64" ||
            func_name == "cm_slice_pop_ptr" || func_name == "cm_slice_delete" ||
            func_name == "cm_slice_clear") {
            // 第1引数からスライスのローカル変数IDを取得
            if (!data.args.empty() && data.args[0]->kind == MirOperand::Copy) {
                auto& place = std::get<MirPlace>(data.args[0]->data);

                // プロジェクションがある場合（構造体フィールドアクセス）
                if (!place.projections.empty()) {
                    // 構造体からスライスフィールドを取得
                    auto it = ctx.locals.find(place.local);
                    if (it != ctx.locals.end() && it->second.type() == typeid(StructValue)) {
                        auto& struct_val = std::any_cast<StructValue&>(it->second);
                        // フィールドインデックスを取得
                        for (const auto& proj : place.projections) {
                            if (proj.kind == ProjectionKind::Field) {
                                size_t field_idx = proj.field_id;
                                auto field_it = struct_val.fields.find(field_idx);
                                // フィールドが存在しない場合はスライスとして初期化
                                if (field_it == struct_val.fields.end()) {
                                    SliceValue sv;
                                    sv.capacity = 4;
                                    struct_val.fields[field_idx] = Value(sv);
                                    field_it = struct_val.fields.find(field_idx);
                                }
                                if (field_it != struct_val.fields.end()) {
                                    auto& field_val = field_it->second;
                                    // フィールドがまだSliceValueでない場合は変換
                                    if (field_val.type() != typeid(SliceValue)) {
                                        SliceValue sv;
                                        sv.capacity = 4;
                                        field_val = Value(sv);
                                    }
                                    auto& slice = std::any_cast<SliceValue&>(field_val);

                                    if (func_name.find("cm_slice_push") == 0 && args.size() >= 2) {
                                        slice.push(args[1]);
                                    } else if (func_name.find("cm_slice_pop") == 0) {
                                        Value result = slice.pop();
                                        if (data.destination) {
                                            Evaluator::store_to_place(ctx, *data.destination,
                                                                      result);
                                        }
                                        return;
                                    } else if (func_name == "cm_slice_delete" && args.size() >= 2) {
                                        int64_t idx = 0;
                                        if (args[1].type() == typeid(int64_t)) {
                                            idx = std::any_cast<int64_t>(args[1]);
                                        } else if (args[1].type() == typeid(int)) {
                                            idx = std::any_cast<int>(args[1]);
                                        }
                                        slice.remove(static_cast<size_t>(idx));
                                    } else if (func_name == "cm_slice_clear") {
                                        slice.clear();
                                    }
                                }
                            }
                        }
                    }
                    return;
                }

                // プロジェクションがない場合（直接スライス変数）
                LocalId slice_local = place.local;

                auto it = ctx.locals.find(slice_local);
                if (it != ctx.locals.end() && it->second.type() == typeid(SliceValue)) {
                    auto& slice = std::any_cast<SliceValue&>(it->second);

                    if (func_name.find("cm_slice_push") == 0 && args.size() >= 2) {
                        slice.push(args[1]);
                    } else if (func_name.find("cm_slice_pop") == 0) {
                        Value result = slice.pop();
                        if (data.destination) {
                            Evaluator::store_to_place(ctx, *data.destination, result);
                        }
                        return;
                    } else if (func_name == "cm_slice_delete" && args.size() >= 2) {
                        int64_t idx = 0;
                        if (args[1].type() == typeid(int64_t)) {
                            idx = std::any_cast<int64_t>(args[1]);
                        } else if (args[1].type() == typeid(int)) {
                            idx = std::any_cast<int>(args[1]);
                        }
                        slice.remove(static_cast<size_t>(idx));
                    } else if (func_name == "cm_slice_clear") {
                        slice.clear();
                    }
                }
            }
            return;
        }

        // スライスlen/cap操作（読み取り専用）
        if (func_name == "cm_slice_len" || func_name == "cm_slice_cap") {
            if (!data.args.empty() && data.args[0]->kind == MirOperand::Copy) {
                auto& place = std::get<MirPlace>(data.args[0]->data);

                // プロジェクションがある場合（構造体フィールドアクセス）
                if (!place.projections.empty()) {
                    auto it = ctx.locals.find(place.local);
                    if (it != ctx.locals.end() && it->second.type() == typeid(StructValue)) {
                        auto& struct_val = std::any_cast<StructValue&>(it->second);
                        for (const auto& proj : place.projections) {
                            if (proj.kind == ProjectionKind::Field) {
                                size_t field_idx = proj.field_id;
                                auto field_it = struct_val.fields.find(field_idx);
                                // フィールドが存在しない場合はスライスとして初期化
                                if (field_it == struct_val.fields.end()) {
                                    SliceValue sv;
                                    sv.capacity = 4;
                                    struct_val.fields[field_idx] = Value(sv);
                                    field_it = struct_val.fields.find(field_idx);
                                }
                                if (field_it != struct_val.fields.end()) {
                                    const auto& field_val = field_it->second;
                                    if (field_val.type() == typeid(SliceValue)) {
                                        const auto& slice = std::any_cast<SliceValue>(field_val);
                                        Value result;
                                        if (func_name == "cm_slice_len") {
                                            result = Value(static_cast<int64_t>(slice.len()));
                                        } else {
                                            result = Value(static_cast<int64_t>(slice.cap()));
                                        }
                                        if (data.destination) {
                                            Evaluator::store_to_place(ctx, *data.destination,
                                                                      result);
                                        }
                                        return;
                                    }
                                }
                            }
                        }
                    }
                    return;
                }

                LocalId slice_local = place.local;

                auto it = ctx.locals.find(slice_local);
                if (it != ctx.locals.end() && it->second.type() == typeid(SliceValue)) {
                    const auto& slice = std::any_cast<SliceValue>(it->second);
                    Value result;
                    if (func_name == "cm_slice_len") {
                        result = Value(static_cast<int64_t>(slice.len()));
                    } else {
                        result = Value(static_cast<int64_t>(slice.cap()));
                    }
                    if (data.destination) {
                        Evaluator::store_to_place(ctx, *data.destination, result);
                    }
                    return;
                }
            }
        }

        // スライスget操作
        if (func_name.find("cm_slice_get") == 0) {
            bool handled = false;
            if (data.args.size() >= 2 && data.args[0]->kind == MirOperand::Copy) {
                auto& place = std::get<MirPlace>(data.args[0]->data);

                // プロジェクションがある場合（構造体フィールドアクセス）
                if (!place.projections.empty()) {
                    auto it = ctx.locals.find(place.local);
                    if (it != ctx.locals.end() && it->second.type() == typeid(StructValue)) {
                        const auto& struct_val = std::any_cast<StructValue>(it->second);
                        for (const auto& proj : place.projections) {
                            if (proj.kind == ProjectionKind::Field) {
                                size_t field_idx = proj.field_id;
                                auto field_it = struct_val.fields.find(field_idx);
                                if (field_it != struct_val.fields.end()) {
                                    const auto& field_val = field_it->second;
                                    if (field_val.type() == typeid(SliceValue)) {
                                        const auto& slice = std::any_cast<SliceValue>(field_val);
                                        int64_t idx = 0;
                                        if (args[1].type() == typeid(int64_t)) {
                                            idx = std::any_cast<int64_t>(args[1]);
                                        } else if (args[1].type() == typeid(int)) {
                                            idx = std::any_cast<int>(args[1]);
                                        }
                                        Value result = slice.get(static_cast<size_t>(idx));
                                        if (data.destination) {
                                            Evaluator::store_to_place(ctx, *data.destination,
                                                                      result);
                                        }
                                        handled = true;
                                    }
                                }
                            }
                        }
                    }
                } else {
                    LocalId slice_local = place.local;

                    auto it = ctx.locals.find(slice_local);
                    if (it != ctx.locals.end() && it->second.type() == typeid(SliceValue)) {
                        const auto& slice = std::any_cast<SliceValue>(it->second);
                        int64_t idx = 0;
                        if (args[1].type() == typeid(int64_t)) {
                            idx = std::any_cast<int64_t>(args[1]);
                        } else if (args[1].type() == typeid(int)) {
                            idx = std::any_cast<int>(args[1]);
                        }
                        Value result = slice.get(static_cast<size_t>(idx));
                        if (data.destination) {
                            Evaluator::store_to_place(ctx, *data.destination, result);
                        }
                        handled = true;
                    }
                }
            }
            if (handled)
                return;
            // 特別処理で処理できなかった場合、ビルトイン関数にフォールスルー
        }

        // 配列高級関数（some, every, findIndex, map, filter, sort, sortBy, find, reduce）の特別処理
        if (func_name.find("__builtin_array_some") == 0 ||
            func_name.find("__builtin_array_every") == 0 ||
            func_name.find("__builtin_array_findIndex") == 0 ||
            func_name.find("__builtin_array_map") == 0 ||
            func_name.find("__builtin_array_filter") == 0 ||
            func_name.find("__builtin_array_sort") == 0 ||
            func_name.find("__builtin_array_sortBy") == 0 ||
            func_name.find("__builtin_array_find") == 0 ||
            func_name.find("__builtin_array_reduce") == 0) {
            Value result = execute_array_higher_order(ctx, func_name, args);
            if (data.destination) {
                Evaluator::store_to_place(ctx, *data.destination, result);
            }
            return;
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

    /// 関数名を取得（関数ポインタ変数からの呼び出しも対応）
    std::string get_function_name_from_operand(ExecutionContext& ctx,
                                               const MirTerminator::CallData& data) {
        if (!data.func)
            return "";

        // 直接の関数参照
        if (data.func->kind == MirOperand::FunctionRef) {
            if (auto* name = std::get_if<std::string>(&data.func->data)) {
                return *name;
            }
        }
        // 定数
        else if (data.func->kind == MirOperand::Constant) {
            auto& constant = std::get<MirConstant>(data.func->data);
            if (auto* name = std::get_if<std::string>(&constant.value)) {
                return *name;
            }
        }
        // 関数ポインタ変数（Copy/Move）
        else if (data.func->kind == MirOperand::Copy || data.func->kind == MirOperand::Move) {
            Value val = Evaluator::evaluate_operand(ctx, *data.func);
            // クロージャの場合
            if (val.type() == typeid(ClosureValue)) {
                const auto& cv = std::any_cast<ClosureValue>(val);
                return cv.func_name;
            }
            // 関数ポインタはstringとして保存されている
            if (val.type() == typeid(std::string)) {
                return std::any_cast<std::string>(val);
            }
            // PointerValueの場合、参照先から関数名を取得
            if (val.type() == typeid(PointerValue)) {
                const auto& pv = std::any_cast<PointerValue>(val);
                auto it = ctx.locals.find(pv.target_local);
                if (it != ctx.locals.end() && it->second.type() == typeid(std::string)) {
                    return std::any_cast<std::string>(it->second);
                }
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

    /// 配列高級関数を実行（some, every, findIndex, map, filter, sort, sortBy, find）
    Value execute_array_higher_order(ExecutionContext& ctx, const std::string& func_name,
                                     std::vector<Value>& args) {
        // sortは2引数、他は3引数以上
        bool is_sort_only = func_name.find("__builtin_array_sort") == 0 &&
                            func_name.find("__builtin_array_sortBy") == std::string::npos;

        if (is_sort_only) {
            if (args.size() < 2) {
                debug::interp::log(debug::interp::Id::Error, "Array sort requires 2 args",
                                   debug::Level::Warn);
                return Value(false);
            }
        } else {
            if (args.size() < 3) {
                debug::interp::log(debug::interp::Id::Error,
                                   "Array higher-order function requires 3 args",
                                   debug::Level::Warn);
                return Value(false);
            }
        }

        // 配列を取得
        std::vector<Value> const* arr_ptr = nullptr;
        if (args[0].type() == typeid(PointerValue)) {
            const auto& pv = std::any_cast<PointerValue>(args[0]);
            auto it = ctx.locals.find(pv.target_local);
            if (it != ctx.locals.end() && it->second.type() == typeid(ArrayValue)) {
                arr_ptr = &std::any_cast<const ArrayValue&>(it->second).elements;
            }
        } else if (args[0].type() == typeid(ArrayValue)) {
            arr_ptr = &std::any_cast<const ArrayValue&>(args[0]).elements;
        }

        if (!arr_ptr) {
            debug::interp::log(debug::interp::Id::Error, "Could not get array for higher-order fn",
                               debug::Level::Warn);
            return Value(false);
        }
        const auto& arr = *arr_ptr;

        // サイズを取得
        int64_t size = 0;
        if (args[1].type() == typeid(int64_t)) {
            size = std::any_cast<int64_t>(args[1]);
        }

        // sortはコールバック不要
        if (func_name.find("__builtin_array_sort") == 0 &&
            func_name.find("__builtin_array_sortBy") == std::string::npos) {
            // sort: デフォルトの昇順ソート（コールバック不要）
            std::vector<Value> result_arr;
            result_arr.reserve(static_cast<size_t>(size));
            for (int64_t i = 0; i < size && i < static_cast<int64_t>(arr.size()); i++) {
                result_arr.push_back(arr[i]);
            }
            // 値をソート
            std::sort(result_arr.begin(), result_arr.end(), [](const Value& a, const Value& b) {
                if (a.type() == typeid(int64_t) && b.type() == typeid(int64_t)) {
                    return std::any_cast<int64_t>(a) < std::any_cast<int64_t>(b);
                }
                if (a.type() == typeid(double) && b.type() == typeid(double)) {
                    return std::any_cast<double>(a) < std::any_cast<double>(b);
                }
                // 構造体の場合: 最初のフィールドで比較
                if (a.type() == typeid(StructValue) && b.type() == typeid(StructValue)) {
                    const auto& sa = std::any_cast<const StructValue&>(a);
                    const auto& sb = std::any_cast<const StructValue&>(b);
                    if (!sa.fields.empty() && !sb.fields.empty()) {
                        auto it_a = sa.fields.begin();
                        auto it_b = sb.fields.begin();
                        if (it_a->second.type() == typeid(int64_t) &&
                            it_b->second.type() == typeid(int64_t)) {
                            return std::any_cast<int64_t>(it_a->second) <
                                   std::any_cast<int64_t>(it_b->second);
                        }
                    }
                }
                return false;
            });
            SliceValue sv;
            sv.elements = std::move(result_arr);
            return Value(sv);
        }

        // 関数名を取得（コールバックが必要な関数のみ）
        std::string callback_name;
        std::vector<Value> captured_values;  // クロージャのキャプチャ値

        // _closure版の場合は4引数（配列, サイズ, 関数名, キャプチャ値）
        bool is_closure_version = func_name.find("_closure") != std::string::npos;

        if (args.size() > 2) {
            if (args[2].type() == typeid(std::string)) {
                callback_name = std::any_cast<std::string>(args[2]);
                // _closure版の場合は4番目の引数がキャプチャ値
                if (is_closure_version && args.size() > 3) {
                    captured_values.push_back(args[3]);
                }
            } else if (args[2].type() == typeid(ClosureValue)) {
                // クロージャの場合
                const auto& cv = std::any_cast<ClosureValue>(args[2]);
                callback_name = cv.func_name;
                captured_values = cv.captured_values;
            } else {
                debug::interp::log(debug::interp::Id::Error,
                                   "Callback is not a function name or closure: " +
                                       std::string(args[2].type().name()),
                                   debug::Level::Warn);
                return Value(false);
            }
        } else {
            debug::interp::log(debug::interp::Id::Error, "Missing callback argument",
                               debug::Level::Warn);
            return Value(false);
        }

        // コールバック関数を検索
        const MirFunction* callback = find_function(callback_name);
        if (!callback) {
            debug::interp::log(debug::interp::Id::Error,
                               "Callback function not found: " + callback_name, debug::Level::Warn);
            return Value(false);
        }

        // クロージャ呼び出しヘルパー
        auto call_callback = [&](const Value& elem) -> Value {
            std::vector<Value> cb_args;
            // キャプチャ値を先頭に追加
            for (const auto& cap : captured_values) {
                cb_args.push_back(cap);
            }
            cb_args.push_back(elem);
            return execute_function(*callback, cb_args);
        };

        // 関数の種類に応じて処理
        if (func_name.find("__builtin_array_some") == 0) {
            for (int64_t i = 0; i < size && i < static_cast<int64_t>(arr.size()); i++) {
                Value result = call_callback(arr[i]);
                if (result.type() == typeid(bool) && std::any_cast<bool>(result)) {
                    return Value(true);
                }
            }
            return Value(false);
        } else if (func_name.find("__builtin_array_every") == 0) {
            for (int64_t i = 0; i < size && i < static_cast<int64_t>(arr.size()); i++) {
                Value result = call_callback(arr[i]);
                if (result.type() == typeid(bool) && !std::any_cast<bool>(result)) {
                    return Value(false);
                }
            }
            return Value(true);
        } else if (func_name.find("__builtin_array_findIndex") == 0) {
            for (int64_t i = 0; i < size && i < static_cast<int64_t>(arr.size()); i++) {
                Value result = call_callback(arr[i]);
                if (result.type() == typeid(bool) && std::any_cast<bool>(result)) {
                    return Value(i);
                }
            }
            return Value(int64_t{-1});
        } else if (func_name.find("__builtin_array_map") == 0) {
            // map: 各要素に関数を適用し、新しい配列を返す
            std::vector<Value> result_arr;
            result_arr.reserve(static_cast<size_t>(size));
            for (int64_t i = 0; i < size && i < static_cast<int64_t>(arr.size()); i++) {
                Value result = call_callback(arr[i]);
                result_arr.push_back(result);
            }
            // SliceValueとして返す（動的配列）
            SliceValue sv;
            sv.elements = std::move(result_arr);
            return Value(sv);
        } else if (func_name.find("__builtin_array_filter") == 0) {
            // filter: 条件を満たす要素のみを含む新しい配列を返す
            std::vector<Value> result_arr;
            for (int64_t i = 0; i < size && i < static_cast<int64_t>(arr.size()); i++) {
                Value result = call_callback(arr[i]);
                if (result.type() == typeid(bool) && std::any_cast<bool>(result)) {
                    result_arr.push_back(arr[i]);
                }
            }
            // SliceValueとして返す（動的配列）
            SliceValue sv;
            sv.elements = std::move(result_arr);
            return Value(sv);
        } else if (func_name.find("__builtin_array_sortBy") == 0) {
            // sortBy: コンパレータ関数を使用
            std::vector<Value> result_arr;
            result_arr.reserve(static_cast<size_t>(size));
            for (int64_t i = 0; i < size && i < static_cast<int64_t>(arr.size()); i++) {
                result_arr.push_back(arr[i]);
            }
            // コンパレータ関数を使ってソート
            auto* self = this;
            std::sort(result_arr.begin(), result_arr.end(),
                      [callback, self](const Value& a, const Value& b) {
                          std::vector<Value> cb_args = {a, b};
                          Value result = self->execute_function(*callback, cb_args);
                          if (result.type() == typeid(int64_t)) {
                              return std::any_cast<int64_t>(result) < 0;
                          }
                          if (result.type() == typeid(bool)) {
                              return std::any_cast<bool>(result);
                          }
                          return false;
                      });
            SliceValue sv;
            sv.elements = std::move(result_arr);
            return Value(sv);
        } else if (func_name.find("__builtin_array_find") == 0) {
            // find: 条件を満たす最初の要素を返す
            for (int64_t i = 0; i < size && i < static_cast<int64_t>(arr.size()); i++) {
                std::vector<Value> cb_args = {arr[i]};
                Value result = execute_function(*callback, cb_args);
                if (result.type() == typeid(bool) && std::any_cast<bool>(result)) {
                    return arr[i];
                }
            }
            // 見つからない場合はデフォルト値
            return Value(int64_t{0});
        } else if (func_name.find("__builtin_array_reduce") == 0) {
            // reduce: 畳み込み関数
            // args[3]が初期値
            if (args.size() < 4) {
                return Value(int64_t{0});
            }
            Value accumulator = args[3];
            for (int64_t i = 0; i < size && i < static_cast<int64_t>(arr.size()); i++) {
                std::vector<Value> cb_args = {accumulator, arr[i]};
                accumulator = execute_function(*callback, cb_args);
            }
            return accumulator;
        }

        return Value(false);
    }
};

}  // namespace cm::mir::interp
