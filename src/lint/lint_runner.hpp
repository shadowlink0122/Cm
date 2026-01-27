#pragma once

// ============================================================
// Lint Runner - DiagnosticEngineを使用
// ============================================================

#include "diagnostics/engine.hpp"
#include "frontend/ast/nodes.hpp"

namespace cm {
namespace lint {

/// Lint実行結果
struct LintResult {
    size_t error_count = 0;
    size_t warning_count = 0;
    size_t hint_count = 0;

    bool has_issues() const { return error_count + warning_count + hint_count > 0; }
};

/// Lint Runner - 統一診断システムを使用
class LintRunner {
   public:
    LintRunner() : engine_() {}

    /// Lintを実行
    LintResult run(const ast::Program& program) {
        LintResult result;

        // TODO: 各診断チェックを実行
        // 現在はスケルトン - 完全実装はv0.12.1で
        //
        // 実装予定:
        // - W001: check_unused_variables(program)
        // - W004: check_unused_imports(program)
        // - L002: check_missing_const(program)

        (void)program;  // unused warning回避

        // 結果を集計
        for (const auto& diag : engine_.diagnostics()) {
            switch (diag.level) {
                case diagnostics::DiagnosticLevel::Error:
                    result.error_count++;
                    break;
                case diagnostics::DiagnosticLevel::Warning:
                    result.warning_count++;
                    break;
                default:
                    result.hint_count++;
                    break;
            }
        }

        return result;
    }

    /// 結果を表示
    void print(const Source& source, std::ostream& out = std::cout) const {
        engine_.print(source, out);
    }

    /// DiagnosticEngineへのアクセス
    diagnostics::DiagnosticEngine& engine() { return engine_; }
    const diagnostics::DiagnosticEngine& engine() const { return engine_; }

   private:
    diagnostics::DiagnosticEngine engine_;
};

}  // namespace lint
}  // namespace cm
