// auto_impl.cpp - with キーワードによる自動実装（ビルトインメソッド）の生成
// eq/lt/clone/hash/css/debug/display の通常版とモノモーフィゼーション版。
// lowering.cpp（2,912行）から分割（013 §4.3-4 巨大TU分割）

#include "internal/base/debug.hpp"
#include "lowering.hpp"

#include <algorithm>
#include <memory>
#include <numeric>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cm::mir {

// with キーワードによる自動実装を生成
void MirLowering::generate_auto_impls(const hir::HirProgram& hir_program) {
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
                if (iface_name == "Debug") {
                    generate_builtin_debug_method(struct_decl);
                    continue;
                }
                if (iface_name == "Display") {
                    generate_builtin_display_method(struct_decl);
                    continue;
                }
                if (iface_name == "Css") {
                    generate_builtin_css_method(struct_decl);
                    generate_builtin_to_css_method(struct_decl);
                    generate_builtin_is_css_method(struct_decl);
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

// モノモーフィゼーション後のジェネリック構造体に対する自動実装を生成
void MirLowering::generate_monomorphized_auto_impls() {
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
            } else if (iface_name == "Debug") {
                generate_builtin_debug_method_for_monomorphized(*mir_struct);
            } else if (iface_name == "Display") {
                generate_builtin_display_method_for_monomorphized(*mir_struct);
            } else if (iface_name == "Css") {
                generate_builtin_css_method_for_monomorphized(*mir_struct);
                generate_builtin_to_css_method_for_monomorphized(*mir_struct);
                generate_builtin_is_css_method_for_monomorphized(*mir_struct);
            }
        }
    }
}

// モノモーフィゼーションされた構造体用のCloneメソッドを生成
void MirLowering::generate_builtin_clone_method_for_monomorphized(const MirStruct& st) {
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

    block->statements.push_back(MirStatement::assign(
        MirPlace(mir_func->return_local), MirRvalue::use(MirOperand::copy(MirPlace(self_local)))));

    block->terminator = MirTerminator::return_value();

    impl_info[st.name]["Clone"] = func_name;
    mir_program.functions.push_back(std::move(mir_func));
}

