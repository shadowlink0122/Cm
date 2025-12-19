#pragma once

#include "expr_lowering.hpp"
#include "lowering_base.hpp"
#include "monomorphization.hpp"
#include "stmt_lowering.hpp"

namespace cm::mir {

// ============================================================
// MIR Lowering
// HIRからMIRへの変換のメインクラス
// ============================================================
class MirLowering : public MirLoweringBase {
   private:
    StmtLowering stmt_lowering;
    ExprLowering expr_lowering;
    Monomorphization monomorphizer;

   public:
    MirLowering() {
        // 相互参照の設定
        stmt_lowering.set_expr_lowering(&expr_lowering);
        // impl_infoを共有
        expr_lowering.set_shared_impl_info(&impl_info);
        stmt_lowering.set_shared_impl_info(&impl_info);
    }

    // HIRプログラムをMIRに変換
    MirProgram lower(const hir::HirProgram& hir_program) {
        // Pass 0: インポートを処理
        process_imports(hir_program);

        // Pass 1: 構造体、typedef、enum、インターフェースの登録
        register_declarations(hir_program);

        // Pass 1.5: with キーワードによる自動実装を生成（非ジェネリック構造体のみ）
        generate_auto_impls(hir_program);

        // Pass 2: 関数のlowering
        lower_functions(hir_program);

        // Pass 3: impl内のメソッドのlowering
        lower_impl_methods(hir_program);

        // Pass 4: モノモーフィゼーション（インターフェース特殊化）
        perform_monomorphization();

        // Pass 5: モノモーフィゼーション後のジェネリック構造体に対する自動実装を生成
        generate_monomorphized_auto_impls();

        // Pass 6: 構造体比較演算子を関数呼び出しに変換
        rewrite_struct_comparison_operators();

        return std::move(mir_program);
    }

