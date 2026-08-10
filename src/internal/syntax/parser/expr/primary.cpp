// ============================================================
// 式パーサ - 一次式（リテラル・識別子・組み込み関数・括弧式・ラムダ等）の解析
// ============================================================

#include "internal/base/debug/par.hpp"
#include "internal/base/i18n.hpp"
#include "internal/syntax/parser/parser.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace cm {

// 一次式
ast::ExprPtr Parser::parse_primary() {
    debug::par::log(debug::par::Id::PrimaryExpr, "Parsing primary expression", debug::Level::Trace);
    uint32_t start_pos = current().start;

    // 数値リテラル
    if (check(TokenKind::IntLiteral)) {
        int64_t val = current().get_int();
        bool is_unsigned = current().is_unsigned;
        const auto& bit_info = current().bit_info;
        debug::par::log(debug::par::Id::IntLiteral, "Found integer literal: " + std::to_string(val),
                        debug::Level::Debug);
        advance();
        if (bit_info) {
            // SV幅付きリテラル
            return ast::make_int_literal(val, is_unsigned, bit_info->width, bit_info->base,
                                         bit_info->original, Span{start_pos, previous().end});
        }
        return ast::make_int_literal(val, is_unsigned, Span{start_pos, previous().end});
    }

    if (check(TokenKind::FloatLiteral)) {
        double val = current().get_float();
        debug::par::log(debug::par::Id::FloatLiteral, "Found float literal: " + std::to_string(val),
                        debug::Level::Debug);
        advance();
        return ast::make_float_literal(val, Span{start_pos, previous().end});
    }

    // 文字列リテラル
    if (check(TokenKind::StringLiteral)) {
        std::string val(current().get_string());
        debug::par::log(debug::par::Id::StringLiteral, "Found string literal: \"" + val + "\"",
                        debug::Level::Debug);
        advance();
        return ast::make_string_literal(std::move(val), Span{start_pos, previous().end});
    }

    // 文字リテラル
    if (check(TokenKind::CharLiteral)) {
        std::string s(current().get_string());
        char val = s.empty() ? '\0' : s[0];
        debug::par::log(debug::par::Id::CharLiteral,
                        "Found char literal: '" + std::string(1, val) + "'", debug::Level::Debug);
        advance();
        auto lit = std::make_unique<ast::LiteralExpr>(val);
        return std::make_unique<ast::Expr>(std::move(lit), Span{start_pos, previous().end});
    }

    // true/false
    if (consume_if(TokenKind::KwTrue)) {
        debug::par::log(debug::par::Id::BoolLiteral, "Found boolean literal: true",
                        debug::Level::Debug);
        return ast::make_bool_literal(true, Span{start_pos, previous().end});
    }
    if (consume_if(TokenKind::KwFalse)) {
        debug::par::log(debug::par::Id::BoolLiteral, "Found boolean literal: false",
                        debug::Level::Debug);
        return ast::make_bool_literal(false, Span{start_pos, previous().end});
    }

    // null
    if (consume_if(TokenKind::KwNull)) {
        debug::par::log(debug::par::Id::NullLiteral, "Found null literal", debug::Level::Debug);
        return ast::make_null_literal(Span{start_pos, previous().end});
    }

    // self（impl内でのself参照）
    if (consume_if(TokenKind::KwSelf)) {
        debug::par::log(debug::par::Id::PrimaryExpr, "Found 'self' reference", debug::Level::Debug);
        return ast::make_ident("self", Span{start_pos, previous().end});
    }

    // sizeof式 - sizeof(型) または sizeof(式)
    if (consume_if(TokenKind::KwSizeof)) {
        debug::par::log(debug::par::Id::PrimaryExpr, "Found 'sizeof' expression",
                        debug::Level::Debug);
        expect(TokenKind::LParen);
        return parse_sizeof_operand(start_pos);
    }

    // typeof式 - typeof(式) で型名を文字列として取得
    if (consume_if(TokenKind::KwTypeof)) {
        debug::par::log(debug::par::Id::PrimaryExpr, "Found 'typeof' expression",
                        debug::Level::Debug);
        expect(TokenKind::LParen);
        // 型か式かを判定
        if (is_type_start()) {
            auto type = parse_type();
            expect(TokenKind::RParen);
            return ast::make_typename_of(std::move(type), Span{start_pos, previous().end});
        } else {
            auto expr = parse_expr();
            expect(TokenKind::RParen);
            return ast::make_typename_of_expr(std::move(expr), Span{start_pos, previous().end});
        }
    }

    // コンパイラ組み込み関数 __sizeof__(T) または __sizeof__(expr)
    if (consume_if(TokenKind::KwIntrinsicSizeof)) {
        debug::par::log(debug::par::Id::PrimaryExpr, "Found '__sizeof__' intrinsic",
                        debug::Level::Debug);
        expect(TokenKind::LParen);
        return parse_sizeof_operand(start_pos);
    }

    // コンパイラ組み込み関数 __typeof__(expr)
    // コンパイラ組み込み関数 __typeof__(expr) - typeofと同じ動作
    if (consume_if(TokenKind::KwIntrinsicTypeof)) {
        debug::par::log(debug::par::Id::PrimaryExpr, "Found '__typeof__' intrinsic",
                        debug::Level::Debug);
        expect(TokenKind::LParen);
        if (is_type_start()) {
            auto type = parse_type();
            expect(TokenKind::RParen);
            return ast::make_typename_of(std::move(type), Span{start_pos, previous().end});
        } else {
            auto expr = parse_expr();
            expect(TokenKind::RParen);
            return ast::make_typename_of_expr(std::move(expr), Span{start_pos, previous().end});
        }
    }

    // コンパイラ組み込み関数 __typename__(T) - typeofと同じ動作（後方互換性）
    if (consume_if(TokenKind::KwIntrinsicTypename)) {
        debug::par::log(debug::par::Id::PrimaryExpr, "Found '__typename__' intrinsic",
                        debug::Level::Debug);
        expect(TokenKind::LParen);
        if (is_type_start()) {
            auto type = parse_type();
            expect(TokenKind::RParen);
            return ast::make_typename_of(std::move(type), Span{start_pos, previous().end});
        } else {
            auto expr = parse_expr();
            expect(TokenKind::RParen);
            return ast::make_typename_of_expr(std::move(expr), Span{start_pos, previous().end});
        }
    }

    // コンパイラ組み込み関数 __alignof__(T)
    if (consume_if(TokenKind::KwIntrinsicAlignof)) {
        debug::par::log(debug::par::Id::PrimaryExpr, "Found '__alignof__' intrinsic",
                        debug::Level::Debug);
        expect(TokenKind::LParen);
        auto type = parse_type();
        expect(TokenKind::RParen);
        return ast::make_alignof(std::move(type), Span{start_pos, previous().end});
    }

    // match式
    if (consume_if(TokenKind::KwMatch)) {
        debug::par::log(debug::par::Id::PrimaryExpr, "Found match expression", debug::Level::Debug);
        return parse_match_expr(start_pos);
    }

    // 識別子（enum値アクセスを含む）
    if (check(TokenKind::Ident)) {
        std::string name(current().get_string());
        debug::par::log(debug::par::Id::IdentifierRef, "Found identifier: " + name,
                        debug::Level::Debug);
        advance();

        // ジェネリック型静的メソッド呼び出し: Vec<int>::new() パターンを先読み
        // <Type, Type...>::method の形式をチェック
        if (check(TokenKind::Lt)) {
            // 先読みで<...>::パターンかどうかを判定
            size_t saved_pos = pos_;
            advance();  // < を消費

            // 型引数をスキップ（ネストした<>を考慮）
            // <> 内の最初のトークンが型として妥当かも検証する
            int angle_depth = 1;
            bool found_close = false;
            bool looks_like_type_args = true;

            // <直後のトークンが型引数として妥当かチェック
            // 数値リテラル、演算子などが来た場合は比較演算子の<と判断
            {
                auto first_kind = current().kind;
                if (first_kind == TokenKind::IntLiteral || first_kind == TokenKind::FloatLiteral ||
                    first_kind == TokenKind::StringLiteral ||
                    first_kind == TokenKind::CharLiteral || first_kind == TokenKind::KwTrue ||
                    first_kind == TokenKind::KwFalse || first_kind == TokenKind::KwNull ||
                    first_kind == TokenKind::Minus || first_kind == TokenKind::Bang ||
                    first_kind == TokenKind::Eq || first_kind == TokenKind::Semicolon ||
                    first_kind == TokenKind::RBrace || first_kind == TokenKind::RParen) {
                    looks_like_type_args = false;
                }
            }

            if (looks_like_type_args) {
                while (!is_at_end() && angle_depth > 0) {
                    if (check(TokenKind::Lt)) {
                        angle_depth++;
                    } else if (check(TokenKind::Gt)) {
                        angle_depth--;
                        if (angle_depth == 0) {
                            found_close = true;
                        }
                    } else if (check(TokenKind::GtGt)) {
                        // >> は >> または > > として扱う可能性がある
                        if (angle_depth >= 2) {
                            angle_depth -= 2;
                            if (angle_depth == 0) {
                                found_close = true;
                            }
                        } else {
                            break;  // パターンに合致しない
                        }
                    } else if (check(TokenKind::RParen) || check(TokenKind::LBrace) ||
                               check(TokenKind::RBrace) || check(TokenKind::Semicolon) ||
                               check(TokenKind::Eq) || check(TokenKind::PlusEq) ||
                               check(TokenKind::MinusEq) || check(TokenKind::StarEq) ||
                               check(TokenKind::SlashEq) || check(TokenKind::PercentEq) ||
                               check(TokenKind::KwReturn) || check(TokenKind::KwIf) ||
                               check(TokenKind::KwWhile) || check(TokenKind::KwFor)) {
                        // ジェネリック型引数<T, U>内に出現し得ないトークン
                        // → <は比較演算子と判断してlookaheadを中止
                        break;
                    }
                    advance();
                }
            }

            // >の後に::が続くかチェック
            if (found_close && check(TokenKind::ColonColon)) {
                // ジェネリック型静的メソッド呼び出しパターン: Vec<int>::method()
                // 位置を戻して型引数を正しくパース
                pos_ = saved_pos;
                advance();  // < を消費

                std::string type_args_str = "<";
                std::vector<ast::TypePtr> type_args;
                do {
                    size_t type_parse_pos = pos_;
                    auto type_arg = parse_type();
                    // parse_type()がトークンを消費しなかった場合はスタック防止
                    if (pos_ == type_parse_pos) {
                        if (!is_at_end() && !check(TokenKind::Gt))
                            advance();
                        break;
                    }
                    type_args_str += ast::type_to_string(*type_arg);
                    type_args.push_back(std::move(type_arg));
                    if (consume_if(TokenKind::Comma)) {
                        type_args_str += ", ";
                    }
                } while (!check(TokenKind::Gt) && !is_at_end());
                expect(TokenKind::Gt);
                type_args_str += ">";

                // :: とメソッド名を解析
                expect(TokenKind::ColonColon);
                std::string method_name = expect_ident();

                // 完全修飾名を構築: Vec<int>::method
                std::string qualified_name = name + type_args_str + "::" + method_name;

                debug::par::log(debug::par::Id::IdentifierRef,
                                "Generic static method call: " + qualified_name,
                                debug::Level::Debug);
                return ast::make_ident(std::move(qualified_name), Span{start_pos, previous().end});
            } else if (found_close && check(TokenKind::LParen)) {
                // ジェネリック関数呼び出しパターン: size_of<WorkerArg>()
                // 位置を戻して型引数を正しくパース
                pos_ = saved_pos;
                advance();  // < を消費

                std::string type_args_str = "<";
                do {
                    size_t type_parse_pos = pos_;
                    auto type_arg = parse_type();
                    // parse_type()がトークンを消費しなかった場合はスタック防止
                    if (pos_ == type_parse_pos) {
                        if (!is_at_end() && !check(TokenKind::Gt))
                            advance();
                        break;
                    }
                    type_args_str += ast::type_to_string(*type_arg);
                    if (consume_if(TokenKind::Comma)) {
                        type_args_str += ", ";
                    }
                } while (!check(TokenKind::Gt) && !is_at_end());
                expect(TokenKind::Gt);
                type_args_str += ">";

                // ジェネリック関数名を構築: size_of<WorkerArg>
                std::string generic_name = name + type_args_str;

                debug::par::log(debug::par::Id::IdentifierRef,
                                "Generic function call: " + generic_name, debug::Level::Debug);
                return ast::make_ident(std::move(generic_name), Span{start_pos, previous().end});
            } else if (found_close && check(TokenKind::LBrace)) {
                // ジェネリック構造体構築式: Box<int>{v: 7}（局所処理調査「その他」）。
                // 型引数を消費して名前へ含め、後置がstructリテラルとして {...} を処理する。
                // Ident<バランスした型引数>{ は比較式にならない（a < b > {...} は不正）ため曖昧性はない
                pos_ = saved_pos;
                advance();  // < を消費

                std::string type_args_str = "<";
                do {
                    size_t type_parse_pos = pos_;
                    auto type_arg = parse_type();
                    if (pos_ == type_parse_pos) {
                        if (!is_at_end() && !check(TokenKind::Gt))
                            advance();
                        break;
                    }
                    type_args_str += ast::type_to_string(*type_arg);
                    if (consume_if(TokenKind::Comma)) {
                        type_args_str += ", ";
                    }
                } while (!check(TokenKind::Gt) && !is_at_end());
                expect(TokenKind::Gt);
                type_args_str += ">";

                std::string generic_name = name + type_args_str;
                debug::par::log(debug::par::Id::IdentifierRef,
                                "Generic struct literal: " + generic_name, debug::Level::Debug);
                return ast::make_ident(std::move(generic_name), Span{start_pos, previous().end});
            } else {
                // パターンに合致しない場合は位置を戻して通常の識別子として処理
                pos_ = saved_pos;
            }
        }

        // 名前空間またはenum値アクセス: A::B または A::B::C::...
        // 複数レベルの::をサポート
        if (consume_if(TokenKind::ColonColon)) {
            std::string qualified_name = name;
            do {
                std::string member = expect_ident();
                qualified_name += "::" + member;
                debug::par::log(debug::par::Id::IdentifierRef,
                                "Building qualified name: " + qualified_name, debug::Level::Debug);
            } while (consume_if(TokenKind::ColonColon));

            debug::par::log(debug::par::Id::IdentifierRef,
                            "Final qualified name: " + qualified_name, debug::Level::Debug);
            return ast::make_ident(std::move(qualified_name), Span{start_pos, previous().end});
        }

        debug::par::log(debug::par::Id::VariableDetected, "Variable/Function reference: " + name,
                        debug::Level::Debug);
        return ast::make_ident(std::move(name), Span{start_pos, previous().end});
    }

    // 配列リテラル: [elem1, elem2, ...]
    if (consume_if(TokenKind::LBracket)) {
        debug::par::log(debug::par::Id::PrimaryExpr, "Found array literal", debug::Level::Debug);
        std::vector<ast::ExprPtr> elements;

        if (!check(TokenKind::RBracket)) {
            do {
                elements.push_back(parse_expr());
            } while (consume_if(TokenKind::Comma));
        }

        expect(TokenKind::RBracket);
        debug::par::log(
            debug::par::Id::PrimaryExpr,
            "Created array literal with " + std::to_string(elements.size()) + " elements",
            debug::Level::Debug);
        return ast::make_array_literal(std::move(elements), Span{start_pos, previous().end});
    }

    // {expr, ...} / {N{expr}} / {field: val, ...} (SVプラットフォーム限定)
    // 3パターンの判別:
    //   (1) {ident: expr, ...} → 構造体リテラル (colonあり)
    //   (2) {N{expr}} → 複製 (intリテラル + LBrace)
    //   (3) {expr, expr, ...} → 連接 (カンマ区切りの式)
    if (is_sv_platform_ && check(TokenKind::LBrace)) {
        // 先読みで構造体リテラルかどうかを判別
        auto saved_pos = pos_;
        advance();  // { を消費

        // 連接式をパースするヘルパー
        auto parse_concat_expr = [&]() -> ast::ExprPtr {
            std::vector<ast::ExprPtr> elements;
            elements.push_back(parse_expr());
            while (consume_if(TokenKind::Comma)) {
                elements.push_back(parse_expr());
            }
            expect(TokenKind::RBrace);
            auto callee = ast::make_ident("__builtin_concat", Span{start_pos, start_pos});
            return ast::make_call(std::move(callee), std::move(elements),
                                  Span{start_pos, previous().end});
        };

        // 空の {} は空の連接として解釈
        if (check(TokenKind::RBrace)) {
            advance();  // } を消費
            // __builtin_concat() を引数なしで呼び出し（空の連接）
            auto callee = ast::make_ident("__builtin_concat", Span{start_pos, start_pos});
            std::vector<ast::ExprPtr> elements;
            return ast::make_call(std::move(callee), std::move(elements),
                                  Span{start_pos, previous().end});
        }
        // パターン2: {N{expr}} → 複製式
        else if (check(TokenKind::IntLiteral)) {
            auto int_pos = pos_;
            int64_t count = current().get_int();
            advance();  // intリテラルを消費
            if (check(TokenKind::LBrace)) {
                advance();  // 内側の { を消費
                auto inner_expr = parse_expr();
                expect(TokenKind::RBrace);  // 内側の }
                expect(TokenKind::RBrace);  // 外側の }
                // __builtin_replicate(count, expr) として表現
                auto callee = ast::make_ident("__builtin_replicate", Span{start_pos, start_pos});
                std::vector<ast::ExprPtr> args;
                args.push_back(ast::make_int_literal(count, Span{start_pos, start_pos}));
                args.push_back(std::move(inner_expr));
                return ast::make_call(std::move(callee), std::move(args),
                                      Span{start_pos, previous().end});
            }
            // intリテラルの後にLBraceがない → 連接として解析
            pos_ = int_pos;
            return parse_concat_expr();
        }
        // パターン1: {ident: ...} → 構造体リテラル
        else if (check(TokenKind::Ident)) {
            auto ident_pos = pos_;
            advance();  // ident を消費
            if (check(TokenKind::Colon)) {
                // 構造体リテラル確定
                pos_ = saved_pos;
                advance();  // { を再消費
                debug::par::log(debug::par::Id::PrimaryExpr, "Found implicit struct literal",
                                debug::Level::Debug);
                std::vector<ast::StructLiteralField> fields;

                if (!check(TokenKind::RBrace)) {
                    do {
                        if (!check(TokenKind::Ident)) {
                            error(i18n::msg(i18n::MsgId::PsExpectedFieldNameStructLiteral));
                        }

                        std::string field_name(current().get_string());
                        advance();

                        if (!check(TokenKind::Colon)) {
                            error(i18n::msgf(i18n::MsgId::PsExpectedFieldNameStructLiteral2,
                                             field_name));
                        }
                        advance();

                        auto value = parse_expr();
                        fields.emplace_back(std::move(field_name), std::move(value));
                    } while (consume_if(TokenKind::Comma));
                }

                expect(TokenKind::RBrace);
                return ast::make_struct_literal("", std::move(fields),
                                                Span{start_pos, previous().end});
            }
            // ident の後に : がない → 連接として解析
            pos_ = ident_pos;
            return parse_concat_expr();
        }
        // パターン3: {expr, expr, ...} → 連接式
        else {
            return parse_concat_expr();
        }
    }

    // 非SVプラットフォーム: 暗黙的構造体リテラル {field: val, ...}
    // SVプラットフォームでは上のブロックで処理済み
    if (!is_sv_platform_ && check(TokenKind::LBrace)) {
        // 先読みで構造体リテラルかどうかを判別
        auto saved_pos = pos_;
        advance();  // { を消費

        // {ident: ...} パターン → 構造体リテラル
        if (check(TokenKind::Ident)) {
            advance();  // ident を消費
            if (check(TokenKind::Colon)) {
                // 構造体リテラル確定
                pos_ = saved_pos;
                advance();  // { を再消費
                debug::par::log(debug::par::Id::PrimaryExpr, "Found implicit struct literal",
                                debug::Level::Debug);
                std::vector<ast::StructLiteralField> fields;

                if (!check(TokenKind::RBrace)) {
                    do {
                        if (!check(TokenKind::Ident)) {
                            error(i18n::msg(i18n::MsgId::PsExpectedFieldNameStructLiteral));
                        }

                        std::string field_name(current().get_string());
                        advance();

                        if (!check(TokenKind::Colon)) {
                            error(i18n::msgf(i18n::MsgId::PsExpectedFieldNameStructLiteral2,
                                             field_name));
                        }
                        advance();

                        auto value = parse_expr();
                        fields.emplace_back(std::move(field_name), std::move(value));
                    } while (consume_if(TokenKind::Comma));
                }

                expect(TokenKind::RBrace);
                return ast::make_struct_literal("", std::move(fields),
                                                Span{start_pos, previous().end});
            }
            // ident の後に : がない → 構造体リテラルではない
            pos_ = saved_pos;
        } else {
            // ident でもない → 構造体リテラルではない
            pos_ = saved_pos;
        }
    }

    // 括弧式またはラムダ式
    if (consume_if(TokenKind::LParen)) {
        debug::par::log(debug::par::Id::ParenExpr, "Found parenthesized expression or lambda",
                        debug::Level::Trace);

        // 空の括弧の場合、() => ... のラムダかもしれない
        if (check(TokenKind::RParen)) {
            advance();  // )を消費
            if (check(TokenKind::Arrow)) {
                // () => ... ラムダ式
                advance();  // => を消費
                return parse_lambda_body({}, start_pos);
            }
            // ()だけの場合はエラー
            error(i18n::msg(i18n::MsgId::PsEmptyParenthesesWithoutLambdaBody));
            return ast::make_null_literal();
        }

        // ラムダ式のパラメータ: (int x) または (int x, int y)
        // 通常の括弧式: (expr)

        // 先読みのためにトークン位置を保存
        size_t saved_pos = pos_;
        size_t saved_diag_count = diagnostics_.size();
        std::vector<ast::Param> potential_params;
        bool could_be_lambda = true;

        // ラムダの先読み: 最初のトークンが型キーワードかどうか
        // 型キーワード: int, bool, float, etc. または識別子（ユーザー定義型）
        // ただし、true/false/null などのリテラルキーワードは除外
        auto is_type_start = [](TokenKind kind) {
            switch (kind) {
                case TokenKind::KwVoid:
                case TokenKind::KwBool:
                case TokenKind::KwTiny:
                case TokenKind::KwShort:
                case TokenKind::KwInt:
                case TokenKind::KwLong:
                case TokenKind::KwUtiny:
                case TokenKind::KwUshort:
                case TokenKind::KwUint:
                case TokenKind::KwUlong:
                case TokenKind::KwFloat:
                case TokenKind::KwDouble:
                case TokenKind::KwChar:
                case TokenKind::KwString:
                case TokenKind::Ident:
                case TokenKind::Star:      // *Type (ポインタ)
                case TokenKind::Amp:       // &Type (参照)
                case TokenKind::LBracket:  // [Type] (配列)
                    return true;
                default:
                    return false;
            }
        };

        // 最初のトークンが型の開始でなければ通常の括弧式
        if (!is_type_start(current().kind)) {
            could_be_lambda = false;
        }

        // パラメータリストとして解析を試みる
        while (could_be_lambda) {
            // 型をパース
            auto param_type = parse_type();
            if (!param_type || param_type->kind == ast::TypeKind::Error) {
                could_be_lambda = false;
                break;
            }

            // パラメータ名
            if (!check(TokenKind::Ident)) {
                could_be_lambda = false;
                break;
            }

            ast::Param param;
            param.type = param_type;
            param.name = std::string(current().get_string());
            advance();

            potential_params.push_back(std::move(param));

            if (check(TokenKind::RParen)) {
                advance();  // )を消費
                break;
            }
            if (!consume_if(TokenKind::Comma)) {
                could_be_lambda = false;
                break;
            }
        }

        // => があればラムダ式
        if (could_be_lambda && check(TokenKind::Arrow)) {
            advance();  // => を消費

            return parse_lambda_body(std::move(potential_params), start_pos);
        }

        // ラムダではないので、位置を戻して通常の括弧式として処理
        // パース中に追加されたエラーも削除
        pos_ = saved_pos;
        while (diagnostics_.size() > saved_diag_count) {
            diagnostics_.pop_back();
        }
        auto expr = parse_expr();
        // (a, b) のようなカンマ区切りはタプル/分解代入として誤解されやすいため専用診断で誘導する
        if (check(TokenKind::Comma)) {
            error(i18n::msg(i18n::MsgId::PsTupleUnsupported));
        }
        expect(TokenKind::RParen);
        debug::par::log(debug::par::Id::ParenClose, "Closed parenthesized expression",
                        debug::Level::Trace);
        return expr;
    }

    // BUG修正(v0.14.2): 型キーワード + :: パターンをnamespace修飾子として処理
    // "string::strlen(s)" のような呼び出しを正しくパースする
    // 型キーワードが :: の前に来た場合、型ではなくnamespace名として扱う
    {
        auto kind = current().kind;
        bool is_type_keyword =
            (kind == TokenKind::KwString || kind == TokenKind::KwInt || kind == TokenKind::KwUint ||
             kind == TokenKind::KwLong || kind == TokenKind::KwUlong ||
             kind == TokenKind::KwShort || kind == TokenKind::KwUshort ||
             kind == TokenKind::KwTiny || kind == TokenKind::KwUtiny ||
             kind == TokenKind::KwFloat || kind == TokenKind::KwDouble ||
             kind == TokenKind::KwUfloat || kind == TokenKind::KwUdouble ||
             kind == TokenKind::KwBool || kind == TokenKind::KwChar || kind == TokenKind::KwVoid ||
             kind == TokenKind::KwIsize || kind == TokenKind::KwUsize ||
             kind == TokenKind::KwCstring);

        if (is_type_keyword && pos_ + 1 < tokens_.size() &&
            tokens_[pos_ + 1].kind == TokenKind::ColonColon) {
            // 型キーワードをnamespace名として取得
            // BUG修正: キーワードトークンはget_string()が空文字を返すため
            // token_kind_to_string()を使用してキーワード名を取得
            std::string name(token_kind_to_string(kind));
            advance();  // 型キーワードを消費

            // :: で修飾されたパスを構築（Identと同じロジック）
            if (consume_if(TokenKind::ColonColon)) {
                std::string qualified_name = name;
                do {
                    std::string member = expect_ident();
                    qualified_name += "::" + member;
                } while (consume_if(TokenKind::ColonColon));

                return ast::make_ident(std::move(qualified_name), Span{start_pos, previous().end});
            }

            return ast::make_ident(std::move(name), Span{start_pos, previous().end});
        }
    }

    std::string error_msg = "Expected expression but found: ";
    error_msg += token_kind_to_string(current().kind);
    debug::par::log(debug::par::Id::ExprError, error_msg, debug::Level::Error);
    error(i18n::msg(i18n::MsgId::PsExpectedExpression));
    return ast::make_null_literal();
}

