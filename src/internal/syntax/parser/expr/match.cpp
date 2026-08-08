// ============================================================
// 式パーサ - match式とmatchパターンの解析
// ============================================================

#include "internal/base/debug/par.hpp"
#include "internal/base/i18n.hpp"
#include "internal/syntax/parser/parser.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace cm {

// match式の解析
// match (expr) {
//     pattern => body,
//     pattern if guard => body,
//     _ => { default_body },
// }
// v0.13.0: 両方の形式をサポート:
//   - 式形式: pattern => expr (値を返す)
//   - ブロック形式: pattern => { stmts } (文として処理)
ast::ExprPtr Parser::parse_match_expr(uint32_t start_pos) {
    // 括弧付き構文: match (expr) { ... }
    // 括弧なし構文: match expr { ... }（Rustスタイル）
    ast::ExprPtr scrutinee;
    if (consume_if(TokenKind::LParen)) {
        scrutinee = parse_expr();
        expect(TokenKind::RParen);
    } else {
        // 括弧なし: { が来るまで一次式と後置演算子を解析
        // ただし、{ をstruct literalとして解釈しないよう注意
        scrutinee = parse_primary();

        // 後置演算子（メンバアクセス、関数呼び出し、配列インデックス）を処理
        // ただし { は処理しない（match本体の開始）
        while (true) {
            if (check(TokenKind::Dot)) {
                advance();
                std::string member = expect_ident();
                auto mem_expr = std::make_unique<ast::MemberExpr>(std::move(scrutinee), member);
                scrutinee = std::make_unique<ast::Expr>(std::move(mem_expr));
            } else if (check(TokenKind::LParen)) {
                advance();
                std::vector<ast::ExprPtr> args;
                if (!check(TokenKind::RParen)) {
                    do {
                        args.push_back(parse_expr());
                    } while (consume_if(TokenKind::Comma));
                }
                expect(TokenKind::RParen);
                scrutinee = ast::make_call(std::move(scrutinee), std::move(args));
            } else if (check(TokenKind::LBracket)) {
                advance();
                auto index = parse_expr();
                expect(TokenKind::RBracket);
                auto idx_expr =
                    std::make_unique<ast::IndexExpr>(std::move(scrutinee), std::move(index));
                scrutinee = std::make_unique<ast::Expr>(std::move(idx_expr));
            } else {
                break;  // { や他のトークンで停止
            }
        }
    }

    // LBrace不在チェック: 'match'がキーワードとして誤認識された場合の保護
    // 例: ulong match = 1; → matchがKwMatchとしてレキシングされ、= がLBraceでないためエラー
    if (!check(TokenKind::LBrace)) {
        error(i18n::msg(i18n::MsgId::ParseIsRequiredAfterMatchMatch));
        // ダミーのmatch式を返して呼び出し元に安全に戻る
        auto match_expr =
            std::make_unique<ast::MatchExpr>(std::move(scrutinee), std::vector<ast::MatchArm>{});
        return std::make_unique<ast::Expr>(std::move(match_expr), Span{start_pos, previous().end});
    }
    expect(TokenKind::LBrace);

    std::vector<ast::MatchArm> arms;

    // 無限ループ防止: posが進まない場合はbreak
    size_t last_pos = pos_;
    int match_arm_iterations = 0;
    const int MAX_MATCH_ARMS = 1000;

    while (!check(TokenKind::RBrace) && !is_at_end() && match_arm_iterations < MAX_MATCH_ARMS) {
        // stuck検出: posが前回と同じ→パーサが進めていない（初回含む）
        if (pos_ == last_pos && match_arm_iterations > 0) {
            error(i18n::msg(i18n::MsgId::ParseParserStalledWhileParsingMatch));
            // 次のRBraceまでスキップ
            while (!check(TokenKind::RBrace) && !is_at_end()) {
                advance();
            }
            break;
        }
        last_pos = pos_;

        // パターンをパース
        auto pattern = parse_match_pattern();

        // オプションのガード条件 (if condition)
        ast::ExprPtr guard = nullptr;
        if (consume_if(TokenKind::KwIf)) {
            guard = parse_expr();
        }

        // => (arrow)
        expect(TokenKind::Arrow);

        // アームの本体: { で始まればブロック形式、それ以外は式形式。
        // ただし { ident : は無名構造体リテラル式として式形式へ回す（局所処理調査C4。従来は無条件でブロック化され、値アームの構造体リテラルがvoidブロックへ化けていた）。判定はprimaryの暗黙構造体リテラルと同じ先読み
        bool brace_is_struct_literal = false;
        if (!is_sv_platform_ && check(TokenKind::LBrace)) {
            auto saved_pos = pos_;
            advance();
            if (check(TokenKind::Ident)) {
                advance();
                brace_is_struct_literal = check(TokenKind::Colon);
            }
            pos_ = saved_pos;
        }
        if (check(TokenKind::LBrace) && !brace_is_struct_literal) {
            // ブロック形式
            auto body = parse_block();
            arms.emplace_back(std::move(pattern), std::move(guard), std::move(body));
        } else {
            // 式形式
            auto expr = parse_expr();
            arms.emplace_back(std::move(pattern), std::move(guard), std::move(expr));
        }

        // カンマ（式形式では必須、ブロック形式ではオプショナル）
        consume_if(TokenKind::Comma);
        match_arm_iterations++;
    }

    expect(TokenKind::RBrace);

    auto match_expr = std::make_unique<ast::MatchExpr>(std::move(scrutinee), std::move(arms));
    return std::make_unique<ast::Expr>(std::move(match_expr), Span{start_pos, previous().end});
}

