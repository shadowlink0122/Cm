#include "context.hpp"
#include "internal/base/debug.hpp"
#include "internal/base/mangle.hpp"
#include "internal/base/target.hpp"
#include "internal/mir/lowering/layout.hpp"
#include "internal/mir/lowering/slice_dispatch.hpp"
#include "lowering.hpp"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cm::mir {

// 演算子実装のlowering
std::unique_ptr<MirFunction> MirLowering::lower_operator(const hir::HirOperatorImpl& op_impl,
                                                         const std::string& type_name) {
    std::string op_name;
    switch (op_impl.op) {
        case hir::HirOperatorKind::Eq:
            op_name = "op_eq";
            break;
        case hir::HirOperatorKind::Ne:
            op_name = "op_ne";
            break;
        case hir::HirOperatorKind::Lt:
            op_name = "op_lt";
            break;
        case hir::HirOperatorKind::Gt:
            op_name = "op_gt";
            break;
        case hir::HirOperatorKind::Le:
            op_name = "op_le";
            break;
        case hir::HirOperatorKind::Ge:
            op_name = "op_ge";
            break;
        case hir::HirOperatorKind::Add:
            op_name = "op_add";
            break;
        case hir::HirOperatorKind::Sub:
            op_name = "op_sub";
            break;
        case hir::HirOperatorKind::Mul:
            op_name = "op_mul";
            break;
        case hir::HirOperatorKind::Div:
            op_name = "op_div";
            break;
        case hir::HirOperatorKind::Mod:
            op_name = "op_mod";
            break;
        case hir::HirOperatorKind::BitAnd:
            op_name = "op_bitand";
            break;
        case hir::HirOperatorKind::BitOr:
            op_name = "op_bitor";
            break;
        case hir::HirOperatorKind::BitXor:
            op_name = "op_bitxor";
            break;
        case hir::HirOperatorKind::Shl:
            op_name = "op_shl";
            break;
        case hir::HirOperatorKind::Shr:
            op_name = "op_shr";
            break;
        default:
            op_name = "op_unknown";
            break;
    }

    debug::log(debug::Stage::Mir, debug::Level::Info,
               "Lowering operator: " + mangle::method_name(type_name, op_name));

    auto mir_func = std::make_unique<MirFunction>();
    mir_func->name = mangle::method_name(type_name, op_name);

    // 戻り値用のローカル変数
    mir_func->return_local = 0;
    auto resolved_return_type = resolve_typedef(op_impl.return_type);
    mir_func->locals.emplace_back(0, "@return", resolved_return_type, true, false);

    // エントリーブロックを作成
    mir_func->entry_block = 0;
    mir_func->basic_blocks.push_back(std::make_unique<BasicBlock>(0));

    // LoweringContextを作成
    LoweringContext ctx(mir_func.get());
    ctx.enum_defs = &enum_defs;
    ctx.typedef_defs = &typedef_defs;
    ctx.struct_defs = &struct_defs;
    ctx.interface_names = &interface_names;
    ctx.hir_func_defs = &hir_functions;
    ctx.interface_method_returns = &interface_method_returns_;
    ctx.tagged_union_names = &tagged_union_names;
    ctx.global_const_values = &global_const_values;

    // selfパラメータを登録（値型として - 呼び出し側が参照を渡す）。
    // ジェネリックimpl（type_name="Wrap<T>"等）は基底名で型付けする。型引数付きの名前のままだと
    // struct_defs（基底名キー）のフィールド解決に失敗し、self.xの代入文が<error>型で黙って欠落していた
    // （otherパラメータはHIR解決済みで基底名になっており、selfだけが非対称だった）
    std::string self_type_name = type_name;
    if (auto lt = self_type_name.find('<'); lt != std::string::npos) {
        self_type_name = self_type_name.substr(0, lt);
    }
    auto self_type = hir::make_named(self_type_name);
    LocalId self_id = ctx.new_local("self", self_type, false);
    mir_func->arg_locals.push_back(self_id);
    ctx.register_variable("self", self_id);

    // 他のパラメータを登録
    for (const auto& param : op_impl.params) {
        auto resolved_param_type = resolve_typedef(param.type);

        LocalId param_id = ctx.new_local(param.name, resolved_param_type, false);
        mir_func->arg_locals.push_back(param_id);
        ctx.register_variable(param.name, param_id);
    }

    // 文を処理
    for (const auto& stmt : op_impl.body) {
        if (stmt) {
            stmt_lowering.lower_statement(*stmt, ctx);
        }
    }

    // デフォルトのreturn
    auto* current = ctx.get_current_block();
    if (current && !current->terminator) {
        // 戻り値がvoid、構造体、配列（動的スライス含む）型でない場合のみデフォルト値を設定
        if (resolved_return_type && resolved_return_type->kind != hir::TypeKind::Void &&
            resolved_return_type->kind != hir::TypeKind::Struct &&
            resolved_return_type->kind != hir::TypeKind::Array) {
            MirConstant default_return;
            default_return.type = resolved_return_type;
            if (resolved_return_type->is_floating()) {
                default_return.value = 0.0;
            } else {
                // すべての型で整数0を使用（LLVMコード生成側で適切に変換）
                default_return.value = int64_t(0);
            }

            ctx.push_statement(MirStatement::assign(
                MirPlace{0}, MirRvalue::use(MirOperand::constant(default_return))));
        }
        ctx.set_terminator(MirTerminator::return_value());
    }

    return mir_func;
}

