// ============================================================
// モジュール関連パーサ - トップレベル宣言（const・グローバル変数・enum・typedef・extern・initial）の解析
// ============================================================

#include "internal/base/debug/par.hpp"
#include "internal/base/i18n.hpp"
#include "internal/syntax/ast/module.hpp"
#include "internal/syntax/parser/parser.hpp"

#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cm {

// ============================================================
// 定数宣言（export const用）
// ============================================================
ast::DeclPtr Parser::parse_const_decl(bool is_export, std::vector<ast::AttributeNode> attributes) {
    uint32_t start_pos = current().start;
    expect(TokenKind::KwConst);

    // 型
    auto type = parse_type_with_union();

    // 変数名
    std::string name = expect_ident();
    reject_reserved_ident(name);

    // 初期化子
    expect(TokenKind::Eq);
    auto init = parse_expr();

    expect(TokenKind::Semicolon);

    // v4: GlobalVarDeclを使用してconst宣言を表現
    auto global_var = std::make_unique<ast::GlobalVarDecl>(
        std::move(name), std::move(type), std::move(init), true  // is_const = true
    );
    global_var->visibility = is_export ? ast::Visibility::Export : ast::Visibility::Private;
    global_var->attributes = std::move(attributes);

    return std::make_unique<ast::Decl>(std::move(global_var), Span{start_pos, previous().end});
}

// ============================================================
// グローバル変数宣言（トップレベル: TYPE NAME = EXPR;）
// ============================================================
ast::DeclPtr Parser::parse_global_var_decl(bool is_export,
                                           std::vector<ast::AttributeNode> attributes) {
    uint32_t start_pos = current().start;

    // 型
    auto type = parse_type_with_union();

    // 変数名
    std::string name = expect_ident();
    reject_reserved_ident(name);

    // 初期化子省略をSVポート型/アトリビュートに限定
    // posedge/negedge型、または#[input]/#[output]属性付きはport宣言のため初期化子不要
    bool is_sv_port = false;
    if (type && (type->name == "posedge" || type->name == "negedge")) {
        is_sv_port = true;
    }
    for (const auto& attr : attributes) {
        if (attr.name == "input" || attr.name == "output" || attr.name == "inout") {
            is_sv_port = true;
            break;
        }
    }

    ast::ExprPtr init;
    if (consume_if(TokenKind::Eq)) {
        init = parse_expr();
    } else if (!is_sv_port && !(is_sv_platform_ && check(TokenKind::Semicolon))) {
        // 非SVポートでは初期化子を必須とする
        // ただしSVプラットフォームでは初期値なし宣言を許可（extern struct インスタンス等）
        error(i18n::msg(i18n::MsgId::PsExpectedGlobalVariableInitializer));
    }

    expect(TokenKind::Semicolon);

    // GlobalVarDeclとして表現（is_const = false）
    auto global_var = std::make_unique<ast::GlobalVarDecl>(
        std::move(name), std::move(type), std::move(init), false  // is_const = false
    );
    global_var->visibility = is_export ? ast::Visibility::Export : ast::Visibility::Private;
    global_var->attributes = std::move(attributes);

    return std::make_unique<ast::Decl>(std::move(global_var), Span{start_pos, previous().end});
}

// ============================================================
// constexpr宣言
// ============================================================
ast::DeclPtr Parser::parse_constexpr() {
    expect(TokenKind::KwConstexpr);

    // constexpr変数またはconstexpr関数
    auto type = parse_type_with_union();
    std::string name = expect_ident();
    reject_reserved_ident(name);

    if (check(TokenKind::LParen)) {
        // constexpr関数
        expect(TokenKind::LParen);
        auto params = parse_params();
        expect(TokenKind::RParen);
        auto body = parse_block();

        auto func = std::make_unique<ast::FunctionDecl>(std::move(name), std::move(params),
                                                        std::move(type), std::move(body));
        // R11: コンパイル時評価は未実装のため、フラグを立てて通常関数として受理しcheckerが警告を出す
        func->is_constexpr = true;

        return std::make_unique<ast::Decl>(std::move(func));
    } else {
        // constexpr変数
        expect(TokenKind::Eq);
        auto init = parse_expr();
        expect(TokenKind::Semicolon);

        // R11: constexpr変数は未実装。従来はnullptrを返して後続が無関係な構文エラーに化けていたため、専用診断を出しつつconst宣言として回復する
        error(i18n::msg(i18n::MsgId::PsConstexprVarUnsupported));
        auto global_var = std::make_unique<ast::GlobalVarDecl>(std::move(name), std::move(type),
                                                               std::move(init), true);
        return std::make_unique<ast::Decl>(std::move(global_var));
    }
}

