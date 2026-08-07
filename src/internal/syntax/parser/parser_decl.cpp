#include "internal/base/debug/par.hpp"
#include "internal/base/i18n.hpp"
#include "parser.hpp"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cm {

// プログラム全体を解析
ast::Program Parser::parse() {
    debug::par::log(debug::par::Id::Start);

    // 字句エラー（不正エスケープ等）はメッセージ付きErrorトークンとして届く。
    // 診断へ変換してからトークン列から除去し、残りは通常どおりパースして後続の診断も収集する（R5）
    {
        std::vector<Token> rest;
        rest.reserve(tokens_.size());
        for (auto& tok : tokens_) {
            if (tok.kind == TokenKind::Error && std::holds_alternative<std::string>(tok.value)) {
                diagnostics_.emplace_back(DiagKind::Error, Span{tok.start, tok.end},
                                          std::get<std::string>(tok.value));
            } else {
                rest.push_back(std::move(tok));
            }
        }
        tokens_ = std::move(rest);
    }

    ast::Program program;
    // 宣言数の固定上限は設けない（M5）。無限ループは pos_ が進まないことの検出で防ぐ。
    // 機械生成コードのように宣言数が多くても、進捗がある限り最後まで解析する。
    bool is_first = true;
    size_t last_pos = pos_;

    while (!is_at_end()) {
        // エラーが蓄積しすぎた場合はコンパイルを中断
        if (diagnostics_.size() > 50) {
            error(i18n::msg(i18n::MsgId::ParseTooManyErrorsAbortingCompilation));
            break;
        }
        // 無限ループ検出（進捗が無い場合のみ強制的に進める）
        if (pos_ == last_pos && !is_first) {
            error(i18n::msg(i18n::MsgId::PsParserStuckNoProgressMade));
            if (!is_at_end()) {
                advance();  // 強制的に進める
            }
        }
        last_pos = pos_;
        is_first = false;

        if (auto decl = parse_top_level()) {
            program.declarations.push_back(std::move(decl));
        } else {
            synchronize();
        }
    }

    debug::par::log(debug::par::Id::End,
                    std::to_string(program.declarations.size()) + " declarations");
    return program;
}

