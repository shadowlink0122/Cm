/// @file builtins.cpp
/// @brief 組み込みランタイム関数の宣言（print/型変換/文字列/スライス/配列系）
///
/// シグネチャの定義はビルトインレジストリ（codegen/common/builtin_registry.hpp）へ一元化した（runtime-builtin-registry 第1段）。
/// 従来のelse-if連鎖117分岐（名前とシグネチャの手書き分散）は表引きへ置換済みで、ランタイム関数の追加はレジストリ表の1行追加で完結する。

#include "internal/codegen/common/builtin_registry.hpp"
#include "internal/codegen/llvm/core/mir_to_llvm.hpp"

#include <string>
#include <vector>

namespace cm::codegen::llvm_backend {

/// print・文字列・スライス・配列系ランタイム関数のシグネチャ宣言（レジストリに無い場合はnullptr）
llvm::Function* MIRToLLVM::declareBuiltinRuntimeFunction(const std::string& name) {
    const cm::codegen::BuiltinSig* sig = cm::codegen::find_builtin_sig(name);
    if (!sig) {
        // 未該当: 呼び出し元（declareExternalFunction）で後続の解決を継続する
        return nullptr;
    }

    // 型タグ→LLVM型（boolはコンテキストのbool表現に合わせる）
    auto to_llvm = [&](cm::codegen::TypeTag tag) -> llvm::Type* {
        switch (tag) {
            case cm::codegen::TypeTag::Void:
                return ctx.getVoidType();
            case cm::codegen::TypeTag::I1:
                return ctx.getBoolType();
            case cm::codegen::TypeTag::I8:
                return ctx.getI8Type();
            case cm::codegen::TypeTag::I16:
                return ctx.getI16Type();
            case cm::codegen::TypeTag::I32:
                return ctx.getI32Type();
            case cm::codegen::TypeTag::I64:
                return ctx.getI64Type();
            case cm::codegen::TypeTag::F32:
                return ctx.getF32Type();
            case cm::codegen::TypeTag::F64:
                return ctx.getF64Type();
            case cm::codegen::TypeTag::Ptr:
                return ctx.getPtrType();
        }
        return ctx.getPtrType();
    };

    std::vector<llvm::Type*> arg_types;
    arg_types.reserve(sig->num_args);
    for (size_t i = 0; i < sig->num_args; ++i) {
        arg_types.push_back(to_llvm(sig->args[i]));
    }
    auto* func_type = llvm::FunctionType::get(to_llvm(sig->ret), arg_types, sig->vararg);

    // 実シンボル名の別名解決（例: __println__はlibcのprintfとして宣言する）
    const std::string symbol = sig->symbol ? sig->symbol : name;
    auto callee = module->getOrInsertFunction(symbol, func_type);
    return llvm::cast<llvm::Function>(callee.getCallee());
}

}  // namespace cm::codegen::llvm_backend