// ============================================================
// テンプレート宣言
// ============================================================
ast::DeclPtr Parser::parse_template_decl() {
    expect(TokenKind::KwTemplate);

    // テンプレートパラメータ
    expect(TokenKind::Lt);
    std::vector<std::string> template_params;

    do {
        if (consume_if(TokenKind::KwTypename)) {
            template_params.push_back(expect_ident());
        } else {
            // その他のテンプレートパラメータ型
            auto type = parse_type();
            template_params.push_back(expect_ident());
        }
    } while (consume_if(TokenKind::Comma));

    expect(TokenKind::Gt);

    // テンプレート化される宣言
    // TODO: テンプレート対応の実装

    return nullptr;  // 一時的な実装
}

// ============================================================
// Enum宣言（Tagged Union & ジェネリック対応）
// ============================================================
ast::DeclPtr Parser::parse_enum_decl(bool is_export, std::vector<ast::AttributeNode> attributes,
                                     bool allow_anonymous) {
    uint32_t start_pos = current().start;
    expect(TokenKind::KwEnum);

    // C/C++スタイルの匿名enum（enum { ... } 宣言子;）は名前を省略でき、呼び出し元が宣言子から名前を合成する
    std::string name;
    if (!allow_anonymous || check(TokenKind::Ident)) {
        name = expect_ident();
        reject_reserved_ident(name);
    }

    // ジェネリックパラメータ: enum Result<T, E> { ... }
    std::vector<std::string> generic_params;
    if (consume_if(TokenKind::Lt)) {
        do {
            generic_params.push_back(expect_ident());
        } while (consume_if(TokenKind::Comma));
        expect(TokenKind::Gt);
    }

    expect(TokenKind::LBrace);

    std::vector<ast::EnumMember> members;
    std::vector<ast::DeclPtr> nested_types;
    int64_t next_value = 0;                   // オートインクリメント用
    std::unordered_set<int64_t> used_values;  // 重複チェック用
    bool has_associated_data = false;         // Associated dataがあればtrueになる

    while (!check(TokenKind::RBrace) && !is_at_end()) {
        // ネスト型宣言（enum/struct）: 値スロットを消費せず、hoistパスでOuter::Inner名へ平坦化される
        if (check(TokenKind::KwEnum) || check(TokenKind::KwStruct)) {
            auto nested = parse_nested_type_decl(is_export, {}, !generic_params.empty(), false);
            if (nested) {
                nested_types.push_back(std::move(nested));
            }
            consume_if(TokenKind::Comma);
            continue;
        }

        std::string member_name = expect_ident();
        reject_reserved_ident(member_name);

        // Associated dataをチェック: Variant(int x, string y)
        if (consume_if(TokenKind::LParen)) {
            has_associated_data = true;
            std::vector<std::pair<std::string, ast::TypePtr>> fields;

            if (!check(TokenKind::RParen)) {
                do {
                    // 型をパース
                    auto field_type = parse_type();
                    // フィールド名（オプション: 型のみの場合もあり）
                    std::string field_name;
                    if (check(TokenKind::Ident)) {
                        field_name = current_text();
                        advance();
                    } else {
                        // 名前がない場合は _0, _1, ... を使用
                        field_name = "_" + std::to_string(fields.size());
                    }
                    fields.emplace_back(std::move(field_name), std::move(field_type));
                } while (consume_if(TokenKind::Comma));
            }

            expect(TokenKind::RParen);
            members.emplace_back(std::move(member_name), std::move(fields));
        }
        // 明示的な値指定: Variant = 42
        else if (consume_if(TokenKind::Eq)) {
            // 負の数をサポート
            bool is_negative = consume_if(TokenKind::Minus);

            int64_t value = 0;
            if (check(TokenKind::IntLiteral)) {
                value = static_cast<int64_t>(current().get_int());
                advance();
            } else if (check(TokenKind::CharLiteral)) {
                // 文字リテラル: 'a' → 97 (ASCII値)
                std::string s(current().get_string());
                value = static_cast<int64_t>(s.empty() ? 0 : static_cast<unsigned char>(s[0]));
                advance();
            } else {
                error(i18n::msg(i18n::MsgId::ParseEnumValuesRequireAnInteger));
                return nullptr;
            }

            if (is_negative) {
                value = -value;
            }

            // 重複チェック（Associated dataがない場合のみ）
            if (!has_associated_data && used_values.count(value)) {
                error(i18n::msgf(i18n::MsgId::ParseEnumValueIsAlreadyUsed, std::to_string(value)));
                return nullptr;
            }
            used_values.insert(value);

            members.emplace_back(std::move(member_name), value);
            next_value = value + 1;
        }
        // シンプルなバリアント: Variant
        else {
            if (!has_associated_data) {
                // シンプルなenumの場合はオートインクリメント
                if (used_values.count(next_value)) {
                    error(i18n::msgf(i18n::MsgId::ParseEnumValueIsAlreadyUsed,
                                     std::to_string(next_value)));
                    return nullptr;
                }
                used_values.insert(next_value);
                members.emplace_back(std::move(member_name), next_value);
                next_value++;
            } else {
                // Tagged Unionでデータなしのバリアント
                members.emplace_back(std::move(member_name),
                                     std::vector<std::pair<std::string, ast::TypePtr>>{});
            }
        }

        // カンマは省略可能（最後の要素の後も許可）
        consume_if(TokenKind::Comma);
    }

    expect(TokenKind::RBrace);

    // #[derive] は enum 未対応（将来拡張の余地として明示エラー）
    for (const auto& attr : attributes) {
        if (attr.name == "derive") {
            error(i18n::msg(i18n::MsgId::PsDeriveNotSupportedEnumsYet));
        }
    }

    auto enum_decl = std::make_unique<ast::EnumDecl>(std::move(name), std::move(members));
    enum_decl->visibility = is_export ? ast::Visibility::Export : ast::Visibility::Private;
    enum_decl->attributes = std::move(attributes);
    enum_decl->generic_params = std::move(generic_params);
    enum_decl->nested_types = std::move(nested_types);
    return std::make_unique<ast::Decl>(std::move(enum_decl), Span{start_pos, previous().end});
}

