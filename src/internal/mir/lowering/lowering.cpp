// lowering.cpp - MIR Lowering メインクラスの実装
// HIRからMIRへの変換

#include "lowering.hpp"

#include "internal/base/debug.hpp"
#include "internal/base/i18n.hpp"
#include "internal/base/mangle.hpp"

#include <algorithm>
#include <iostream>
#include <memory>
#include <numeric>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace cm::mir {

// MIRに__error__シンボル（未解決型のマングリング成果物）が残っていないか検査する。
// 関数名・呼び出し先FunctionRefの両方を走査し、検出時はエラー診断として報告する（codegen前停止はドライバが行う）
void MirLowering::check_error_artifacts(const MirProgram& mir_program) {
    constexpr const char* kErrorPrefix = "__error__";
    for (const auto& func : mir_program.functions) {
        if (!func) {
            continue;
        }
        if (func->name.rfind(kErrorPrefix, 0) == 0) {
            report_error(Span{}, i18n::msgf(i18n::MsgId::MirErrorSymbol, func->name, func->name));
            continue;
        }
        for (const auto& block : func->basic_blocks) {
            if (!block || !block->terminator || block->terminator->kind != MirTerminator::Call) {
                continue;
            }
            const auto& call = std::get<MirTerminator::CallData>(block->terminator->data);
            if (!call.func || call.func->kind != MirOperand::FunctionRef) {
                continue;
            }
            if (const auto* name = std::get_if<std::string>(&call.func->data)) {
                if (name->rfind(kErrorPrefix, 0) == 0) {
                    report_error(Span{},
                                 i18n::msgf(i18n::MsgId::MirErrorSymbol, *name, func->name));
                }
            }
        }
    }
}