// モノモーフィゼーションされた構造体用のHashメソッドを生成
void MirLowering::generate_builtin_hash_method_for_monomorphized(const MirStruct& st) {
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

        block->statements.push_back(MirStatement::assign(MirPlace(mir_func->return_local),
                                                         MirRvalue::use(std::move(const_zero))));
    } else {
        // ネスト構造体フィールドの__hashを先に生成（再帰的自動実装）
        for (const auto& field : st.fields) {
            if (field.type && field.type->kind == hir::TypeKind::Struct) {
                std::string nested_func_name = field.type->name + "__hash";
                bool exists = false;
                for (const auto& func : mir_program.functions) {
                    if (func && func->name == nested_func_name) {
                        exists = true;
                        break;
                    }
                }
                if (!exists) {
                    for (const auto& mir_st : mir_program.structs) {
                        if (mir_st && mir_st->name == field.type->name) {
                            generate_builtin_hash_method_for_monomorphized(*mir_st);
                            break;
                        }
                    }
                }
            }
        }

        LocalId acc = mir_func->add_local("_hash_acc", hir::make_int(), true, false);

        auto make_int_const = [&](int64_t v) {
            auto op = std::make_unique<MirOperand>();
            op->kind = MirOperand::Constant;
            MirConstant c;
            c.value = v;
            c.type = hir::make_int();
            op->data = c;
            return op;
        };

        block->statements.push_back(
            MirStatement::assign(MirPlace(acc), MirRvalue::use(make_int_const(0))));

        BlockId current_block = entry_block;

        // acc = acc + value をカレントブロックへ追加する
        auto mix_value = [&](LocalId value, const std::string& tag) {
            auto* cur_block = mir_func->get_block(current_block);
            LocalId new_acc = mir_func->add_local("_new_acc" + tag, hir::make_int(), true, false);
            cur_block->statements.push_back(MirStatement::assign(
                MirPlace(new_acc),
                MirRvalue::binary(MirBinaryOp::Add, MirOperand::copy(MirPlace(acc)),
                                  MirOperand::copy(MirPlace(value)))));
            acc = new_acc;
        };

        for (size_t i = 0; i < st.fields.size(); ++i) {
            const auto& field = st.fields[i];
            const std::string tag = std::to_string(i);
            auto* cur_block = mir_func->get_block(current_block);

            if (field.type && field.type->kind == hir::TypeKind::Struct) {
                // ネスト構造体: Field__hash を呼び出して結果を混合する
                LocalId field_val = mir_func->add_local("_field" + tag, field.type, true, false);
                cur_block->statements.push_back(MirStatement::assign(
                    MirPlace(field_val), MirRvalue::use(MirOperand::copy(
                                             MirPlace(self_local, {PlaceProjection::field(i)})))));

                LocalId hash_val =
                    mir_func->add_local("_f_hash" + tag, hir::make_int(), true, false);
                BlockId next_block = mir_func->add_block();

                std::vector<MirOperandPtr> hash_args;
                hash_args.push_back(MirOperand::copy(MirPlace(field_val)));
                auto call_term = std::make_unique<MirTerminator>();
                call_term->kind = MirTerminator::Call;
                call_term->data =
                    MirTerminator::CallData{MirOperand::function_ref(field.type->name + "__hash"),
                                            std::move(hash_args),
                                            MirPlace(hash_val),
                                            next_block,
                                            std::nullopt,
                                            "",
                                            "",
                                            false};
                cur_block->terminator = std::move(call_term);
                current_block = next_block;

                mix_value(hash_val, tag);
            } else if (field.type && field.type->kind == hir::TypeKind::Array &&
                       (field.type->array_size.has_value() || field.type->dimensions.size() == 1)) {
                // 固定長1次元配列: 要素を順に混合する（コンパイル時展開）
                uint32_t n = field.type->array_size.has_value() ? *field.type->array_size
                                                                : field.type->dimensions[0];
                auto elem_type =
                    field.type->element_type ? field.type->element_type : hir::make_int();
                for (uint32_t j = 0; j < n; ++j) {
                    const std::string etag = tag + "_" + std::to_string(j);
                    LocalId idx = mir_func->add_local("_idx" + etag, hir::make_int(), true, false);
                    cur_block->statements.push_back(MirStatement::assign(
                        MirPlace(idx), MirRvalue::use(make_int_const(int64_t(j)))));

                    LocalId elem_val = mir_func->add_local("_elem" + etag, elem_type, true, false);
                    cur_block->statements.push_back(MirStatement::assign(
                        MirPlace(elem_val), MirRvalue::use(MirOperand::copy(MirPlace(
                                                self_local, {PlaceProjection::field(i),
                                                             PlaceProjection::index(idx)})))));

                    LocalId elem_as_int =
                        mir_func->add_local("_e_int" + etag, hir::make_int(), true, false);
                    cur_block->statements.push_back(
                        MirStatement::assign(MirPlace(elem_as_int),
                                             MirRvalue::use(MirOperand::copy(MirPlace(elem_val)))));

                    mix_value(elem_as_int, etag);
                }
            } else {
                // プリミティブ型: int値として混合する
                LocalId field_val = mir_func->add_local("_field" + tag, field.type, true, false);
                cur_block->statements.push_back(MirStatement::assign(
                    MirPlace(field_val), MirRvalue::use(MirOperand::copy(
                                             MirPlace(self_local, {PlaceProjection::field(i)})))));

                LocalId field_as_int =
                    mir_func->add_local("_f_int" + tag, hir::make_int(), true, false);
                cur_block->statements.push_back(MirStatement::assign(
                    MirPlace(field_as_int), MirRvalue::use(MirOperand::copy(MirPlace(field_val)))));

                mix_value(field_as_int, tag);
            }
        }

        auto* final_block = mir_func->get_block(current_block);
        final_block->statements.push_back(MirStatement::assign(
            MirPlace(mir_func->return_local), MirRvalue::use(MirOperand::copy(MirPlace(acc)))));
        final_block->terminator = MirTerminator::return_value();

        impl_info[st.name]["Hash"] = func_name;
        mir_program.functions.push_back(std::move(mir_func));
        return;
    }

    block->terminator = MirTerminator::return_value();

    impl_info[st.name]["Hash"] = func_name;
    mir_program.functions.push_back(std::move(mir_func));
}

