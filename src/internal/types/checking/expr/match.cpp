// ============================================================
// TypeChecker 実装 - match式の型推論・網羅性検査・パターン検査
// ============================================================

#include "internal/base/i18n.hpp"
#include "internal/base/text_utils.hpp"
#include "internal/types/type_checker.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cm {

// v0.13.0: matchは両方の形式をサポート:
//   - 式形式: pattern => expr (同じ型を返す)
//   - ブロック形式: pattern => { stmts } (void または return文の型)
ast::TypePtr TypeChecker::infer_match(ast::MatchExpr& match) {
    auto scrutinee_type = infer_type(*match.scrutinee);
    if (!scrutinee_type) {
        error(current_span_, i18n::msg(i18n::MsgId::TcCannotInferTypeMatchScrutinee));
        return ast::make_error();
    }

    // 全armが式形式かブロック形式かを判定
    bool all_expr_form = true;
    bool all_block_form = true;
    for (auto& arm : match.arms) {
        if (arm.is_block_form) {
            all_expr_form = false;
        } else {
            all_block_form = false;
        }
    }

    // 混在時は警告を出す（式形式とブロック形式が混在）
    bool is_mixed = !all_expr_form && !all_block_form;
    if (is_mixed) {
        // 警告のみ、エラーではない
        // 混在時はvoid型を返す
    }

    ast::TypePtr result_type = nullptr;
    size_t arm_index = 0;

    for (auto& arm : match.arms) {
        // パターン束縛（Option::Some(v) の v 等）はアームのスコープに閉じる。
        // 従来はスコープpush前に定義され関数スコープへ漏れていた（既知の問題を修正）
        scopes_.push();

        check_match_pattern(arm.pattern.get(), scrutinee_type);

        if (arm.guard) {
            auto guard_type = infer_type(*arm.guard);
            if (!guard_type || guard_type->kind != ast::TypeKind::Bool) {
                error(current_span_, i18n::msg(i18n::MsgId::TcMatchGuardMustBooleanExpression));
            }
        }

        // 内側スコープ: EnumVariantWithBindingのペイロード精密型がcheck_match_patternの粗い定義をシャドウできるようにする（同一スコープでの再定義は無効のためネストが必要）
        scopes_.push();

        // EnumVariantWithBindingの場合、バインディング変数をスコープに追加
        if (arm.pattern && arm.pattern->kind == ast::MatchPatternKind::EnumVariantWithBinding) {
            if (!arm.pattern->binding_name.empty()) {
                // ペイロードの実際の型を取得（enum定義からバリアントのフィールド型を取得）
                ast::TypePtr binding_type = scrutinee_type;  // フォールバック

                // scrutinee_typeの型名でenum定義を検索
                if (scrutinee_type && !scrutinee_type->name.empty()) {
                    auto enum_it = enum_defs_.find(scrutinee_type->name);
                    if (enum_it != enum_defs_.end() && enum_it->second) {
                        // バリアント名を取得（Type::Variant形式からVariantを抽出）
                        std::string variant_name = arm.pattern->enum_variant;
                        variant_name = cm::text::strip_namespace(variant_name);
                        // enum定義からフィールド型を取得
                        for (const auto& member : enum_it->second->members) {
                            if (member.name == variant_name && !member.fields.empty()) {
                                // 最初のフィールドの型を使用（設計: 1フィールド推奨）
                                binding_type = member.fields[0].second;

                                // ジェネリック型パラメータを具象型に置換
                                // 例: Result<int, string> の Ok(T) → T を int に置換
                                if (binding_type && !scrutinee_type->type_args.empty()) {
                                    const auto& enum_decl = enum_it->second;
                                    const auto& gparams = enum_decl->generic_params.empty()
                                                              ? std::vector<std::string>{}
                                                              : enum_decl->generic_params;
                                    // generic_params_v2からも名前を取得
                                    std::vector<std::string> param_names;
                                    if (!gparams.empty()) {
                                        param_names = gparams;
                                    } else {
                                        for (const auto& gp : enum_decl->generic_params_v2) {
                                            param_names.push_back(gp.name);
                                        }
                                    }

                                    // マッピング構築: T → int, E → string
                                    if (param_names.size() == scrutinee_type->type_args.size()) {
                                        for (size_t i = 0; i < param_names.size(); ++i) {
                                            if (binding_type->name == param_names[i]) {
                                                binding_type = scrutinee_type->type_args[i];
                                                break;
                                            }
                                        }
                                    }
                                }

                                break;
                            }
                        }
                    }
                }
                scopes_.current().define(arm.pattern->binding_name, binding_type);
                mark_variable_initialized(arm.pattern->binding_name);
            }
        }

        if (arm.is_block_form) {
            // ブロック形式: 各文をチェック
            for (auto& stmt : arm.block_body) {
                check_statement(*stmt);
            }
            // ブロック形式はvoid扱い（return文があっても関数のreturn）
        } else {
            // 式形式: 式の型をチェック
            if (arm.expr_body) {
                auto arm_type = infer_type(*arm.expr_body);
                if (arm_type && arm_type->kind != ast::TypeKind::Error) {
                    if (!result_type) {
                        result_type = arm_type;
                    } else if (!types_compatible(result_type, arm_type)) {
                        error(current_span_, i18n::msgf(i18n::MsgId::TcMatchArmHasIncompatibleType,
                                                        std::to_string(arm_index + 1),
                                                        ast::type_to_string(*result_type),
                                                        ast::type_to_string(*arm_type)));
                    }
                }
            }
        }

        scopes_.pop();
        scopes_.pop();
        arm_index++;
    }

    if (match.arms.empty()) {
        error(current_span_, i18n::msg(i18n::MsgId::TcMatchStatementHasNoArms));
        return ast::make_error();
    }

    check_match_exhaustiveness(match, scrutinee_type);

    // 式形式でresult_typeがあればそれを返す
    if (all_expr_form && result_type) {
        return result_type;
    }

    // 混在またはブロック形式のみの場合はvoid
    return ast::make_void();
}

