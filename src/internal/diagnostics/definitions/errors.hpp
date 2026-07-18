#pragma once

// ============================================================
// 診断定義 - エラー (E001-E999)
// メッセージテンプレートは英語（原文キー）。日本語訳は common/i18n.cpp のカタログに登録し、
// DiagnosticEngine::report がフォーマット前に tr() で解決する
// ============================================================

#include "internal/diagnostics/catalog.hpp"

namespace cm {
namespace diagnostics {
namespace definitions {

/// エラー定義を登録
inline void register_errors(DiagnosticCatalog& catalog) {
    using DL = DiagnosticLevel;
    using DS = DetectionStage;

    // E001-E099: 構文エラー
    catalog.register_definition(
        {"E001", "syntax-error", DL::Error, i18n::MsgId::DiagE001, DS::Parser, false});

    catalog.register_definition(
        {"E002", "missing-semicolon", DL::Error, i18n::MsgId::DiagE002, DS::Parser, true});

    catalog.register_definition(
        {"E003", "unmatched-brace", DL::Error, i18n::MsgId::DiagE003, DS::Parser, false});

    // E100-E199: 型エラー
    catalog.register_definition(
        {"E100", "type-mismatch", DL::Error, i18n::MsgId::DiagE100, DS::TypeCheck, false});

    catalog.register_definition(
        {"E101", "undefined-variable", DL::Error, i18n::MsgId::DiagE101, DS::Semantic, false});

    catalog.register_definition(
        {"E102", "undefined-function", DL::Error, i18n::MsgId::DiagE102, DS::Semantic, false});

    catalog.register_definition(
        {"E103", "undefined-type", DL::Error, i18n::MsgId::DiagE103, DS::Semantic, false});

    // E200-E299: 所有権エラー
    catalog.register_definition(
        {"E200", "use-after-move", DL::Error, i18n::MsgId::DiagE200, DS::Semantic, false});

    catalog.register_definition(
        {"E201", "modify-while-borrowed", DL::Error, i18n::MsgId::DiagE201, DS::Semantic, false});

    // E300-E399: ポインタエラー
    catalog.register_definition(
        {"E300", "null-dereference", DL::Error, i18n::MsgId::DiagE300, DS::Semantic, false});

    catalog.register_definition({"E301", "invalid-pointer-arithmetic", DL::Error,
                                 i18n::MsgId::DiagE301, DS::Semantic, false});

    catalog.register_definition(
        {"E302", "pointer-type-mismatch", DL::Error, i18n::MsgId::DiagE302, DS::TypeCheck, false});

    catalog.register_definition({"E303", "const-pointer-modification", DL::Error,
                                 i18n::MsgId::DiagE303, DS::TypeCheck, false});

    catalog.register_definition(
        {"E304", "field-access-on-pointer", DL::Error, i18n::MsgId::DiagE304, DS::TypeCheck, true});

    // E400-E499: ジェネリクスエラー
    catalog.register_definition({"E400", "type-param-count-mismatch", DL::Error,
                                 i18n::MsgId::DiagE400, DS::TypeCheck, false});

    catalog.register_definition({"E401", "constraint-not-satisfied", DL::Error,
                                 i18n::MsgId::DiagE401, DS::TypeCheck, false});

    catalog.register_definition({"E402", "recursive-type-instantiation", DL::Error,
                                 i18n::MsgId::DiagE402, DS::TypeCheck, false});

    catalog.register_definition(
        {"E403", "invalid-type-argument", DL::Error, i18n::MsgId::DiagE403, DS::TypeCheck, false});

    catalog.register_definition({"E404", "generic-instantiation-failed", DL::Error,
                                 i18n::MsgId::DiagE404, DS::TypeCheck, false});

    // E500-E599: enum/matchエラー
    catalog.register_definition(
        {"E500", "non-exhaustive-match", DL::Error, i18n::MsgId::DiagE500, DS::Semantic, false});

    catalog.register_definition(
        {"E501", "duplicate-match-arm", DL::Error, i18n::MsgId::DiagE501, DS::Semantic, false});

    catalog.register_definition(
        {"E502", "invalid-enum-variant", DL::Error, i18n::MsgId::DiagE502, DS::Semantic, false});

    catalog.register_definition(
        {"E503", "match-guard-type-error", DL::Error, i18n::MsgId::DiagE503, DS::TypeCheck, false});

    catalog.register_definition(
        {"E504", "unreachable-match-arm", DL::Error, i18n::MsgId::DiagE504, DS::Semantic, false});

    // E600-E699: リテラル/定数エラー
    catalog.register_definition(
        {"E600", "invalid-literal", DL::Error, i18n::MsgId::DiagE600, DS::Parser, false});

    catalog.register_definition(
        {"E601", "literal-overflow", DL::Error, i18n::MsgId::DiagE601, DS::TypeCheck, false});

    catalog.register_definition(
        {"E602", "const-expr-required", DL::Error, i18n::MsgId::DiagE602, DS::Semantic, false});
}

}  // namespace definitions
}  // namespace diagnostics
}  // namespace cm