// 組み込みCloneメソッドの自動実装を生成
void MirLowering::generate_builtin_clone_method(const hir::HirStruct& st) {
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
    block->statements.push_back(MirStatement::assign(
        MirPlace(mir_func->return_local), MirRvalue::use(MirOperand::copy(MirPlace(self_local)))));

    block->terminator = MirTerminator::return_value();

    // impl_info に登録
    impl_info[st.name]["Clone"] = func_name;

    // MIRプログラムに追加
    mir_program.functions.push_back(std::move(mir_func));
}

// 組み込みHashメソッドの自動実装を生成
void MirLowering::generate_builtin_hash_method(const hir::HirStruct& st) {
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

    // FNV-1a ハッシュ実装
    // hash = FNV_OFFSET_BASIS
    // for each byte:
    //   hash ^= byte
    //   hash *= FNV_PRIME
    // 簡略化: フィールド値をintとして扱い、XORと乗算で混合
    constexpr int64_t FNV_OFFSET_BASIS = 0x811c9dc5;  // 32-bit FNV-1a
    constexpr int64_t FNV_PRIME = 0x01000193;

    if (st.fields.empty()) {
        // フィールドがない場合はFNV_OFFSET_BASIS
        auto const_basis = std::make_unique<MirOperand>();
        const_basis->kind = MirOperand::Constant;
        MirConstant c;
        c.value = FNV_OFFSET_BASIS;
        c.type = hir::make_int();
        const_basis->data = c;

        block->statements.push_back(MirStatement::assign(MirPlace(mir_func->return_local),
                                                         MirRvalue::use(std::move(const_basis))));
    } else {
        // ネスト構造体フィールドの__hashを先に生成（再帰的自動実装）
        for (const auto& field : st.fields) {
            if (field.type && field.type->kind == hir::TypeKind::Struct) {
                std::string nested_func_name = field.type->name + "__hash";
                bool exists = false;
                for (const auto& func : mir_program.functions) {
                    if (func && func->name == nested_func_name) {
                        exists = true;
                        break;
                    }
                }
                if (!exists) {
                    for (const auto& mir_st : mir_program.structs) {
                        if (mir_st && mir_st->name == field.type->name) {
                            generate_builtin_hash_method_for_monomorphized(*mir_st);
                            break;
                        }
                    }
                }
            }
        }

        // FNV-1a: hash ^= field; hash *= prime
        LocalId acc = mir_func->add_local("_hash_acc", hir::make_int(), true, false);

        auto make_int_const = [&](int64_t v) {
            auto op = std::make_unique<MirOperand>();
            op->kind = MirOperand::Constant;
            MirConstant c;
            c.value = v;
            c.type = hir::make_int();
            op->data = c;
            return op;
        };

        // 初期値 = FNV_OFFSET_BASIS
        block->statements.push_back(
            MirStatement::assign(MirPlace(acc), MirRvalue::use(make_int_const(FNV_OFFSET_BASIS))));

        BlockId current_block = entry_block;

        // hash = (hash ^ value) * FNV_PRIME をカレントブロックへ追加する
        auto mix_value = [&](LocalId value, const std::string& tag) {
            auto* cur_block = mir_func->get_block(current_block);
            LocalId xor_acc = mir_func->add_local("_xor" + tag, hir::make_int(), true, false);
            cur_block->statements.push_back(MirStatement::assign(
                MirPlace(xor_acc),
                MirRvalue::binary(MirBinaryOp::BitXor, MirOperand::copy(MirPlace(acc)),
                                  MirOperand::copy(MirPlace(value)), hir::make_int())));

            LocalId mul_acc = mir_func->add_local("_mul" + tag, hir::make_int(), true, false);
            cur_block->statements.push_back(MirStatement::assign(
                MirPlace(mul_acc),
                MirRvalue::binary(MirBinaryOp::Mul, MirOperand::copy(MirPlace(xor_acc)),
                                  make_int_const(FNV_PRIME), hir::make_int())));
            acc = mul_acc;
        };

        for (size_t i = 0; i < st.fields.size(); ++i) {
            const auto& field = st.fields[i];
            const std::string tag = std::to_string(i);
            auto* cur_block = mir_func->get_block(current_block);

            if (field.type && field.type->kind == hir::TypeKind::Struct) {
                // ネスト構造体: Field__hash を呼び出して結果を混合する
                LocalId field_val = mir_func->add_local("_f" + tag, field.type, true, false);
                cur_block->statements.push_back(MirStatement::assign(
                    MirPlace(field_val), MirRvalue::use(MirOperand::copy(
                                             MirPlace(self_local, {PlaceProjection::field(i)})))));

                LocalId hash_val =
                    mir_func->add_local("_f_hash" + tag, hir::make_int(), true, false);
                BlockId next_block = mir_func->add_block();

                std::vector<MirOperandPtr> hash_args;
                hash_args.push_back(MirOperand::copy(MirPlace(field_val)));
                auto call_term = std::make_unique<MirTerminator>();
                call_term->kind = MirTerminator::Call;
                call_term->data =
                    MirTerminator::CallData{MirOperand::function_ref(field.type->name + "__hash"),
                                            std::move(hash_args),
                                            MirPlace(hash_val),
                                            next_block,
                                            std::nullopt,
                                            "",
                                            "",
                                            false};
                cur_block->terminator = std::move(call_term);
                current_block = next_block;

                mix_value(hash_val, tag);
            } else if (field.type && field.type->kind == hir::TypeKind::Array &&
                       (field.type->array_size.has_value() || field.type->dimensions.size() == 1)) {
                // 固定長1次元配列: 要素を順に混合する（コンパイル時展開）
                uint32_t n = field.type->array_size.has_value() ? *field.type->array_size
                                                                : field.type->dimensions[0];
                auto elem_type =
                    field.type->element_type ? field.type->element_type : hir::make_int();
                for (uint32_t j = 0; j < n; ++j) {
                    const std::string etag = tag + "_" + std::to_string(j);
                    LocalId idx = mir_func->add_local("_idx" + etag, hir::make_int(), true, false);
                    cur_block->statements.push_back(MirStatement::assign(
                        MirPlace(idx), MirRvalue::use(make_int_const(int64_t(j)))));

                    LocalId elem_val = mir_func->add_local("_elem" + etag, elem_type, true, false);
                    cur_block->statements.push_back(MirStatement::assign(
                        MirPlace(elem_val), MirRvalue::use(MirOperand::copy(MirPlace(
                                                self_local, {PlaceProjection::field(i),
                                                             PlaceProjection::index(idx)})))));

                    mix_value(elem_val, etag);
                }
            } else {
                // プリミティブ型: 値を直接混合する
                LocalId field_val = mir_func->add_local("_f" + tag, field.type, true, false);
                cur_block->statements.push_back(MirStatement::assign(
                    MirPlace(field_val), MirRvalue::use(MirOperand::copy(
                                             MirPlace(self_local, {PlaceProjection::field(i)})))));

                mix_value(field_val, tag);
            }
        }

        auto* final_block = mir_func->get_block(current_block);
        final_block->statements.push_back(MirStatement::assign(
            MirPlace(mir_func->return_local), MirRvalue::use(MirOperand::copy(MirPlace(acc)))));
        final_block->terminator = MirTerminator::return_value();

        impl_info[st.name]["Hash"] = func_name;
        mir_program.functions.push_back(std::move(mir_func));
        return;
    }

    block->terminator = MirTerminator::return_value();

    // impl_info に登録
    impl_info[st.name]["Hash"] = func_name;

    // MIRプログラムに追加
    mir_program.functions.push_back(std::move(mir_func));
}