void TypeChecker::check_match_exhaustiveness(ast::MatchExpr& match, ast::TypePtr scrutinee_type) {
    if (!scrutinee_type)
        return;

    bool has_wildcard = false;
    bool has_variable_binding = false;
    std::set<std::string> covered_values;
    std::string detected_enum_name;

    for (const auto& arm : match.arms) {
        if (!arm.pattern)
            continue;

        switch (arm.pattern->kind) {
            case ast::MatchPatternKind::Wildcard:
                has_wildcard = true;
                break;
            case ast::MatchPatternKind::Variable:
                if (!arm.guard) {
                    has_variable_binding = true;
                }
                break;
            case ast::MatchPatternKind::Literal:
                if (arm.pattern->value) {
                    if (auto* lit = arm.pattern->value->as<ast::LiteralExpr>()) {
                        if (lit->is_int()) {
                            covered_values.insert(std::to_string(std::get<int64_t>(lit->value)));
                        } else if (lit->is_bool()) {
                            covered_values.insert(std::get<bool>(lit->value) ? "true" : "false");
                        }
                    }
                }
                break;
            case ast::MatchPatternKind::EnumVariant:
                if (arm.pattern->value) {
                    if (auto* ident = arm.pattern->value->as<ast::IdentExpr>()) {
                        covered_values.insert(ident->name);
                        auto pos = ident->name.find("::");
                        if (pos != std::string::npos) {
                            std::string enum_name = ident->name.substr(0, pos);
                            if (enum_names_.count(enum_name)) {
                                detected_enum_name = enum_name;
                            }
                        }
                    }
                }
                break;
            case ast::MatchPatternKind::EnumVariantWithBinding:
                // EnumType::Variant(binding) パターン
                if (!arm.pattern->enum_variant.empty()) {
                    covered_values.insert(arm.pattern->enum_variant);
                    auto pos = arm.pattern->enum_variant.find("::");
                    if (pos != std::string::npos) {
                        std::string enum_name = arm.pattern->enum_variant.substr(0, pos);
                        if (enum_names_.count(enum_name)) {
                            detected_enum_name = enum_name;
                        }
                    }
                }
                break;
            case ast::MatchPatternKind::Range:
                // 範囲パターンは完全性チェックが複雑なため、現時点ではスキップ（範囲内の値をカバーとみなす）
                break;
            case ast::MatchPatternKind::Or:
                // ORパターンは各サブパターンをカバーとみなす
                // TODO: 再帰的にサブパターンをチェック
                break;
            case ast::MatchPatternKind::Masked:
                // don't careビットマッチ（0b1?00）は複数値をカバーするが、網羅性の強制はbool/enumのみが対象のため個別値は追跡しない
                break;
            case ast::MatchPatternKind::Type:
                // ユニオン型パターン（int i => ...）。網羅性の強制はbool/enumのみが対象のためカバー値は追跡しない
                break;
        }
    }
    if (has_wildcard || has_variable_binding) {
        // R12: return網羅解析（stmt_terminates）が網羅済みmatchを終端と認識できるよう記録する
        match.known_exhaustive = true;
        return;
    }

    if (scrutinee_type->kind == ast::TypeKind::Bool) {
        if (!covered_values.count("true") || !covered_values.count("false")) {
            error(current_span_, i18n::msg(i18n::MsgId::TcNonExhaustiveMatchMissingTrue));
        } else {
            match.known_exhaustive = true;
        }
        return;
    }

    if (!detected_enum_name.empty()) {
        std::set<std::string> all_variants;
        for (const auto& [key, value] : enum_values_) {
            if (key.find(detected_enum_name + "::") == 0) {
                all_variants.insert(key);
            }
        }

        for (const auto& variant : all_variants) {
            if (!covered_values.count(variant)) {
                error(current_span_,
                      i18n::msgf(i18n::MsgId::TcNonExhaustiveMatchMissingPattern, variant));
                return;
            }
        }
        match.known_exhaustive = true;
        return;
    }

    std::string type_name = ast::type_to_string(*scrutinee_type);
    if (enum_names_.count(type_name)) {
        std::set<std::string> all_variants;
        for (const auto& [key, value] : enum_values_) {
            if (key.find(type_name + "::") == 0) {
                all_variants.insert(key);
            }
        }

        for (const auto& variant : all_variants) {
            if (!covered_values.count(variant)) {
                error(current_span_,
                      i18n::msgf(i18n::MsgId::TcNonExhaustiveMatchMissingPattern, variant));
                return;
            }
        }
        match.known_exhaustive = true;
        return;
    }

    if (scrutinee_type->is_integer()) {
        error(current_span_, i18n::msg(i18n::MsgId::TcNonExhaustiveMatchIntegerPatterns));
    }
}