// matchパターン要素の解析（単一パターン）
std::unique_ptr<ast::MatchPattern> Parser::parse_match_pattern_element() {
    // don't careビット付き2進リテラル（0b1?00）→ マスク付きパターン
    if (check(TokenKind::MaskedBinLiteral)) {
        std::string bits(current().get_string());
        advance();
        int64_t value = 0;
        int64_t mask = 0;
        for (char c : bits) {
            value <<= 1;
            mask <<= 1;
            if (c == '1') {
                value |= 1;
                mask |= 1;
            } else if (c == '0') {
                mask |= 1;
            }
            // '?' は value=0, mask=0（don't care）
        }
        return ast::MatchPattern::make_masked(value, mask);
    }

    uint32_t start_pos = current().start;

    // ワイルドカード (_)
    if (check(TokenKind::Ident) && current().get_string() == "_") {
        advance();
        debug::par::log(debug::par::Id::PrimaryExpr, "Match pattern: wildcard",
                        debug::Level::Debug);
        return ast::MatchPattern::make_wildcard();
    }

    // ユニオンの型パターン: 型キーワード + 束縛名（int i, string s 等）
    // 実行時タグで変種を判別し、束縛名にペイロードを束縛する
    if (is_type_start() && !check(TokenKind::Ident)) {
        auto pattern_type = parse_type();
        std::string binding = "_";
        if (check(TokenKind::Ident)) {
            binding = std::string(current().get_string());
            advance();
        }
        debug::par::log(debug::par::Id::PrimaryExpr, "Match pattern: type with binding " + binding,
                        debug::Level::Debug);
        return ast::MatchPattern::make_type(std::move(pattern_type), std::move(binding));
    }

    // リテラルパターン (数値、文字列、真偽値)
    // R12: 単項マイナス付き数値リテラル（-1 や -5...-1）もリテラルパターンとして受理する。
    // 下流（網羅性検査のLiteralExpr直読み・HIR lowering）が負値をそのまま扱えるよう、UnaryExprでなく値を符号反転したLiteralExprに畳み込む
    auto is_negative_literal_start = [&]() {
        return check(TokenKind::Minus) &&
               (peek_kind() == TokenKind::IntLiteral || peek_kind() == TokenKind::FloatLiteral);
    };
    auto parse_literal_pattern_operand = [&]() -> ast::ExprPtr {
        bool negated = false;
        if (is_negative_literal_start()) {
            advance();
            negated = true;
        }
        auto lit_expr = parse_primary();
        if (negated && lit_expr) {
            if (auto* lit = lit_expr->as<ast::LiteralExpr>()) {
                if (lit->is_int()) {
                    lit->value = -std::get<int64_t>(lit->value);
                } else if (lit->is_float()) {
                    lit->value = -std::get<double>(lit->value);
                }
            }
        }
        return lit_expr;
    };
    if (check(TokenKind::IntLiteral) || check(TokenKind::FloatLiteral) ||
        check(TokenKind::StringLiteral) || check(TokenKind::CharLiteral) ||
        check(TokenKind::KwTrue) || check(TokenKind::KwFalse) || check(TokenKind::KwNull) ||
        is_negative_literal_start()) {
        auto lit_expr = parse_literal_pattern_operand();

        // 範囲パターンチェック: val...val
        if (consume_if(TokenKind::Ellipsis)) {
            auto end_expr = parse_literal_pattern_operand();
            debug::par::log(debug::par::Id::PrimaryExpr, "Match pattern: range",
                            debug::Level::Debug);
            return ast::MatchPattern::make_range(std::move(lit_expr), std::move(end_expr));
        }

        debug::par::log(debug::par::Id::PrimaryExpr, "Match pattern: literal", debug::Level::Debug);
        return ast::MatchPattern::make_literal(std::move(lit_expr));
    }

    // enum値パターン (EnumName::Variant) または EnumName::Variant(binding)
    if (check(TokenKind::Ident)) {
        std::string name(current().get_string());
        advance();

        // 名前空間またはenum値アクセス: A::B または A::B::C::...
        if (consume_if(TokenKind::ColonColon)) {
            std::string qualified_name = name;
            do {
                std::string member = expect_ident();
                qualified_name += "::" + member;
            } while (consume_if(TokenKind::ColonColon));

            // パターンバインディング: Option::Some(value)
            if (consume_if(TokenKind::LParen)) {
                // バインディング変数名を取得
                std::string binding_name;
                if (check(TokenKind::Ident)) {
                    binding_name = std::string(current().get_string());
                    advance();
                } else {
                    error(i18n::msg(i18n::MsgId::PsExpectedBindingVariableNamePattern));
                    binding_name = "_";
                }
                expect(TokenKind::RParen);

                debug::par::log(debug::par::Id::PrimaryExpr,
                                "Match pattern: enum variant with binding " + qualified_name + "(" +
                                    binding_name + ")",
                                debug::Level::Debug);
                return ast::MatchPattern::make_enum_variant_with_binding(std::move(qualified_name),
                                                                         std::move(binding_name));
            }

            auto enum_expr =
                ast::make_ident(std::move(qualified_name), Span{start_pos, previous().end});
            debug::par::log(debug::par::Id::PrimaryExpr,
                            "Match pattern: qualified name " + qualified_name, debug::Level::Debug);
            return ast::MatchPattern::make_enum_variant(std::move(enum_expr));
        }

        // 名前付き型の型パターン: TypeName binder（Circle c 等）
        // 識別子が2つ連続する場合は型パターンとして解釈する
        if (check(TokenKind::Ident)) {
            std::string binding = std::string(current().get_string());
            advance();
            auto pattern_type = std::make_shared<ast::Type>(ast::TypeKind::Struct);
            pattern_type->name = name;
            debug::par::log(debug::par::Id::PrimaryExpr,
                            "Match pattern: named type " + name + " with binding " + binding,
                            debug::Level::Debug);
            return ast::MatchPattern::make_type(std::move(pattern_type), std::move(binding));
        }

        // 変数束縛パターン
        debug::par::log(debug::par::Id::PrimaryExpr, "Match pattern: variable " + name,
                        debug::Level::Debug);
        return ast::MatchPattern::make_variable(name);
    }

    error(i18n::msg(i18n::MsgId::PsExpectedMatchPattern));
    return ast::MatchPattern::make_wildcard();
}

// matchパターンの解析（ORパターンをサポート: 1 | 2 | 3）
std::unique_ptr<ast::MatchPattern> Parser::parse_match_pattern() {
    std::vector<std::unique_ptr<ast::MatchPattern>> or_patterns;

    // 最初のパターン要素を解析
    auto first_pattern = parse_match_pattern_element();
    or_patterns.push_back(std::move(first_pattern));

    // ORパターン（|）をチェック
    while (consume_if(TokenKind::Pipe)) {
        auto next_pattern = parse_match_pattern_element();
        or_patterns.push_back(std::move(next_pattern));
    }

    // 単一パターンの場合はそのまま返す
    if (or_patterns.size() == 1) {
        return std::move(or_patterns[0]);
    }

    // 複数パターンの場合はORパターンとして返す
    debug::par::log(debug::par::Id::PrimaryExpr,
                    "Match pattern: OR with " + std::to_string(or_patterns.size()) + " patterns",
                    debug::Level::Debug);
    return ast::MatchPattern::make_or(std::move(or_patterns));
}

}  // namespace cm