// 関数のlowering - モジュラーコンポーネントを使用
std::unique_ptr<MirFunction> MirLowering::lower_function(const hir::HirFunction& func) {
    debug::log(debug::Stage::Mir, debug::Level::Info, "Lowering function: " + func.name);

    // MirFunctionを作成
    auto mir_func = std::make_unique<MirFunction>();
    // Bug#45修正: モジュール修飾名 (e.g. "kmalloc::heap_size_to_class") を単純名 (e.g. "heap_size_to_class") に正規化する。
    // ただし、current_module_pathと一致するプレフィックスのみを剥がす。
    // 他モジュールのnamespace修飾は保持し、シンボル衝突を防止する。
    std::string func_name = func.name;
    if (!current_module_path.empty()) {
        std::string module_prefix = current_module_path + "::";
        if (func_name.find(module_prefix) == 0) {
            func_name = func_name.substr(module_prefix.size());
        }
    }
    mir_func->name = func_name;
    mir_func->module_path = current_module_path;  // モジュールパスを設定
    mir_func->is_export = func.is_export;         // エクスポートフラグを設定
    mir_func->is_extern = func.is_extern;         // externフラグを設定
    mir_func->is_variadic = func.is_variadic;     // 可変長引数フラグを設定
    mir_func->is_async = func.is_async;           // asyncフラグを設定
    mir_func->is_always = func.is_always;         // alwaysフラグを設定
    // always_kind を伝搬（HIR→MIR: enum値をintでキャスト）
    mir_func->always_kind =
        static_cast<MirFunction::AlwaysKind>(static_cast<int>(func.always_kind));
    mir_func->attributes = func.attributes;  // SV属性を伝搬（sv::latch等）

    // #[test] 関数はSVテストベンチ生成でHIR文を直接変換するため保持する
    for (const auto& attr : func.attributes) {
        if (attr == "test") {
            for (const auto& stmt : func.body) {
                if (stmt) {
                    mir_func->hir_stmts.push_back(stmt.get());
                }
            }
            break;
        }
    }

    // 戻り値用のローカル変数（typedefを解決）
    mir_func->return_local = 0;
    auto resolved_return_type = resolve_typedef(func.return_type);
    mir_func->locals.emplace_back(0, "@return", resolved_return_type, true, false);

    // extern関数は宣言のみでボディなし
    if (func.is_extern) {
        // パラメータを記録
        for (const auto& param : func.params) {
            auto resolved_param_type = resolve_typedef(param.type);

            LocalId param_id = static_cast<LocalId>(mir_func->locals.size());
            mir_func->locals.emplace_back(param_id, param.name, resolved_param_type, false, false);
            mir_func->arg_locals.push_back(param_id);
        }
        return mir_func;
    }

    // エントリーブロックを作成
    mir_func->entry_block = 0;
    mir_func->basic_blocks.push_back(std::make_unique<BasicBlock>(0));

    // LoweringContextを作成
    LoweringContext ctx(mir_func.get());
    ctx.enum_defs = &enum_defs;
    ctx.typedef_defs = &typedef_defs;
    ctx.struct_defs = &struct_defs;
    ctx.interface_names = &interface_names;
    ctx.hir_func_defs = &hir_functions;
    ctx.interface_method_returns = &interface_method_returns_;
    ctx.tagged_union_names = &tagged_union_names;
    ctx.global_const_values = &global_const_values;

    // デストラクタを持つ型の情報をコンテキストに渡す
    for (const auto& type_name : types_with_destructor) {
        ctx.register_type_with_destructor(type_name);
    }

    // 関数パラメータをローカル変数として登録（typedefを解決）。
    // グローバル変数より先に登録し「パラメータ=ローカル1..N」の規約を保つ
    // （LLVM側のself判定・一時変数割り付けはこの番号付けを前提とする）
    for (const auto& param : func.params) {
        auto resolved_param_type = resolve_typedef(param.type);

        LocalId param_id = ctx.new_local(param.name, resolved_param_type, false);
        mir_func->arg_locals.push_back(param_id);
        ctx.register_variable(param.name, param_id);

        debug::log(
            debug::Stage::Mir, debug::Level::Debug,
            "Registered parameter '" + param.name + "' as local " + std::to_string(param_id));
    }

    // グローバル変数をスコープに登録（is_global=trueのLocalDeclとして）。
    // パラメータ登録後に行い、同名はパラメータ側を優先する
    for (const auto& gv : mir_program.global_vars) {
        if (!gv)
            continue;
        if (ctx.resolve_variable(gv->name)) {
            continue;
        }
        LocalId gv_id = ctx.new_local(gv->name, gv->type, !gv->is_const, true, false, true);
        ctx.register_variable(gv->name, gv_id);
    }

    // mainのエントリでグローバル変数の非定数初期化子を評価する。
    // 定数評価できない初期化子（関数呼び出し・構造体リテラル・スライスリテラル等）は
    // 従来コード生成で黙って捨てられ、グローバルがゼロ値のままになっていた
    // R1: #[test]関数はテストモードでmainを経由せず直接JITエントリになる（関数ごとに独立JITで状態隔離）ため、同じ初期化列を各テスト関数のエントリにも注入する（従来はスライス等の非定数グローバルがnullのままでテストのみSIGSEGVしていた）
    bool inject_global_inits = (func.name == "main");
    if (!inject_global_inits) {
        for (const auto& fn_attr : func.attributes) {
            if (fn_attr == "test") {
                inject_global_inits = true;
                break;
            }
        }
    }
    if (inject_global_inits) {
        for (const auto& gv : mir_program.global_vars) {
            if (!gv || !gv->type) {
                continue;
            }
            auto gtype = resolve_typedef(gv->type);
            auto gid_opt = ctx.resolve_variable(gv->name);
            if (!gid_opt) {
                continue;
            }
            const LocalId gid = *gid_opt;
            if (!gv->init_expr) {
                continue;
            }
            // 型名なしの構造体リテラル（JsonArena arena = { count: 0 }; 等）は
            // 無名構造体一時の型解決ができずコード生成が壊れるため、フィールド単位の代入へ分解する
            if (gtype && gtype->kind == hir::TypeKind::Struct) {
                if (auto* lit_ptr =
                        std::get_if<std::unique_ptr<hir::HirStructLiteral>>(&gv->init_expr->kind)) {
                    if (*lit_ptr && (*lit_ptr)->type_name.empty()) {
                        for (const auto& field : (*lit_ptr)->fields) {
                            if (!field.value) {
                                continue;
                            }
                            auto fidx = ctx.get_field_index(gtype->name, field.name);
                            if (!fidx) {
                                continue;
                            }
                            LocalId fval = expr_lowering.lower_expression(*field.value, ctx);
                            MirPlace fplace{gid};
                            fplace.projections.push_back(PlaceProjection::field(*fidx));
                            ctx.push_statement(MirStatement::assign(
                                fplace, MirRvalue::use(MirOperand::copy(MirPlace{fval}))));
                        }
                        continue;
                    }
                }
            }
            LocalId init_val = expr_lowering.lower_expression(*gv->init_expr, ctx);
            hir::TypePtr vt =
                (init_val < ctx.func->locals.size()) ? ctx.func->locals[init_val].type : nullptr;
            if (vt) {
                vt = ctx.resolve_typedef(vt);
            }
            const bool gtype_is_slice =
                gtype && gtype->kind == hir::TypeKind::Array && !gtype->array_size.has_value();
            if (gtype_is_slice && vt && vt->kind == hir::TypeKind::Array &&
                vt->array_size.has_value()) {
                // スライス型グローバルへ固定長配列で実体化された初期化子は
                // cm_array_to_sliceでヒープスライスへ変換して格納
                const int64_t arr_size = static_cast<int64_t>(vt->array_size.value_or(0));
                const int64_t elem_size = layout::array_elem_stride(ctx, vt->element_type);
                LocalId addr_local = ctx.new_temp(hir::make_pointer(vt->element_type));
                ctx.push_statement(MirStatement::assign(MirPlace{addr_local},
                                                        MirRvalue::ref(MirPlace{init_val}, false)));
                LocalId size_local = ctx.new_temp(hir::make_long());
                MirConstant size_const;
                size_const.value = arr_size;
                size_const.type = hir::make_long();
                ctx.push_statement(MirStatement::assign(
                    MirPlace{size_local}, MirRvalue::use(MirOperand::constant(size_const))));
                LocalId es_local = ctx.new_temp(hir::make_long());
                MirConstant es_const;
                es_const.value = elem_size;
                es_const.type = hir::make_long();
                ctx.push_statement(MirStatement::assign(
                    MirPlace{es_local}, MirRvalue::use(MirOperand::constant(es_const))));
                BlockId conv_block = ctx.new_block();
                std::vector<MirOperandPtr> conv_args;
                conv_args.push_back(MirOperand::copy(MirPlace{addr_local}));
                conv_args.push_back(MirOperand::copy(MirPlace{size_local}));
                conv_args.push_back(MirOperand::copy(MirPlace{es_local}));
                auto conv_term = std::make_unique<MirTerminator>();
                conv_term->kind = MirTerminator::Call;
                conv_term->data =
                    MirTerminator::CallData{MirOperand::function_ref("cm_array_to_slice"),
                                            std::move(conv_args),
                                            MirPlace{gid},
                                            conv_block,
                                            std::nullopt,
                                            "",
                                            "",
                                            false};
                ctx.set_terminator(std::move(conv_term));
                ctx.switch_to_block(conv_block);
            } else {
                ctx.push_statement(MirStatement::assign(
                    MirPlace{gid}, MirRvalue::use(MirOperand::copy(MirPlace{init_val}))));
            }
        }
    }

    // 文を処理（モジュラーコンポーネントを使用）
    for (const auto& stmt : func.body) {
        if (stmt) {
            stmt_lowering.lower_statement(*stmt, ctx);
        }
    }

    // デフォルト値で戻る（return文がない場合）
    auto* current = ctx.get_current_block();
    if (current && !current->terminator) {
        // 暗黙の関数終端でも明示returnと同一のdefer展開（逆順）を通す（B9）
        auto end_defers = ctx.get_defer_stmts();
        for (const auto* defer_stmt : end_defers) {
            stmt_lowering.lower_statement(*defer_stmt, ctx);
        }

        // デストラクタを呼び出す（defer逆順→dtor逆順の規約を維持）
        emit_destructors(ctx);

        // 構造体、void、配列（動的スライス含む）型はデフォルト値代入をスキップ
        bool skip_default_assign = false;
        auto resolved_return_type = resolve_typedef(func.return_type);
        if (resolved_return_type) {
            if (resolved_return_type->kind == hir::TypeKind::Struct ||
                resolved_return_type->kind == hir::TypeKind::Void ||
                resolved_return_type->kind == hir::TypeKind::Array) {
                skip_default_assign = true;
            }
        }

        if (!skip_default_assign) {
            MirConstant default_return;
            default_return.type = resolved_return_type;
            // 型に応じたデフォルト値を設定
            if (resolved_return_type && resolved_return_type->is_floating()) {
                default_return.value = 0.0;
            } else {
                default_return.value = int64_t(0);
            }

            ctx.push_statement(MirStatement::assign(
                MirPlace{0}, MirRvalue::use(MirOperand::constant(default_return))));
        }
        ctx.set_terminator(MirTerminator::return_value());
    }

    // デバッグ: 最終的なbb0の内容を確認
    if (mir_func->name == "main" && !mir_func->basic_blocks.empty()) {
        auto* bb0 = mir_func->basic_blocks[0].get();
        if (bb0) {
            debug_msg("mir_final_bb0", "[MIR] Final bb0 for main has " +
                                           std::to_string(bb0->statements.size()) + " statements");
            for (size_t i = 0; i < bb0->statements.size(); i++) {
                if (bb0->statements[i] && bb0->statements[i]->kind == MirStatement::Assign) {
                    auto& assign = std::get<MirStatement::AssignData>(bb0->statements[i]->data);
                    debug_msg("mir_final_bb0", "[MIR]   Statement " + std::to_string(i) +
                                                   ": assign to local " +
                                                   std::to_string(assign.place.local));
                }
            }
        }
    }

    return mir_func;
}

