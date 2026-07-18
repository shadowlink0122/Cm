#pragma once

// ============================================================
// 診断定義 - 警告 (W001-W999)
// メッセージテンプレートは英語（原文キー）。日本語訳は common/i18n.cpp のカタログに登録し、
// DiagnosticEngine::report がフォーマット前に tr() で解決する
// ============================================================

#include "../catalog.hpp"

namespace cm {
namespace diagnostics {
namespace definitions {

/// 警告定義を登録
inline void register_warnings(DiagnosticCatalog& catalog) {
    using DL = DiagnosticLevel;
    using DS = DetectionStage;

    // W001-W099: 一般的な警告
    catalog.register_definition({
        "W001", "unused-variable", DL::Warning, "variable '{0}' is unused", DS::Semantic,
        true  // 自動修正可能（削除または_プレフィックス）
    });

    catalog.register_definition({
        "W002", "unreachable-code", DL::Warning, "code after a return statement is unreachable",
        DS::Semantic,
        true  // 自動修正可能（削除）
    });

    catalog.register_definition({"W003", "implicit-conversion", DL::Warning,
                                 "implicit conversion from '{0}' to '{1}'", DS::TypeCheck, false});

    catalog.register_definition({
        "W004", "unused-import", DL::Warning, "import '{0}' is unused", DS::Semantic,
        true  // 自動修正可能（削除）
    });

    catalog.register_definition({
        "W005", "unused-parameter", DL::Warning, "parameter '{0}' is unused", DS::Semantic,
        true  // 自動修正可能（_プレフィックス）
    });

    catalog.register_definition({"W006", "shadowed-variable", DL::Warning,
                                 "variable '{0}' shadows a variable in an outer scope",
                                 DS::Semantic, false});

    // W100-W199: ポインタ警告
    catalog.register_definition(
        {"W100", "unchecked-null", DL::Warning, "pointer '{0}' may be null", DS::Semantic, false});

    catalog.register_definition({"W101", "raw-pointer-return", DL::Warning,
                                 "returning a raw pointer; ownership may become unclear",
                                 DS::Semantic, false});

    catalog.register_definition({"W102", "pointer-to-local", DL::Warning,
                                 "returning a pointer to local variable '{0}'", DS::Semantic,
                                 false});

    // W200-W299: ジェネリクス警告
    catalog.register_definition({"W200", "unused-type-parameter", DL::Warning,
                                 "type parameter '{0}' is unused", DS::Semantic, true});

    catalog.register_definition({"W201", "redundant-type-annotation", DL::Warning,
                                 "type annotation '{0}' can be inferred", DS::Semantic, true});

    catalog.register_definition({"W202", "generic-instantiation-heavy", DL::Warning,
                                 "many instantiations of generic type '{0}' may increase binary "
                                 "size",
                                 DS::Semantic, false});

    // W300-W399: enum/match警告
    catalog.register_definition({"W300", "match-all-wildcard", DL::Warning,
                                 "wildcard pattern '_' captures all cases", DS::Semantic, false});

    catalog.register_definition({"W301", "enum-variant-unused", DL::Warning,
                                 "enum variant '{0}' is unused", DS::Semantic, false});

    catalog.register_definition({"W302", "match-expression-unused", DL::Warning,
                                 "the result of the match expression is unused", DS::Semantic,
                                 false});

    // W400-W499: リテラル/定数警告
    catalog.register_definition({"W400", "magic-number", DL::Warning,
                                 "consider replacing magic number '{0}' with a named constant",
                                 DS::Lint, false});

    catalog.register_definition({"W401", "literal-precision-loss", DL::Warning,
                                 "converting literal '{0}' to '{1}' may lose precision",
                                 DS::TypeCheck, false});
}

}  // namespace definitions
}  // namespace diagnostics
}  // namespace cm
