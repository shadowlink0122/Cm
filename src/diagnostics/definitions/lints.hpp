#pragma once

// ============================================================
// 診断定義 - Lintルール (L001-L999)
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
                                 "名前 '{0}' は {1} 命名規則に従っていません", DS::Lint, true});

    catalog.register_definition({"L002", "missing-const", DL::Suggestion,
                                 "変数 '{0}' は const にできます", DS::Lint, true});

    catalog.register_definition({"L003", "line-too-long", DL::Suggestion,
                                 "行が長すぎます（{0} > {1}文字）", DS::Lint, false});

    catalog.register_definition({"L004", "trailing-whitespace", DL::Suggestion,
                                 "行末に不要な空白があります", DS::Lint, true});

    catalog.register_definition({"L005", "missing-newline-at-eof", DL::Suggestion,
                                 "ファイル末尾に改行がありません", DS::Lint, true});

    // L100-L199: パフォーマンスルール
    catalog.register_definition({"L100", "unnecessary-copy", DL::Hint,
                                 "'{0}' の不要なコピーを避けられます", DS::Lint, true});

    catalog.register_definition(
        {"L101", "inefficient-loop", DL::Hint, "ループを for-in で最適化できます", DS::Lint, true});

    catalog.register_definition({"L102", "redundant-computation", DL::Hint,
                                 "ループ内で不変な計算 '{0}' があります", DS::Lint, false});
}

}  // namespace definitions
}  // namespace diagnostics
}  // namespace cm