// デストラクタ呼び出しを生成
void MirLowering::emit_destructors(LoweringContext& ctx) {
    auto destructor_vars = ctx.get_all_destructor_vars();
    for (const auto& [local_id, type_name] : destructor_vars) {
        // ネストジェネリック型名の正規化（Vector<int> → Vector__int）
        std::string normalized_name = type_name;
        if (normalized_name.find('<') != std::string::npos) {
            std::string result;
            for (char c : normalized_name) {
                if (c == '<' || c == '>') {
                    if (c == '<')
                        result += "__";
                } else if (c == ',' || c == ' ') {
                    // カンマと空白は省略
                } else {
                    result += c;
                }
            }
            normalized_name = result;
        }
        std::string dtor_name = normalized_name + "__dtor";

        // デストラクタ呼び出しを生成
        std::vector<MirOperandPtr> args;
        args.push_back(MirOperand::copy(MirPlace{local_id}));

        BlockId success_block = ctx.new_block();

        auto func_operand = MirOperand::function_ref(dtor_name);
        auto call_term = std::make_unique<MirTerminator>();
        call_term->kind = MirTerminator::Call;
        call_term->data = MirTerminator::CallData{std::move(func_operand),
                                                  std::move(args),
                                                  std::nullopt,  // void戻り値
                                                  success_block,
                                                  std::nullopt,
                                                  "",
                                                  "",
                                                  false};  // 通常の関数呼び出し
        ctx.set_terminator(std::move(call_term));
        ctx.switch_to_block(success_block);
    }
}