// トップレベル宣言
ast::DeclPtr Parser::parse_top_level() {
    // アトリビュート（#[...]) を収集
    std::vector<ast::AttributeNode> attrs;
    while (is_attribute_start()) {
        attrs.push_back(parse_attribute());
    }

    // @[macro]の場合 (廃止予定 - #macroを使用してください)
    for (const auto& attr : attrs) {
        if (attr.name == "macro") {
            return nullptr;  // @[macro]はサポートしない
        }
    }

    // module宣言
    if (check(TokenKind::KwModule)) {
        return parse_module();
    }

    // namespace宣言
    if (check(TokenKind::KwNamespace)) {
        return parse_namespace();
    }

    // import
    if (check(TokenKind::KwImport)) {
        return parse_import_stmt(std::move(attrs));
    }

    // use
    if (check(TokenKind::KwUse)) {
        return parse_use(std::move(attrs));
    }

    // export (v4: 宣言時エクスポートと分離エクスポートの両方をサポート)
    if (check(TokenKind::KwExport)) {
        // 次のトークンを先読み
        auto saved_pos = pos_;
        advance();  // consume 'export'

        // export struct, export interface, export enum, export typedef, export const
        if (check(TokenKind::KwStruct)) {
            return parse_struct(true, std::move(attrs));
        }
        if (check(TokenKind::KwExtern)) {
            advance();  // consume 'extern'
            if (check(TokenKind::KwStruct)) {
                auto struct_decl = parse_struct(true, std::move(attrs), true);
                if (auto* s = struct_decl->as<ast::StructDecl>()) {
                    s->is_extern = true;
                }
                return struct_decl;
            }
            pos_ = saved_pos;
            return parse_export();
        }
        if (check(TokenKind::KwInterface)) {
            return parse_interface(true, std::move(attrs));
        }
        if (check(TokenKind::KwEnum)) {
            return parse_enum_decl(true, std::move(attrs));
        }
        if (check(TokenKind::KwTypedef)) {
            return parse_typedef_decl(true, std::move(attrs));
        }
        if (check(TokenKind::KwConst)) {
            return parse_const_decl(true, std::move(attrs));
        }
        if (check(TokenKind::KwImpl)) {
            return parse_impl_export(std::move(attrs));
        }
        // v0.13.0: export macro
        if (check(TokenKind::KwMacro)) {
            return parse_macro(true);
        }

        // export function (型・ジェネリックパラメータ・修飾子から始まる関数の場合)
        // 修飾子: static, inline, async, always, always_ff, always_comb, always_latch
        // <T: Eq> void assert_eq(...) のようなジェネリック関数のexportも受理する
        if (is_type_start() || check(TokenKind::Lt) || check(TokenKind::KwStatic) ||
            check(TokenKind::KwInline) || check(TokenKind::KwAsync) || check(TokenKind::KwAlways) ||
            check(TokenKind::KwAlwaysFF) || check(TokenKind::KwAlwaysComb) ||
            check(TokenKind::KwAlwaysLatch)) {
            // 修飾子を収集
            bool is_static = consume_if(TokenKind::KwStatic);
            bool is_inline = consume_if(TokenKind::KwInline);
            bool is_async = consume_if(TokenKind::KwAsync);
            bool is_always = consume_if(TokenKind::KwAlways);
            // always_ff/always_comb/always_latch の明示指定
            auto ak = ast::FunctionDecl::AlwaysKind::None;
            if (is_always) {
                ak = ast::FunctionDecl::AlwaysKind::Auto;
            } else if (consume_if(TokenKind::KwAlwaysFF)) {
                is_always = true;
                ak = ast::FunctionDecl::AlwaysKind::FF;
            } else if (consume_if(TokenKind::KwAlwaysComb)) {
                is_always = true;
                ak = ast::FunctionDecl::AlwaysKind::Comb;
            } else if (consume_if(TokenKind::KwAlwaysLatch)) {
                is_always = true;
                ak = ast::FunctionDecl::AlwaysKind::Latch;
            }

            // グローバル変数判定（型 名前 = ... のパターン）
            if (!is_static && !is_inline && !is_async && !is_always && is_global_var_start()) {
                return parse_global_var_decl(true, std::move(attrs));
            }

            auto func_decl = parse_function(true, is_static, is_inline, std::move(attrs), is_async);
            if (is_always) {
                if (auto* f = func_decl->as<ast::FunctionDecl>()) {
                    f->is_always = true;
                    f->always_kind = ak;
                }
            }
            return func_decl;
        }

        // それ以外は分離エクスポート (export NAME1, NAME2;)
        if (!attrs.empty()) {
            error(i18n::msg(i18n::MsgId::PsAttributesNotSupportedExportLists));
        }
        pos_ = saved_pos;
        return parse_export();
    }

    // extern
    if (check(TokenKind::KwExtern)) {
        return parse_extern(std::move(attrs));
    }

    // 修飾子を収集
    bool is_static = consume_if(TokenKind::KwStatic);
    bool is_inline = consume_if(TokenKind::KwInline);
    bool is_async = consume_if(TokenKind::KwAsync);
    bool is_always = consume_if(TokenKind::KwAlways);
    // always_ff/always_comb/always_latch の明示指定
    auto ak = ast::FunctionDecl::AlwaysKind::None;
    if (is_always) {
        ak = ast::FunctionDecl::AlwaysKind::Auto;
    } else if (consume_if(TokenKind::KwAlwaysFF)) {
        is_always = true;
        ak = ast::FunctionDecl::AlwaysKind::FF;
    } else if (consume_if(TokenKind::KwAlwaysComb)) {
        is_always = true;
        ak = ast::FunctionDecl::AlwaysKind::Comb;
    } else if (consume_if(TokenKind::KwAlwaysLatch)) {
        is_always = true;
        ak = ast::FunctionDecl::AlwaysKind::Latch;
    }

    // SV assign文: assign type name = expr;
    if (consume_if(TokenKind::KwAssign)) {
        auto gv = parse_global_var_decl(false, std::move(attrs));
        if (auto* g = gv->as<ast::GlobalVarDecl>()) {
            g->is_assign = true;
        }
        return gv;
    }

    // SV initial ブロック: initial { ... }
    if (check(TokenKind::KwInitial)) {
        return parse_initial_block(std::move(attrs));
    }

    // struct
    if (check(TokenKind::KwStruct)) {
        return parse_struct(false, std::move(attrs));
    }

    // interface
    if (check(TokenKind::KwInterface)) {
        return parse_interface(false, std::move(attrs));
    }

    // impl
    if (check(TokenKind::KwImpl)) {
        return parse_impl(std::move(attrs));
    }

    // template
    if (check(TokenKind::KwTemplate)) {
        return parse_template_decl();
    }

    // enum
    if (check(TokenKind::KwEnum)) {
        return parse_enum_decl(false, std::move(attrs));
    }

    // typedef
    if (check(TokenKind::KwTypedef)) {
        return parse_typedef_decl(false, std::move(attrs));
    }

    // const (v4: トップレベルconst宣言のサポート)
    if (check(TokenKind::KwConst)) {
        return parse_const_decl(false, std::move(attrs));
    }

    // #macro (新しいC++風マクロ構文)
    if (check(TokenKind::Hash)) {
        // #macroか他のディレクティブか確認
        auto saved_pos = pos_;
        advance();  // consume '#'

        if (check(TokenKind::KwMacro)) {
            return parse_macro(false);
        }

        // その他のディレクティブ（#test, #bench, #deprecated等）
        if (check(TokenKind::Ident)) {
            std::string directive_name = std::string(current().get_string());
            if (directive_name == "test" || directive_name == "bench" ||
                directive_name == "deprecated" || directive_name == "inline" ||
                directive_name == "optimize") {
                error(i18n::msgf(i18n::MsgId::PsDirectiveNotYetImplemented, directive_name));
                while (!is_at_end() && current().kind != TokenKind::Semicolon &&
                       current().kind != TokenKind::LBrace) {
                    advance();
                }
                return nullptr;
            }
        }

        pos_ = saved_pos;
        error(i18n::msg(i18n::MsgId::PsUnknownInvalidDirective));
        return nullptr;
    }

    // macro (v0.13.0: 型付きマクロ)
    if (check(TokenKind::KwMacro)) {
        return parse_macro(false);
    }

    // constexpr
    if (check(TokenKind::KwConstexpr)) {
        return parse_constexpr();
    }

    // グローバル変数判定（型 名前 = ... のパターン）
    if (!is_static && !is_inline && !is_async && !is_always && is_global_var_start()) {
        return parse_global_var_decl(false, std::move(attrs));
    }

    // 関数 (型 名前 ...)
    auto func_decl = parse_function(false, is_static, is_inline, std::move(attrs), is_async);
    if (is_always) {
        if (auto* f = func_decl->as<ast::FunctionDecl>()) {
            f->is_always = true;
            f->always_kind = ak;
        }
    }
    return func_decl;
}

