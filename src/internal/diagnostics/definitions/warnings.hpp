#pragma once

// ============================================================
// 診断定義 - 警告 (W001-W999)
// メッセージテンプレートは英語（原文キー）。日本語訳は common/i18n.cpp のカタログに登録し、
// DiagnosticEngine::report がフォーマット前に tr() で解決する
// ============================================================

#include "internal/diagnostics/catalog.hpp"

namespace cm {
namespace diagnostics {
namespace definitions {

/// 警告定義を登録
inline void register_warnings(DiagnosticCatalog& catalog) {
    using DL = DiagnosticLevel;
    using DS = DetectionStage;

    // W001-W099: 一般的な警告
    catalog.register_definition({
        "W001", "unused-variable", DL::Warning, i18n::MsgId::DiagW001, DS::Semantic,
        true  // 自動修正可能（削除または_プレフィックス）
    });

    catalog.register_definition({
        "W002", "unreachable-code", DL::Warning, i18n::MsgId::DiagW002, DS::Semantic,
        true  // 自動修正可能（削除）
    });

    catalog.register_definition(
        {"W003", "implicit-conversion", DL::Warning, i18n::MsgId::DiagW003, DS::TypeCheck, false});

    catalog.register_definition({
        "W004", "unused-import", DL::Warning, i18n::MsgId::DiagW004, DS::Semantic,
        true  // 自動修正可能（削除）
    });

    catalog.register_definition({
        "W005", "unused-parameter", DL::Warning, i18n::MsgId::DiagW005, DS::Semantic,
        true  // 自動修正可能（_プレフィックス）
    });

    catalog.register_definition(
        {"W006", "shadowed-variable", DL::Warning, i18n::MsgId::DiagW006, DS::Semantic, false});

    // W100-W199: ポインタ警告
    catalog.register_definition(
        {"W100", "unchecked-null", DL::Warning, i18n::MsgId::DiagW100, DS::Semantic, false});

    catalog.register_definition(
        {"W101", "raw-pointer-return", DL::Warning, i18n::MsgId::DiagW101, DS::Semantic, false});

    catalog.register_definition(
        {"W102", "pointer-to-local", DL::Warning, i18n::MsgId::DiagW102, DS::Semantic, false});

    // W200-W299: ジェネリクス警告
    catalog.register_definition(
        {"W200", "unused-type-parameter", DL::Warning, i18n::MsgId::DiagW200, DS::Semantic, true});

    catalog.register_definition({"W201", "redundant-type-annotation", DL::Warning,
                                 i18n::MsgId::DiagW201, DS::Semantic, true});

    catalog.register_definition({"W202", "generic-instantiation-heavy", DL::Warning,
                                 i18n::MsgId::DiagW202, DS::Semantic, false});

    // W300-W399: enum/match警告
    catalog.register_definition(
        {"W300", "match-all-wildcard", DL::Warning, i18n::MsgId::DiagW300, DS::Semantic, false});

    catalog.register_definition(
        {"W301", "enum-variant-unused", DL::Warning, i18n::MsgId::DiagW301, DS::Semantic, false});

    catalog.register_definition({"W302", "match-expression-unused", DL::Warning,
                                 i18n::MsgId::DiagW302, DS::Semantic, false});

    // W400-W499: リテラル/定数警告
    catalog.register_definition(
        {"W400", "magic-number", DL::Warning, i18n::MsgId::DiagW400, DS::Lint, false});

    catalog.register_definition({"W401", "literal-precision-loss", DL::Warning,
                                 i18n::MsgId::DiagW401, DS::TypeCheck, false});
}

}  // namespace definitions
}  // namespace diagnostics
}  // namespace cm