// ============================================================
// typedef宣言
// typedef T = Type1 | Type2 | ...;
// typedef T = "literal1" | "literal2" | ...;
// ============================================================
ast::DeclPtr Parser::parse_typedef_decl(bool is_export,
                                        std::vector<ast::AttributeNode> attributes) {
    uint32_t start_pos = current().start;
    expect(TokenKind::KwTypedef);

    std::string name = expect_ident();
    reject_reserved_ident(name);
    expect(TokenKind::Eq);

    // ユニオン型 or リテラル型をパース
    // 最初の要素を見て判断

    // リテラル型かどうかをチェック
    bool is_literal_type = check(TokenKind::StringLiteral) || check(TokenKind::IntLiteral) ||
                           check(TokenKind::FloatLiteral);

    if (is_literal_type) {
        // リテラル型: typedef T = "a" | "b" | 1 | 2;
        std::vector<ast::LiteralType> literals;

        do {
            if (check(TokenKind::StringLiteral)) {
                literals.emplace_back(std::string(current().get_string()));
                advance();
            } else if (check(TokenKind::IntLiteral)) {
                literals.emplace_back(static_cast<int64_t>(current().get_int()));
                advance();
            } else if (check(TokenKind::FloatLiteral)) {
                literals.emplace_back(current().get_float());
                advance();
            } else {
                error(i18n::msg(i18n::MsgId::ParseLiteralTypesRequireAString));
                return nullptr;
            }
        } while (consume_if(TokenKind::Pipe));

        expect(TokenKind::Semicolon);

        auto lit_union = ast::make_literal_union(std::move(literals));
        auto typedef_decl =
            std::make_unique<ast::TypedefDecl>(std::move(name), std::move(lit_union));
        typedef_decl->visibility = is_export ? ast::Visibility::Export : ast::Visibility::Private;
        typedef_decl->attributes = std::move(attributes);
        return std::make_unique<ast::Decl>(std::move(typedef_decl),
                                           Span{start_pos, previous().end});
    } else {
        // ユニオン型: typedef T = Type1 | Type2;
        std::vector<ast::TypePtr> types;

        do {
            auto type = parse_type();
            if (!type) {
                error(i18n::msg(i18n::MsgId::ParseTypedefRequiresAValidType));
                return nullptr;
            }
            // C++スタイルの配列・ポインタサフィックスをチェック (T*, T[N])
            type = check_array_suffix(std::move(type));
            types.push_back(std::move(type));
        } while (consume_if(TokenKind::Pipe));

        expect(TokenKind::Semicolon);

        // 単一の型の場合はエイリアス、複数の場合はユニオン型
        ast::TypePtr result_type;
        if (types.size() == 1) {
            result_type = std::move(types[0]);
        } else {
            // ユニオン型を作成
            std::vector<ast::UnionVariant> variants;
            for (auto& t : types) {
                // 型名をタグとして使用
                std::string tag = ast::type_to_string(*t);
                ast::UnionVariant v(tag);
                v.fields.push_back(std::move(t));
                variants.push_back(std::move(v));
            }
            result_type = ast::make_union(std::move(variants));
        }

        auto typedef_decl =
            std::make_unique<ast::TypedefDecl>(std::move(name), std::move(result_type));
        typedef_decl->visibility = is_export ? ast::Visibility::Export : ast::Visibility::Private;
        typedef_decl->attributes = std::move(attributes);
        return std::make_unique<ast::Decl>(std::move(typedef_decl),
                                           Span{start_pos, previous().end});
    }
}