// impl内のメソッドをlowering
void MirLowering::lower_impl(const hir::HirImpl& impl) {
    if (impl.target_type.empty())
        return;

    std::string type_name = impl.target_type;

    // 各メソッドをlowering
    for (const auto& method : impl.methods) {
        // メソッドを関数として処理
        auto mir_func = lower_function(*method);
        if (mir_func) {
            // コンストラクタ/デストラクタは既にマングル化された名前を持っている
            if (method->is_constructor || method->is_destructor) {
                mir_func->name = method->name;
            } else {
                // 通常のメソッドは type__method_name 形式にする（規則はmangle.hppへ集約）
                mir_func->name = mangle::method_name(type_name, method->name);
            }

            // hir_functionsへ登録する（ジェネリックはモノモーフィゼーション用、非ジェネリックも補間ミニパイプラインの戻り型解決が参照するため必要。
            // モノモーフィゼーション側はgeneric_paramsと名前の'<'で判別するので
            // 非ジェネリックの登録は無害）
            hir_functions[mir_func->name] = method.get();
            bool has_generic = !method->generic_params.empty() || !impl.generic_params.empty() ||
                               type_name.find('<') != std::string::npos;
            if (has_generic) {
                debug_msg("MIR",
                          "Registered generic impl method: " + mir_func->name +
                              " (method params: " + std::to_string(method->generic_params.size()) +
                              ", impl params: " + std::to_string(impl.generic_params.size()) +
                              ", type_name: " + type_name + ")");
            }

            mir_program.functions.push_back(std::move(mir_func));
        }
    }

    // 各演算子実装をlowering
    for (const auto& op_impl : impl.operators) {
        if (!op_impl)
            continue;

        // 専用のlowering関数を使用
        auto mir_func = lower_operator(*op_impl, type_name);
        if (mir_func) {
            debug_msg("MIR", "Lowered operator: " + mir_func->name);

            // impl_infoに登録
            if (op_impl->op == hir::HirOperatorKind::Eq) {
                impl_info[type_name]["Eq"] = mir_func->name;
            } else if (op_impl->op == hir::HirOperatorKind::Lt) {
                impl_info[type_name]["Ord"] = mir_func->name;
            } else if (op_impl->op == hir::HirOperatorKind::Add) {
                impl_info[type_name]["Add"] = mir_func->name;
            } else if (op_impl->op == hir::HirOperatorKind::Sub) {
                impl_info[type_name]["Sub"] = mir_func->name;
            } else if (op_impl->op == hir::HirOperatorKind::Mul) {
                impl_info[type_name]["Mul"] = mir_func->name;
            } else if (op_impl->op == hir::HirOperatorKind::Div) {
                impl_info[type_name]["Div"] = mir_func->name;
            } else if (op_impl->op == hir::HirOperatorKind::Mod) {
                impl_info[type_name]["Mod"] = mir_func->name;
            } else if (op_impl->op == hir::HirOperatorKind::BitAnd) {
                impl_info[type_name]["BitAnd"] = mir_func->name;
            } else if (op_impl->op == hir::HirOperatorKind::BitOr) {
                impl_info[type_name]["BitOr"] = mir_func->name;
            } else if (op_impl->op == hir::HirOperatorKind::BitXor) {
                impl_info[type_name]["BitXor"] = mir_func->name;
            } else if (op_impl->op == hir::HirOperatorKind::Shl) {
                impl_info[type_name]["Shl"] = mir_func->name;
            } else if (op_impl->op == hir::HirOperatorKind::Shr) {
                impl_info[type_name]["Shr"] = mir_func->name;
            }

            mir_program.functions.push_back(std::move(mir_func));
        }
    }
}

}  // namespace cm::mir