void TypeChecker::check_match_pattern(ast::MatchPattern* pattern, ast::TypePtr expected_type) {
    if (!pattern)
        return;

    switch (pattern->kind) {
        case ast::MatchPatternKind::Literal:
            if (pattern->value) {
                auto lit_type = infer_type(*pattern->value);
                if (!types_compatible(lit_type, expected_type)) {
                    error(current_span_, i18n::msg(i18n::MsgId::TcPatternTypeDoesNotMatch));
                }
            }
            break;

        case ast::MatchPatternKind::Variable:
            if (!pattern->var_name.empty()) {
                scopes_.current().define(pattern->var_name, expected_type);
                mark_variable_initialized(pattern->var_name);
            }
            break;

        case ast::MatchPatternKind::EnumVariant:
            if (pattern->value) {
                // enum型のscrutineeに対するenumバリアントパターン
                // Option型に対してOption::Someパターンをチェック
                if (auto* ident = pattern->value->as<ast::IdentExpr>()) {
                    // パターン名からenum型を抽出（例：Option::Some -> Option）
                    auto pos = ident->name.find("::");
                    if (pos != std::string::npos) {
                        std::string pattern_enum_name = ident->name.substr(0, pos);
                        // パターンのenum型がenum_names_に登録されていれば許可
                        // (scrutineeはint型として解決されているため、直接比較できない)
                        if (enum_names_.count(pattern_enum_name)) {
                            // enumパターンがenum型として有効 - OK scrutineeは必ずint型に解決されるため、チェックをパス
                            break;
                        }
                    }
                }
                // フォールバック: 通常のtype互換性チェック
                auto enum_type = infer_type(*pattern->value);
                if (!types_compatible(enum_type, expected_type)) {
                    error(current_span_, i18n::msg(i18n::MsgId::TcEnumPatternTypeDoesNot));
                }
            }
            break;

        case ast::MatchPatternKind::EnumVariantWithBinding: {
            // EnumType::Variant(binding) のパターン
            // バリアント名を検証し、バインディング変数をスコープに追加
            if (!pattern->enum_variant.empty()) {
                // パターン名からenum型を抽出（例：Option::Some -> Option）
                auto pos = pattern->enum_variant.find("::");
                bool type_matched = false;
                if (pos != std::string::npos) {
                    std::string pattern_enum_name = pattern->enum_variant.substr(0, pos);
                    // パターンのenum型がenum_names_に登録されていれば許可
                    // (scrutineeはint型として解決されているため、直接比較できない)
                    if (enum_names_.count(pattern_enum_name)) {
                        type_matched = true;
                    }
                }

                if (!type_matched) {
                    // フォールバック: 通常のtype互換性チェック
                    auto enum_ident = ast::make_ident(pattern->enum_variant, {});
                    auto enum_type = infer_type(*enum_ident);
                    if (!types_compatible(enum_type, expected_type)) {
                        error(current_span_, i18n::msg(i18n::MsgId::TcEnumPatternTypeDoesNot));
                    }
                }

                // TODO: バインディング変数にAssociated Dataの実際の型を設定
                // 現時点ではexpected_typeをそのまま使用
                if (!pattern->binding_name.empty()) {
                    scopes_.current().define(pattern->binding_name, expected_type);
                    mark_variable_initialized(pattern->binding_name);
                }
            }
            break;
        }

        case ast::MatchPatternKind::Wildcard:
            break;

        case ast::MatchPatternKind::Range:
            // 範囲パターンのチェック
            if (pattern->range_start) {
                auto start_type = infer_type(*pattern->range_start);
                if (!types_compatible(start_type, expected_type)) {
                    error(current_span_, i18n::msg(i18n::MsgId::TcRangeStartTypeDoesNot));
                }
            }
            if (pattern->range_end) {
                auto end_type = infer_type(*pattern->range_end);
                if (!types_compatible(end_type, expected_type)) {
                    error(current_span_, i18n::msg(i18n::MsgId::TcRangeEndTypeDoesNot));
                }
            }
            break;

        case ast::MatchPatternKind::Or:
            // ORパターン内の各サブパターンをチェック
            for (const auto& sub_pattern : pattern->or_patterns) {
                check_match_pattern(sub_pattern.get(), expected_type);
            }
            break;

        case ast::MatchPatternKind::Type: {
            // ユニオンの型パターン: scrutineeがユニオン型で、パターン型が変種のいずれかであること
            auto resolved = resolve_typedef(expected_type);
            auto variants = ast::union_variant_types(resolved);
            if (!resolved || resolved->kind != ast::TypeKind::Union || variants.empty()) {
                error(current_span_,
                      i18n::msgf(i18n::MsgId::TypeTypePatternsCanOnlyBe,
                                 (expected_type ? ast::type_to_string(*expected_type)
                                                : i18n::msg(i18n::MsgId::TypeLabelUnknown))));
            } else if (pattern->type_pattern) {
                std::string target_name = ast::type_to_string(*pattern->type_pattern);
                bool found = false;
                for (const auto& v : variants) {
                    if (v && ast::type_to_string(*v) == target_name) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    error(current_span_,
                          i18n::msgf(i18n::MsgId::TypeTypePatternIsNotA, target_name));
                }
            }
            // 束縛変数をパターン型で登録
            if (!pattern->binding_name.empty() && pattern->binding_name != "_") {
                scopes_.current().define(pattern->binding_name, pattern->type_pattern);
                mark_variable_initialized(pattern->binding_name);
            }
            break;
        }

        default:
            break;
    }
}

}  // namespace cm
