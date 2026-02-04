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

    // E300-E399: ポインタエラー
    catalog.register_definition(
        {"E300", "null-dereference", DL::Error, "nullポインタの参照解除です", DS::Semantic, false});

    catalog.register_definition({"E301", "invalid-pointer-arithmetic", DL::Error,
                                 "無効なポインタ演算: '{0}'", DS::Semantic, false});

    catalog.register_definition({"E302", "pointer-type-mismatch", DL::Error,
                                 "ポインタ型が一致しません: '{0}' と '{1}'", DS::TypeCheck, false});

    catalog.register_definition({"E303", "const-pointer-modification", DL::Error,
                                 "constポインタ経由で変更できません", DS::TypeCheck, false});

    catalog.register_definition({"E304", "field-access-on-pointer", DL::Error,
                                 "ポインタ型に対するフィールドアクセスには '->' を使用してください",
                                 DS::TypeCheck, true});

    // E400-E499: ジェネリクスエラー
    catalog.register_definition(
        {"E400", "type-param-count-mismatch", DL::Error,
         "型パラメータの数が一致しません: '{0}' は {1} 個の型パラメータを必要とします",
         DS::TypeCheck, false});

    catalog.register_definition({"E401", "constraint-not-satisfied", DL::Error,
                                 "型制約を満たしていません: '{0}' は '{1}' を実装していません",
                                 DS::TypeCheck, false});

    catalog.register_definition({"E402", "recursive-type-instantiation", DL::Error,
                                 "再帰的な型のインスタンス化はサポートされていません: '{0}'",
                                 DS::TypeCheck, false});

    catalog.register_definition({"E403", "invalid-type-argument", DL::Error,
                                 "無効な型引数: '{0}' は型パラメータ '{1}' に使用できません",
                                 DS::TypeCheck, false});

    catalog.register_definition({"E404", "generic-instantiation-failed", DL::Error,
                                 "ジェネリック型 '{0}' のインスタンス化に失敗しました",
                                 DS::TypeCheck, false});

    // E500-E599: enum/matchエラー
    catalog.register_definition({"E500", "non-exhaustive-match", DL::Error,
                                 "match式が網羅的ではありません: '{0}' がカバーされていません",
                                 DS::Semantic, false});

    catalog.register_definition({"E501", "duplicate-match-arm", DL::Error,
                                 "重複するmatchアーム: '{0}'", DS::Semantic, false});

    catalog.register_definition({"E502", "invalid-enum-variant", DL::Error,
                                 "'{0}' は enum '{1}' のバリアントではありません", DS::Semantic,
                                 false});

    catalog.register_definition({"E503", "match-guard-type-error", DL::Error,
                                 "matchガードはbool型である必要があります", DS::TypeCheck, false});

    catalog.register_definition({"E504", "unreachable-match-arm", DL::Error,
                                 "このmatchアームには到達できません", DS::Semantic, false});

    // E600-E699: リテラル/定数エラー
    catalog.register_definition(
        {"E600", "invalid-literal", DL::Error, "無効なリテラル: '{0}'", DS::Parser, false});

    catalog.register_definition({"E601", "literal-overflow", DL::Error,
                                 "リテラル '{0}' は型 '{1}' の範囲を超えています", DS::TypeCheck,
                                 false});

    catalog.register_definition(
        {"E602", "const-expr-required", DL::Error, "定数式が必要です", DS::Semantic, false});
}

}  // namespace definitions
}  // namespace diagnostics
}  // namespace cm