// モノモーフィゼーション版Debug自動実装
void MirLowering::generate_builtin_debug_method_for_monomorphized(const MirStruct& st) {
    std::string func_name = st.name + "__debug";

    // 既に生成されている場合はスキップ
    for (const auto& func : mir_program.functions) {
        if (func && func->name == func_name)
            return;
    }

    auto mir_func = std::make_unique<MirFunction>();
    mir_func->name = func_name;

    auto struct_type = hir::make_named(st.name);

    mir_func->return_local = mir_func->add_local("_0", hir::make_string(), true, false);
    LocalId self_local = mir_func->add_local("self", struct_type, false, true);
    mir_func->arg_locals.push_back(self_local);

    BlockId entry_block = mir_func->add_block();
    auto* block = mir_func->get_block(entry_block);

    std::string initial_str = st.name + " { ";
    LocalId result = mir_func->add_local("_result", hir::make_string(), true, false);

    auto const_init = std::make_unique<MirOperand>();
    const_init->kind = MirOperand::Constant;
    MirConstant c_init;
    c_init.value = initial_str;
    c_init.type = hir::make_string();
    const_init->data = c_init;
    block->statements.push_back(
        MirStatement::assign(MirPlace(result), MirRvalue::use(std::move(const_init))));

    for (size_t i = 0; i < st.fields.size(); ++i) {
        const auto& field = st.fields[i];

        std::string field_prefix = field.name + ": ";
        LocalId prefix_str =
            mir_func->add_local("_prefix" + std::to_string(i), hir::make_string(), true, false);

        auto const_prefix = std::make_unique<MirOperand>();
        const_prefix->kind = MirOperand::Constant;
        MirConstant c_prefix;
        c_prefix.value = field_prefix;
        c_prefix.type = hir::make_string();
        const_prefix->data = c_prefix;
        block->statements.push_back(
            MirStatement::assign(MirPlace(prefix_str), MirRvalue::use(std::move(const_prefix))));

        LocalId concat1 =
            mir_func->add_local("_concat1_" + std::to_string(i), hir::make_string(), true, false);
        block->statements.push_back(MirStatement::assign(
            MirPlace(concat1),
            MirRvalue::binary(MirBinaryOp::Add, MirOperand::copy(MirPlace(result)),
                              MirOperand::copy(MirPlace(prefix_str)))));
        result = concat1;

        LocalId field_val =
            mir_func->add_local("_field" + std::to_string(i), field.type, true, false);
        auto field_place = MirPlace(self_local, {PlaceProjection::field(i)});
        block->statements.push_back(MirStatement::assign(
            MirPlace(field_val), MirRvalue::use(MirOperand::copy(field_place))));

        LocalId field_str =
            mir_func->add_local("_fstr" + std::to_string(i), hir::make_string(), true, false);

        std::string convert_func;
        if (field.type->kind == hir::TypeKind::Int) {
            convert_func = "cm_format_int";
        } else if (field.type->kind == hir::TypeKind::UInt) {
            convert_func = "cm_format_uint";
        } else if (field.type->kind == hir::TypeKind::Bool) {
            convert_func = "cm_format_bool";
        } else if (field.type->kind == hir::TypeKind::Float ||
                   field.type->kind == hir::TypeKind::Double) {
            convert_func = "cm_format_double";
        } else if (field.type->kind == hir::TypeKind::String) {
            block->statements.push_back(MirStatement::assign(
                MirPlace(field_str), MirRvalue::use(MirOperand::copy(MirPlace(field_val)))));
            convert_func = "";
        } else if (field.type->kind == hir::TypeKind::Char) {
            convert_func = "cm_format_char";
        } else if (field.type->kind == hir::TypeKind::Struct) {
            convert_func = field.type->name + "__debug";
        } else {
            convert_func = "cm_format_int";
        }

        if (!convert_func.empty()) {
            std::vector<MirOperandPtr> args;
            args.push_back(MirOperand::copy(MirPlace(field_val)));

            BlockId next_block = mir_func->add_block();
            auto call_term = std::make_unique<MirTerminator>();
            call_term->kind = MirTerminator::Call;
            call_term->data = MirTerminator::CallData{MirOperand::function_ref(convert_func),
                                                      std::move(args),
                                                      MirPlace(field_str),
                                                      next_block,
                                                      std::nullopt,
                                                      std::string(),
                                                      std::string(),
                                                      false};
            block->terminator = std::move(call_term);
            block = mir_func->get_block(next_block);
        }

        LocalId concat2 =
            mir_func->add_local("_concat2_" + std::to_string(i), hir::make_string(), true, false);
        block->statements.push_back(MirStatement::assign(
            MirPlace(concat2),
            MirRvalue::binary(MirBinaryOp::Add, MirOperand::copy(MirPlace(result)),
                              MirOperand::copy(MirPlace(field_str)))));
        result = concat2;

        if (i + 1 < st.fields.size()) {
            std::string sep = ", ";
            LocalId sep_str =
                mir_func->add_local("_sep" + std::to_string(i), hir::make_string(), true, false);

            auto const_sep = std::make_unique<MirOperand>();
            const_sep->kind = MirOperand::Constant;
            MirConstant c_sep;
            c_sep.value = sep;
            c_sep.type = hir::make_string();
            const_sep->data = c_sep;
            block->statements.push_back(
                MirStatement::assign(MirPlace(sep_str), MirRvalue::use(std::move(const_sep))));

            LocalId concat3 = mir_func->add_local("_concat3_" + std::to_string(i),
                                                  hir::make_string(), true, false);
            block->statements.push_back(MirStatement::assign(
                MirPlace(concat3),
                MirRvalue::binary(MirBinaryOp::Add, MirOperand::copy(MirPlace(result)),
                                  MirOperand::copy(MirPlace(sep_str)))));
            result = concat3;
        }
    }

    std::string closing = st.fields.empty() ? "}" : " }";
    LocalId close_str = mir_func->add_local("_close", hir::make_string(), true, false);

    auto const_close = std::make_unique<MirOperand>();
    const_close->kind = MirOperand::Constant;
    MirConstant c_close;
    c_close.value = closing;
    c_close.type = hir::make_string();
    const_close->data = c_close;
    block->statements.push_back(
        MirStatement::assign(MirPlace(close_str), MirRvalue::use(std::move(const_close))));

    LocalId final_result = mir_func->add_local("_final", hir::make_string(), true, false);
    block->statements.push_back(
        MirStatement::assign(MirPlace(final_result),
                             MirRvalue::binary(MirBinaryOp::Add, MirOperand::copy(MirPlace(result)),
                                               MirOperand::copy(MirPlace(close_str)))));

    block->statements.push_back(
        MirStatement::assign(MirPlace(mir_func->return_local),
                             MirRvalue::use(MirOperand::copy(MirPlace(final_result)))));

    block->terminator = MirTerminator::return_value();

    impl_info[st.name]["Debug"] = func_name;
    mir_program.functions.push_back(std::move(mir_func));
}

