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
    catalog.register_definition(
        {"L001", "naming-convention", DL::Suggestion, i18n::MsgId::DiagL001, DS::Lint, true});

    catalog.register_definition(
        {"L002", "missing-const", DL::Suggestion, i18n::MsgId::DiagL002, DS::Lint, true});

    catalog.register_definition(
        {"L003", "line-too-long", DL::Suggestion, i18n::MsgId::DiagL003, DS::Lint, false});

    catalog.register_definition(
        {"L004", "trailing-whitespace", DL::Suggestion, i18n::MsgId::DiagL004, DS::Lint, true});

    catalog.register_definition(
        {"L005", "missing-newline-at-eof", DL::Suggestion, i18n::MsgId::DiagL005, DS::Lint, true});

    catalog.register_definition({"L006", "inconsistent-indentation", DL::Suggestion,
                                 i18n::MsgId::DiagL006, DS::Lint, true});

    catalog.register_definition(
        {"L007", "brace-style", DL::Suggestion, i18n::MsgId::DiagL007, DS::Lint, true});

    // L100-L199: パフォーマンスルール
    catalog.register_definition(
        {"L100", "unnecessary-copy", DL::Hint, i18n::MsgId::DiagL100, DS::Lint, true});

    catalog.register_definition(
        {"L101", "inefficient-loop", DL::Hint, i18n::MsgId::DiagL101, DS::Lint, true});

    catalog.register_definition(
        {"L102", "redundant-computation", DL::Hint, i18n::MsgId::DiagL102, DS::Lint, false});

    catalog.register_definition(
        {"L103", "prefer-iterator", DL::Hint, i18n::MsgId::DiagL103, DS::Lint, false});

    // L200-L299: ジェネリクスルール
    catalog.register_definition({"L200", "prefer-explicit-type-args", DL::Suggestion,
                                 i18n::MsgId::DiagL200, DS::Lint, false});

    catalog.register_definition(
        {"L201", "simplifiable-type", DL::Suggestion, i18n::MsgId::DiagL201, DS::Lint, true});

    catalog.register_definition({"L202", "generic-interface-required", DL::Suggestion,
                                 i18n::MsgId::DiagL202, DS::Lint, false});

    // L300-L399: ポインタルール
    catalog.register_definition(
        {"L300", "prefer-reference", DL::Suggestion, i18n::MsgId::DiagL300, DS::Lint, false});

    catalog.register_definition(
        {"L301", "raw-pointer-in-struct", DL::Suggestion, i18n::MsgId::DiagL301, DS::Lint, false});

    catalog.register_definition(
        {"L302", "pointer-arrow-style", DL::Suggestion, i18n::MsgId::DiagL302, DS::Lint, true});

    // L400-L499: enum/matchルール
    catalog.register_definition(
        {"L400", "prefer-match-over-if", DL::Suggestion, i18n::MsgId::DiagL400, DS::Lint, false});

    catalog.register_definition(
        {"L401", "match-single-arm", DL::Suggestion, i18n::MsgId::DiagL401, DS::Lint, true});

    catalog.register_definition(
        {"L402", "enum-prefer-exhaustive", DL::Suggestion, i18n::MsgId::DiagL402, DS::Lint, false});
}

}  // namespace definitions
}  // namespace diagnostics
}  // namespace cm