// グローバル変数宣言かどうかを先読みで判定
bool Parser::is_global_var_start() {
    if (!is_type_start())
        return false;

    auto saved_pos = pos_;

    // posedge/negedge型は初期化子なしでもグローバル変数宣言
    // 例: posedge clk; / negedge rst;
    // Identテキスト比較（非SVモード）またはKwPosedge/KwNegedge（SVモード）
    bool is_posedge_negedge = false;
    if (check(TokenKind::Ident)) {
        std::string text(current().get_string());
        is_posedge_negedge = (text == "posedge" || text == "negedge");
    } else if (check(TokenKind::KwPosedge) || check(TokenKind::KwNegedge)) {
        is_posedge_negedge = true;
    }

    if (is_posedge_negedge) {
        advance();  // posedge/negedge
        if (!is_at_end() &&
            (check(TokenKind::Ident) || check(TokenKind::KwPosedge) ||
             check(TokenKind::KwNegedge) || check(TokenKind::KwWire) || check(TokenKind::KwReg))) {
            advance();  // 変数名
            bool result = check(TokenKind::Semicolon) || check(TokenKind::Eq);
            pos_ = saved_pos;
            return result;
        }
        pos_ = saved_pos;
        return false;
    }

    advance();

    // 名前空間修飾型（mod::Type、mod::sub::Type）をスキップ
    while (!is_at_end() && check(TokenKind::ColonColon)) {
        advance();  // ::
        if (!is_at_end() && check(TokenKind::Ident)) {
            advance();  // Type
        } else {
            pos_ = saved_pos;
            return false;
        }
    }

    // 配列サフィックス [N] をスキップ（bit[4], utiny[1024], bit[WIDTH] 等。
    // サイズはリテラルまたはconst/パラメータ参照の識別子）
    while (!is_at_end() && check(TokenKind::LBracket)) {
        advance();  // [
        if (!is_at_end() && (check(TokenKind::IntLiteral) || check(TokenKind::Ident))) {
            advance();  // N or NAME
        }
        if (!is_at_end() && check(TokenKind::RBracket)) {
            advance();  // ]
        }
    }

    // ポインタ修飾子 * をスキップ
    while (!is_at_end() && check(TokenKind::Star)) {
        advance();
    }

    bool result = false;
    if (!is_at_end() && check(TokenKind::Ident)) {
        advance();
        // 初期化子あり (=) または初期化子なし (;) の両方をサポート
        if (!is_at_end() && (check(TokenKind::Eq) || check(TokenKind::Semicolon))) {
            result = true;
        }
    }

    pos_ = saved_pos;
    return result;
}