// モノモーフィゼーション版Display自動実装
void MirLowering::generate_builtin_display_method_for_monomorphized(const MirStruct& st) {
    std::string func_name = st.name + "__toString";

    for (const auto& func : mir_program.functions) {
        if (func && func->name == func_name)
            return;
    }

    auto mir_func = std::make_unique<MirFunction>();
    mir_func->name = func_name;

    auto struct_type = hir::make_named(st.name);

    mir_func->return_local = mir_func->add_local("_0", hir::make_string(), true, false);
    LocalId self_local = mir_func->add_local("self", struct_type, false, true);
    mir_func->arg_locals.push_back(self_local);

    BlockId entry_block = mir_func->add_block();
    auto* block = mir_func->get_block(entry_block);

    std::string initial_str = "(";
    LocalId result = mir_func->add_local("_result", hir::make_string(), true, false);

    auto const_init = std::make_unique<MirOperand>();
    const_init->kind = MirOperand::Constant;
    MirConstant c_init;
    c_init.value = initial_str;
    c_init.type = hir::make_string();
    const_init->data = c_init;
    block->statements.push_back(
        MirStatement::assign(MirPlace(result), MirRvalue::use(std::move(const_init))));

    for (size_t i = 0; i < st.fields.size(); ++i) {
        const auto& field = st.fields[i];

        LocalId field_val =
            mir_func->add_local("_field" + std::to_string(i), field.type, true, false);
        auto field_place = MirPlace(self_local, {PlaceProjection::field(i)});
        block->statements.push_back(MirStatement::assign(
            MirPlace(field_val), MirRvalue::use(MirOperand::copy(field_place))));

        LocalId field_str =
            mir_func->add_local("_fstr" + std::to_string(i), hir::make_string(), true, false);

        std::string convert_func;
        if (field.type->kind == hir::TypeKind::Int) {
            convert_func = "cm_format_int";
        } else if (field.type->kind == hir::TypeKind::UInt) {
            convert_func = "cm_format_uint";
        } else if (field.type->kind == hir::TypeKind::Bool) {
            convert_func = "cm_format_bool";
        } else if (field.type->kind == hir::TypeKind::Float ||
                   field.type->kind == hir::TypeKind::Double) {
            convert_func = "cm_format_double";
        } else if (field.type->kind == hir::TypeKind::String) {
            block->statements.push_back(MirStatement::assign(
                MirPlace(field_str), MirRvalue::use(MirOperand::copy(MirPlace(field_val)))));
            convert_func = "";
        } else if (field.type->kind == hir::TypeKind::Char) {
            convert_func = "cm_format_char";
        } else if (field.type->kind == hir::TypeKind::Struct) {
            convert_func = field.type->name + "__toString";
        } else {
            convert_func = "cm_format_int";
        }

        if (!convert_func.empty()) {
            std::vector<MirOperandPtr> args;
            args.push_back(MirOperand::copy(MirPlace(field_val)));

            BlockId next_block = mir_func->add_block();
            auto call_term = std::make_unique<MirTerminator>();
            call_term->kind = MirTerminator::Call;
            call_term->data = MirTerminator::CallData{MirOperand::function_ref(convert_func),
                                                      std::move(args),
                                                      MirPlace(field_str),
                                                      next_block,
                                                      std::nullopt,
                                                      std::string(),
                                                      std::string(),
                                                      false};
            block->terminator = std::move(call_term);
            block = mir_func->get_block(next_block);
        }

        LocalId concat =
            mir_func->add_local("_concat" + std::to_string(i), hir::make_string(), true, false);
        block->statements.push_back(MirStatement::assign(
            MirPlace(concat),
            MirRvalue::binary(MirBinaryOp::Add, MirOperand::copy(MirPlace(result)),
                              MirOperand::copy(MirPlace(field_str)))));
        result = concat;

        if (i + 1 < st.fields.size()) {
            std::string sep = ", ";
            LocalId sep_str =
                mir_func->add_local("_sep" + std::to_string(i), hir::make_string(), true, false);

            auto const_sep = std::make_unique<MirOperand>();
            const_sep->kind = MirOperand::Constant;
            MirConstant c_sep;
            c_sep.value = sep;
            c_sep.type = hir::make_string();
            const_sep->data = c_sep;
            block->statements.push_back(
                MirStatement::assign(MirPlace(sep_str), MirRvalue::use(std::move(const_sep))));

            LocalId concat2 = mir_func->add_local("_concat2_" + std::to_string(i),
                                                  hir::make_string(), true, false);
            block->statements.push_back(MirStatement::assign(
                MirPlace(concat2),
                MirRvalue::binary(MirBinaryOp::Add, MirOperand::copy(MirPlace(result)),
                                  MirOperand::copy(MirPlace(sep_str)))));
            result = concat2;
        }
    }

    std::string closing = ")";
    LocalId close_str = mir_func->add_local("_close", hir::make_string(), true, false);

    auto const_close = std::make_unique<MirOperand>();
    const_close->kind = MirOperand::Constant;
    MirConstant c_close;
    c_close.value = closing;
    c_close.type = hir::make_string();
    const_close->data = c_close;
    block->statements.push_back(
        MirStatement::assign(MirPlace(close_str), MirRvalue::use(std::move(const_close))));

    LocalId final_result = mir_func->add_local("_final", hir::make_string(), true, false);
    block->statements.push_back(
        MirStatement::assign(MirPlace(final_result),
                             MirRvalue::binary(MirBinaryOp::Add, MirOperand::copy(MirPlace(result)),
                                               MirOperand::copy(MirPlace(close_str)))));

    block->statements.push_back(
        MirStatement::assign(MirPlace(mir_func->return_local),
                             MirRvalue::use(MirOperand::copy(MirPlace(final_result)))));

    block->terminator = MirTerminator::return_value();

    impl_info[st.name]["Display"] = func_name;
    mir_program.functions.push_back(std::move(mir_func));
}

