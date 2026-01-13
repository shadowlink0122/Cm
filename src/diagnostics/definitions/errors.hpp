#pragma once

// ============================================================
// 診断定義 - エラー (E001-E999)
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
        {"E001", "syntax-error", DL::Error, "予期しないトークン '{0}'", DS::Parser, false});

    catalog.register_definition(
        {"E002", "missing-semicolon", DL::Error, "文の後にセミコロンが必要です", DS::Parser, true});

    catalog.register_definition({"E003", "unmatched-brace", DL::Error,
                                 "対応する閉じ括弧がありません: '{0}'", DS::Parser, false});

    // E100-E199: 型エラー
    catalog.register_definition({"E100", "type-mismatch", DL::Error,
                                 "型が一致しません: '{0}' と '{1}'", DS::TypeCheck, false});

    catalog.register_definition({"E101", "undefined-variable", DL::Error,
                                 "変数 '{0}' は定義されていません", DS::Semantic, false});

    catalog.register_definition({"E102", "undefined-function", DL::Error,
                                 "関数 '{0}' は定義されていません", DS::Semantic, false});

    catalog.register_definition({"E103", "undefined-type", DL::Error,
                                 "型 '{0}' は定義されていません", DS::Semantic, false});

    // E200-E299: 所有権エラー
    catalog.register_definition({"E200", "use-after-move", DL::Error,
                                 "move後の変数 '{0}' を使用しています", DS::Semantic, false});

    catalog.register_definition({"E201", "modify-while-borrowed", DL::Error,
                                 "借用中の '{0}' を変更できません", DS::Semantic, false});
}

}  // namespace definitions
}  // namespace diagnostics
}  // namespace cm