// 関数定義
ast::DeclPtr Parser::parse_function(bool is_export, bool is_static, bool is_inline,
                                    std::vector<ast::AttributeNode> attributes, bool is_async) {
    uint32_t start_pos = current().start;
    debug::par::log(debug::par::Id::FuncDef, "", debug::Level::Trace);

    // 明示的なジェネリックパラメータをチェック（例: <T> T max(T a, T b)）
    auto [generic_params, generic_params_v2] = parse_generic_params_v2();

    auto return_type = parse_type_with_union();

    // 名前のスパンを記録（Lint警告用）
    uint32_t name_start = current().start;
    std::string name = expect_ident();
    uint32_t name_end = previous().end;

    // main関数はエクスポート不可
    if (is_export && name == "main") {
        error(i18n::msg(i18n::MsgId::ParseTheMainFunctionCannotBe));
    }

    expect(TokenKind::LParen);
    auto params = parse_params();
    expect(TokenKind::RParen);

    auto body = parse_block();

    auto func = std::make_unique<ast::FunctionDecl>(std::move(name), std::move(params),
                                                    std::move(return_type), std::move(body));

    // 名前のスパンを設定
    func->name_span = Span{name_start, name_end};

    // ジェネリックパラメータを設定（明示的に指定された場合）
    if (!generic_params.empty()) {
        func->generic_params = std::move(generic_params);
        func->generic_params_v2 = std::move(generic_params_v2);

        std::string params_str = "Function '" + func->name + "' has generic params: ";
        for (const auto& p : func->generic_params) {
            params_str += p + " ";
        }
        debug::par::log(debug::par::Id::FuncDef, params_str, debug::Level::Info);
    }

    func->visibility = is_export ? ast::Visibility::Export : ast::Visibility::Private;
    func->is_static = is_static;
    func->is_inline = is_inline;
    func->is_async = is_async;
    func->attributes = std::move(attributes);

    return std::make_unique<ast::Decl>(std::move(func), Span{start_pos, previous().end});
}

// パラメータリスト
std::vector<ast::Param> Parser::parse_params() {
    std::vector<ast::Param> params;
    bool has_default = false;

    if (!check(TokenKind::RParen)) {
        do {
            ast::Param param;
            param.qualifiers.is_const = consume_if(TokenKind::KwConst);
            param.type = parse_type_with_union();

            param.name = expect_ident();

            // デフォルト引数をパース
            if (consume_if(TokenKind::Eq)) {
                param.default_value = parse_expr();
                has_default = true;
            } else if (has_default) {
                error(i18n::msg(i18n::MsgId::PsDefaultArgumentRequiredParameterDefault));
            }

            params.push_back(std::move(param));
        } while (consume_if(TokenKind::Comma));
    }

    return params;
}

