#pragma once

// ============================================================
// 診断定義 - エラー (E001-E999)
// メッセージテンプレートは英語（原文キー）。日本語訳は common/i18n.cpp のカタログに登録し、
// DiagnosticEngine::report がフォーマット前に tr() で解決する
// ============================================================

#include "../catalog.hpp"

namespace cm {
namespace diagnostics {
namespace definitions {

/// エラー定義を登録
inline void register_errors(DiagnosticCatalog& catalog) {
    using DL = DiagnosticLevel;
    using DS = DetectionStage;

    // E001-E099: 構文エラー
    catalog.register_definition(
        {"E001", "syntax-error", DL::Error, "unexpected token '{0}'", DS::Parser, false});

    catalog.register_definition({"E002", "missing-semicolon", DL::Error,
                                 "a semicolon is required after the statement", DS::Parser, true});

    catalog.register_definition({"E003", "unmatched-brace", DL::Error,
                                 "unmatched closing brace: '{0}'", DS::Parser, false});

    // E100-E199: 型エラー
    catalog.register_definition({"E100", "type-mismatch", DL::Error,
                                 "type mismatch: '{0}' and '{1}'", DS::TypeCheck, false});

    catalog.register_definition({"E101", "undefined-variable", DL::Error,
                                 "variable '{0}' is not defined", DS::Semantic, false});

    catalog.register_definition({"E102", "undefined-function", DL::Error,
                                 "function '{0}' is not defined", DS::Semantic, false});

    catalog.register_definition(
        {"E103", "undefined-type", DL::Error, "type '{0}' is not defined", DS::Semantic, false});

    // E200-E299: 所有権エラー
    catalog.register_definition({"E200", "use-after-move", DL::Error,
                                 "variable '{0}' is used after move", DS::Semantic, false});

    catalog.register_definition({"E201", "modify-while-borrowed", DL::Error,
                                 "cannot modify '{0}' while it is borrowed", DS::Semantic, false});

    // E300-E399: ポインタエラー
    catalog.register_definition(
        {"E300", "null-dereference", DL::Error, "null pointer dereference", DS::Semantic, false});

    catalog.register_definition({"E301", "invalid-pointer-arithmetic", DL::Error,
                                 "invalid pointer arithmetic: '{0}'", DS::Semantic, false});

    catalog.register_definition({"E302", "pointer-type-mismatch", DL::Error,
                                 "pointer type mismatch: '{0}' and '{1}'", DS::TypeCheck, false});

    catalog.register_definition({"E303", "const-pointer-modification", DL::Error,
                                 "cannot modify through a const pointer", DS::TypeCheck, false});

    catalog.register_definition({"E304", "field-access-on-pointer", DL::Error,
                                 "use '->' for field access on pointer types", DS::TypeCheck,
                                 true});

    // E400-E499: ジェネリクスエラー
    catalog.register_definition({"E400", "type-param-count-mismatch", DL::Error,
                                 "type parameter count mismatch: '{0}' requires {1} type "
                                 "parameter(s)",
                                 DS::TypeCheck, false});

    catalog.register_definition({"E401", "constraint-not-satisfied", DL::Error,
                                 "type constraint not satisfied: '{0}' does not implement '{1}'",
                                 DS::TypeCheck, false});

    catalog.register_definition({"E402", "recursive-type-instantiation", DL::Error,
                                 "recursive type instantiation is not supported: '{0}'",
                                 DS::TypeCheck, false});

    catalog.register_definition({"E403", "invalid-type-argument", DL::Error,
                                 "invalid type argument: '{0}' cannot be used for type parameter "
                                 "'{1}'",
                                 DS::TypeCheck, false});

    catalog.register_definition({"E404", "generic-instantiation-failed", DL::Error,
                                 "failed to instantiate generic type '{0}'", DS::TypeCheck, false});

    // E500-E599: enum/matchエラー
    catalog.register_definition({"E500", "non-exhaustive-match", DL::Error,
                                 "match expression is not exhaustive: '{0}' is not covered",
                                 DS::Semantic, false});

    catalog.register_definition({"E501", "duplicate-match-arm", DL::Error,
                                 "duplicate match arm: '{0}'", DS::Semantic, false});

    catalog.register_definition({"E502", "invalid-enum-variant", DL::Error,
                                 "'{0}' is not a variant of enum '{1}'", DS::Semantic, false});

    catalog.register_definition({"E503", "match-guard-type-error", DL::Error,
                                 "match guards must be of type bool", DS::TypeCheck, false});

    catalog.register_definition({"E504", "unreachable-match-arm", DL::Error,
                                 "this match arm is unreachable", DS::Semantic, false});

    // E600-E699: リテラル/定数エラー
    catalog.register_definition(
        {"E600", "invalid-literal", DL::Error, "invalid literal: '{0}'", DS::Parser, false});

    catalog.register_definition({"E601", "literal-overflow", DL::Error,
                                 "literal '{0}' is out of range for type '{1}'", DS::TypeCheck,
                                 false});

    catalog.register_definition({"E602", "const-expr-required", DL::Error,
                                 "a constant expression is required", DS::Semantic, false});
}

}  // namespace definitions
}  // namespace diagnostics
}  // namespace cm