// ラムダ式本体の解析
// (params) => expr または (params) => { stmts }
ast::ExprPtr Parser::parse_lambda_body(std::vector<ast::Param> params, uint32_t start_pos) {
    debug::par::log(debug::par::Id::PrimaryExpr, "Parsing lambda body", debug::Level::Debug);

    auto lambda = std::make_unique<ast::LambdaExpr>();
    lambda->params = std::move(params);
    lambda->return_type = nullptr;  // 型は推論

    if (check(TokenKind::LBrace)) {
        // ブロック本体
        auto block = parse_block();
        lambda->body = std::move(block);
    } else {
        // 式本体
        auto expr = parse_expr();
        lambda->body = std::move(expr);
    }

    debug::par::log(debug::par::Id::PrimaryExpr, "Lambda expression parsed", debug::Level::Debug);
    return std::make_unique<ast::Expr>(std::move(lambda), Span{start_pos, previous().end});
}

// sizeof被演算子が識別子始まりのとき、型パスが閉じ括弧に達すれば型・途中に式演算子が現れれば式と非破壊で判定する（局所処理調査A2）。
// sizeof(Point)/sizeof(Point*)/sizeof(Point[2])/sizeof(Vec<int>) は型、sizeof(p.x)/sizeof(f())/sizeof(x+y) は式に分かれる。
// 識別子始まりの添字 sizeof(a[0]) は型パスとして受理し、変数被添字かどうかは型チェッカで救済する（要素型サイズを返す）
bool Parser::sizeof_operand_ident_is_type() const {
    size_t i = pos_;
    if (i >= tokens_.size() || tokens_[i].kind != TokenKind::Ident) {
        return false;
    }
    ++i;  // 先頭識別子
    // 名前空間修飾 ns::Type
    while (i + 1 < tokens_.size() && tokens_[i].kind == TokenKind::ColonColon &&
           tokens_[i + 1].kind == TokenKind::Ident) {
        i += 2;
    }
    // ジェネリック型引数 <...>（バランスが取れる場合のみ型パスに含める。閉じなければ < は比較演算子とみなす）
    if (i < tokens_.size() && tokens_[i].kind == TokenKind::Lt) {
        size_t j = i + 1;
        int depth = 1;
        while (j < tokens_.size() && depth > 0) {
            const TokenKind k = tokens_[j].kind;
            if (k == TokenKind::Lt) {
                ++depth;
            } else if (k == TokenKind::Gt) {
                --depth;
            } else if (k == TokenKind::GtGt) {
                depth -= 2;
            } else if (k == TokenKind::Semicolon || k == TokenKind::LBrace ||
                       k == TokenKind::RBrace || k == TokenKind::Eof) {
                break;  // 型引数内に出現しないトークン → 比較演算子
            }
            ++j;
        }
        if (depth <= 0) {
            i = j;  // <...> を消費
        }
    }
    // ポインタ/参照サフィックス連鎖 * &
    while (i < tokens_.size() &&
           (tokens_[i].kind == TokenKind::Star || tokens_[i].kind == TokenKind::Amp)) {
        ++i;
    }
    // 配列サフィックス連鎖 [...]（バランス）
    while (i < tokens_.size() && tokens_[i].kind == TokenKind::LBracket) {
        size_t j = i + 1;
        int depth = 1;
        while (j < tokens_.size() && depth > 0) {
            const TokenKind k = tokens_[j].kind;
            if (k == TokenKind::LBracket) {
                ++depth;
            } else if (k == TokenKind::RBracket) {
                --depth;
            } else if (k == TokenKind::Eof) {
                break;
            }
            ++j;
        }
        if (depth != 0) {
            break;
        }
        i = j;
    }
    return i < tokens_.size() && tokens_[i].kind == TokenKind::RParen;
}