// 構造体
ast::DeclPtr Parser::parse_struct(bool is_export, std::vector<ast::AttributeNode> attributes,
                                  bool is_extern) {
    uint32_t start_pos = current().start;
    debug::par::log(debug::par::Id::StructDef, "", debug::Level::Trace);

    expect(TokenKind::KwStruct);

    uint32_t name_start = current().start;
    std::string name = expect_ident();
    uint32_t name_end = previous().end;

    auto [generic_params, generic_params_v2] = parse_generic_params_v2();

    // with キーワード
    std::vector<std::string> auto_impls;
    if (consume_if(TokenKind::KwWith)) {
        do {
            auto_impls.push_back(expect_ident());
        } while (consume_if(TokenKind::Comma));
    }

    // #[derive(...)] 属性は with と同一の自動実装機構へ合流する
    for (const auto& attr : attributes) {
        if (attr.name == "derive") {
            if (attr.args.empty()) {
                error(i18n::msg(i18n::MsgId::PsDeriveRequiresAtLeastOne));
            }
            for (const auto& arg : attr.args) {
                auto_impls.push_back(arg);
            }
        }
    }

    // where句をパース
    if (consume_if(TokenKind::KwWhere)) {
        do {
            std::string type_param = expect_ident();
            expect(TokenKind::Colon);

            std::vector<std::string> interfaces;
            interfaces.push_back(expect_ident());
            ast::ConstraintKind constraint_kind = ast::ConstraintKind::Single;

            if (check(TokenKind::Pipe)) {
                constraint_kind = ast::ConstraintKind::Or;
                while (consume_if(TokenKind::Pipe)) {
                    interfaces.push_back(expect_ident());
                }
            } else if (check(TokenKind::Plus)) {
                constraint_kind = ast::ConstraintKind::And;
                while (consume_if(TokenKind::Plus)) {
                    interfaces.push_back(expect_ident());
                }
            }

            for (auto& gp : generic_params_v2) {
                if (gp.name == type_param) {
                    gp.type_constraint = ast::TypeConstraint(constraint_kind, interfaces);
                    gp.constraints = interfaces;
                    break;
                }
            }
        } while (consume_if(TokenKind::Comma));
    }

    expect(TokenKind::LBrace);

    std::vector<ast::Field> fields;
    bool has_default_field = false;
    while (!check(TokenKind::RBrace) && !is_at_end()) {
        ast::Field field;

        // フィールド属性（#[sv::param], #[input], #[output] 等）
        while (check(TokenKind::Hash)) {
            field.attributes.push_back(parse_attribute());
        }

        field.visibility =
            consume_if(TokenKind::KwPrivate) ? ast::Visibility::Private : ast::Visibility::Export;

        if (consume_if(TokenKind::KwDefault)) {
            if (has_default_field) {
                error(i18n::msg(i18n::MsgId::PsOnlyOneDefaultMemberAllowed));
            }
            field.is_default = true;
            has_default_field = true;
        }

        field.qualifiers.is_const = consume_if(TokenKind::KwConst);

        if (check(TokenKind::RBrace)) {
            break;
        }

        field.type = parse_type_with_union();

        field.name = expect_ident();

        // フィールドのデフォルト値（= expr）: extern struct、IOフィールド（#[input]/#[output]/#[inout] 属性付き）、パラメータフィールド（#[sv::param] 属性付き）で許可する
        bool has_dir_attr = false;
        for (const auto& a : field.attributes) {
            if (a.name == "input" || a.name == "output" || a.name == "inout" ||
                a.name == "sv::param" || a.name == "verilog::param") {
                has_dir_attr = true;
            }
        }
        if ((is_extern || has_dir_attr) && consume_if(TokenKind::Eq)) {
            field.default_value = parse_expr();
        }

        expect(TokenKind::Semicolon);
        fields.push_back(std::move(field));
    }

    expect(TokenKind::RBrace);
    // C/C++スタイルの末尾セミコロン（struct X { ... };）を許容する
    consume_if(TokenKind::Semicolon);

    auto decl = std::make_unique<ast::StructDecl>(std::move(name), std::move(fields));
    decl->name_span = Span{name_start, name_end};
    decl->visibility = is_export ? ast::Visibility::Export : ast::Visibility::Private;
    decl->auto_impls = std::move(auto_impls);
    decl->attributes = std::move(attributes);

    if (!generic_params.empty()) {
        decl->generic_params = std::move(generic_params);
        decl->generic_params_v2 = std::move(generic_params_v2);

        std::string params_str = "Struct '" + decl->name + "' has generic params: ";
        for (const auto& p : decl->generic_params) {
            params_str += p + " ";
        }
        debug::par::log(debug::par::Id::StructDef, params_str, debug::Level::Info);
    }

    return std::make_unique<ast::Decl>(std::move(decl), Span{start_pos, previous().end});
}

// 演算子の種類をパース
std::optional<ast::OperatorKind> Parser::parse_operator_kind() {
    if (check(TokenKind::EqEq)) {
        advance();
        return ast::OperatorKind::Eq;
    }
    if (check(TokenKind::BangEq)) {
        advance();
        return ast::OperatorKind::Ne;
    }
    if (check(TokenKind::Lt)) {
        advance();
        return ast::OperatorKind::Lt;
    }
    if (check(TokenKind::Gt)) {
        advance();
        return ast::OperatorKind::Gt;
    }
    if (check(TokenKind::LtEq)) {
        advance();
        return ast::OperatorKind::Le;
    }
    if (check(TokenKind::GtEq)) {
        advance();
        return ast::OperatorKind::Ge;
    }
    if (check(TokenKind::Plus)) {
        advance();
        return ast::OperatorKind::Add;
    }
    if (check(TokenKind::Minus)) {
        advance();
        return ast::OperatorKind::Sub;
    }
    if (check(TokenKind::Star)) {
        advance();
        return ast::OperatorKind::Mul;
    }
    if (check(TokenKind::Slash)) {
        advance();
        return ast::OperatorKind::Div;
    }
    if (check(TokenKind::Percent)) {
        advance();
        return ast::OperatorKind::Mod;
    }
    if (check(TokenKind::Amp)) {
        advance();
        return ast::OperatorKind::BitAnd;
    }
    if (check(TokenKind::Pipe)) {
        advance();
        return ast::OperatorKind::BitOr;
    }
    if (check(TokenKind::Caret)) {
        advance();
        return ast::OperatorKind::BitXor;
    }
    if (check(TokenKind::LtLt)) {
        advance();
        return ast::OperatorKind::Shl;
    }
    if (check(TokenKind::GtGt)) {
        advance();
        return ast::OperatorKind::Shr;
    }
    if (check(TokenKind::Tilde)) {
        advance();
        return ast::OperatorKind::BitNot;
    }
    if (check(TokenKind::Bang)) {
        advance();
        return ast::OperatorKind::Not;
    }
    return std::nullopt;
}