// ============================================================
// extern宣言
// ============================================================
ast::DeclPtr Parser::parse_extern(std::vector<ast::AttributeNode> attributes) {
    uint32_t start_pos = current().start;
    expect(TokenKind::KwExtern);

    // extern "C" { ... } or extern "C" function
    if (check(TokenKind::StringLiteral)) {
        std::string lang = std::string(current().get_string());
        advance();

        if (consume_if(TokenKind::LBrace)) {
            // extern "C" { ... } ブロック
            auto extern_block = std::make_unique<ast::ExternBlockDecl>(lang);
            while (!check(TokenKind::RBrace) && !is_at_end()) {
                if (auto func = parse_extern_func_decl()) {
                    extern_block->declarations.push_back(std::move(func));
                }
            }
            expect(TokenKind::RBrace);
            extern_block->attributes = std::move(attributes);

            return std::make_unique<ast::Decl>(std::move(extern_block),
                                               Span{start_pos, previous().end});
        } else {
            // 単一の extern "C" 宣言
            return parse_extern_decl(std::move(attributes));
        }
    }

    // extern struct (外部ハードウェアモジュール / FFI構造体)
    if (check(TokenKind::KwStruct)) {
        auto struct_decl = parse_struct(false, std::move(attributes), true);
        if (auto* s = struct_decl->as<ast::StructDecl>()) {
            s->is_extern = true;
        }
        return struct_decl;
    }

    // extern だけの場合（C++スタイル）
    return parse_extern_decl(std::move(attributes));
}