// 演算子の自動実装を生成（ユーザー定義インターフェース用）
void MirLowering::generate_auto_operator_impl(const hir::HirStruct& st,
                                              const hir::HirInterface& iface,
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
void MirLowering::lower_functions(const hir::HirProgram& hir_program) {
    // 事前パス: 全HIR関数を登録する。
    // 後方の関数を呼ぶ呼び出しでもデフォルト引数補完（lower_call）が参照できるようにするため、本体のlowering前にマップを完成させる
    for (const auto& decl : hir_program.declarations) {
        if (auto* func = std::get_if<std::unique_ptr<hir::HirFunction>>(&decl->kind)) {
            hir_functions[(*func)->name] = func->get();
        } else if (auto* eb = std::get_if<std::unique_ptr<hir::HirExternBlock>>(&decl->kind)) {
            for (const auto& func : (*eb)->functions) {
                hir_functions[func->name] = func.get();
            }
        } else if (auto* impl = std::get_if<std::unique_ptr<hir::HirImpl>>(&decl->kind)) {
            // implメソッドもマングル名（type__method）で登録する。
            // 補間ミニパイプラインの戻り型解決が関数本体のlowering中に参照するため、Pass 3を待たずここで登録する必要がある
            if (!(*impl)->target_type.empty()) {
                for (const auto& method : (*impl)->methods) {
                    if (!method) {
                        continue;
                    }
                    std::string mangled = (method->is_constructor || method->is_destructor)
                                              ? method->name
                                              : (*impl)->target_type + "__" + method->name;
                    hir_functions[mangled] = method.get();
                }
            }
        }
    }

    for (const auto& decl : hir_program.declarations) {
        if (auto* func = std::get_if<std::unique_ptr<hir::HirFunction>>(&decl->kind)) {
            if (auto mir_func = lower_function(**func)) {
                // ソースファイル情報を設定（モジュール分割用）
                mir_func->source_file = resolve_source_file(decl->span.start);
                // HIR関数の参照を保存
                hir_functions[(*func)->name] = func->get();
                mir_program.functions.push_back(std::move(mir_func));
            }
        } else if (auto* extern_block =
                       std::get_if<std::unique_ptr<hir::HirExternBlock>>(&decl->kind)) {
            // extern "C" ブロック内の関数を処理
            for (const auto& func : (*extern_block)->functions) {
                if (auto mir_func = lower_function(*func)) {
                    mir_func->package_name = (*extern_block)->package_name;
                    // ソースファイル情報を設定（モジュール分割用）
                    mir_func->source_file = resolve_source_file(decl->span.start);
                    hir_functions[func->name] = func.get();
                    mir_program.functions.push_back(std::move(mir_func));
                }
            }
        } else if (auto* initial_block =
                       std::get_if<std::unique_ptr<hir::HirInitialBlock>>(&decl->kind)) {
            // SV initial ブロックを処理
            auto mir_initial = std::make_unique<MirInitialBlock>();
            mir_initial->attributes = (*initial_block)->attributes;

            // HIR文への参照を保持（SVコードジェネレータで使用）
            for (const auto& stmt : (*initial_block)->body) {
                if (stmt) {
                    mir_initial->hir_stmts.push_back(stmt.get());
                }
            }

            mir_program.initial_blocks.push_back(std::move(mir_initial));
        }
    }
}

// impl内のメソッドをlowering
void MirLowering::lower_impl_methods(const hir::HirProgram& hir_program) {
    for (const auto& decl : hir_program.declarations) {
        if (auto* impl = std::get_if<std::unique_ptr<hir::HirImpl>>(&decl->kind)) {
            // lowering前の関数数を記録
            size_t prev_count = mir_program.functions.size();
            lower_impl(**impl);
            // 新たに追加された関数にソースファイル情報を設定
            std::string src = resolve_source_file(decl->span.start);
            for (size_t i = prev_count; i < mir_program.functions.size(); ++i) {
                if (mir_program.functions[i]) {
                    mir_program.functions[i]->source_file = src;
                }
            }
        }
    }
}

// モノモーフィゼーションを実行
void MirLowering::perform_monomorphization() {
    monomorphizer.monomorphize(mir_program, hir_functions, struct_defs);
}

}  // namespace cm::mir