// sizeof/__sizeof__ の被演算子（'(' 消費済み）を型/式のいずれかで解析しノードを返す（局所処理調査A2: 両組込の重複switchを一本化）
ast::ExprPtr Parser::parse_sizeof_operand(uint32_t start_pos) {
    bool could_be_type = false;
    switch (current().kind) {
        case TokenKind::KwAuto:
        case TokenKind::KwVoid:
        case TokenKind::KwBool:
        case TokenKind::KwTiny:
        case TokenKind::KwShort:
        case TokenKind::KwInt:
        case TokenKind::KwLong:
        case TokenKind::KwUtiny:
        case TokenKind::KwUshort:
        case TokenKind::KwUint:
        case TokenKind::KwUlong:
        case TokenKind::KwIsize:
        case TokenKind::KwUsize:
        case TokenKind::KwFloat:
        case TokenKind::KwDouble:
        case TokenKind::KwUfloat:
        case TokenKind::KwUdouble:
        case TokenKind::KwChar:
        case TokenKind::KwString:
        case TokenKind::KwCstring:
        case TokenKind::
            KwTypeof:  // sizeof(typeof(x)) は typeof を型として解析する（被演算式を保持し型チェッカで解決。局所処理調査B系）
        case TokenKind::Star:
        case TokenKind::Amp:
        case TokenKind::LBracket:
            could_be_type = true;
            break;
        case TokenKind::Ident:
            // 識別子始まりは型パスが閉じ括弧に達する場合のみ型として解析する（. ( 二項演算子 等が続けば式）
            could_be_type = sizeof_operand_ident_is_type();
            break;
        default:
            break;
    }

    if (could_be_type) {
        auto type = parse_type();
        type = check_array_suffix(std::move(type));  // T*, T[N] などをサポート
        expect(TokenKind::RParen);
        return ast::make_sizeof(std::move(type), Span{start_pos, previous().end});
    }
    auto expr = parse_expr();
    expect(TokenKind::RParen);
    return ast::make_sizeof_expr(std::move(expr), Span{start_pos, previous().end});
}

}  // namespace cm
