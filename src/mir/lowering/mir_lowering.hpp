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
    }

    // HIRプログラムをMIRに変換
    MirProgram lower(const hir::HirProgram& hir_program) {
        // Pass 1: 構造体、typedef、enum、インターフェースの登録
        register_declarations(hir_program);

        // Pass 2: 関数のlowering
        lower_functions(hir_program);

        // Pass 3: impl内のメソッドのlowering
        lower_impl_methods(hir_program);

        // Pass 4: モノモーフィゼーション（インターフェース特殊化）
        perform_monomorphization();

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

    // インターフェース定義を登録
    void register_interface(const hir::HirInterface& iface) {
        auto mir_iface = std::make_unique<MirInterface>();
        mir_iface->name = iface.name;

        for (const auto& method : iface.methods) {
            MirInterfaceMethod mir_method;
            mir_method.name = method.name;
            mir_method.return_type = method.return_type;
            for (const auto& param : method.params) {
                mir_method.param_types.push_back(param.type);
            }
            mir_iface->methods.push_back(std::move(mir_method));
        }

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
    void perform_monomorphization() { monomorphizer.monomorphize(mir_program, hir_functions); }

    // 関数のlowering
    std::unique_ptr<MirFunction> lower_function(const hir::HirFunction& func);

    // impl内のメソッドをlowering
    void lower_impl(const hir::HirImpl& impl);

    // デストラクタを生成（構造体用）
    void generate_destructor(const std::string& type_name, LoweringContext& ctx);

    // デストラクタ呼び出しを挿入
    void emit_destructors(LoweringContext& ctx);
};

}  // namespace cm::mir