MirProgram MirLowering::lower(const hir::HirProgram& hir_program) {
    if (cm::debug::debug_mode())
        std::cerr << "[MIR] Pass 0: process_imports" << std::endl;
    // Pass 0: インポートを処理
    process_imports(hir_program);

    if (cm::debug::debug_mode())
        std::cerr << "[MIR] Pass 1: register_declarations" << std::endl;
    // Pass 1: 構造体、typedef、enum、インターフェースの登録
    register_declarations(hir_program);

    if (cm::debug::debug_mode())
        std::cerr << "[MIR] Pass 1.5: generate_auto_impls" << std::endl;
    // Pass 1.5: with キーワードによる自動実装を生成（非ジェネリック構造体のみ）
    generate_auto_impls(hir_program);

    if (cm::debug::debug_mode())
        std::cerr << "[MIR] Pass 2: lower_functions" << std::endl;
    // Pass 2: 関数のlowering
    lower_functions(hir_program);

    if (cm::debug::debug_mode())
        std::cerr << "[MIR] Pass 3: lower_impl_methods" << std::endl;
    // Pass 3: impl内のメソッドのlowering
    lower_impl_methods(hir_program);

    if (cm::debug::debug_mode())
        std::cerr << "[MIR] Pass 4: perform_monomorphization" << std::endl;
    // Pass 4: モノモーフィゼーション（インターフェース特殊化）
    perform_monomorphization();

    if (cm::debug::debug_mode())
        std::cerr << "[MIR] Pass 5: generate_monomorphized_auto_impls" << std::endl;
    // Pass 5: モノモーフィゼーション後のジェネリック構造体に対する自動実装を生成
    generate_monomorphized_auto_impls();

    if (cm::debug::debug_mode())
        std::cerr << "[MIR] Pass 6: rewrite_struct_comparison_operators" << std::endl;
    // Pass 6: 構造体比較演算子を関数呼び出しに変換
    rewrite_struct_comparison_operators();

    if (cm::debug::debug_mode())
        std::cerr << "[MIR] Pass 7: propagate_closure_info" << std::endl;
    // Pass 7: クロージャ情報を伝播（固定点まで繰り返し）
    propagate_closure_info();

    if (cm::debug::debug_mode())
        std::cerr << "[MIR] Pass 8: rewrite_hof_calls_for_closures" << std::endl;
    // Pass 8: 高階関数呼び出しをクロージャ版に書き換え
    rewrite_hof_calls_for_closures();

    if (cm::debug::debug_mode())
        std::cerr << "[MIR] All passes complete" << std::endl;

    // Bug#45修正: モジュール修飾名の正規化
    // import先の関数がモジュール修飾名（例: kmalloc::heap_size_to_class）でFunctionRefに登録されている場合、関数定義名（heap_size_to_class）と一致しないためDCEで誤削除される。全FunctionRefを単純名に統一する。
    {
        // まず関数名のマッピングを構築（修飾名 → 単純名）
        std::unordered_map<std::string, std::string> name_map;
        for (const auto& func : mir_program.functions) {
            if (!func)
                continue;
            // 関数名に :: が含まれていない場合はそのまま
            // 関数定義自体の名前は lower_function で既に正規化済み
            name_map[func->name] = func->name;
        }

        for (auto& func : mir_program.functions) {
            if (!func)
                continue;
            for (auto& bb : func->basic_blocks) {
                if (!bb || !bb->terminator)
                    continue;
                if (bb->terminator->kind == MirTerminator::Call) {
                    auto& call_data = std::get<MirTerminator::CallData>(bb->terminator->data);
                    if (call_data.func && call_data.func->kind == MirOperand::FunctionRef) {
                        auto& ref_name = std::get<std::string>(call_data.func->data);
                        // モジュール修飾を除去
                        auto pos = ref_name.rfind("::");
                        if (pos != std::string::npos) {
                            std::string simple_name = ref_name.substr(pos + 2);
                            // 単純名の関数が存在するか確認
                            if (name_map.count(simple_name) > 0) {
                                ref_name = simple_name;
                            }
                        }
                    }
                }
            }
            // ステートメント内のFunctionRefも正規化
            for (auto& bb : func->basic_blocks) {
                if (!bb)
                    continue;
                for (auto& stmt : bb->statements) {
                    if (!stmt || stmt->kind != MirStatement::Assign)
                        continue;
                    auto& assign = std::get<MirStatement::AssignData>(stmt->data);
                    if (!assign.rvalue)
                        continue;
                    if (assign.rvalue->kind == MirRvalue::Use) {
                        auto& use_data = std::get<MirRvalue::UseData>(assign.rvalue->data);
                        if (use_data.operand && use_data.operand->kind == MirOperand::FunctionRef) {
                            auto& ref_name = std::get<std::string>(use_data.operand->data);
                            auto pos = ref_name.rfind("::");
                            if (pos != std::string::npos) {
                                std::string simple_name = ref_name.substr(pos + 2);
                                if (name_map.count(simple_name) > 0) {
                                    ref_name = simple_name;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // typedef定義をMirProgramにコピー（LLVM backendでTypeAlias解決に使用）
    mir_program.typedef_defs = typedef_defs;

    // エラー型成果物の検査（diagnostics-engine-unification 第3段）:
    // 型検査のエラー回復で漏れた未解決型はマングリングで__error__*シンボルになりリンク不能・誤コンパイルとして顕在化する（B6/B7/W5(d)/N2族）。
    // HIR→MIR境界の最終検査としてMIRに__error__シンボルが存在しないことを保証し、検出時はcodegen前に停止させる
    check_error_artifacts(mir_program);

    return std::move(mir_program);
}

// 高階関数呼び出し（map/filter）をクロージャ対応版に書き換え
void MirLowering::rewrite_hof_calls_for_closures() {
    for (auto& func : mir_program.functions) {
        if (!func)
            continue;

        for (auto& block : func->basic_blocks) {
            if (!block || !block->terminator)
                continue;

            auto& term = block->terminator;
            if (term->kind != MirTerminator::Call)
                continue;

            auto& call_data = std::get<MirTerminator::CallData>(term->data);
            if (!call_data.func)
                continue;

            // 関数名を取得
            std::string func_name;
            if (call_data.func->kind == MirOperand::FunctionRef) {
                func_name = std::get<std::string>(call_data.func->data);
            } else {
                continue;
            }

            // 既に_closure版なら処理しない
            if (func_name.find("_closure") != std::string::npos)
                continue;

            // 対象の高階関数かチェック（コールバックは第3引数 args[2]。
            // reduceは第4引数に初期値を持つためargs.back()ではなくインデックスで特定する）
            static const std::unordered_set<std::string> hof_with_closure_support = {
                "__builtin_array_map",
                "__builtin_array_map_i8",
                "__builtin_array_map_i16",
                "__builtin_array_map_i64",
                "__builtin_array_map_f32",
                "__builtin_array_map_f64",
                "__builtin_array_filter",
                "__builtin_array_filter_i8",
                "__builtin_array_filter_i16",
                "__builtin_array_filter_i64",
                "__builtin_array_filter_f32",
                "__builtin_array_filter_f64",
                "__builtin_array_reduce_i8",
                "__builtin_array_reduce_i16",
                "__builtin_array_reduce_i32",
                "__builtin_array_reduce_i64",
                "__builtin_array_reduce_f32",
                "__builtin_array_reduce_f64",
                "__builtin_array_reduce_i32_acc64",
                "__builtin_array_forEach_i8",
                "__builtin_array_forEach_i16",
                "__builtin_array_forEach_i32",
                "__builtin_array_forEach_i64",
                "__builtin_array_forEach_f32",
                "__builtin_array_forEach_f64",
                "__builtin_array_some_i8",
                "__builtin_array_some_i16",
                "__builtin_array_some_i32",
                "__builtin_array_some_i64",
                "__builtin_array_some_f32",
                "__builtin_array_some_f64",
                "__builtin_array_every_i8",
                "__builtin_array_every_i16",
                "__builtin_array_every_i32",
                "__builtin_array_every_i64",
                "__builtin_array_every_f32",
                "__builtin_array_every_f64",
                "__builtin_array_findIndex_i8",
                "__builtin_array_findIndex_i16",
                "__builtin_array_findIndex_i32",
                "__builtin_array_findIndex_i64",
                "__builtin_array_findIndex_f32",
                "__builtin_array_findIndex_f64",
            };
            if (hof_with_closure_support.count(func_name) == 0)
                continue;

            // コールバック引数（args[2]）がクロージャかチェック
            if (call_data.args.size() < 3)
                continue;

            auto& fn_arg = call_data.args[2];
            if (!fn_arg || (fn_arg->kind != MirOperand::Copy && fn_arg->kind != MirOperand::Move))
                continue;

            auto& place = std::get<MirPlace>(fn_arg->data);
            if (place.local >= func->locals.size())
                continue;

            const auto& local_decl = func->locals[place.local];
            if (!local_decl.is_closure || local_decl.captured_locals.empty())
                continue;

            // クロージャ版に書き換え
            call_data.func = MirOperand::function_ref(func_name + "_closure");

            // コールバックを関数参照に置き換え
            call_data.args[2] = MirOperand::function_ref(local_decl.closure_func_name);

            // キャプチャ値を末尾引数として追加（reduceは初期値の後ろに並ぶ）
            for (LocalId cap_local : local_decl.captured_locals) {
                call_data.args.push_back(MirOperand::copy(MirPlace{cap_local}));
            }
        }
    }
}

// クロージャ情報を代入先変数に伝播（固定点まで繰り返し）
void MirLowering::propagate_closure_info() {
    bool changed = true;
    int max_iterations = 20;  // 無限ループ防止

    while (changed && max_iterations > 0) {
        changed = false;
        max_iterations--;

        for (auto& func : mir_program.functions) {
            if (!func)
                continue;

            // 各ブロックのステートメントを走査
            for (auto& block : func->basic_blocks) {
                if (!block)
                    continue;

                for (auto& stmt : block->statements) {
                    if (!stmt || stmt->kind != MirStatement::Assign)
                        continue;

                    auto& data = std::get<MirStatement::AssignData>(stmt->data);
                    if (!data.rvalue)
                        continue;

                    // Use(Copy/Move)の場合
                    if (data.rvalue->kind == MirRvalue::Use) {
                        auto& use_data = std::get<MirRvalue::UseData>(data.rvalue->data);
                        if (!use_data.operand)
                            continue;

                        if (use_data.operand->kind == MirOperand::Copy ||
                            use_data.operand->kind == MirOperand::Move) {
                            auto& src_place = std::get<MirPlace>(use_data.operand->data);

                            // ソース変数がクロージャかチェック
                            if (src_place.local < func->locals.size()) {
                                const auto& src_decl = func->locals[src_place.local];
                                if (src_decl.is_closure && !src_decl.captured_locals.empty()) {
                                    // 宛先変数にもクロージャ情報をコピー
                                    if (data.place.projections.empty() &&
                                        data.place.local < func->locals.size()) {
                                        auto& dst_decl = func->locals[data.place.local];
                                        if (!dst_decl.is_closure) {
                                            dst_decl.is_closure = true;
                                            dst_decl.closure_func_name = src_decl.closure_func_name;
                                            dst_decl.captured_locals = src_decl.captured_locals;
                                            changed = true;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

// 宣言の登録
void MirLowering::register_declarations(const hir::HirProgram& hir_program) {
    // typedef定義を収集（最初に登録）
    for (const auto& decl : hir_program.declarations) {
        if (auto* td = std::get_if<std::unique_ptr<hir::HirTypedef>>(&decl->kind)) {
            register_typedef(**td);
        }
    }

    // enum定義を収集（構造体より前に登録）
    for (const auto& decl : hir_program.declarations) {
        if (auto* e = std::get_if<std::unique_ptr<hir::HirEnum>>(&decl->kind)) {
            register_enum(**e);
            // ソースファイル情報を設定（モジュール分割用）
            if (!mir_program.enums.empty()) {
                mir_program.enums.back()->source_file = resolve_source_file(decl->span.start);
            }
        }
    }

    // グローバルconst変数を収集（文字列補間で使用）
    for (const auto& decl : hir_program.declarations) {
        if (auto* gv = std::get_if<std::unique_ptr<hir::HirGlobalVar>>(&decl->kind)) {
            register_global_var(**gv);
        }
    }

    // 構造体定義を収集（typedef/enumが登録された後に処理）
    for (const auto& decl : hir_program.declarations) {
        if (auto* st = std::get_if<std::unique_ptr<hir::HirStruct>>(&decl->kind)) {
            register_struct(**st);
            // ジェネリック構造体はモノモーフィゼーション時に特殊化されるのでスキップ
            if (!(*st)->generic_params.empty()) {
                continue;
            }
            // MIR構造体を生成してプログラムに追加
            auto mir_struct = create_mir_struct(**st);
            // ソースファイル情報を設定（モジュール分割用）
            mir_struct.source_file = resolve_source_file(decl->span.start);
            mir_program.structs.push_back(std::make_unique<MirStruct>(std::move(mir_struct)));
        }
    }

    // インターフェース定義を収集
    for (const auto& decl : hir_program.declarations) {
        if (auto* iface = std::get_if<std::unique_ptr<hir::HirInterface>>(&decl->kind)) {
            interface_names.insert((*iface)->name);
            register_interface(**iface);
        }
    }

    // impl定義から実装情報を収集
    for (const auto& decl : hir_program.declarations) {
        if (auto* impl = std::get_if<std::unique_ptr<hir::HirImpl>>(&decl->kind)) {
            register_impl(**impl);
        }
    }

    // vtable生成（impl登録後に実行）
    generate_vtables();
}

// HirOperatorKindをMirOperatorKindに変換
MirOperatorKind MirLowering::convert_hir_operator_kind(hir::HirOperatorKind kind) {
    switch (kind) {
        case hir::HirOperatorKind::Eq:
            return MirOperatorKind::Eq;
        case hir::HirOperatorKind::Ne:
            return MirOperatorKind::Ne;
        case hir::HirOperatorKind::Lt:
            return MirOperatorKind::Lt;
        case hir::HirOperatorKind::Gt:
            return MirOperatorKind::Gt;
        case hir::HirOperatorKind::Le:
            return MirOperatorKind::Le;
        case hir::HirOperatorKind::Ge:
            return MirOperatorKind::Ge;
        case hir::HirOperatorKind::Add:
            return MirOperatorKind::Add;
        case hir::HirOperatorKind::Sub:
            return MirOperatorKind::Sub;
        case hir::HirOperatorKind::Mul:
            return MirOperatorKind::Mul;
        case hir::HirOperatorKind::Div:
            return MirOperatorKind::Div;
        case hir::HirOperatorKind::Mod:
            return MirOperatorKind::Mod;
        default:
            return MirOperatorKind::Eq;  // デフォルト
    }
}

// インターフェース定義を登録
void MirLowering::register_interface(const hir::HirInterface& iface) {
    auto mir_iface = std::make_unique<MirInterface>();
    mir_iface->name = iface.name;

    // ジェネリックパラメータを記録
    for (const auto& param : iface.generic_params) {
        mir_iface->generic_params.push_back(param.name);
    }

    for (const auto& method : iface.methods) {
        MirInterfaceMethod mir_method;
        mir_method.name = method.name;
        mir_method.return_type = method.return_type;
        for (const auto& param : method.params) {
            mir_method.param_types.push_back(param.type);
        }
        mir_iface->methods.push_back(std::move(mir_method));

        // 補間ミニパイプラインの戻り値型解決用にインターフェイス宣言のシグネチャを記録する（B7）
        interface_method_returns_[mangle::method_name(iface.name, method.name)] =
            method.return_type;
    }

    // 演算子シグネチャを登録
    for (const auto& op : iface.operators) {
        MirOperatorSig mir_op;
        mir_op.op = convert_hir_operator_kind(op.op);
        mir_op.return_type = op.return_type;
        for (const auto& param : op.params) {
            mir_op.param_types.push_back(param.type);
        }
        mir_iface->operators.push_back(std::move(mir_op));
    }

    // インターフェース定義を保存（自動実装生成に使用）
    interface_defs_[iface.name] = &iface;

    mir_program.interfaces.push_back(std::move(mir_iface));
}

// vtable生成
void MirLowering::generate_vtables() {
    // 各インターフェース実装に対してvtableを生成
    for (const auto& [type_name, iface_map] : impl_info) {
        for (const auto& [interface_name, impl_method_name] : iface_map) {
            // @initは内部用なのでスキップ
            if (interface_name == "@init")
                continue;

            // 対応するインターフェース定義を検索
            const MirInterface* mir_iface = nullptr;
            for (const auto& iface : mir_program.interfaces) {
                if (iface && iface->name == interface_name) {
                    mir_iface = iface.get();
                    break;
                }
            }

            if (!mir_iface)
                continue;

            // vtableを生成
            auto vtable = std::make_unique<VTable>();
            vtable->type_name = type_name;
            vtable->interface_name = interface_name;

            // 各メソッドのエントリを追加
            for (const auto& method : mir_iface->methods) {
                VTableEntry entry;
                entry.method_name = method.name;
                entry.impl_function_name = type_name + "__" + method.name;
                vtable->entries.push_back(std::move(entry));
            }

            mir_program.vtables.push_back(std::move(vtable));
        }
    }
}

// impl定義を登録
void MirLowering::register_impl(const hir::HirImpl& impl) {
    if (impl.target_type.empty())
        return;

    std::string type_name = impl.target_type;

    // インターフェース実装の場合
    if (!impl.interface_name.empty()) {
        std::string interface_name = impl.interface_name;
        for (const auto& method : impl.methods) {
            std::string impl_method_name = type_name + "__" + method->name;
            impl_info[type_name][interface_name] = impl_method_name;
        }
    }

    // デストラクタを持つ型を記録
    for (const auto& method : impl.methods) {
        if (method && method->is_destructor) {
            types_with_destructor.insert(type_name);
            break;
        }
    }
}

}  // namespace cm::mir
