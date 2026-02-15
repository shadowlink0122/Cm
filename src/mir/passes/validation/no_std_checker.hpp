#pragma once

#include "../../nodes.hpp"

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

    /// no_std環境で禁止される関数呼び出しをチェック
    CheckResult check(const MirProgram& program);

   private:
    static bool isForbiddenFunction(const std::string& name);
    static std::string getErrorMessage(const std::string& funcName, const std::string& callee);
    void checkFunction(const MirFunction& func, CheckResult& result);
};

}  // namespace cm::mir::opt
