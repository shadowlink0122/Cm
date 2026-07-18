#pragma once

// ============================================================
// 診断定義 - Lintルール (L001-L999)
// メッセージテンプレートは英語（原文キー）。日本語訳は common/i18n.cpp のカタログに登録し、
// DiagnosticEngine::report がフォーマット前に tr() で解決する
// ============================================================

#include "../catalog.hpp"

namespace cm {
namespace diagnostics {
namespace definitions {

/// Lint定義を登録
inline void register_lints(DiagnosticCatalog& catalog) {
    using DL = DiagnosticLevel;
    using DS = DetectionStage;

    // L001-L099: スタイルルール
    catalog.register_definition({"L001", "naming-convention", DL::Suggestion,
                                 "name '{0}' does not follow the {1} naming convention", DS::Lint,
                                 true});

    catalog.register_definition(
        {"L002", "missing-const", DL::Suggestion, "variable '{0}' can be const", DS::Lint, true});

    catalog.register_definition({"L003", "line-too-long", DL::Suggestion,
                                 "line is too long ({0} > {1} characters)", DS::Lint, false});

    catalog.register_definition({"L004", "trailing-whitespace", DL::Suggestion,
                                 "trailing whitespace at end of line", DS::Lint, true});

    catalog.register_definition({"L005", "missing-newline-at-eof", DL::Suggestion,
                                 "missing newline at end of file", DS::Lint, true});

    catalog.register_definition({"L006", "inconsistent-indentation", DL::Suggestion,
                                 "inconsistent indentation", DS::Lint, true});

    catalog.register_definition({"L007", "brace-style", DL::Suggestion,
                                 "inconsistent brace style (K&R style recommended)", DS::Lint,
                                 true});

    // L100-L199: パフォーマンスルール
    catalog.register_definition({"L100", "unnecessary-copy", DL::Hint,
                                 "an unnecessary copy of '{0}' can be avoided", DS::Lint, true});

    catalog.register_definition({"L101", "inefficient-loop", DL::Hint,
                                 "the loop can be optimized with for-in", DS::Lint, true});

    catalog.register_definition({"L102", "redundant-computation", DL::Hint,
                                 "loop-invariant computation '{0}' inside the loop", DS::Lint,
                                 false});

    catalog.register_definition({"L103", "prefer-iterator", DL::Hint,
                                 "consider using an iterator instead of an index-based loop",
                                 DS::Lint, false});

    // L200-L299: ジェネリクスルール
    catalog.register_definition({"L200", "prefer-explicit-type-args", DL::Suggestion,
                                 "consider specifying type arguments explicitly", DS::Lint, false});

    catalog.register_definition({"L201", "simplifiable-type", DL::Suggestion,
                                 "type '{0}' can be written more simply as '{1}'", DS::Lint, true});

    catalog.register_definition({"L202", "generic-interface-required", DL::Suggestion,
                                 "consider adding an interface constraint to type parameter '{0}'",
                                 DS::Lint, false});

    // L300-L399: ポインタルール
    catalog.register_definition({"L300", "prefer-reference", DL::Suggestion,
                                 "consider using a reference instead of a raw pointer", DS::Lint,
                                 false});

    catalog.register_definition({"L301", "raw-pointer-in-struct", DL::Suggestion,
                                 "raw pointers in structs may cause ownership issues", DS::Lint,
                                 false});

    catalog.register_definition({"L302", "pointer-arrow-style", DL::Suggestion,
                                 "use '->' for access through pointers", DS::Lint, true});

    // L400-L499: enum/matchルール
    catalog.register_definition({"L400", "prefer-match-over-if", DL::Suggestion,
                                 "consider using a match expression instead of multiple if-else",
                                 DS::Lint, false});

    catalog.register_definition({"L401", "match-single-arm", DL::Suggestion,
                                 "a single-arm match expression can be replaced with if-let",
                                 DS::Lint, true});

    catalog.register_definition({"L402", "enum-prefer-exhaustive", DL::Suggestion,
                                 "consider using explicit patterns instead of a wildcard", DS::Lint,
                                 false});
}

}  // namespace definitions
}  // namespace diagnostics
}  // namespace cm
