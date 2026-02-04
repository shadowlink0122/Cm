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

    // W100-W199: ポインタ警告
    catalog.register_definition({"W100", "unchecked-null", DL::Warning,
                                 "ポインタ '{0}' がnullである可能性があります", DS::Semantic,
                                 false});

    catalog.register_definition({"W101", "raw-pointer-return", DL::Warning,
                                 "生ポインタを返しています。所有権が不明確になる可能性があります",
                                 DS::Semantic, false});

    catalog.register_definition({"W102", "pointer-to-local", DL::Warning,
                                 "ローカル変数 '{0}' へのポインタを返しています", DS::Semantic,
                                 false});

    // W200-W299: ジェネリクス警告
    catalog.register_definition({"W200", "unused-type-parameter", DL::Warning,
                                 "型パラメータ '{0}' は使用されていません", DS::Semantic, true});

    catalog.register_definition({"W201", "redundant-type-annotation", DL::Warning,
                                 "型注釈 '{0}' は推論可能です", DS::Semantic, true});

    catalog.register_definition(
        {"W202", "generic-instantiation-heavy", DL::Warning,
         "ジェネリック型 '{0}' "
         "の多数のインスタンス化によりバイナリサイズが増加する可能性があります",
         DS::Semantic, false});

    // W300-W399: enum/match警告
    catalog.register_definition({"W300", "match-all-wildcard", DL::Warning,
                                 "ワイルドカードパターン '_' がすべてのケースを捕捉しています",
                                 DS::Semantic, false});

    catalog.register_definition({"W301", "enum-variant-unused", DL::Warning,
                                 "enumバリアント '{0}' は使用されていません", DS::Semantic, false});

    catalog.register_definition({"W302", "match-expression-unused", DL::Warning,
                                 "match式の結果が使用されていません", DS::Semantic, false});

    // W400-W499: リテラル/定数警告
    catalog.register_definition({"W400", "magic-number", DL::Warning,
                                 "マジックナンバー '{0}' を定数に置き換えることを検討してください",
                                 DS::Lint, false});

    catalog.register_definition(
        {"W401", "literal-precision-loss", DL::Warning,
         "リテラル '{0}' を '{1}' に変換すると精度が失われる可能性があります", DS::TypeCheck,
         false});
}

}  // namespace definitions
}  // namespace diagnostics
}  // namespace cm