   private:
    // 宣言の登録
    void register_declarations(const hir::HirProgram& hir_program) {
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
    MirOperatorKind convert_hir_operator_kind(hir::HirOperatorKind kind) {
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
    void register_interface(const hir::HirInterface& iface) {
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
    void generate_vtables() {
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
    void register_impl(const hir::HirImpl& impl) {
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

    // with キーワードによる自動実装を生成
    void generate_auto_impls(const hir::HirProgram& hir_program) {
        for (const auto& decl : hir_program.declarations) {
            if (auto* st = std::get_if<std::unique_ptr<hir::HirStruct>>(&decl->kind)) {
                const auto& struct_decl = **st;

                // auto_impls が空なら何もしない
                if (struct_decl.auto_impls.empty())
                    continue;

                // ジェネリック構造体はモノモーフィゼーション時に処理
                if (!struct_decl.generic_params.empty()) {
                    // auto_implsを保存してモノモーフィゼーション後に生成
                    generic_struct_auto_impls_[struct_decl.name] = struct_decl.auto_impls;
                    continue;
                }

                for (const auto& iface_name : struct_decl.auto_impls) {
                    // 組み込みインターフェースの場合は直接生成
                    if (iface_name == "Eq") {
                        generate_builtin_eq_operator(struct_decl);
                        continue;
                    }
                    if (iface_name == "Ord") {
                        generate_builtin_lt_operator(struct_decl);
                        continue;
                    }
                    if (iface_name == "Copy") {
                        // マーカーインターフェース、コード生成なし
                        impl_info[struct_decl.name]["Copy"] = "";
                        continue;
                    }
                    if (iface_name == "Clone") {
                        generate_builtin_clone_method(struct_decl);
                        continue;
                    }
                    if (iface_name == "Hash") {
                        generate_builtin_hash_method(struct_decl);
                        continue;
                    }

                    // ユーザー定義インターフェースの場合
                    auto iface_it = interface_defs_.find(iface_name);
                    if (iface_it == interface_defs_.end()) {
                        // インターフェースが見つからない場合はスキップ
                        continue;
                    }

                    const auto* iface = iface_it->second;

                    // 演算子の自動実装を生成
                    for (const auto& op : iface->operators) {
                        generate_auto_operator_impl(struct_decl, *iface, op);
                    }
                }
            }
        }
    }

    // ジェネリック構造体のauto_implsを保存
    std::unordered_map<std::string, std::vector<std::string>> generic_struct_auto_impls_;

    // モノモーフィゼーション後のジェネリック構造体に対する自動実装を生成
    void generate_monomorphized_auto_impls() {
        // MIRプログラム内のモノモーフィゼーションされた構造体を走査
        for (const auto& mir_struct : mir_program.structs) {
            if (!mir_struct)
                continue;

            const std::string& struct_name = mir_struct->name;

            // 元のジェネリック構造体名を抽出（例: Pair__int__int -> Pair）
            std::string base_name = struct_name;
            auto underscore_pos = struct_name.find("__");
            if (underscore_pos != std::string::npos) {
                base_name = struct_name.substr(0, underscore_pos);
            }

            // このジェネリック構造体にauto_implsがあるか確認
            auto it = generic_struct_auto_impls_.find(base_name);
            if (it == generic_struct_auto_impls_.end())
                continue;

            // 自動実装を生成
            for (const auto& iface_name : it->second) {
                if (iface_name == "Eq") {
                    generate_builtin_eq_operator_for_monomorphized(*mir_struct);
                } else if (iface_name == "Ord") {
                    generate_builtin_lt_operator_for_monomorphized(*mir_struct);
                } else if (iface_name == "Copy") {
                    impl_info[struct_name]["Copy"] = "";
                } else if (iface_name == "Clone") {
                    generate_builtin_clone_method_for_monomorphized(*mir_struct);
                } else if (iface_name == "Hash") {
                    generate_builtin_hash_method_for_monomorphized(*mir_struct);
                }
            }
        }
    }

    // モノモーフィゼーションされた構造体用のEq演算子を生成
    void generate_builtin_eq_operator_for_monomorphized(const MirStruct& st) {
        std::string func_name = st.name + "__op_eq";

        // 既に生成されている場合はスキップ
        for (const auto& func : mir_program.functions) {
            if (func && func->name == func_name)
                return;
        }

        auto mir_func = std::make_unique<MirFunction>();
        mir_func->name = func_name;

        mir_func->return_local = mir_func->add_local("_0", hir::make_bool(), true, false);

        auto struct_type = hir::make_named(st.name);
        LocalId self_local = mir_func->add_local("self", struct_type, false, true);
        LocalId other_local = mir_func->add_local("other", struct_type, false, true);
        mir_func->arg_locals.push_back(self_local);
        mir_func->arg_locals.push_back(other_local);

        BlockId entry_block = mir_func->add_block();
        auto* block = mir_func->get_block(entry_block);

        if (st.fields.empty()) {
            auto const_true = std::make_unique<MirOperand>();
            const_true->kind = MirOperand::Constant;
            MirConstant c;
            c.value = true;
            c.type = hir::make_bool();
            const_true->data = c;

            block->statements.push_back(MirStatement::assign(
                MirPlace(mir_func->return_local), MirRvalue::use(std::move(const_true))));
            block->terminator = MirTerminator::return_value();
        } else {
            std::vector<LocalId> cmp_results;

            for (size_t i = 0; i < st.fields.size(); ++i) {
                const auto& field = st.fields[i];

                LocalId cmp_result =
                    mir_func->add_local("_cmp" + std::to_string(i), hir::make_bool(), true, false);
                cmp_results.push_back(cmp_result);

                LocalId self_field =
                    mir_func->add_local("_self_f" + std::to_string(i), field.type, true, false);
                auto self_place = MirPlace(self_local, {PlaceProjection::field(i)});
                block->statements.push_back(MirStatement::assign(
                    MirPlace(self_field), MirRvalue::use(MirOperand::copy(self_place))));

                LocalId other_field =
                    mir_func->add_local("_other_f" + std::to_string(i), field.type, true, false);
                auto other_place = MirPlace(other_local, {PlaceProjection::field(i)});
                block->statements.push_back(MirStatement::assign(
                    MirPlace(other_field), MirRvalue::use(MirOperand::copy(other_place))));

                block->statements.push_back(MirStatement::assign(
                    MirPlace(cmp_result),
                    MirRvalue::binary(MirBinaryOp::Eq, MirOperand::copy(MirPlace(self_field)),
                                      MirOperand::copy(MirPlace(other_field)))));
            }

            if (cmp_results.size() == 1) {
                block->statements.push_back(MirStatement::assign(
                    MirPlace(mir_func->return_local),
                    MirRvalue::use(MirOperand::copy(MirPlace(cmp_results[0])))));
            } else {
                LocalId acc = cmp_results[0];
                for (size_t i = 1; i < cmp_results.size(); ++i) {
                    LocalId new_acc = mir_func->add_local("_acc" + std::to_string(i),
                                                          hir::make_bool(), true, false);
                    block->statements.push_back(MirStatement::assign(
                        MirPlace(new_acc),
                        MirRvalue::binary(MirBinaryOp::And, MirOperand::copy(MirPlace(acc)),
                                          MirOperand::copy(MirPlace(cmp_results[i])))));
                    acc = new_acc;
                }
                block->statements.push_back(
                    MirStatement::assign(MirPlace(mir_func->return_local),
                                         MirRvalue::use(MirOperand::copy(MirPlace(acc)))));
            }

            block->terminator = MirTerminator::return_value();
        }

        impl_info[st.name]["Eq"] = func_name;
        mir_program.functions.push_back(std::move(mir_func));
    }

    // モノモーフィゼーションされた構造体用のOrd演算子を生成
    void generate_builtin_lt_operator_for_monomorphized(const MirStruct& st) {
        std::string func_name = st.name + "__op_lt";

        for (const auto& func : mir_program.functions) {
            if (func && func->name == func_name)
                return;
        }

        auto mir_func = std::make_unique<MirFunction>();
        mir_func->name = func_name;

        mir_func->return_local = mir_func->add_local("_0", hir::make_bool(), true, false);

        auto struct_type = hir::make_named(st.name);
        LocalId self_local = mir_func->add_local("self", struct_type, false, true);
        LocalId other_local = mir_func->add_local("other", struct_type, false, true);
        mir_func->arg_locals.push_back(self_local);
        mir_func->arg_locals.push_back(other_local);

        BlockId entry_block = mir_func->add_block();
        auto* block = mir_func->get_block(entry_block);

        if (st.fields.empty()) {
            auto const_false = std::make_unique<MirOperand>();
            const_false->kind = MirOperand::Constant;
            MirConstant c;
            c.value = false;
            c.type = hir::make_bool();
            const_false->data = c;

            block->statements.push_back(MirStatement::assign(
                MirPlace(mir_func->return_local), MirRvalue::use(std::move(const_false))));
            block->terminator = MirTerminator::return_value();
        } else {
            BlockId false_block = mir_func->add_block();

            std::vector<BlockId> field_blocks;
            for (size_t i = 0; i < st.fields.size(); ++i) {
                field_blocks.push_back(mir_func->add_block());
            }

            block->terminator = MirTerminator::goto_block(field_blocks[0]);

            for (size_t i = 0; i < st.fields.size(); ++i) {
                const auto& field = st.fields[i];
                auto* field_block = mir_func->get_block(field_blocks[i]);

                LocalId self_field =
                    mir_func->add_local("_self_f" + std::to_string(i), field.type, true, false);
                auto self_place = MirPlace(self_local, {PlaceProjection::field(i)});
                field_block->statements.push_back(MirStatement::assign(
                    MirPlace(self_field), MirRvalue::use(MirOperand::copy(self_place))));

                LocalId other_field =
                    mir_func->add_local("_other_f" + std::to_string(i), field.type, true, false);
                auto other_place = MirPlace(other_local, {PlaceProjection::field(i)});
                field_block->statements.push_back(MirStatement::assign(
                    MirPlace(other_field), MirRvalue::use(MirOperand::copy(other_place))));

                LocalId lt_result =
                    mir_func->add_local("_lt" + std::to_string(i), hir::make_bool(), true, false);
                field_block->statements.push_back(MirStatement::assign(
                    MirPlace(lt_result),
                    MirRvalue::binary(MirBinaryOp::Lt, MirOperand::copy(MirPlace(self_field)),
                                      MirOperand::copy(MirPlace(other_field)))));

                BlockId lt_true_block = mir_func->add_block();
                BlockId lt_false_check_block = mir_func->add_block();
                field_block->terminator =
                    MirTerminator::switch_int(MirOperand::copy(MirPlace(lt_result)),
                                              {{1, lt_true_block}}, lt_false_check_block);

                auto* true_ret_block = mir_func->get_block(lt_true_block);
                auto const_true = std::make_unique<MirOperand>();
                const_true->kind = MirOperand::Constant;
                MirConstant c_true;
                c_true.value = true;
                c_true.type = hir::make_bool();
                const_true->data = c_true;
                true_ret_block->statements.push_back(MirStatement::assign(
                    MirPlace(mir_func->return_local), MirRvalue::use(std::move(const_true))));
                true_ret_block->terminator = MirTerminator::return_value();

                auto* gt_check_block = mir_func->get_block(lt_false_check_block);
                LocalId gt_result =
                    mir_func->add_local("_gt" + std::to_string(i), hir::make_bool(), true, false);
                gt_check_block->statements.push_back(MirStatement::assign(
                    MirPlace(gt_result),
                    MirRvalue::binary(MirBinaryOp::Gt, MirOperand::copy(MirPlace(self_field)),
                                      MirOperand::copy(MirPlace(other_field)))));

                BlockId next_block = (i + 1 < st.fields.size()) ? field_blocks[i + 1] : false_block;
                gt_check_block->terminator = MirTerminator::switch_int(
                    MirOperand::copy(MirPlace(gt_result)), {{1, false_block}}, next_block);
            }

            auto* final_false_block = mir_func->get_block(false_block);
            auto const_false = std::make_unique<MirOperand>();
            const_false->kind = MirOperand::Constant;
            MirConstant c_false;
            c_false.value = false;
            c_false.type = hir::make_bool();
            const_false->data = c_false;
            final_false_block->statements.push_back(MirStatement::assign(
                MirPlace(mir_func->return_local), MirRvalue::use(std::move(const_false))));
            final_false_block->terminator = MirTerminator::return_value();
        }

        impl_info[st.name]["Ord"] = func_name;
        mir_program.functions.push_back(std::move(mir_func));
    }

    // モノモーフィゼーションされた構造体用のCloneメソッドを生成
    void generate_builtin_clone_method_for_monomorphized(const MirStruct& st) {
        std::string func_name = st.name + "__clone";

        for (const auto& func : mir_program.functions) {
            if (func && func->name == func_name)
                return;
        }

        auto mir_func = std::make_unique<MirFunction>();
        mir_func->name = func_name;

        auto struct_type = hir::make_named(st.name);
        mir_func->return_local = mir_func->add_local("_0", struct_type, true, false);

        LocalId self_local = mir_func->add_local("self", struct_type, false, true);
        mir_func->arg_locals.push_back(self_local);

        BlockId entry_block = mir_func->add_block();
        auto* block = mir_func->get_block(entry_block);

        block->statements.push_back(
            MirStatement::assign(MirPlace(mir_func->return_local),
                                 MirRvalue::use(MirOperand::copy(MirPlace(self_local)))));

        block->terminator = MirTerminator::return_value();

        impl_info[st.name]["Clone"] = func_name;
        mir_program.functions.push_back(std::move(mir_func));
    }

    // モノモーフィゼーションされた構造体用のHashメソッドを生成
    void generate_builtin_hash_method_for_monomorphized(const MirStruct& st) {
        std::string func_name = st.name + "__hash";

        for (const auto& func : mir_program.functions) {
            if (func && func->name == func_name)
                return;
        }

        auto mir_func = std::make_unique<MirFunction>();
        mir_func->name = func_name;

        auto struct_type = hir::make_named(st.name);
        mir_func->return_local = mir_func->add_local("_0", hir::make_int(), true, false);

        LocalId self_local = mir_func->add_local("self", struct_type, false, true);
        mir_func->arg_locals.push_back(self_local);

        BlockId entry_block = mir_func->add_block();
        auto* block = mir_func->get_block(entry_block);

        if (st.fields.empty()) {
            auto const_zero = std::make_unique<MirOperand>();
            const_zero->kind = MirOperand::Constant;
            MirConstant c;
            c.value = int64_t(0);
            c.type = hir::make_int();
            const_zero->data = c;

            block->statements.push_back(MirStatement::assign(
                MirPlace(mir_func->return_local), MirRvalue::use(std::move(const_zero))));
        } else {
            LocalId acc = mir_func->add_local("_hash_acc", hir::make_int(), true, false);

            auto const_zero = std::make_unique<MirOperand>();
            const_zero->kind = MirOperand::Constant;
            MirConstant c_zero;
            c_zero.value = int64_t(0);
            c_zero.type = hir::make_int();
            const_zero->data = c_zero;

            block->statements.push_back(
                MirStatement::assign(MirPlace(acc), MirRvalue::use(std::move(const_zero))));

            for (size_t i = 0; i < st.fields.size(); ++i) {
                const auto& field = st.fields[i];

                // フィールド値を取得
                LocalId field_val =
                    mir_func->add_local("_field" + std::to_string(i), field.type, true, false);
                auto self_place = MirPlace(self_local, {PlaceProjection::field(i)});
                block->statements.push_back(MirStatement::assign(
                    MirPlace(field_val), MirRvalue::use(MirOperand::copy(self_place))));

                // フィールド値をint型の一時変数にコピー（プリミティブ型を想定、暗黙変換）
                LocalId field_as_int =
                    mir_func->add_local("_f_int" + std::to_string(i), hir::make_int(), true, false);
                block->statements.push_back(MirStatement::assign(
                    MirPlace(field_as_int), MirRvalue::use(MirOperand::copy(MirPlace(field_val)))));

                LocalId new_acc = mir_func->add_local("_new_acc" + std::to_string(i),
                                                      hir::make_int(), true, false);
                block->statements.push_back(MirStatement::assign(
                    MirPlace(new_acc),
                    MirRvalue::binary(MirBinaryOp::Add, MirOperand::copy(MirPlace(acc)),
                                      MirOperand::copy(MirPlace(field_as_int)))));
                acc = new_acc;
            }

            block->statements.push_back(MirStatement::assign(
                MirPlace(mir_func->return_local), MirRvalue::use(MirOperand::copy(MirPlace(acc)))));
        }

        block->terminator = MirTerminator::return_value();

        impl_info[st.name]["Hash"] = func_name;
        mir_program.functions.push_back(std::move(mir_func));
    }

    // 構造体の比較演算子を関数呼び出しに変換するパス
    void rewrite_struct_comparison_operators() {
        for (auto& func : mir_program.functions) {
            if (!func)
                continue;

            // 各ブロックを走査
            for (size_t block_idx = 0; block_idx < func->basic_blocks.size(); ++block_idx) {
                auto* block = func->get_block(block_idx);
                if (!block)
                    continue;

                // 各文を走査
                for (size_t stmt_idx = 0; stmt_idx < block->statements.size(); ++stmt_idx) {
                    auto& stmt = block->statements[stmt_idx];
                    if (!stmt || stmt->kind != MirStatement::Assign)
                        continue;

                    auto* assign_data = std::get_if<MirStatement::AssignData>(&stmt->data);
                    if (!assign_data || !assign_data->rvalue)
                        continue;

                    auto& rvalue = assign_data->rvalue;
                    if (rvalue->kind != MirRvalue::BinaryOp)
                        continue;

                    auto& bin_data = std::get<MirRvalue::BinaryOpData>(rvalue->data);

                    // ==, !=, <, <=, >, >= の演算子をチェック
                    if (bin_data.op != MirBinaryOp::Eq && bin_data.op != MirBinaryOp::Ne &&
                        bin_data.op != MirBinaryOp::Lt && bin_data.op != MirBinaryOp::Le &&
                        bin_data.op != MirBinaryOp::Gt && bin_data.op != MirBinaryOp::Ge) {
                        continue;
                    }

                    // オペランドのローカル変数を取得
                    if (bin_data.lhs->kind != MirOperand::Copy &&
                        bin_data.lhs->kind != MirOperand::Move) {
                        continue;
                    }

                    auto& lhs_place = std::get<MirPlace>(bin_data.lhs->data);
                    LocalId lhs_local = lhs_place.local;

                    // ローカル変数の型をチェック
                    const auto& local_info = func->locals[lhs_local];
                    if (!local_info.type || local_info.type->kind != hir::TypeKind::Struct) {
                        continue;
                    }

                    std::string type_name = local_info.type->name;

                    // impl_info で対応する演算子関数が実装されているかチェック
                    std::string op_func_name;
                    bool need_negate = false;

                    if (bin_data.op == MirBinaryOp::Eq || bin_data.op == MirBinaryOp::Ne) {
                        if (impl_info.count(type_name) && impl_info[type_name].count("Eq")) {
                            op_func_name = type_name + "__op_eq";
                            need_negate = (bin_data.op == MirBinaryOp::Ne);
                        }
                    } else if (bin_data.op == MirBinaryOp::Lt || bin_data.op == MirBinaryOp::Le ||
                               bin_data.op == MirBinaryOp::Gt || bin_data.op == MirBinaryOp::Ge) {
                        if (impl_info.count(type_name) && impl_info[type_name].count("Ord")) {
                            op_func_name = type_name + "__op_lt";
                            // > は < の引数を入れ替え、>= と <= は結果を反転
                        }
                    }

                    if (op_func_name.empty())
                        continue;

                    // 比較演算を関数呼び出しに変換
                    // 現在の文を関数呼び出しのターミネータに置き換え、新しいブロックを作成

                    // 結果を格納する場所
                    MirPlace result_place = assign_data->place;

                    // 引数を準備
                    std::vector<MirOperandPtr> args;
                    auto lhs_op = std::make_unique<MirOperand>();
                    lhs_op->kind = bin_data.lhs->kind;
                    lhs_op->data = bin_data.lhs->data;

                    auto rhs_op = std::make_unique<MirOperand>();
                    rhs_op->kind = bin_data.rhs->kind;
                    rhs_op->data = bin_data.rhs->data;

                    // > と >= は引数を入れ替え
                    if (bin_data.op == MirBinaryOp::Gt || bin_data.op == MirBinaryOp::Ge) {
                        args.push_back(std::move(rhs_op));
                        args.push_back(std::move(lhs_op));
                    } else {
                        args.push_back(std::move(lhs_op));
                        args.push_back(std::move(rhs_op));
                    }

                    // 継続ブロックを作成
                    BlockId cont_block = func->add_block();

                    // 残りの文を継続ブロックに移動
                    auto* cont = func->get_block(cont_block);
                    for (size_t i = stmt_idx + 1; i < block->statements.size(); ++i) {
                        cont->statements.push_back(std::move(block->statements[i]));
                    }
                    cont->terminator = std::move(block->terminator);

                    // 現在のブロックの文を切り詰め
                    block->statements.resize(stmt_idx);

                    // <= と >= の場合は、< の結果または a == b の結果が true
                    // 単純化のため、< と == を両方呼び出す必要がある
                    // 今回は < のみ使用して、<= は !( > ) として実装

                    if (bin_data.op == MirBinaryOp::Le || bin_data.op == MirBinaryOp::Ge) {
                        // <= は !(a > b) = !(b < a)
                        // >= は !(a < b)
                        // 一時変数を作成して結果を反転
                        LocalId temp_result =
                            func->add_local("_lt_tmp", hir::make_bool(), true, false);

                        // 関数呼び出しターミネータを設定
                        auto func_op = MirOperand::function_ref(op_func_name);
                        BlockId negate_block = func->add_block();

                        auto call_term = std::make_unique<MirTerminator>();
                        call_term->kind = MirTerminator::Call;
                        call_term->data = MirTerminator::CallData{std::move(func_op),
                                                                  std::move(args),
                                                                  MirPlace(temp_result),
                                                                  negate_block,
                                                                  std::nullopt,
                                                                  "",
                                                                  "",
                                                                  false};  // 通常の関数呼び出し
                        block->terminator = std::move(call_term);

                        // 反転ブロック
                        auto* neg_block = func->get_block(negate_block);
                        auto unary_rvalue = std::make_unique<MirRvalue>();
                        unary_rvalue->kind = MirRvalue::UnaryOp;
                        unary_rvalue->data = MirRvalue::UnaryOpData{
                            MirUnaryOp::Not, MirOperand::copy(MirPlace(temp_result))};
                        neg_block->statements.push_back(
                            MirStatement::assign(result_place, std::move(unary_rvalue)));
                        neg_block->terminator = MirTerminator::goto_block(cont_block);
                    } else if (need_negate) {
                        // != の場合
                        LocalId temp_result =
                            func->add_local("_eq_tmp", hir::make_bool(), true, false);

                        auto func_op = MirOperand::function_ref(op_func_name);
                        BlockId negate_block = func->add_block();

                        auto call_term = std::make_unique<MirTerminator>();
                        call_term->kind = MirTerminator::Call;
                        call_term->data = MirTerminator::CallData{std::move(func_op),
                                                                  std::move(args),
                                                                  MirPlace(temp_result),
                                                                  negate_block,
                                                                  std::nullopt,
                                                                  "",
                                                                  "",
                                                                  false};  // 通常の関数呼び出し
                        block->terminator = std::move(call_term);

                        auto* neg_block = func->get_block(negate_block);
                        auto unary_rvalue = std::make_unique<MirRvalue>();
                        unary_rvalue->kind = MirRvalue::UnaryOp;
                        unary_rvalue->data = MirRvalue::UnaryOpData{
                            MirUnaryOp::Not, MirOperand::copy(MirPlace(temp_result))};
                        neg_block->statements.push_back(
                            MirStatement::assign(result_place, std::move(unary_rvalue)));
                        neg_block->terminator = MirTerminator::goto_block(cont_block);
                    } else {
                        // ==, < の場合はそのまま
                        auto func_op = MirOperand::function_ref(op_func_name);

                        auto call_term = std::make_unique<MirTerminator>();
                        call_term->kind = MirTerminator::Call;
                        call_term->data = MirTerminator::CallData{std::move(func_op),
                                                                  std::move(args),
                                                                  result_place,
                                                                  cont_block,
                                                                  std::nullopt,
                                                                  "",
                                                                  "",
                                                                  false};  // 通常の関数呼び出し
                        block->terminator = std::move(call_term);
                    }

                    // ブロックが変更されたのでこのブロックの走査を終了
                    break;
                }
            }
        }
    }

    // 組み込みEq演算子（==）の自動実装を生成
    void generate_builtin_eq_operator(const hir::HirStruct& st) {
        // 関数名: TypeName__op_eq
        std::string func_name = st.name + "__op_eq";

        auto mir_func = std::make_unique<MirFunction>();
        mir_func->name = func_name;

        // 戻り値: bool (_0)
        mir_func->return_local = mir_func->add_local("_0", hir::make_bool(), true, false);

        // 引数: self (値), other (値) - 両方とも値渡し
        auto struct_type = hir::make_named(st.name);

        LocalId self_local = mir_func->add_local("self", struct_type, false, true);
        LocalId other_local = mir_func->add_local("other", struct_type, false, true);
        mir_func->arg_locals.push_back(self_local);
        mir_func->arg_locals.push_back(other_local);

        // エントリブロックを作成
        BlockId entry_block = mir_func->add_block();
        auto* block = mir_func->get_block(entry_block);

        // フィールド比較のロジックを生成
        if (st.fields.empty()) {
            // フィールドがない場合は常にtrue
            auto const_true = std::make_unique<MirOperand>();
            const_true->kind = MirOperand::Constant;
            MirConstant c;
            c.value = true;
            c.type = hir::make_bool();
            const_true->data = c;

            block->statements.push_back(MirStatement::assign(
                MirPlace(mir_func->return_local), MirRvalue::use(std::move(const_true))));
            block->terminator = MirTerminator::return_value();
        } else {
            // 各フィールドを比較
            std::vector<LocalId> cmp_results;

            for (size_t i = 0; i < st.fields.size(); ++i) {
                const auto& field = st.fields[i];

                LocalId cmp_result =
                    mir_func->add_local("_cmp" + std::to_string(i), hir::make_bool(), true, false);
                cmp_results.push_back(cmp_result);

                // self.field を読み込む
                LocalId self_field =
                    mir_func->add_local("_self_f" + std::to_string(i), field.type, true, false);
                auto self_place = MirPlace(self_local, {PlaceProjection::field(i)});
                block->statements.push_back(MirStatement::assign(
                    MirPlace(self_field), MirRvalue::use(MirOperand::copy(self_place))));

                // other.field を読み込む
                LocalId other_field =
                    mir_func->add_local("_other_f" + std::to_string(i), field.type, true, false);
                auto other_place = MirPlace(other_local, {PlaceProjection::field(i)});
                block->statements.push_back(MirStatement::assign(
                    MirPlace(other_field), MirRvalue::use(MirOperand::copy(other_place))));

                // 比較結果を格納
                block->statements.push_back(MirStatement::assign(
                    MirPlace(cmp_result),
                    MirRvalue::binary(MirBinaryOp::Eq, MirOperand::copy(MirPlace(self_field)),
                                      MirOperand::copy(MirPlace(other_field)))));
            }

            // 全ての比較結果をANDで結合
            if (cmp_results.size() == 1) {
                block->statements.push_back(MirStatement::assign(
                    MirPlace(mir_func->return_local),
                    MirRvalue::use(MirOperand::copy(MirPlace(cmp_results[0])))));
            } else {
                LocalId acc = cmp_results[0];
                for (size_t i = 1; i < cmp_results.size(); ++i) {
                    LocalId new_acc = mir_func->add_local("_acc" + std::to_string(i),
                                                          hir::make_bool(), true, false);
                    block->statements.push_back(MirStatement::assign(
                        MirPlace(new_acc),
                        MirRvalue::binary(MirBinaryOp::And, MirOperand::copy(MirPlace(acc)),
                                          MirOperand::copy(MirPlace(cmp_results[i])))));
                    acc = new_acc;
                }
                block->statements.push_back(
                    MirStatement::assign(MirPlace(mir_func->return_local),
                                         MirRvalue::use(MirOperand::copy(MirPlace(acc)))));
            }

            block->terminator = MirTerminator::return_value();
        }

        // impl_info に登録
        impl_info[st.name]["Eq"] = func_name;

        // MIRプログラムに追加
        mir_program.functions.push_back(std::move(mir_func));
    }

    // 組み込みOrd演算子（<）の自動実装を生成
    void generate_builtin_lt_operator(const hir::HirStruct& st) {
        // 関数名: TypeName__op_lt
        std::string func_name = st.name + "__op_lt";

        auto mir_func = std::make_unique<MirFunction>();
        mir_func->name = func_name;

        // 戻り値: bool (_0)
        mir_func->return_local = mir_func->add_local("_0", hir::make_bool(), true, false);

        // 引数: self (値), other (値)
        auto struct_type = hir::make_named(st.name);

        LocalId self_local = mir_func->add_local("self", struct_type, false, true);
        LocalId other_local = mir_func->add_local("other", struct_type, false, true);
        mir_func->arg_locals.push_back(self_local);
        mir_func->arg_locals.push_back(other_local);

        // エントリブロックを作成
        BlockId entry_block = mir_func->add_block();
        auto* block = mir_func->get_block(entry_block);

        if (st.fields.empty()) {
            // フィールドがない場合は常にfalse（同じなので < ではない）
            auto const_false = std::make_unique<MirOperand>();
            const_false->kind = MirOperand::Constant;
            MirConstant c;
            c.value = false;
            c.type = hir::make_bool();
            const_false->data = c;

            block->statements.push_back(MirStatement::assign(
                MirPlace(mir_func->return_local), MirRvalue::use(std::move(const_false))));
            block->terminator = MirTerminator::return_value();
        } else {
            // 辞書式順序で比較
            // フィールドを順番に比較し、最初の異なるフィールドで判定
            // self.f0 < other.f0 -> true
            // self.f0 > other.f0 -> false
            // self.f0 == other.f0 -> 次のフィールドへ

            // 各フィールドについてブロックを作成
            std::vector<BlockId> field_blocks;
            for (size_t i = 0; i < st.fields.size(); ++i) {
                field_blocks.push_back(mir_func->add_block());
            }
            BlockId false_block = mir_func->add_block();

            // 最初のフィールドのブロックへジャンプ
            block->terminator = MirTerminator::goto_block(field_blocks[0]);

            for (size_t i = 0; i < st.fields.size(); ++i) {
                const auto& field = st.fields[i];
                auto* field_block = mir_func->get_block(field_blocks[i]);

                // フィールド値を読み込み
                LocalId self_field =
                    mir_func->add_local("_self_f" + std::to_string(i), field.type, true, false);
                auto self_place = MirPlace(self_local, {PlaceProjection::field(i)});
                field_block->statements.push_back(MirStatement::assign(
                    MirPlace(self_field), MirRvalue::use(MirOperand::copy(self_place))));

                LocalId other_field =
                    mir_func->add_local("_other_f" + std::to_string(i), field.type, true, false);
                auto other_place = MirPlace(other_local, {PlaceProjection::field(i)});
                field_block->statements.push_back(MirStatement::assign(
                    MirPlace(other_field), MirRvalue::use(MirOperand::copy(other_place))));

                // self.field < other.field をチェック
                LocalId lt_result =
                    mir_func->add_local("_lt" + std::to_string(i), hir::make_bool(), true, false);
                field_block->statements.push_back(MirStatement::assign(
                    MirPlace(lt_result),
                    MirRvalue::binary(MirBinaryOp::Lt, MirOperand::copy(MirPlace(self_field)),
                                      MirOperand::copy(MirPlace(other_field)))));

                // lt_result が true なら true を返す
                BlockId lt_true_block = mir_func->add_block();
                BlockId lt_false_check_block = mir_func->add_block();
                field_block->terminator =
                    MirTerminator::switch_int(MirOperand::copy(MirPlace(lt_result)),
                                              {{1, lt_true_block}}, lt_false_check_block);

                // self < other なので true を返す
                auto* true_ret_block = mir_func->get_block(lt_true_block);
                auto const_true = std::make_unique<MirOperand>();
                const_true->kind = MirOperand::Constant;
                MirConstant c_true;
                c_true.value = true;
                c_true.type = hir::make_bool();
                const_true->data = c_true;
                true_ret_block->statements.push_back(MirStatement::assign(
                    MirPlace(mir_func->return_local), MirRvalue::use(std::move(const_true))));
                true_ret_block->terminator = MirTerminator::return_value();

                // self.field >= other.field の場合
                auto* gt_check_block = mir_func->get_block(lt_false_check_block);

                // self.field > other.field をチェック
                LocalId gt_result =
                    mir_func->add_local("_gt" + std::to_string(i), hir::make_bool(), true, false);
                gt_check_block->statements.push_back(MirStatement::assign(
                    MirPlace(gt_result),
                    MirRvalue::binary(MirBinaryOp::Gt, MirOperand::copy(MirPlace(self_field)),
                                      MirOperand::copy(MirPlace(other_field)))));

                // gt_result が true なら false を返す、そうでなければ次のフィールドへ
                BlockId next_block = (i + 1 < st.fields.size()) ? field_blocks[i + 1] : false_block;
                gt_check_block->terminator = MirTerminator::switch_int(
                    MirOperand::copy(MirPlace(gt_result)), {{1, false_block}}, next_block);
            }

            // 全フィールドが等しい場合は false
            auto* final_false_block = mir_func->get_block(false_block);
            auto const_false = std::make_unique<MirOperand>();
            const_false->kind = MirOperand::Constant;
            MirConstant c_false;
            c_false.value = false;
            c_false.type = hir::make_bool();
            const_false->data = c_false;
            final_false_block->statements.push_back(MirStatement::assign(
                MirPlace(mir_func->return_local), MirRvalue::use(std::move(const_false))));
            final_false_block->terminator = MirTerminator::return_value();
        }

        // impl_info に登録
        impl_info[st.name]["Ord"] = func_name;

        // MIRプログラムに追加
        mir_program.functions.push_back(std::move(mir_func));
    }

    // 組み込みCloneメソッドの自動実装を生成
    void generate_builtin_clone_method(const hir::HirStruct& st) {
        // 関数名: TypeName__clone
        std::string func_name = st.name + "__clone";

        auto mir_func = std::make_unique<MirFunction>();
        mir_func->name = func_name;

        auto struct_type = hir::make_named(st.name);

        // 戻り値: StructType (_0)
        mir_func->return_local = mir_func->add_local("_0", struct_type, true, false);

        // 引数: self (値)
        LocalId self_local = mir_func->add_local("self", struct_type, false, true);
        mir_func->arg_locals.push_back(self_local);

        // エントリブロックを作成
        BlockId entry_block = mir_func->add_block();
        auto* block = mir_func->get_block(entry_block);

        // selfをそのまま返す（値コピー）
        block->statements.push_back(
            MirStatement::assign(MirPlace(mir_func->return_local),
                                 MirRvalue::use(MirOperand::copy(MirPlace(self_local)))));

        block->terminator = MirTerminator::return_value();

        // impl_info に登録
        impl_info[st.name]["Clone"] = func_name;

        // MIRプログラムに追加
        mir_program.functions.push_back(std::move(mir_func));
    }

    // 組み込みHashメソッドの自動実装を生成
    void generate_builtin_hash_method(const hir::HirStruct& st) {
        // 関数名: TypeName__hash
        std::string func_name = st.name + "__hash";

        auto mir_func = std::make_unique<MirFunction>();
        mir_func->name = func_name;

        auto struct_type = hir::make_named(st.name);

        // 戻り値: int (_0)
        mir_func->return_local = mir_func->add_local("_0", hir::make_int(), true, false);

        // 引数: self (値)
        LocalId self_local = mir_func->add_local("self", struct_type, false, true);
        mir_func->arg_locals.push_back(self_local);

        // エントリブロックを作成
        BlockId entry_block = mir_func->add_block();
        auto* block = mir_func->get_block(entry_block);

        // 簡略化実装: 各フィールドの値を足し合わせてハッシュとする
        // TODO: より良いハッシュ関数の実装（FNV-1a等）

        if (st.fields.empty()) {
            // フィールドがない場合は0
            auto const_zero = std::make_unique<MirOperand>();
            const_zero->kind = MirOperand::Constant;
            MirConstant c;
            c.value = int64_t(0);
            c.type = hir::make_int();
            const_zero->data = c;

            block->statements.push_back(MirStatement::assign(
                MirPlace(mir_func->return_local), MirRvalue::use(std::move(const_zero))));
        } else {
            // 各フィールドの値を加算
            LocalId acc = mir_func->add_local("_hash_acc", hir::make_int(), true, false);

            // 初期値 = 0
            auto const_zero = std::make_unique<MirOperand>();
            const_zero->kind = MirOperand::Constant;
            MirConstant c;
            c.value = int64_t(0);
            c.type = hir::make_int();
            const_zero->data = c;

            block->statements.push_back(
                MirStatement::assign(MirPlace(acc), MirRvalue::use(std::move(const_zero))));

            for (size_t i = 0; i < st.fields.size(); ++i) {
                const auto& field = st.fields[i];

                // フィールド値を読み込み
                LocalId field_val =
                    mir_func->add_local("_f" + std::to_string(i), field.type, true, false);
                auto field_place = MirPlace(self_local, {PlaceProjection::field(i)});
                block->statements.push_back(MirStatement::assign(
                    MirPlace(field_val), MirRvalue::use(MirOperand::copy(field_place))));

                // acc += field_val (簡略化: intにキャスト)
                // TODO: 型に応じたハッシュ計算
                LocalId new_acc =
                    mir_func->add_local("_acc" + std::to_string(i), hir::make_int(), true, false);
                block->statements.push_back(MirStatement::assign(
                    MirPlace(new_acc),
                    MirRvalue::binary(MirBinaryOp::Add, MirOperand::copy(MirPlace(acc)),
                                      MirOperand::copy(MirPlace(field_val)))));
                acc = new_acc;
            }

            block->statements.push_back(MirStatement::assign(
                MirPlace(mir_func->return_local), MirRvalue::use(MirOperand::copy(MirPlace(acc)))));
        }

        block->terminator = MirTerminator::return_value();

        // impl_info に登録
        impl_info[st.name]["Hash"] = func_name;

        // MIRプログラムに追加
        mir_program.functions.push_back(std::move(mir_func));
    }

    // 演算子の自動実装を生成（ユーザー定義インターフェース用）
    void generate_auto_operator_impl(const hir::HirStruct& st, const hir::HirInterface& iface,
                                     const hir::HirOperatorSig& op) {
        // Eq演算子（==）の自動実装
        if (op.op == hir::HirOperatorKind::Eq) {
            generate_builtin_eq_operator(st);
            impl_info[st.name][iface.name] = st.name + "__op_eq";
        }
        // Ord演算子（<）の自動実装
        else if (op.op == hir::HirOperatorKind::Lt) {
            generate_builtin_lt_operator(st);
            impl_info[st.name][iface.name] = st.name + "__op_lt";
        }
    }

    // 通常の関数をlowering
    void lower_functions(const hir::HirProgram& hir_program) {
        for (const auto& decl : hir_program.declarations) {
            if (auto* func = std::get_if<std::unique_ptr<hir::HirFunction>>(&decl->kind)) {
                if (auto mir_func = lower_function(**func)) {
                    // HIR関数の参照を保存
                    hir_functions[(*func)->name] = func->get();
                    mir_program.functions.push_back(std::move(mir_func));
                }
            }
        }
    }

    // impl内のメソッドをlowering
    void lower_impl_methods(const hir::HirProgram& hir_program) {
        for (const auto& decl : hir_program.declarations) {
            if (auto* impl = std::get_if<std::unique_ptr<hir::HirImpl>>(&decl->kind)) {
                lower_impl(**impl);
            }
        }
    }

    // モノモーフィゼーションを実行
    void perform_monomorphization() {
        monomorphizer.monomorphize(mir_program, hir_functions, struct_defs);
    }

    // 関数のlowering
    std::unique_ptr<MirFunction> lower_function(const hir::HirFunction& func);

    // 演算子実装のlowering
    std::unique_ptr<MirFunction> lower_operator(const hir::HirOperatorImpl& op_impl,
                                                const std::string& type_name);

    // impl内のメソッドをlowering
    void lower_impl(const hir::HirImpl& impl);

    // デストラクタを生成（構造体用）
    void generate_destructor(const std::string& type_name, LoweringContext& ctx);

    // デストラクタ呼び出しを挿入
    void emit_destructors(LoweringContext& ctx);
};

}  // namespace cm::mir