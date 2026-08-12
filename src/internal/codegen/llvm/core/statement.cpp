/// @file statement.cpp
/// @brief MIR文（Assign/Asm等）→ LLVM IR 変換

#include "internal/base/debug/codegen.hpp"
#include "internal/codegen/llvm/monitoring/compilation_guard.hpp"
#include "mir_to_llvm.hpp"

#include <iostream>
#include <llvm/IR/InlineAsm.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cm::codegen::llvm_backend {

void MIRToLLVM::convertStatement(const mir::MirStatement& stmt) {
    // 無限ループ検出用のカウンタ
    static thread_local std::unordered_map<const mir::MirStatement*, int> statementProcessCount;
    static thread_local const mir::MirFunction* lastFunction = nullptr;

    // 新しい関数に入った場合はカウンタをリセット
    if (currentMIRFunction != lastFunction) {
        statementProcessCount.clear();
        lastFunction = currentMIRFunction;
    }

    // 同じステートメントが過度に処理されている場合はエラー
    auto& count = statementProcessCount[&stmt];
    count++;
    if (count > 100) {
        // 内部エラーにはMIR文脈（関数名）を付与し、バグ報告から再現位置を特定できるようにする
        const std::string fn_ctx =
            currentMIRFunction ? " (function: " + currentMIRFunction->name + ")" : "";
        throw std::runtime_error("Infinite loop detected in convertStatement" + fn_ctx);
    }

    if (cm::debug::debug_mode() && currentMIRFunction && currentMIRFunction->name == "main") {
        debug_msg("MIR",
                  "Processing statement kind: " + std::to_string(static_cast<int>(stmt.kind)));
    }
    switch (stmt.kind) {
        case mir::MirStatement::Assign:
            // Assign文の変換本体はstatement/assign.cppに分離
            convertAssignStatement(std::get<mir::MirStatement::AssignData>(stmt.data));
            break;
        case mir::MirStatement::Asm:
            // インラインアセンブリの変換本体はstatement/asm.cppに分離
            convertAsmStatement(std::get<mir::MirStatement::AsmData>(stmt.data));
            break;
        case mir::MirStatement::StorageLive:
        case mir::MirStatement::StorageDead:
        case mir::MirStatement::Nop:
            // これらは無視
            break;
    }

    // 関数終了のデバッグ
    count--;
}

}  // namespace cm::codegen::llvm_backend