// インターフェース
ast::DeclPtr Parser::parse_interface(bool is_export, std::vector<ast::AttributeNode> attributes) {
    uint32_t start_pos = current().start;
    expect(TokenKind::KwInterface);

    std::string name = expect_ident();

    auto [generic_params, generic_params_v2] = parse_generic_params_v2();

    expect(TokenKind::LBrace);

    std::vector<ast::MethodSig> methods;
    std::vector<ast::OperatorSig> operators;

    while (!check(TokenKind::RBrace) && !is_at_end()) {
        if (check(TokenKind::KwOperator)) {
            advance();
            ast::OperatorSig op_sig;
            in_operator_return_type_ = true;
            op_sig.return_type = parse_type();
            in_operator_return_type_ = false;
            op_sig.return_type = check_array_suffix(std::move(op_sig.return_type));

            auto op_kind = parse_operator_kind();
            if (!op_kind) {
                error(i18n::msg(i18n::MsgId::PsExpectedOperatorSymbolOperator));
                continue;
            }
            op_sig.op = *op_kind;

            expect(TokenKind::LParen);
            op_sig.params = parse_params();
            expect(TokenKind::RParen);
            expect(TokenKind::Semicolon);
            operators.push_back(std::move(op_sig));
        } else {
            ast::MethodSig sig;
            sig.return_type = parse_type_with_union();
            sig.return_type = check_array_suffix(std::move(sig.return_type));
            sig.name = expect_ident();
            expect(TokenKind::LParen);
            sig.params = parse_params();
            expect(TokenKind::RParen);
            expect(TokenKind::Semicolon);
            methods.push_back(std::move(sig));
        }
    }

    expect(TokenKind::RBrace);

    auto decl = std::make_unique<ast::InterfaceDecl>(std::move(name), std::move(methods));
    decl->operators = std::move(operators);
    decl->visibility = is_export ? ast::Visibility::Export : ast::Visibility::Private;
    decl->attributes = std::move(attributes);

    if (!generic_params.empty()) {
        decl->generic_params = std::move(generic_params);
        decl->generic_params_v2 = std::move(generic_params_v2);
    }

    return std::make_unique<ast::Decl>(std::move(decl), Span{start_pos, previous().end});
}

