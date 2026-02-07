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

    catalog.register_definition({"L006", "inconsistent-indentation", DL::Suggestion,
                                 "インデントが一貫していません", DS::Lint, true});

    catalog.register_definition({"L007", "brace-style", DL::Suggestion,
                                 "波括弧のスタイルが一貫していません（K&Rスタイル推奨）", DS::Lint,
                                 true});

    // L100-L199: パフォーマンスルール
    catalog.register_definition({"L100", "unnecessary-copy", DL::Hint,
                                 "'{0}' の不要なコピーを避けられます", DS::Lint, true});

    catalog.register_definition(
        {"L101", "inefficient-loop", DL::Hint, "ループを for-in で最適化できます", DS::Lint, true});

    catalog.register_definition({"L102", "redundant-computation", DL::Hint,
                                 "ループ内で不変な計算 '{0}' があります", DS::Lint, false});

    catalog.register_definition(
        {"L103", "prefer-iterator", DL::Hint,
         "インデックスベースのループよりイテレータを使用することを検討してください", DS::Lint,
         false});

    // L200-L299: ジェネリクスルール
    catalog.register_definition({"L200", "prefer-explicit-type-args", DL::Suggestion,
                                 "型引数を明示的に指定することを検討してください", DS::Lint,
                                 false});

    catalog.register_definition({"L201", "simplifiable-type", DL::Suggestion,
                                 "型 '{0}' はより簡潔に '{1}' と書けます", DS::Lint, true});

    catalog.register_definition(
        {"L202", "generic-interface-required", DL::Suggestion,
         "型パラメータ '{0}' にインターフェース制約を追加することを検討してください", DS::Lint,
         false});

    // L300-L399: ポインタルール
    catalog.register_definition({"L300", "prefer-reference", DL::Suggestion,
                                 "生ポインタよりも参照を使用することを検討してください", DS::Lint,
                                 false});

    catalog.register_definition({"L301", "raw-pointer-in-struct", DL::Suggestion,
                                 "構造体内の生ポインタは所有権の問題を引き起こす可能性があります",
                                 DS::Lint, false});

    catalog.register_definition({"L302", "pointer-arrow-style", DL::Suggestion,
                                 "ポインタ経由のアクセスには '->' を使用してください", DS::Lint,
                                 true});

    // L400-L499: enum/matchルール
    catalog.register_definition({"L400", "prefer-match-over-if", DL::Suggestion,
                                 "複数のif-elseよりもmatch式を使用することを検討してください",
                                 DS::Lint, false});

    catalog.register_definition({"L401", "match-single-arm", DL::Suggestion,
                                 "単一アームのmatch式はif-letに置き換えられます", DS::Lint, true});

    catalog.register_definition(
        {"L402", "enum-prefer-exhaustive", DL::Suggestion,
         "ワイルドカードの代わりに明示的なパターンを使用することを検討してください", DS::Lint,
         false});
}

}  // namespace definitions
}  // namespace diagnostics
}  // namespace cm
