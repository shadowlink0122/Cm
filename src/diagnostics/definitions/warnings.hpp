#pragma once

// ============================================================
// 診断定義 - 警告 (W001-W999)
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
        "W001", "unused-variable", DL::Warning, "変数 '{0}' は使用されていません", DS::Semantic,
        true  // 自動修正可能（削除または_プレフィックス）
    });

    catalog.register_definition({
        "W002", "unreachable-code", DL::Warning, "return文の後のコードは到達不能です", DS::Semantic,
        true  // 自動修正可能（削除）
    });

    catalog.register_definition({"W003", "implicit-conversion", DL::Warning,
                                 "'{0}' から '{1}' への暗黙的な型変換", DS::TypeCheck, false});

    catalog.register_definition({
        "W004", "unused-import", DL::Warning, "import '{0}' は使用されていません", DS::Semantic,
        true  // 自動修正可能（削除）
    });

    catalog.register_definition({
        "W005", "unused-parameter", DL::Warning, "パラメータ '{0}' は使用されていません",
        DS::Semantic, true  // 自動修正可能（_プレフィックス）
    });

    catalog.register_definition({"W006", "shadowed-variable", DL::Warning,
                                 "変数 '{0}' は外側のスコープの変数を隠蔽しています", DS::Semantic,
                                 false});
}

}  // namespace definitions
}  // namespace diagnostics
}  // namespace cm