// impl
ast::DeclPtr Parser::parse_impl(std::vector<ast::AttributeNode> attributes) {
    uint32_t start_pos = current().start;
    expect(TokenKind::KwImpl);

    std::vector<std::string> generic_params;
    std::vector<ast::GenericParam> generic_params_v2;
    if (check(TokenKind::Lt)) {
        auto [params, params_v2] = parse_generic_params_v2();
        generic_params = std::move(params);
        generic_params_v2 = std::move(params_v2);
    }

    auto target = parse_type();
    target = check_array_suffix(std::move(target));

    if (consume_if(TokenKind::KwFor)) {
        std::string iface = expect_ident();

        std::vector<ast::TypePtr> iface_type_args;
        if (check(TokenKind::Lt)) {
            advance();
            do {
                iface_type_args.push_back(parse_type());
            } while (consume_if(TokenKind::Comma));
            expect(TokenKind::Gt);
        }

        // where句をパース
        std::vector<ast::WhereClause> where_clauses;
        if (consume_if(TokenKind::KwWhere)) {
            do {
                std::string type_param = expect_ident();
                expect(TokenKind::Colon);

                std::vector<std::string> interfaces;
                interfaces.push_back(expect_ident());
                ast::ConstraintKind constraint_kind = ast::ConstraintKind::Single;

                if (check(TokenKind::Pipe)) {
                    constraint_kind = ast::ConstraintKind::Or;
                    while (consume_if(TokenKind::Pipe)) {
                        interfaces.push_back(expect_ident());
                    }
                } else if (check(TokenKind::Plus)) {
                    constraint_kind = ast::ConstraintKind::And;
                    while (consume_if(TokenKind::Plus)) {
                        interfaces.push_back(expect_ident());
                    }
                }

                ast::TypeConstraint constraint(constraint_kind, std::move(interfaces));
                where_clauses.emplace_back(std::move(type_param), std::move(constraint));
            } while (consume_if(TokenKind::Comma));
        }

        expect(TokenKind::LBrace);

        auto decl = std::make_unique<ast::ImplDecl>(std::move(iface), std::move(target));
        decl->interface_type_args = std::move(iface_type_args);
        decl->where_clauses = std::move(where_clauses);
        decl->attributes = std::move(attributes);

        if (!generic_params.empty()) {
            decl->generic_params = std::move(generic_params);
            decl->generic_params_v2 = std::move(generic_params_v2);
        }

        while (!check(TokenKind::RBrace) && !is_at_end()) {
            try {
                std::vector<ast::AttributeNode> method_attrs;
                while (is_attribute_start()) {
                    method_attrs.push_back(parse_attribute());
                }

                if (check(TokenKind::KwOperator)) {
                    advance();
                    auto op_impl = std::make_unique<ast::OperatorImpl>();
                    in_operator_return_type_ = true;
                    op_impl->return_type = parse_type();
                    in_operator_return_type_ = false;

                    auto op_kind = parse_operator_kind();
                    if (!op_kind) {
                        error(i18n::msg(i18n::MsgId::PsExpectedOperatorSymbolOperator));
                        continue;
                    }
                    op_impl->op = *op_kind;

                    expect(TokenKind::LParen);
                    op_impl->params = parse_params();
                    expect(TokenKind::RParen);
                    op_impl->body = parse_block();
                    decl->operators.push_back(std::move(op_impl));
                } else {
                    bool is_private = consume_if(TokenKind::KwPrivate);
                    bool is_static = consume_if(TokenKind::KwStatic);

                    auto func = parse_function(false, is_static, false, std::move(method_attrs));
                    if (auto* f = func->as<ast::FunctionDecl>()) {
                        if (is_private) {
                            f->visibility = ast::Visibility::Private;
                        } else {
                            f->visibility = ast::Visibility::Export;
                        }
                        decl->methods.push_back(
                            std::unique_ptr<ast::FunctionDecl>(static_cast<ast::FunctionDecl*>(
                                std::get<std::unique_ptr<ast::FunctionDecl>>(func->kind)
                                    .release())));
                    }
                }
            } catch (...) {
                synchronize();
            }
        }

        expect(TokenKind::RBrace);
        return std::make_unique<ast::Decl>(std::move(decl), Span{start_pos, previous().end});
    } else {
        return parse_impl_ctor(start_pos, std::move(target), std::move(attributes),
                               std::move(generic_params), std::move(generic_params_v2));
    }
}

// コンストラクタ/デストラクタ専用implの解析
ast::DeclPtr Parser::parse_impl_ctor(uint32_t start_pos, ast::TypePtr target,
                                     std::vector<ast::AttributeNode> attributes,
                                     std::vector<std::string> generic_params,
                                     std::vector<ast::GenericParam> generic_params_v2) {
    expect(TokenKind::LBrace);

    auto decl = std::make_unique<ast::ImplDecl>(std::move(target));
    decl->attributes = std::move(attributes);

    if (!generic_params.empty()) {
        decl->generic_params = std::move(generic_params);
        decl->generic_params_v2 = std::move(generic_params_v2);
    }

    while (!check(TokenKind::RBrace) && !is_at_end()) {
        try {
            bool is_overload = consume_if(TokenKind::KwOverload);

            // デストラクタ: ~self()
            if (check(TokenKind::Tilde)) {
                advance();
                if (current().kind == TokenKind::KwSelf ||
                    (current().kind == TokenKind::Ident && current().get_string() == "self")) {
                    advance();
                    expect(TokenKind::LParen);
                    expect(TokenKind::RParen);
                    auto body = parse_block();

                    auto dtor = std::make_unique<ast::FunctionDecl>(
                        "~self", std::vector<ast::Param>{}, ast::make_void(), std::move(body));
                    dtor->is_destructor = true;

                    if (decl->destructor) {
                        error(i18n::msg(i18n::MsgId::PsOnlyOneDestructorAllowedPer));
                    }
                    decl->destructor = std::move(dtor);
                } else {
                    error(i18n::msg(i18n::MsgId::PsExpectedSelf));
                    synchronize();
                }
            }
            // コンストラクタ: self() or overload self(...)
            else if (current().kind == TokenKind::KwSelf ||
                     (current().kind == TokenKind::Ident && current().get_string() == "self")) {
                advance();
                expect(TokenKind::LParen);
                auto params = parse_params();
                expect(TokenKind::RParen);
                auto body = parse_block();

                auto ctor = std::make_unique<ast::FunctionDecl>("self", std::move(params),
                                                                ast::make_void(), std::move(body));
                ctor->is_constructor = true;
                ctor->is_overload = is_overload;

                decl->constructors.push_back(std::move(ctor));
            } else if (check(TokenKind::KwOperator)) {
                advance();
                auto op_impl = std::make_unique<ast::OperatorImpl>();
                in_operator_return_type_ = true;
                op_impl->return_type = parse_type();
                in_operator_return_type_ = false;

                auto op_kind = parse_operator_kind();
                if (!op_kind) {
                    error(i18n::msg(i18n::MsgId::PsExpectedOperatorSymbolOperator));
                    continue;
                }
                op_impl->op = *op_kind;

                expect(TokenKind::LParen);
                op_impl->params = parse_params();
                expect(TokenKind::RParen);
                op_impl->body = parse_block();
                decl->operators.push_back(std::move(op_impl));
            } else {
                std::vector<ast::AttributeNode> method_attrs;
                while (is_attribute_start()) {
                    method_attrs.push_back(parse_attribute());
                }

                bool is_private = consume_if(TokenKind::KwPrivate);
                bool is_static = consume_if(TokenKind::KwStatic);

                auto func = parse_function(false, is_static, false, std::move(method_attrs));
                if (auto* f = func->as<ast::FunctionDecl>()) {
                    if (is_private) {
                        f->visibility = ast::Visibility::Private;
                    } else {
                        f->visibility = ast::Visibility::Export;
                    }
                    decl->methods.push_back(
                        std::unique_ptr<ast::FunctionDecl>(static_cast<ast::FunctionDecl*>(
                            std::get<std::unique_ptr<ast::FunctionDecl>>(func->kind).release())));
                }
            }
        } catch (...) {
            synchronize();
        }
    }

    expect(TokenKind::RBrace);
    return std::make_unique<ast::Decl>(std::move(decl), Span{start_pos, previous().end});
}

