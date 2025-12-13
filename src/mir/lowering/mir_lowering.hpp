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
        // 構造体定義を収集
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
            }
        }

        // typedef定義を収集
        for (const auto& decl : hir_program.declarations) {
            if (auto* td = std::get_if<std::unique_ptr<hir::HirTypedef>>(&decl->kind)) {
                register_typedef(**td);
            }
        }

        // enum定義を収集
        for (const auto& decl : hir_program.declarations) {
            if (auto* e = std::get_if<std::unique_ptr<hir::HirEnum>>(&decl->kind)) {
                register_enum(**e);
            }
        }

        // impl定義から実装情報を収集
        for (const auto& decl : hir_program.declarations) {
            if (auto* impl = std::get_if<std::unique_ptr<hir::HirImpl>>(&decl->kind)) {
                register_impl(**impl);
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