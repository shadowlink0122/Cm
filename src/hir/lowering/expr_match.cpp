// ============================================================
// HIR lowering - match式（パターン条件の構築・ガード・複製）
// ============================================================

#include "expr_internal.hpp"
#include "fwd.hpp"

#include <algorithm>

namespace cm::hir {

// match式のlowering
// v0.13.0: 両方の形式をサポート:
//   - 式形式: pattern => expr (三項演算子チェーンに変換)
//   - ブロック形式: 文として処理されるのでここでは呼ばれない
HirExprPtr HirLowering::lower_match(ast::MatchExpr& match, TypePtr type) {
    // 全てのarmが式形式かチェック
    bool all_expr_form = true;
    for (auto& arm : match.arms) {
        if (arm.is_block_form) {
            all_expr_form = false;
            break;
        }
    }

    // ブロック形式が混在している場合は警告してダミー値を返す
    if (!all_expr_form) {
        debug::hir::log(debug::hir::Id::Warning,
                        "match with block arms should be used as statement", debug::Level::Warn);
        auto lit = std::make_unique<HirLiteral>();
        lit->value = int64_t{0};
        return std::make_unique<HirExpr>(std::move(lit), ast::make_void());
    }

    // 式形式: 三項演算子のチェーンに変換
    auto scrutinee = lower_expr(*match.scrutinee);
    auto scrutinee_type = match.scrutinee->type;

    // scrutinee_type->nameが空の場合、パターンのenum_variantからenum名を抽出
    std::string original_enum_name;
    if (scrutinee_type && !scrutinee_type->name.empty()) {
        original_enum_name = scrutinee_type->name;
    } else {
        for (const auto& arm : match.arms) {
            if (arm.pattern &&
                (arm.pattern->kind == ast::MatchPatternKind::EnumVariant ||
                 arm.pattern->kind == ast::MatchPatternKind::EnumVariantWithBinding)) {
                const std::string& variant_name = arm.pattern->enum_variant;
                auto sep = variant_name.rfind("::");
                if (sep != std::string::npos) {
                    original_enum_name = variant_name.substr(0, sep);
                    break;
                }
            }
        }
    }

    // armsを逆順でネストされた三項演算子に変換
    HirExprPtr result = nullptr;

    // デフォルト値（ガードなしワイルドカード/Variablearmがあればそれ、なければ0）
    for (auto& arm : match.arms) {
        // ガード条件がある場合はデフォルトとして扱わない
        if ((arm.pattern->kind == ast::MatchPatternKind::Wildcard ||
             arm.pattern->kind == ast::MatchPatternKind::Variable) &&
            !arm.guard) {
            if (arm.expr_body) {
                result = lower_expr(*arm.expr_body);
            }
            break;
        }
    }

    if (!result) {
        result = make_default_value(type);
    }

    // 非ワイルドカードarmを逆順に処理してネスト
    for (auto it = match.arms.rbegin(); it != match.arms.rend(); ++it) {
        auto& arm = *it;

        // ワイルドカードはスキップ（デフォルト値として既に処理済み）
        // ただし、ガード条件がある場合はスキップしない
        if ((arm.pattern->kind == ast::MatchPatternKind::Wildcard ||
             arm.pattern->kind == ast::MatchPatternKind::Variable) &&
            !arm.guard) {
            continue;
        }

        // 条件を構築
        HirExprPtr cond;

        if (arm.pattern->kind == ast::MatchPatternKind::Variable && arm.guard) {
            // 変数バインディング+ガードの場合：ガード条件のみを評価
            // 変数をscrutineeで置換
            std::string var_name = arm.pattern->var_name;
            cond = lower_guard_with_binding(*arm.guard, var_name, scrutinee, scrutinee_type);
        } else {
            cond = build_match_condition(scrutinee, scrutinee_type, arm);

            // ガード条件がある場合は論理AND
            if (arm.guard) {
                // パターンがenum variant + bindingの場合、ガード内の変数を置換
                HirExprPtr guard;
                if (arm.pattern->kind == ast::MatchPatternKind::EnumVariantWithBinding &&
                    !arm.pattern->binding_name.empty()) {
                    // バインディング変数をペイロード値で置換
                    // ペイロード型を取得
                    TypePtr payload_type = scrutinee_type;
                    std::string variant_name = arm.pattern->enum_variant;
                    if (!original_enum_name.empty()) {
                        auto enum_it = enum_defs_.find(original_enum_name);
                        if (enum_it != enum_defs_.end() && enum_it->second) {
                            auto sep = variant_name.rfind("::");
                            std::string short_variant = (sep != std::string::npos)
                                                            ? variant_name.substr(sep + 2)
                                                            : variant_name;
                            for (const auto& member : enum_it->second->members) {
                                if (member.name == short_variant && !member.fields.empty()) {
                                    payload_type = member.fields[0].second;
                                    break;
                                }
                            }
                        }
                    }
                    // ペイロード抽出式を生成
                    auto payload_extract = std::make_unique<HirEnumPayload>();
                    payload_extract->scrutinee = clone_hir_expr(scrutinee);
                    payload_extract->variant_name = variant_name;
                    payload_extract->payload_type = payload_type;
                    auto payload_expr =
                        std::make_unique<HirExpr>(std::move(payload_extract), payload_type);
                    // ガード内のバインディング変数をペイロード式で置換
                    guard = lower_guard_with_binding(*arm.guard, arm.pattern->binding_name,
                                                     payload_expr, payload_type);
                } else {
                    guard = lower_expr(*arm.guard);
                }
                auto and_cond = std::make_unique<HirBinary>();
                and_cond->op = HirBinaryOp::And;
                and_cond->lhs = std::move(cond);
                and_cond->rhs = std::move(guard);
                cond = std::make_unique<HirExpr>(std::move(and_cond),
                                                 std::make_shared<ast::Type>(ast::TypeKind::Bool));
            }
        }

        // このarmの値
        HirExprPtr arm_value;
        if (arm.expr_body) {
            // EnumVariantWithBindingパターンの場合、バインディング変数をペイロード式で置換
            if (arm.pattern->kind == ast::MatchPatternKind::EnumVariantWithBinding &&
                !arm.pattern->binding_name.empty()) {
                // ペイロード型を取得
                TypePtr payload_type = scrutinee_type;
                std::string variant_name = arm.pattern->enum_variant;
                if (!original_enum_name.empty()) {
                    auto enum_it = enum_defs_.find(original_enum_name);
                    if (enum_it != enum_defs_.end() && enum_it->second) {
                        auto sep = variant_name.rfind("::");
                        std::string short_variant = (sep != std::string::npos)
                                                        ? variant_name.substr(sep + 2)
                                                        : variant_name;
                        for (const auto& member : enum_it->second->members) {
                            if (member.name == short_variant && !member.fields.empty()) {
                                payload_type = member.fields[0].second;
                                break;
                            }
                        }
                    }
                }
                // ペイロード抽出式を生成
                auto payload_extract = std::make_unique<HirEnumPayload>();
                payload_extract->scrutinee = clone_hir_expr(scrutinee);
                payload_extract->variant_name = variant_name;
                payload_extract->payload_type = payload_type;
                auto payload_expr =
                    std::make_unique<HirExpr>(std::move(payload_extract), payload_type);

                // arm.expr_body内のバインディング変数をペイロード式で置換
                arm_value = lower_guard_with_binding(*arm.expr_body, arm.pattern->binding_name,
                                                     payload_expr, payload_type);
            } else {
                arm_value = lower_expr(*arm.expr_body);
            }
        } else {
            arm_value = make_default_value(type);
        }

        // 三項演算子を構築
        auto ternary = std::make_unique<HirTernary>();
        ternary->condition = std::move(cond);
        ternary->then_expr = std::move(arm_value);
        ternary->else_expr = std::move(result);
        result = std::make_unique<HirExpr>(std::move(ternary), type);
    }

    return result;
}

// 型に応じたデフォルト値を生成
HirExprPtr HirLowering::make_default_value(TypePtr type) {
    auto lit = std::make_unique<HirLiteral>();

    if (type && type->kind == ast::TypeKind::String) {
        lit->value = std::string("");
    } else if (type && type->kind == ast::TypeKind::Bool) {
        lit->value = false;
    } else if (type &&
               (type->kind == ast::TypeKind::Float || type->kind == ast::TypeKind::Double)) {
        lit->value = 0.0;
    } else if (type && type->kind == ast::TypeKind::Char) {
        lit->value = '\0';
    } else {
        lit->value = int64_t{0};
    }

    return std::make_unique<HirExpr>(std::move(lit), type);
}

// matchパターンの条件式を構築（単一パターン用ヘルパー）
HirExprPtr HirLowering::build_single_pattern_condition(const HirExprPtr& scrutinee,
                                                       const ast::MatchPattern& pattern) {
    HirExprPtr scrutinee_copy = clone_hir_expr(scrutinee);

    switch (pattern.kind) {
        case ast::MatchPatternKind::Type: {
            // ユニオンの型パターン: expr is Type と同じ check_only cast を生成
            auto is_cast = std::make_unique<HirCast>();
            is_cast->operand = std::move(scrutinee_copy);
            is_cast->target_type = pattern.type_pattern;
            is_cast->check_only = true;
            return std::make_unique<HirExpr>(std::move(is_cast), ast::make_bool());
        }

        case ast::MatchPatternKind::Literal: {
            auto pattern_value = lower_expr(*pattern.value);
            // bit[N] スクルティニは比較型をuintへ正規化（ICmp型不一致防止）
            if (scrutinee_copy && scrutinee_copy->type &&
                scrutinee_copy->type->kind == ast::TypeKind::Array &&
                scrutinee_copy->type->element_type &&
                scrutinee_copy->type->element_type->kind == ast::TypeKind::Bit) {
                scrutinee_copy->type = ast::make_uint();
            }
            auto cond = std::make_unique<HirBinary>();
            cond->op = HirBinaryOp::Eq;
            cond->lhs = std::move(scrutinee_copy);
            cond->rhs = std::move(pattern_value);
            return std::make_unique<HirExpr>(std::move(cond),
                                             std::make_shared<ast::Type>(ast::TypeKind::Bool));
        }

        case ast::MatchPatternKind::EnumVariant: {
            // scrutineeがint型（単純enum）の場合は直接値比較
            // scrutineeがTagged Union型の場合は__tagアクセス
            HirExprPtr lhs_expr;

            // パターン値からenum名を抽出してassociated dataを持つかチェック
            bool is_tagged_union = false;
            std::string variant_full_name;
            if (pattern.value) {
                if (auto* member = pattern.value->as<ast::MemberExpr>()) {
                    // MemberExpr形式: Enum.Variant
                    if (auto* obj = member->object->as<ast::IdentExpr>()) {
                        std::string enum_name = obj->name;
                        variant_full_name = enum_name + "::" + member->member;
                        auto it = enum_defs_.find(enum_name);
                        if (it != enum_defs_.end() && it->second) {
                            is_tagged_union = it->second->is_tagged_union();
                        }
                    }
                } else if (auto* ident = pattern.value->as<ast::IdentExpr>()) {
                    // IdentExpr形式: EnumName::Variant（::を含む場合）
                    variant_full_name = ident->name;
                    size_t pos = ident->name.find("::");
                    if (pos != std::string::npos) {
                        std::string enum_name = ident->name.substr(0, pos);
                        auto it = enum_defs_.find(enum_name);
                        if (it != enum_defs_.end() && it->second) {
                            is_tagged_union = it->second->is_tagged_union();
                        }
                    }
                }
            }

            // パターン値を生成
            HirExprPtr pattern_value;
            if (is_tagged_union && !variant_full_name.empty()) {
                // Tagged Union: タグ値を直接intリテラルとして生成
                // lower_exprを使うとHirEnumConstruct(構造体型)が返され、
                // __tag(int)との比較で型不一致になる
                auto ev_it = enum_values_.find(variant_full_name);
                if (ev_it != enum_values_.end()) {
                    auto tag_lit = std::make_unique<HirLiteral>();
                    tag_lit->value = ev_it->second;
                    pattern_value = std::make_unique<HirExpr>(std::move(tag_lit), ast::make_int());
                } else {
                    pattern_value = lower_expr(*pattern.value);
                }
            } else {
                pattern_value = lower_expr(*pattern.value);
            }

            if (is_tagged_union) {
                // Tagged Union: scrutinee.tag (field[0]) を抽出して比較
                auto tag_access = std::make_unique<HirMember>();
                tag_access->object = std::move(scrutinee_copy);
                tag_access->member = "__tag";  // tagフィールド
                lhs_expr = std::make_unique<HirExpr>(std::move(tag_access), make_int());
            } else {
                // 単純なenum（int型）: 直接比較
                lhs_expr = std::move(scrutinee_copy);
            }

            auto cond = std::make_unique<HirBinary>();
            cond->op = HirBinaryOp::Eq;
            cond->lhs = std::move(lhs_expr);
            cond->rhs = std::move(pattern_value);
            return std::make_unique<HirExpr>(std::move(cond),
                                             std::make_shared<ast::Type>(ast::TypeKind::Bool));
        }

        case ast::MatchPatternKind::EnumVariantWithBinding: {
            // Tagged Union: scrutinee.tag (field[0]) を抽出して比較
            auto tag_access = std::make_unique<HirMember>();
            tag_access->object = std::move(scrutinee_copy);
            tag_access->member = "__tag";  // tagフィールド
            auto tag_expr = std::make_unique<HirExpr>(std::move(tag_access), make_int());

            // タグ値を直接intリテラルとして生成
            // lower_exprを使うとTagged Union型のHirEnumConstructが返されるため、
            // __tag(int)との比較で型不一致になる
            HirExprPtr pattern_value;
            auto ev_it = enum_values_.find(pattern.enum_variant);
            if (ev_it != enum_values_.end()) {
                auto tag_lit = std::make_unique<HirLiteral>();
                tag_lit->value = ev_it->second;
                pattern_value = std::make_unique<HirExpr>(std::move(tag_lit), ast::make_int());
            } else {
                // フォールバック: lower_exprを使用
                auto enum_variant_ident = ast::make_ident(pattern.enum_variant, {});
                pattern_value = lower_expr(*enum_variant_ident);
            }

            auto cond = std::make_unique<HirBinary>();
            cond->op = HirBinaryOp::Eq;
            cond->lhs = std::move(tag_expr);
            cond->rhs = std::move(pattern_value);
            return std::make_unique<HirExpr>(std::move(cond),
                                             std::make_shared<ast::Type>(ast::TypeKind::Bool));
        }

        case ast::MatchPatternKind::Variable:
        case ast::MatchPatternKind::Wildcard: {
            auto lit = std::make_unique<HirLiteral>();
            lit->value = true;
            return std::make_unique<HirExpr>(std::move(lit),
                                             std::make_shared<ast::Type>(ast::TypeKind::Bool));
        }

        case ast::MatchPatternKind::Masked: {
            // (scrutinee & mask) == value。
            // bit[N] スクルティニ（ビットスライス結果等）は比較型をuintへ正規化する
            // （bit[N]定数がLLVMでiNに、値側がi32になりICmp型不一致になるため）
            ast::TypePtr cmp_type = scrutinee->type;
            if (cmp_type && cmp_type->kind == ast::TypeKind::Array && cmp_type->element_type &&
                cmp_type->element_type->kind == ast::TypeKind::Bit) {
                cmp_type = ast::make_uint();
                if (scrutinee_copy) {
                    scrutinee_copy->type = cmp_type;
                }
            }
            auto band = std::make_unique<HirBinary>();
            band->op = HirBinaryOp::BitAnd;
            band->lhs = std::move(scrutinee_copy);
            band->rhs = make_int_lit(pattern.masked_mask, cmp_type);
            auto masked = std::make_unique<HirExpr>(std::move(band), cmp_type);
            auto cond = std::make_unique<HirBinary>();
            cond->op = HirBinaryOp::Eq;
            cond->lhs = std::move(masked);
            cond->rhs = make_int_lit(pattern.masked_value, cmp_type);
            return std::make_unique<HirExpr>(std::move(cond),
                                             std::make_shared<ast::Type>(ast::TypeKind::Bool));
        }

        case ast::MatchPatternKind::Range: {
            // 範囲パターン: start <= scrutinee && scrutinee <= end
            auto start_val = lower_expr(*pattern.range_start);
            auto end_val = lower_expr(*pattern.range_end);
            auto scrutinee_copy2 = clone_hir_expr(scrutinee);

            // scrutinee >= start
            auto ge_cond = std::make_unique<HirBinary>();
            ge_cond->op = HirBinaryOp::Ge;
            ge_cond->lhs = std::move(scrutinee_copy);
            ge_cond->rhs = std::move(start_val);
            auto ge_expr = std::make_unique<HirExpr>(
                std::move(ge_cond), std::make_shared<ast::Type>(ast::TypeKind::Bool));

            // scrutinee <= end
            auto le_cond = std::make_unique<HirBinary>();
            le_cond->op = HirBinaryOp::Le;
            le_cond->lhs = std::move(scrutinee_copy2);
            le_cond->rhs = std::move(end_val);
            auto le_expr = std::make_unique<HirExpr>(
                std::move(le_cond), std::make_shared<ast::Type>(ast::TypeKind::Bool));

            // ge && le
            auto and_cond = std::make_unique<HirBinary>();
            and_cond->op = HirBinaryOp::And;
            and_cond->lhs = std::move(ge_expr);
            and_cond->rhs = std::move(le_expr);
            return std::make_unique<HirExpr>(std::move(and_cond),
                                             std::make_shared<ast::Type>(ast::TypeKind::Bool));
        }

        case ast::MatchPatternKind::Or: {
            // 再帰的にORパターンを処理
            if (pattern.or_patterns.empty()) {
                auto lit = std::make_unique<HirLiteral>();
                lit->value = false;
                return std::make_unique<HirExpr>(std::move(lit),
                                                 std::make_shared<ast::Type>(ast::TypeKind::Bool));
            }

            // 最初のパターンの条件を構築
            HirExprPtr result = build_single_pattern_condition(scrutinee, *pattern.or_patterns[0]);

            // 残りのパターンをOR結合
            for (size_t i = 1; i < pattern.or_patterns.size(); ++i) {
                auto next_cond = build_single_pattern_condition(scrutinee, *pattern.or_patterns[i]);

                auto or_cond = std::make_unique<HirBinary>();
                or_cond->op = HirBinaryOp::Or;
                or_cond->lhs = std::move(result);
                or_cond->rhs = std::move(next_cond);
                result = std::make_unique<HirExpr>(
                    std::move(or_cond), std::make_shared<ast::Type>(ast::TypeKind::Bool));
            }

            return result;
        }
    }

    auto lit = std::make_unique<HirLiteral>();
    lit->value = false;
    return std::make_unique<HirExpr>(std::move(lit),
                                     std::make_shared<ast::Type>(ast::TypeKind::Bool));
}

// matchパターンの条件式を構築
HirExprPtr HirLowering::build_match_condition(const HirExprPtr& scrutinee,
                                              TypePtr /*scrutinee_type*/,
                                              const ast::MatchArm& arm) {
    return build_single_pattern_condition(scrutinee, *arm.pattern);
}

// HIR式の簡易クローン
HirExprPtr HirLowering::clone_hir_expr(const HirExprPtr& expr) {
    if (!expr)
        return nullptr;

    if (auto* var = std::get_if<std::unique_ptr<HirVarRef>>(&expr->kind)) {
        auto clone = std::make_unique<HirVarRef>();
        clone->name = (*var)->name;
        return std::make_unique<HirExpr>(std::move(clone), expr->type, expr->span);
    }

    if (auto* lit = std::get_if<std::unique_ptr<HirLiteral>>(&expr->kind)) {
        auto clone = std::make_unique<HirLiteral>();
        clone->value = (*lit)->value;
        return std::make_unique<HirExpr>(std::move(clone), expr->type, expr->span);
    }

    if (auto* member = std::get_if<std::unique_ptr<HirMember>>(&expr->kind)) {
        auto clone = std::make_unique<HirMember>();
        clone->object = clone_hir_expr((*member)->object);
        clone->member = (*member)->member;
        return std::make_unique<HirExpr>(std::move(clone), expr->type, expr->span);
    }

    if (auto* binary = std::get_if<std::unique_ptr<HirBinary>>(&expr->kind)) {
        auto clone = std::make_unique<HirBinary>();
        clone->op = (*binary)->op;
        clone->lhs = clone_hir_expr((*binary)->lhs);
        clone->rhs = clone_hir_expr((*binary)->rhs);
        return std::make_unique<HirExpr>(std::move(clone), expr->type, expr->span);
    }

    if (auto* unary = std::get_if<std::unique_ptr<HirUnary>>(&expr->kind)) {
        auto clone = std::make_unique<HirUnary>();
        clone->op = (*unary)->op;
        clone->operand = clone_hir_expr((*unary)->operand);
        return std::make_unique<HirExpr>(std::move(clone), expr->type, expr->span);
    }

    if (auto* index = std::get_if<std::unique_ptr<HirIndex>>(&expr->kind)) {
        auto clone = std::make_unique<HirIndex>();
        clone->object = clone_hir_expr((*index)->object);
        clone->index = clone_hir_expr((*index)->index);
        return std::make_unique<HirExpr>(std::move(clone), expr->type, expr->span);
    }

    // HirEnumPayloadのクローン
    if (auto* payload = std::get_if<std::unique_ptr<HirEnumPayload>>(&expr->kind)) {
        auto clone = std::make_unique<HirEnumPayload>();
        clone->scrutinee = clone_hir_expr((*payload)->scrutinee);
        clone->variant_name = (*payload)->variant_name;
        clone->payload_type = (*payload)->payload_type;
        return std::make_unique<HirExpr>(std::move(clone), expr->type, expr->span);
    }

    debug::hir::log(debug::hir::Id::Warning, "Complex expression cloning not fully supported",
                    debug::Level::Warn);

    auto clone = std::make_unique<HirLiteral>();
    clone->value = int64_t{0};
    return std::make_unique<HirExpr>(std::move(clone), expr->type, expr->span);
}

// ガード式内の変数束縛をscrutineeで置換してlower
HirExprPtr HirLowering::lower_guard_with_binding(ast::Expr& guard, const std::string& var_name,
                                                 const HirExprPtr& scrutinee,
                                                 TypePtr scrutinee_type) {
    if (auto* ident = guard.as<ast::IdentExpr>()) {
        if (ident->name == var_name) {
            return clone_hir_expr(scrutinee);
        }
    }

    if (auto* binary = guard.as<ast::BinaryExpr>()) {
        auto left = lower_guard_with_binding(*binary->left, var_name, scrutinee, scrutinee_type);
        auto right = lower_guard_with_binding(*binary->right, var_name, scrutinee, scrutinee_type);

        auto hir = std::make_unique<HirBinary>();
        hir->op = convert_binary_op(binary->op);
        hir->lhs = std::move(left);
        hir->rhs = std::move(right);

        TypePtr result_type;
        if (is_comparison_op(binary->op)) {
            result_type = std::make_shared<ast::Type>(ast::TypeKind::Bool);
        } else {
            result_type = left ? left->type : scrutinee_type;
        }

        return std::make_unique<HirExpr>(std::move(hir), result_type);
    }

    if (auto* unary = guard.as<ast::UnaryExpr>()) {
        auto operand =
            lower_guard_with_binding(*unary->operand, var_name, scrutinee, scrutinee_type);

        auto hir = std::make_unique<HirUnary>();
        hir->op = convert_unary_op(unary->op);
        hir->operand = std::move(operand);

        TypePtr result_type;
        if (unary->op == ast::UnaryOp::Not) {
            result_type = std::make_shared<ast::Type>(ast::TypeKind::Bool);
        } else {
            result_type = hir->operand ? hir->operand->type : scrutinee_type;
        }

        return std::make_unique<HirExpr>(std::move(hir), result_type);
    }

    return lower_expr(guard);
}

}  // namespace cm::hir
