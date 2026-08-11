// auto_impl.cpp - with キーワードによる自動実装（ビルトインメソッド）の生成
// eq/lt/clone/hash/css/debug/display の通常版とモノモーフィゼーション版。
// lowering.cpp（2,912行）から分割（013 §4.3-4 巨大TU分割）

#include "internal/base/debug.hpp"
#include "internal/syntax/ast/typekey.hpp"
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
    // HIR構造体の名前引き（演算子auto-implのネスト構造体フィールドの再帰生成で使用）
    for (const auto& decl : hir_program.declarations) {
        if (auto* st = std::get_if<std::unique_ptr<hir::HirStruct>>(&decl->kind)) {
            hir_structs_by_name_[(*st)->name] = st->get();
        }
    }
    for (const auto& decl : hir_program.declarations) {
        if (auto* st = std::get_if<std::unique_ptr<hir::HirStruct>>(&decl->kind)) {
            const auto& struct_decl = **st;

            // auto_impls が空なら何もしない
            if (struct_decl.auto_impls.empty())
                continue;

            // ジェネリック構造体のderiveは総称implのソース合成（macro/derive.cpp）が処理する。
            // ここへ残る総称構造体の名前は展開対象外のトレイト（Copy・非derive可のユーザーinterface等）のみ
            if (!struct_decl.generic_params.empty()) {
                continue;
            }

            for (const auto& iface_name : struct_decl.auto_impls) {
                // 非ジェネリック構造体の組み込みderiveは、derive-as-source-expansionのexpand_derivesがCmソース合成で処理しauto_implsから除去済みである。
                // ここへ到達する組み込み名は展開漏れ（コンパイラ不変条件の違反）であり、無言で旧MIR直生成へ落とさず内部エラーとして停止する。
                // 死に体だった旧生成器clone/hash/debug/display/css系は削除済み。eq/ltの生成器はユーザー定義インターフェースの演算子auto-impl（generate_auto_operator_impl）が現役利用のため本体を維持している
                if (iface_name == "Eq" || iface_name == "Ord" || iface_name == "Clone" ||
                    iface_name == "Hash" || iface_name == "Debug" || iface_name == "Display" ||
                    iface_name == "Css") {
                    throw std::runtime_error("internal: unexpanded builtin derive '" + iface_name +
                                             "' for non-generic struct '" + struct_decl.name +
                                             "' (expand_derives should have consumed it)");
                }
                if (iface_name == "Copy") {
                    // マーカーインターフェース、コード生成なし
                    impl_info[struct_decl.name]["Copy"] = "";
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

// 演算子の自動実装を生成（ユーザー定義インターフェース用）
void MirLowering::generate_auto_operator_impl(const hir::HirStruct& st,
                                              const hir::HirInterface& iface,
                                              const hir::HirOperatorSig& op) {
    // Eq演算子（==）の自動実装
    if (op.op == hir::HirOperatorKind::Eq) {
        generate_builtin_eq_operator(st);
        impl_info[st.name][iface.name] = ast::typekey::spec_fn_prefix(st.name) + "__op_eq";
    }
    // Ord演算子（<）の自動実装
    else if (op.op == hir::HirOperatorKind::Lt) {
        generate_builtin_lt_operator(st);
        impl_info[st.name][iface.name] = ast::typekey::spec_fn_prefix(st.name) + "__op_lt";
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

    // 特殊化した総称演算子impl（impl<T> Foo<T> for Eq { operator ... }）をimpl_infoへ登録する。
    // これによりPass 6のrewrite_struct_comparison_operatorsが特殊化構造体の==/<等を特殊化演算子関数の
    // 呼び出しへ書き換えられる（従来は特殊化キーが無く生の構造体比較へフォールバックし、
    // interp/LLVMでネスト構造体・stringフィールドを含む比較が誤値になっていた）
    for (const auto& so : monomorphizer.specialized_operators()) {
        impl_info[so.struct_name][so.op_kind] = so.func_name;
    }
}

}  // namespace cm::mir
