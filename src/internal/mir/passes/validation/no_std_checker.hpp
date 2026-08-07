#pragma once

#include "internal/mir/nodes.hpp"

#include <string>
#include <vector>

namespace cm::mir::opt {

// ============================================================
// no_std環境でのOS依存機能使用チェック
// ============================================================
class NoStdChecker {
   public:
    struct CheckResult {
        bool has_errors = false;
        std::vector<std::string> errors;
    };

    /// no_std環境で禁止される機能の使用をチェックする。
    /// forbid_float: SSE無効ターゲット（baremetal-x86）で浮動小数点型の使用を専用診断にする
    /// （従来はLLVM内部エラー「SSE register return with SSE disabled」が行番号なしで露出していた。R18）
    CheckResult check(const MirProgram& program, bool forbid_float = false);

   private:
    static bool isForbiddenFunction(const std::string& name);
    static std::string getErrorMessage(const std::string& funcName, const std::string& callee);
    static std::string getAddressOfErrorMessage(const std::string& funcName,
                                                const std::string& callee);
    void checkFunction(const MirFunction& func, bool forbid_float, CheckResult& result);
    // オペランドが禁止関数のFunctionRefなら診断する（&putchar等のアドレス取得によるブロックリスト回避の封止。R18）
    void checkOperand(const MirFunction& func, const MirOperand* op, CheckResult& result);
    void checkRvalue(const MirFunction& func, const MirRvalue* rv, CheckResult& result);
};

}  // namespace cm::mir::opt
