/// @file jit_engine.hpp
/// @brief LLVM ORC JIT コンパイラエンジン
///
/// MIRプログラムをJITコンパイルして即時実行するエンジン

#pragma once

#include "../../../mir/nodes.hpp"

#include <llvm/ExecutionEngine/Orc/Core.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/IR/LLVMContext.h>
#include <memory>
#include <string>

namespace cm::codegen::jit {

/// JIT実行結果
struct JITResult {
    int exitCode = 0;          ///< 終了コード
    std::string errorMessage;  ///< エラーメッセージ
    bool success = true;       ///< 成功フラグ
};

/// LLVM ORC JITエンジン
///
/// MIRプログラムをLLVM IRに変換し、ORC JITで即時実行する
class JITEngine {
   public:
    JITEngine();
    ~JITEngine();

    // コピー禁止
    JITEngine(const JITEngine&) = delete;
    JITEngine& operator=(const JITEngine&) = delete;

    /// MIRプログラムをJITコンパイルして実行
    /// @param program MIRプログラム
    /// @param entryPoint エントリポイント関数名（デフォルト: "main"）
    /// @param optLevel 最適化レベル (0-3)
    /// @return JIT実行結果
    JITResult execute(const mir::MirProgram& program, const std::string& entryPoint = "main",
                      int optLevel = 3);

   private:
    std::unique_ptr<llvm::orc::LLJIT> jit_;
    std::unique_ptr<llvm::orc::ThreadSafeContext> tsContext_;

    /// JITエンジン初期化
    llvm::Error initializeJIT();

    /// ランタイムシンボルを登録
    void registerRuntimeSymbols();

    /// シンボルをマングルして登録
    llvm::orc::SymbolStringPtr mangle(const std::string& name);

    /// LLVM最適化パスを適用
    void optimizeModule(llvm::Module& module, int optLevel);

    /// 関数ポインタからJITシンボルを作成
    template <typename T>
    llvm::JITEvaluatedSymbol toSymbol(T* ptr) {
        return llvm::JITEvaluatedSymbol::fromPointer(ptr);
    }
};

}  // namespace cm::codegen::jit