// ブロック
std::vector<ast::StmtPtr> Parser::parse_block() {
    debug::par::log(debug::par::Id::Block, "", debug::Level::Trace);
    expect(TokenKind::LBrace);

    std::vector<ast::StmtPtr> stmts;
    // 1ブロックあたりの文数の固定上限は設けない（M5）。無限ループは進捗検出で防ぐ。
    bool is_first = true;
    size_t last_pos = pos_;

    while (!check(TokenKind::RBrace) && !is_at_end()) {
        if (pos_ == last_pos && !is_first) {
            error(i18n::msg(i18n::MsgId::PsParserStuckBlockNoProgress));
            while (!is_at_end() && current().kind != TokenKind::Semicolon &&
                   current().kind != TokenKind::RBrace) {
                advance();
            }
            if (current().kind == TokenKind::Semicolon) {
                advance();
            }
            if (is_at_end() || current().kind == TokenKind::RBrace) {
                break;
            }
        }
        last_pos = pos_;
        is_first = false;

        if (auto stmt = parse_stmt()) {
            stmts.push_back(std::move(stmt));
        } else {
            if (!is_at_end() && current().kind != TokenKind::RBrace) {
                advance();
            }
        }
    }

    expect(TokenKind::RBrace);
    return stmts;
}

// エラー報告
void Parser::error(const std::string& msg) {
    uint32_t current_line = current().start;
    if (current_line == last_error_line_ && !diagnostics_.empty()) {
        return;
    }
    last_error_line_ = current_line;

    debug::par::log(debug::par::Id::Error, msg, debug::Level::Error);
    diagnostics_.emplace_back(DiagKind::Error, Span{current().start, current().end}, msg);
}

// エラー復旧 - 同期ポイントまでスキップ
void Parser::synchronize() {
    const int MAX_SKIP = 1000;
    int skipped = 0;

    size_t last_pos = pos_;

    advance();
    while (!is_at_end() && skipped < MAX_SKIP) {
        if (pos_ == last_pos) {
            if (pos_ < tokens_.size() - 1) {
                pos_++;
            } else {
                break;
            }
        }
        last_pos = pos_;

        if (previous().kind == TokenKind::Semicolon)
            return;
        switch (current().kind) {
            case TokenKind::KwStruct:
            case TokenKind::KwInterface:
            case TokenKind::KwImpl:
            case TokenKind::KwImport:
            case TokenKind::KwExport:
            case TokenKind::Hash:
                return;
            case TokenKind::KwBool:
            case TokenKind::KwInt:
            case TokenKind::KwVoid:
            case TokenKind::KwString:
            case TokenKind::KwChar:
            case TokenKind::KwFloat:
            case TokenKind::KwDouble:
                return;
            default:
                advance();
                skipped++;
        }
    }

    if (skipped >= MAX_SKIP) {
        error(i18n::msg(i18n::MsgId::PsParserStuckSynchronizationTooMany));
    }
}

}  // namespace cm