// extern宣言の個別解析（FunctionDecl版）
std::unique_ptr<ast::FunctionDecl> Parser::parse_extern_func_decl() {
    // 関数プロトタイプ - C言語スタイルの型をサポート
    auto return_type = parse_extern_type();
    std::string name = expect_ident();

    expect(TokenKind::LParen);
    auto params = parse_extern_params();
    expect(TokenKind::RParen);

    expect(TokenKind::Semicolon);

    // extern関数として作成（bodyなし）
    auto func = std::make_unique<ast::FunctionDecl>(std::move(name), std::move(params),
                                                    std::move(return_type),
                                                    std::vector<ast::StmtPtr>()  // 空のボディ
    );
    func->is_extern = true;

    return func;
}

// C言語スタイルの型をパース（後置ポインタ T* をサポート）
ast::TypePtr Parser::parse_extern_type() {
    // const修飾子をスキップ（C言語互換）
    bool is_const = consume_if(TokenKind::KwConst);
    (void)is_const;  // 現時点では使用しない

    // 基本型をパース
    ast::TypePtr base_type = parse_type();

    // 後置ポインタをチェック（C言語スタイル: char*, int* など）
    while (check(TokenKind::Star)) {
        advance();  // consume *
        base_type = ast::make_pointer(std::move(base_type));
    }

    // 後置配列型: T[]（スライス）/ T[N]（固定長）。FFI宣言で構造体配列等を受け渡すため
    while (check(TokenKind::LBracket)) {
        advance();  // consume [
        if (check(TokenKind::IntLiteral)) {
            uint32_t size = static_cast<uint32_t>(current().get_int());
            advance();
            base_type = ast::make_array(std::move(base_type), size);
        } else {
            // 要素数なし → 動的配列（スライス）
            base_type = ast::make_array(std::move(base_type));
        }
        if (!consume_if(TokenKind::RBracket)) {
            break;
        }
    }

    return base_type;
}

// extern関数用のパラメータパース
std::vector<ast::Param> Parser::parse_extern_params() {
    std::vector<ast::Param> params;

    if (check(TokenKind::RParen)) {
        return params;
    }

    do {
        ast::Param param;

        // const修飾子をスキップ
        param.qualifiers.is_const = consume_if(TokenKind::KwConst);

        // 型をパース（C言語スタイル）
        param.type = parse_extern_type();

        // パラメータ名（オプション）
        if (check(TokenKind::Ident)) {
            param.name = current_text();
            advance();
        }

        params.push_back(std::move(param));
    } while (consume_if(TokenKind::Comma));

    return params;
}

// extern宣言の個別解析（DeclPtr版）
ast::DeclPtr Parser::parse_extern_decl(std::vector<ast::AttributeNode> attributes) {
    // 呼び出し元（parse_extern）でexternキーワードは消費済み。宣言全体のSpanは現在位置から張る
    uint32_t start_pos = current().start;
    auto func = parse_extern_func_decl();
    if (func) {
        func->attributes = std::move(attributes);
    }
    return std::make_unique<ast::Decl>(std::move(func), Span{start_pos, previous().end});
}

// ============================================================
// SV initial ブロック
// ============================================================
ast::DeclPtr Parser::parse_initial_block(std::vector<ast::AttributeNode> attributes) {
    uint32_t start_pos = current().start;
    expect(TokenKind::KwInitial);
    expect(TokenKind::LBrace);

    std::vector<ast::StmtPtr> body;
    while (!check(TokenKind::RBrace) && !is_at_end()) {
        if (auto stmt = parse_stmt()) {
            body.push_back(std::move(stmt));
        }
    }

    expect(TokenKind::RBrace);

    auto decl = std::make_unique<ast::InitialBlockDecl>(std::move(body));
    decl->attributes = std::move(attributes);
    return std::make_unique<ast::Decl>(std::move(decl), Span{start_pos, previous().end});
}

}  // namespace cm
