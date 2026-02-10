#pragma once

#include "../../common/debug/par.hpp"
#include "../../common/diagnostics.hpp"
#include "../ast/decl.hpp"
#include "../lexer/token.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cm {

// ============================================================
// 診断メッセージ（共通定義への互換性エイリアス）
// ============================================================
// 注: 共通のdiagnostics.hppでSeverity、Diagnosticが定義されています
// DiagKindはSeverityのエイリアスとして残します（後方互換性）
using DiagKind = Severity;

// ============================================================
// パーサ
// ============================================================
class Parser {
   public:
    Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)), pos_(0), last_error_line_(0) {}

    // プログラム全体を解析
    ast::Program parse() {
        debug::par::log(debug::par::Id::Start);

        ast::Program program;
        int iterations = 0;
        const int MAX_ITERATIONS = 10000;
        size_t last_pos = pos_;

        while (!is_at_end() && iterations < MAX_ITERATIONS) {
            // 無限ループ検出
            if (pos_ == last_pos && iterations > 0) {
                error("Parser stuck - no progress made");
                if (!is_at_end()) {
                    advance();  // 強制的に進める
                }
            }
            last_pos = pos_;

            if (auto decl = parse_top_level()) {
                program.declarations.push_back(std::move(decl));
            } else {
                synchronize();
            }
            iterations++;
        }

        if (iterations >= MAX_ITERATIONS) {
            error("Parser exceeded maximum iteration limit");
        }

        debug::par::log(debug::par::Id::End,
                        std::to_string(program.declarations.size()) + " declarations");
        return program;
    }

    const std::vector<Diagnostic>& diagnostics() const { return diagnostics_; }
    bool has_errors() const {
        for (const auto& d : diagnostics_) {
            if (d.severity == DiagKind::Error)
                return true;
        }
        return false;
    }

   private:
    // トップレベル宣言
    ast::DeclPtr parse_top_level() {
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
                // 既に正しい位置にいる（'struct'トークンの位置）
                return parse_struct(true, std::move(attrs));
            }
            if (check(TokenKind::KwInterface)) {
                // 既に正しい位置にいる
                return parse_interface(true, std::move(attrs));
            }
            if (check(TokenKind::KwEnum)) {
                // 既に正しい位置にいる
                return parse_enum_decl(true, std::move(attrs));
            }
            if (check(TokenKind::KwTypedef)) {
                // 既に正しい位置にいる
                return parse_typedef_decl(true, std::move(attrs));
            }
            if (check(TokenKind::KwConst)) {
                // 既に正しい位置にいる
                return parse_const_decl(true, std::move(attrs));
            }
            if (check(TokenKind::KwImpl)) {
                // 既に正しい位置にいる
                return parse_impl_export(std::move(attrs));
            }
            // v0.13.0: export macro
            if (check(TokenKind::KwMacro)) {
                return parse_macro(true);
            }

            // export function (型から始まる関数、または修飾子から始まる関数の場合)
            // 修飾子: static, inline
            if (is_type_start() || check(TokenKind::KwStatic) || check(TokenKind::KwInline)) {
                // 修飾子を収集
                bool is_static = consume_if(TokenKind::KwStatic);
                bool is_inline = consume_if(TokenKind::KwInline);

                return parse_function(true, is_static, is_inline, std::move(attrs));
            }

            // それ以外は分離エクスポート (export NAME1, NAME2;)
            if (!attrs.empty()) {
                error("Attributes are not supported on export lists");
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
                // v0.13.0: #macro も型付きマクロとして処理
                // 注: #hashはすでに消費されているので、macroのみ処理
                return parse_macro(false);
            }

            // その他のディレクティブ（#test, #bench, #deprecated等）
            // 現在未実装のディレクティブをチェック
            if (check(TokenKind::Ident)) {
                std::string directive_name = std::string(current().get_string());
                if (directive_name == "test" || directive_name == "bench" ||
                    directive_name == "deprecated" || directive_name == "inline" ||
                    directive_name == "optimize") {
                    // 未実装のディレクティブ
                    error("Directive '#" + directive_name + "' is not yet implemented");
                    // エラー後はスキップして続行
                    while (!is_at_end() && current().kind != TokenKind::Semicolon &&
                           current().kind != TokenKind::LBrace) {
                        advance();
                    }
                    return nullptr;
                }
            }

            pos_ = saved_pos;  // 位置を戻す
            // 未知のディレクティブ
            error("Unknown or invalid directive after '#'");
            return nullptr;
        }

        // macro (v0.13.0: 型付きマクロ)
        // 構文: macro TYPE NAME = EXPR;
        if (check(TokenKind::KwMacro)) {
            return parse_macro(false);
        }

        // constexpr
        if (check(TokenKind::KwConstexpr)) {
            return parse_constexpr();
        }

        // 関数 (型 名前 ...)
        return parse_function(false, is_static, is_inline, std::move(attrs));
    }

    // 関数定義
    ast::DeclPtr parse_function(bool is_export, bool is_static, bool is_inline,
                                std::vector<ast::AttributeNode> attributes = {}) {
        uint32_t start_pos = current().start;
        debug::par::log(debug::par::Id::FuncDef, "", debug::Level::Trace);

        // 明示的なジェネリックパラメータをチェック（例: <T> T max(T a, T b)）
        auto [generic_params, generic_params_v2] = parse_generic_params_v2();

        auto return_type = parse_type();
        // C++スタイルの配列戻り値型: int[] func(), int[3] func()
        return_type = check_array_suffix(std::move(return_type));

        // 名前のスパンを記録（Lint警告用）
        uint32_t name_start = current().start;
        std::string name = expect_ident();
        uint32_t name_end = previous().end;

        // main関数はエクスポート不可
        if (is_export && name == "main") {
            error("main関数はエクスポートできません");
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

            // デバッグ出力
            std::string params_str = "Function '" + func->name + "' has generic params: ";
            for (const auto& p : func->generic_params) {
                params_str += p + " ";
            }
            debug::par::log(debug::par::Id::FuncDef, params_str, debug::Level::Info);
        }

        func->visibility = is_export ? ast::Visibility::Export : ast::Visibility::Private;
        func->is_static = is_static;
        func->is_inline = is_inline;
        func->attributes = std::move(attributes);

        return std::make_unique<ast::Decl>(std::move(func), Span{start_pos, previous().end});
    }

    // パラメータリスト
    std::vector<ast::Param> parse_params() {
        std::vector<ast::Param> params;
        bool has_default = false;  // デフォルト引数が始まったかどうか

        if (!check(TokenKind::RParen)) {
            do {
                ast::Param param;
                param.qualifiers.is_const = consume_if(TokenKind::KwConst);
                param.type = parse_type();

                // C++スタイルの配列パラメータ: int[10] arr
                param.type = check_array_suffix(std::move(param.type));

                param.name = expect_ident();

                // デフォルト引数をパース
                if (consume_if(TokenKind::Eq)) {
                    param.default_value = parse_expr();
                    has_default = true;
                } else if (has_default) {
                    // デフォルト引数の後には必須引数は置けない
                    error("Default argument required after parameter with default value");
                }

                params.push_back(std::move(param));
            } while (consume_if(TokenKind::Comma));
        }

        return params;
    }

    // 構造体
    ast::DeclPtr parse_struct(bool is_export, std::vector<ast::AttributeNode> attributes = {}) {
        uint32_t start_pos = current().start;
        debug::par::log(debug::par::Id::StructDef, "", debug::Level::Trace);

        expect(TokenKind::KwStruct);

        // 名前のスパンを記録（Lint警告用）
        uint32_t name_start = current().start;
        std::string name = expect_ident();
        uint32_t name_end = previous().end;

        // ジェネリックパラメータをチェック（例: struct Vec<T>）
        auto [generic_params, generic_params_v2] = parse_generic_params_v2();

        // with キーワード
        std::vector<std::string> auto_impls;
        if (consume_if(TokenKind::KwWith)) {
            do {
                auto_impls.push_back(expect_ident());
            } while (consume_if(TokenKind::Comma));
        }

        // where句をパース（例: where T: Eq, U: Ord + Clone, V: I | J）
        // 構造体のジェネリックパラメータに制約を追加
        if (consume_if(TokenKind::KwWhere)) {
            do {
                std::string type_param = expect_ident();
                expect(TokenKind::Colon);

                // インターフェース境界をパース
                std::vector<std::string> interfaces;
                interfaces.push_back(expect_ident());
                ast::ConstraintKind constraint_kind = ast::ConstraintKind::Single;

                // 複合インターフェース境界
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

                // 対応するジェネリックパラメータに制約を追加
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

            // private修飾子: impl/interfaceのselfポインタからのみアクセス可能
            field.visibility = consume_if(TokenKind::KwPrivate) ? ast::Visibility::Private
                                                                : ast::Visibility::Export;

            // default修飾子: デフォルトメンバ（構造体に1つだけ）
            if (consume_if(TokenKind::KwDefault)) {
                if (has_default_field) {
                    error("Only one default member allowed per struct");
                }
                field.is_default = true;
                has_default_field = true;
            }

            field.qualifiers.is_const = consume_if(TokenKind::KwConst);

            // 修飾子の後で再度RBraceをチェック（空の構造体や最後のフィールドの後）
            if (check(TokenKind::RBrace)) {
                break;
            }

            field.type = parse_type();

            // C++スタイルの配列フィールド: int[10] data;
            field.type = check_array_suffix(std::move(field.type));

            field.name = expect_ident();
            expect(TokenKind::Semicolon);
            fields.push_back(std::move(field));
        }

        expect(TokenKind::RBrace);

        auto decl = std::make_unique<ast::StructDecl>(std::move(name), std::move(fields));
        decl->name_span = Span{name_start, name_end};  // 名前のスパンを設定
        decl->visibility = is_export ? ast::Visibility::Export : ast::Visibility::Private;
        decl->auto_impls = std::move(auto_impls);
        decl->attributes = std::move(attributes);

        // ジェネリックパラメータを設定（明示的に指定された場合）
        if (!generic_params.empty()) {
            decl->generic_params = std::move(generic_params);
            decl->generic_params_v2 = std::move(generic_params_v2);

            // デバッグ出力
            std::string params_str = "Struct '" + decl->name + "' has generic params: ";
            for (const auto& p : decl->generic_params) {
                params_str += p + " ";
            }
            debug::par::log(debug::par::Id::StructDef, params_str, debug::Level::Info);
        }

        return std::make_unique<ast::Decl>(std::move(decl), Span{start_pos, previous().end});
    }

    // 演算子の種類をパース
    std::optional<ast::OperatorKind> parse_operator_kind() {
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
    ast::DeclPtr parse_interface(bool is_export, std::vector<ast::AttributeNode> attributes = {}) {
        expect(TokenKind::KwInterface);

        // インターフェース名を取得
        std::string name = expect_ident();

        // ジェネリックパラメータをチェック（例: interface Eq<T>）
        auto [generic_params, generic_params_v2] = parse_generic_params_v2();

        expect(TokenKind::LBrace);

        std::vector<ast::MethodSig> methods;
        std::vector<ast::OperatorSig> operators;

        while (!check(TokenKind::RBrace) && !is_at_end()) {
            // operator キーワードをチェック
            if (check(TokenKind::KwOperator)) {
                advance();  // consume 'operator'
                ast::OperatorSig op_sig;
                // 演算子戻り値型パース中は*&を型サフィックスとして消費しない
                in_operator_return_type_ = true;
                op_sig.return_type = parse_type();
                in_operator_return_type_ = false;
                // C++スタイルの配列戻り値型: int[] operator+(), int[3] operator-()
                op_sig.return_type = check_array_suffix(std::move(op_sig.return_type));

                auto op_kind = parse_operator_kind();
                if (!op_kind) {
                    error("Expected operator symbol after 'operator'");
                    continue;
                }
                op_sig.op = *op_kind;

                expect(TokenKind::LParen);
                op_sig.params = parse_params();
                expect(TokenKind::RParen);
                expect(TokenKind::Semicolon);
                operators.push_back(std::move(op_sig));
            } else {
                // 通常のメソッドシグネチャ
                ast::MethodSig sig;
                sig.return_type = parse_type();
                // C++スタイルの配列戻り値型: int[] func(), int[3] func()
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

        // ジェネリックパラメータを設定（明示的に指定された場合）
        if (!generic_params.empty()) {
            decl->generic_params = std::move(generic_params);
            decl->generic_params_v2 = std::move(generic_params_v2);
        }

        return std::make_unique<ast::Decl>(std::move(decl));
    }

    // impl
    // impl<T> Type<T> for InterfaceName<T> { ... } - ジェネリックメソッド実装
    // impl Type for InterfaceName { ... } - メソッド実装（省略記法）
    // impl<T> Type for InterfaceName<T> where T: SomeType { ... } - where句付き
    // impl<T> Type<T> { ... } - ジェネリックコンストラクタ/デストラクタ
    // impl Type<T> { ... } - コンストラクタ/デストラクタ専用
    ast::DeclPtr parse_impl(std::vector<ast::AttributeNode> attributes = {}) {
        expect(TokenKind::KwImpl);

        // impl<T>構文のチェック
        std::vector<std::string> generic_params;
        std::vector<ast::GenericParam> generic_params_v2;
        if (check(TokenKind::Lt)) {
            // <T>または<T: Constraint>のパース
            auto [params, params_v2] = parse_generic_params_v2();
            generic_params = std::move(params);
            generic_params_v2 = std::move(params_v2);
        }

        auto target = parse_type();  // Type (Vec<T> など、ジェネリクスは型に含まれる)
        // 配列/スライスサフィックスをチェック (int[], int[5] など)
        target = check_array_suffix(std::move(target));

        // forがあればメソッド実装、なければコンストラクタ/デストラクタ専用
        if (consume_if(TokenKind::KwFor)) {
            std::string iface = expect_ident();  // InterfaceName

            // インターフェースの型引数をパース（例: ValueHolder<T>）
            std::vector<ast::TypePtr> iface_type_args;
            if (check(TokenKind::Lt)) {
                advance();  // consume <
                do {
                    iface_type_args.push_back(parse_type());
                } while (consume_if(TokenKind::Comma));
                expect(TokenKind::Gt);
            }

            // where句をパース
            // where T: Interface, U: I + J, V: I | J
            std::vector<ast::WhereClause> where_clauses;
            if (consume_if(TokenKind::KwWhere)) {
                do {
                    std::string type_param = expect_ident();
                    expect(TokenKind::Colon);

                    // インターフェース境界をパース
                    std::vector<std::string> interfaces;
                    interfaces.push_back(expect_ident());
                    ast::ConstraintKind constraint_kind = ast::ConstraintKind::Single;

                    // 複合インターフェース境界
                    if (check(TokenKind::Pipe)) {
                        // OR制約: T: I | J
                        constraint_kind = ast::ConstraintKind::Or;
                        while (consume_if(TokenKind::Pipe)) {
                            interfaces.push_back(expect_ident());
                        }
                    } else if (check(TokenKind::Plus)) {
                        // AND制約: T: I + J
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

            // ジェネリックパラメータを設定
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

                    // operator キーワードをチェック
                    if (check(TokenKind::KwOperator)) {
                        advance();  // consume 'operator'
                        auto op_impl = std::make_unique<ast::OperatorImpl>();
                        // 演算子の戻り値型パース時は*や&を型サフィックスとして
                        // 消費しない（operator Num *(Num other)のように
                        // *が演算子として解釈されるべきため）
                        in_operator_return_type_ = true;
                        op_impl->return_type = parse_type();
                        in_operator_return_type_ = false;

                        auto op_kind = parse_operator_kind();
                        if (!op_kind) {
                            error("Expected operator symbol after 'operator'");
                            continue;
                        }
                        op_impl->op = *op_kind;

                        expect(TokenKind::LParen);
                        op_impl->params = parse_params();
                        expect(TokenKind::RParen);
                        op_impl->body = parse_block();
                        decl->operators.push_back(std::move(op_impl));
                    } else {
                        // private/static修飾子をチェック
                        bool is_private = consume_if(TokenKind::KwPrivate);
                        bool is_static = consume_if(TokenKind::KwStatic);

                        auto func =
                            parse_function(false, is_static, false, std::move(method_attrs));
                        if (auto* f = func->as<ast::FunctionDecl>()) {
                            // privateメソッドの場合はvisibilityを設定
                            if (is_private) {
                                f->visibility = ast::Visibility::Private;
                            } else {
                                // デフォルトはExport（外部から呼び出し可能）
                                f->visibility = ast::Visibility::Export;
                            }
                            decl->methods.push_back(
                                std::unique_ptr<ast::FunctionDecl>(static_cast<ast::FunctionDecl*>(
                                    std::get<std::unique_ptr<ast::FunctionDecl>>(func->kind)
                                        .release())));
                        }
                    }
                } catch (...) {
                    // エラー回復：次の関数定義または閉じ括弧まで進める
                    synchronize();
                }
            }

            expect(TokenKind::RBrace);
            return std::make_unique<ast::Decl>(std::move(decl));
        } else {
            // コンストラクタ/デストラクタ専用impl
            return parse_impl_ctor(std::move(target), std::move(attributes),
                                   std::move(generic_params), std::move(generic_params_v2));
        }
    }

    // コンストラクタ/デストラクタ専用implの解析
    // impl<T> Type<T> { self() { ... } ~self() { ... } }
    ast::DeclPtr parse_impl_ctor(ast::TypePtr target, std::vector<ast::AttributeNode> attributes,
                                 std::vector<std::string> generic_params = {},
                                 std::vector<ast::GenericParam> generic_params_v2 = {}) {
        expect(TokenKind::LBrace);

        auto decl = std::make_unique<ast::ImplDecl>(std::move(target));
        decl->attributes = std::move(attributes);

        // ジェネリックパラメータを設定
        if (!generic_params.empty()) {
            decl->generic_params = std::move(generic_params);
            decl->generic_params_v2 = std::move(generic_params_v2);
        }

        while (!check(TokenKind::RBrace) && !is_at_end()) {
            try {
                // overload修飾子をチェック
                bool is_overload = consume_if(TokenKind::KwOverload);

                // デストラクタ: ~self()
                if (check(TokenKind::Tilde)) {
                    advance();  // consume ~
                    if (current().kind == TokenKind::KwSelf ||
                        (current().kind == TokenKind::Ident && current().get_string() == "self")) {
                        advance();  // consume self
                        expect(TokenKind::LParen);
                        expect(TokenKind::RParen);
                        auto body = parse_block();

                        auto dtor = std::make_unique<ast::FunctionDecl>(
                            "~self", std::vector<ast::Param>{}, ast::make_void(), std::move(body));
                        dtor->is_destructor = true;

                        if (decl->destructor) {
                            error("Only one destructor allowed per impl block");
                        }
                        decl->destructor = std::move(dtor);
                    } else {
                        error("Expected 'self' after '~'");
                        synchronize();
                    }
                }
                // コンストラクタ: self() or overload self(...)
                else if (current().kind == TokenKind::KwSelf ||
                         (current().kind == TokenKind::Ident && current().get_string() == "self")) {
                    advance();  // consume self
                    expect(TokenKind::LParen);
                    auto params = parse_params();
                    expect(TokenKind::RParen);
                    auto body = parse_block();

                    auto ctor = std::make_unique<ast::FunctionDecl>(
                        "self", std::move(params), ast::make_void(), std::move(body));
                    ctor->is_constructor = true;
                    ctor->is_overload = is_overload;

                    decl->constructors.push_back(std::move(ctor));
                } else if (check(TokenKind::KwOperator)) {
                    // operator定義（impl T内で直接定義）
                    advance();  // consume 'operator'
                    auto op_impl = std::make_unique<ast::OperatorImpl>();
                    // 演算子の戻り値型パース時は*や&を型サフィックスとして消費しない
                    in_operator_return_type_ = true;
                    op_impl->return_type = parse_type();
                    in_operator_return_type_ = false;

                    auto op_kind = parse_operator_kind();
                    if (!op_kind) {
                        error("Expected operator symbol after 'operator'");
                        continue;
                    }
                    op_impl->op = *op_kind;

                    expect(TokenKind::LParen);
                    op_impl->params = parse_params();
                    expect(TokenKind::RParen);
                    op_impl->body = parse_block();
                    decl->operators.push_back(std::move(op_impl));
                } else {
                    // selfでもデストラクタでもoperatorでもない場合、通常メソッドとして解析
                    // これにより impl<T> Type<T> { void method() { ... } } が可能になる
                    std::vector<ast::AttributeNode> method_attrs;
                    while (is_attribute_start()) {
                        method_attrs.push_back(parse_attribute());
                    }

                    // private/static修飾子をチェック
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
        return std::make_unique<ast::Decl>(std::move(decl));
    }

    // ブロック
    std::vector<ast::StmtPtr> parse_block() {
        debug::par::log(debug::par::Id::Block, "", debug::Level::Trace);
        expect(TokenKind::LBrace);

        std::vector<ast::StmtPtr> stmts;
        int iterations = 0;
        const int MAX_BLOCK_ITERATIONS = 1000;
        size_t last_pos = pos_;

        while (!check(TokenKind::RBrace) && !is_at_end() && iterations < MAX_BLOCK_ITERATIONS) {
            // 無限ループ検出
            if (pos_ == last_pos && iterations > 0) {
                error("Parser stuck in block - no progress made");
                // エラー復旧: 次のセミコロンまたは閉じ括弧まで進める
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

            if (auto stmt = parse_stmt()) {
                stmts.push_back(std::move(stmt));
            } else {
                // parse_stmtが失敗した場合、エラー復旧
                if (!is_at_end() && current().kind != TokenKind::RBrace) {
                    // 少なくとも1トークン進める
                    advance();
                }
            }
            iterations++;
        }

        if (iterations >= MAX_BLOCK_ITERATIONS) {
            error("Block parsing exceeded maximum iteration limit");
        }

        expect(TokenKind::RBrace);
        return stmts;
    }

    // 文
    ast::StmtPtr parse_stmt();

    // パターン（switch文用）
    std::unique_ptr<ast::Pattern> parse_pattern();
    std::unique_ptr<ast::Pattern> parse_pattern_element();

    // 式
    ast::ExprPtr parse_expr();
    ast::ExprPtr parse_assignment();
    ast::ExprPtr parse_ternary();
    ast::ExprPtr parse_logical_or();
    ast::ExprPtr parse_logical_and();
    ast::ExprPtr parse_bitwise_or();
    ast::ExprPtr parse_bitwise_xor();
    ast::ExprPtr parse_bitwise_and();
    ast::ExprPtr parse_equality();
    ast::ExprPtr parse_relational();
    ast::ExprPtr parse_shift();
    ast::ExprPtr parse_additive();
    ast::ExprPtr parse_multiplicative();
    ast::ExprPtr parse_unary();
    ast::ExprPtr parse_postfix();
    ast::ExprPtr parse_primary();
    ast::ExprPtr parse_lambda_body(std::vector<ast::Param> params, uint32_t start_pos);
    ast::ExprPtr parse_match_expr(uint32_t start_pos);
    std::unique_ptr<ast::MatchPattern> parse_match_pattern();
    std::unique_ptr<ast::MatchPattern> parse_match_pattern_element();

    // ジェネリックパラメータ（<T>, <T: Interface>, <T: I + J>, <T: I | J>, <T, U>）をパース
    // すべての制約はインターフェース境界として解釈される
    // const N: int のような定数パラメータもサポート
    // 戻り値: pair<名前リスト（後方互換）, GenericParamリスト（制約付き）>
    std::pair<std::vector<std::string>, std::vector<ast::GenericParam>> parse_generic_params_v2() {
        std::vector<std::string> names;
        std::vector<ast::GenericParam> params;

        if (!check(TokenKind::Lt)) {
            return {names, params};  // ジェネリックパラメータがない場合
        }

        advance();  // '<' を消費

        // パラメータリストをパース
        do {
            if (check(TokenKind::Gt)) {
                break;  // 空のパラメータリスト
            }

            // パラメータ名を読む
            std::string param_name = expect_ident();

            // コロンがあれば制約を読む
            if (consume_if(TokenKind::Colon)) {
                // const キーワードがあれば定数パラメータ
                // <N: const int> 形式
                if (consume_if(TokenKind::KwConst)) {
                    ast::TypePtr const_type = parse_type();

                    names.push_back(param_name);
                    params.emplace_back(param_name, std::move(const_type));

                    debug::par::log(debug::par::Id::FuncDef,
                                    "Const generic param: " + param_name + " : const " +
                                        (const_type ? ast::type_to_string(*const_type) : "unknown"),
                                    debug::Level::Debug);
                } else {
                    // 通常のインターフェース制約: <T: Interface>
                    std::vector<std::string> interfaces;
                    ast::ConstraintKind constraint_kind = ast::ConstraintKind::Single;

                    interfaces.push_back(expect_ident());

                    // 複合インターフェース境界
                    // T: I | J (OR - いずれかを実装)
                    // T: I + J (AND - すべてを実装)
                    if (check(TokenKind::Pipe)) {
                        // OR制約: T: I | J
                        constraint_kind = ast::ConstraintKind::Or;
                        while (consume_if(TokenKind::Pipe)) {
                            interfaces.push_back(expect_ident());
                        }
                    } else if (check(TokenKind::Plus)) {
                        // AND制約: T: I + J
                        constraint_kind = ast::ConstraintKind::And;
                        while (consume_if(TokenKind::Plus)) {
                            interfaces.push_back(expect_ident());
                        }
                    }

                    names.push_back(param_name);
                    ast::TypeConstraint tc(constraint_kind, interfaces);
                    params.emplace_back(param_name, std::move(tc));

                    // デバッグ出力
                    std::string constraint_str;
                    std::string separator =
                        (constraint_kind == ast::ConstraintKind::Or) ? " | " : " + ";
                    for (size_t i = 0; i < interfaces.size(); ++i) {
                        if (i > 0)
                            constraint_str += separator;
                        constraint_str += interfaces[i];
                    }
                    debug::par::log(debug::par::Id::FuncDef,
                                    "Generic param: " + param_name + " : " + constraint_str,
                                    debug::Level::Debug);
                }
            } else {
                // 制約なし: <T>
                names.push_back(param_name);
                params.emplace_back(param_name);

                debug::par::log(debug::par::Id::FuncDef, "Generic param: " + param_name,
                                debug::Level::Debug);
            }

        } while (consume_if(TokenKind::Comma));

        expect(TokenKind::Gt);  // '>' を消費

        return {names, params};
    }

    // 後方互換性のため維持
    std::vector<std::string> parse_generic_params() {
        auto [names, _] = parse_generic_params_v2();
        return names;
    }

    // 型解析
    ast::TypePtr parse_type() {
        ast::TypePtr type;

        // const修飾子をチェック（借用システム Phase 2）
        // const int* p や const T のような形式をサポート
        bool has_const = consume_if(TokenKind::KwConst);

        // ポインタ/参照
        if (consume_if(TokenKind::Star)) {
            type = ast::make_pointer(parse_type());
            // constポインタの場合、要素型にconst修飾を設定
            if (has_const && type->element_type) {
                type->element_type->qualifiers.is_const = true;
            }
            return type;
        }
        if (consume_if(TokenKind::Amp)) {
            type = ast::make_reference(parse_type());
            return type;
        }

        // 配列 [T] or [T; N]
        if (consume_if(TokenKind::LBracket)) {
            auto elem = parse_type();
            std::optional<uint32_t> size;
            if (consume_if(TokenKind::Semicolon)) {
                if (check(TokenKind::IntLiteral)) {
                    size = static_cast<uint32_t>(current().get_int());
                    advance();
                }
            }
            expect(TokenKind::RBracket);
            return ast::make_array(std::move(elem), size);
        }

        // プリミティブ型
        ast::TypePtr base_type;
        switch (current().kind) {
            case TokenKind::KwAuto:
                advance();
                // autoは型推論を意味する
                base_type = std::make_shared<ast::Type>(ast::TypeKind::Inferred);
                break;
            case TokenKind::KwTypeof: {
                // typeof(式) - 式の型を取得
                advance();
                expect(TokenKind::LParen);
                auto expr = parse_expr();
                expect(TokenKind::RParen);
                // 式の型をtype checkerで解決する必要があるため、
                // ここではInferred型として扱い、後で解決する
                // exprへの参照を保持するためにInferredWithExpr型が必要だが、
                // 簡略化のため、TypeAliasを一時的に使用
                auto type = std::make_shared<ast::Type>(ast::TypeKind::Inferred);
                type->name = "__typeof__";
                // 注: 完全な実装には、式への参照を型に保持する機能が必要
                base_type = type;
                break;
            }
            case TokenKind::KwVoid:
                advance();
                base_type = ast::make_void();
                break;
            case TokenKind::KwBool:
                advance();
                base_type = std::make_shared<ast::Type>(ast::TypeKind::Bool);
                break;
            case TokenKind::KwTiny:
                advance();
                base_type = std::make_shared<ast::Type>(ast::TypeKind::Tiny);
                break;
            case TokenKind::KwShort:
                advance();
                base_type = std::make_shared<ast::Type>(ast::TypeKind::Short);
                break;
            case TokenKind::KwInt:
                advance();
                base_type = ast::make_int();
                break;
            case TokenKind::KwLong:
                advance();
                base_type = ast::make_long();
                break;
            case TokenKind::KwUtiny:
                advance();
                base_type = std::make_shared<ast::Type>(ast::TypeKind::UTiny);
                break;
            case TokenKind::KwUshort:
                advance();
                base_type = std::make_shared<ast::Type>(ast::TypeKind::UShort);
                break;
            case TokenKind::KwUint:
                advance();
                base_type = std::make_shared<ast::Type>(ast::TypeKind::UInt);
                break;
            case TokenKind::KwUlong:
                advance();
                base_type = std::make_shared<ast::Type>(ast::TypeKind::ULong);
                break;
            case TokenKind::KwIsize:
                advance();
                base_type = ast::make_isize();
                break;
            case TokenKind::KwUsize:
                advance();
                base_type = ast::make_usize();
                break;
            case TokenKind::KwFloat:
                advance();
                base_type = ast::make_float();
                break;
            case TokenKind::KwDouble:
                advance();
                base_type = ast::make_double();
                break;
            case TokenKind::KwUfloat:
                advance();
                base_type = ast::make_ufloat();
                break;
            case TokenKind::KwUdouble:
                advance();
                base_type = ast::make_udouble();
                break;
            case TokenKind::KwChar:
                advance();
                base_type = ast::make_char();
                break;
            case TokenKind::KwString:
                advance();
                base_type = ast::make_string();
                break;
            case TokenKind::KwCstring:
                advance();
                base_type = ast::make_cstring();
                break;
            default:
                break;
        }

        // 関数ポインタ型: int*(int, int) または ポインタ型: void*
        // base_typeがあり、次が * の場合
        if (base_type && check(TokenKind::Star)) {
            // *( パターンのチェック - 関数ポインタ
            if (pos_ + 1 < tokens_.size() && tokens_[pos_ + 1].kind == TokenKind::LParen) {
                advance();  // consume *
                advance();  // consume (

                std::vector<ast::TypePtr> param_types;
                if (!check(TokenKind::RParen)) {
                    do {
                        param_types.push_back(parse_type());
                    } while (consume_if(TokenKind::Comma));
                }
                expect(TokenKind::RParen);

                return ast::make_function_ptr(std::move(base_type), std::move(param_types));
            } else {
                // 単純なポインタ型: void*, int*, etc.
                advance();  // consume *
                // const修飾子がある場合（const int* = pointer to const int）
                // base_typeにconst修飾を設定
                if (has_const) {
                    base_type->qualifiers.is_const = true;
                }
                return ast::make_pointer(std::move(base_type));
            }
        }

        if (base_type) {
            return base_type;
        }

        // ユーザー定義型（ジェネリクス対応）
        if (check(TokenKind::Ident)) {
            std::string name = current_text();
            advance();

            // namespace::Type 形式をサポート
            while (check(TokenKind::ColonColon)) {
                advance();  // :: を消費
                if (!check(TokenKind::Ident)) {
                    error("Expected identifier after '::'");
                    return ast::make_error();
                }
                name += "::" + current_text();
                advance();
            }

            // ジェネリック型引数をチェック（例: Vec<int>, Map<K, V>）
            if (check(TokenKind::Lt)) {
                advance();  // '<' を消費

                std::vector<ast::TypePtr> type_args;

                // 型引数をパース
                do {
                    type_args.push_back(parse_type());
                } while (consume_if(TokenKind::Comma));

                // '>'を期待（ネストジェネリクス対応: GtGtも分割処理）
                consume_gt_in_type_context();

                // ジェネリック型として返す
                auto type = ast::make_named(name);
                type->type_args = std::move(type_args);

                // 関数ポインタ型: Vec<T>*(int, int) または ポインタ型: Vec<T>*
                // 演算子戻り値型の場合は*を型サフィックスとしてでなく演算子として扱う
                if (check(TokenKind::Star) && !in_operator_return_type_) {
                    if (pos_ + 1 < tokens_.size() && tokens_[pos_ + 1].kind == TokenKind::LParen) {
                        advance();  // consume *
                        advance();  // consume (

                        std::vector<ast::TypePtr> param_types;
                        if (!check(TokenKind::RParen)) {
                            do {
                                param_types.push_back(parse_type());
                            } while (consume_if(TokenKind::Comma));
                        }
                        expect(TokenKind::RParen);

                        return ast::make_function_ptr(std::move(type), std::move(param_types));
                    } else {
                        // 単純なポインタ型: Vec<T>*
                        advance();  // consume *
                        return ast::make_pointer(std::move(type));
                    }
                }

                return type;
            }

            // 関数ポインタ型: MyStruct*(int, int) または ポインタ型: MyStruct*
            // 演算子戻り値型の場合は*や&を型サフィックスとしてでなく演算子として扱う
            if (check(TokenKind::Star) && !in_operator_return_type_) {
                auto named_type = ast::make_named(name);
                if (pos_ + 1 < tokens_.size() && tokens_[pos_ + 1].kind == TokenKind::LParen) {
                    advance();  // consume *
                    advance();  // consume (

                    std::vector<ast::TypePtr> param_types;
                    if (!check(TokenKind::RParen)) {
                        do {
                            param_types.push_back(parse_type());
                        } while (consume_if(TokenKind::Comma));
                    }
                    expect(TokenKind::RParen);

                    return ast::make_function_ptr(std::move(named_type), std::move(param_types));
                } else {
                    // 単純なポインタ型: MyStruct*
                    advance();  // consume *
                    return ast::make_pointer(std::move(named_type));
                }
            }
            // 演算子戻り値型の場合は&を型サフィックスとして消費しない
            if (check(TokenKind::Amp) && !in_operator_return_type_) {
                auto named_type = ast::make_named(name);
                advance();  // consume &
                return ast::make_reference(std::move(named_type));
            }

            return ast::make_named(name);
        }

        error("Expected type");
        return ast::make_error();
    }

    // C++スタイルの配列サイズ指定とポインタをチェック (T[N], T*, T*[N], T[N]*)
    // parse_type()の呼び出し後に使用
    // 再帰的に処理して複合型をサポート
    ast::TypePtr check_array_suffix(ast::TypePtr base_type) {
        // T[N] 形式をチェック
        if (consume_if(TokenKind::LBracket)) {
            std::optional<uint32_t> size;
            std::string size_param_name;

            if (check(TokenKind::IntLiteral)) {
                // 数値リテラル: int[5]
                size = static_cast<uint32_t>(current().get_int());
                advance();
            } else if (check(TokenKind::Ident)) {
                // 識別子（定数パラメータ）: T[SIZE]
                size_param_name = std::string(current().get_string());
                advance();
            }
            // 空の場合はスライス: T[]

            expect(TokenKind::RBracket);

            ast::TypePtr arr_type;
            if (!size_param_name.empty()) {
                arr_type = ast::make_array_with_param(std::move(base_type), size_param_name);
            } else {
                arr_type = ast::make_array(std::move(base_type), size);
            }

            // 配列の後にさらにサフィックスがあるかチェック（例: int[2]*）
            return check_array_suffix(std::move(arr_type));
        }
        // T* 形式をチェック（C++スタイルのポインタ）
        if (consume_if(TokenKind::Star)) {
            auto ptr_type = ast::make_pointer(std::move(base_type));
            // ポインタの後にさらにサフィックスがあるかチェック（例: int*[2]）
            return check_array_suffix(std::move(ptr_type));
        }
        return base_type;
    }

    // モジュール関連パーサ（parser_module.cppで実装）
    ast::DeclPtr parse_module();
    ast::DeclPtr parse_namespace();
    ast::DeclPtr parse_import_stmt(std::vector<ast::AttributeNode> attributes = {});
    ast::DeclPtr parse_export();
    ast::DeclPtr parse_use(std::vector<ast::AttributeNode> attributes = {});
    ast::DeclPtr parse_macro(bool is_exported = false);
    ast::DeclPtr parse_extern(std::vector<ast::AttributeNode> attributes = {});
    ast::DeclPtr parse_extern_decl(std::vector<ast::AttributeNode> attributes = {});
    std::unique_ptr<ast::FunctionDecl> parse_extern_func_decl();
    ast::TypePtr parse_extern_type();
    std::vector<ast::Param> parse_extern_params();
    ast::AttributeNode parse_directive();
    ast::AttributeNode parse_attribute();
    ast::DeclPtr parse_const_decl(bool is_export = false,
                                  std::vector<ast::AttributeNode> attributes = {});
    ast::DeclPtr parse_constexpr();
    ast::DeclPtr parse_template_decl();
    ast::DeclPtr parse_enum_decl(bool is_export = false,
                                 std::vector<ast::AttributeNode> attributes = {});
    ast::DeclPtr parse_typedef_decl(bool is_export = false,
                                    std::vector<ast::AttributeNode> attributes = {});
    ast::DeclPtr parse_impl_export(std::vector<ast::AttributeNode> attributes = {});

    // ユーティリティ
    const Token& current() const { return tokens_[pos_]; }
    const Token& previous() const { return tokens_[pos_ > 0 ? pos_ - 1 : 0]; }
    bool is_at_end() const { return current().kind == TokenKind::Eof; }

    bool check(TokenKind kind) const { return current().kind == kind; }
    TokenKind peek_kind(size_t offset = 1) const {
        size_t idx = pos_ + offset;
        if (idx >= tokens_.size()) {
            return TokenKind::Eof;
        }
        return tokens_[idx].kind;
    }
    bool is_attribute_start() const {
        return check(TokenKind::At) ||
               (check(TokenKind::Hash) && peek_kind() == TokenKind::LBracket);
    }

    Token advance() {
        if (!is_at_end())
            ++pos_;
        return previous();
    }

    bool consume_if(TokenKind kind) {
        if (check(kind)) {
            advance();
            return true;
        }
        return false;
    }

    void expect(TokenKind kind) {
        if (!consume_if(kind)) {
            error(std::string("Expected '") + token_kind_to_string(kind) + "'");
        }
    }

    // ネストジェネリクス対応: 型パース中に'>'を消費
    // GtGt（>>）トークンが来た場合、1つの'>'として消費し、残りを保持
    void consume_gt_in_type_context() {
        // 前回のGtGtから残った'>'がある場合
        if (pending_gt_count_ > 0) {
            pending_gt_count_--;
            return;
        }

        // 通常の'>'トークン
        if (consume_if(TokenKind::Gt)) {
            return;
        }

        // '>>'トークン - 分割して処理
        if (check(TokenKind::GtGt)) {
            advance();              // GtGtを消費
            pending_gt_count_ = 1;  // 残りの'>'を保持
            return;
        }

        // '>>>'トークンがあればさらに対応（将来の拡張用）
        // 現時点では未サポート

        error("Expected '>'");
    }

    std::string expect_ident() {
        if (check(TokenKind::Ident)) {
            std::string name(current().get_string());
            advance();
            return name;
        }
        error("Expected identifier, got '" + std::string(current().get_string()) + "'");
        advance();  // エラー回復：1トークン進める
        return "<error>";
    }

    std::string current_text() const {
        if (check(TokenKind::Ident)) {
            return std::string(current().get_string());
        }
        return "";
    }

    void error(const std::string& msg) {
        // 同じ行で連続したエラーを抑制
        uint32_t current_line =
            current().start;  // Spanはバイトオフセットだが、行単位で簡易チェック
        if (current_line == last_error_line_ && !diagnostics_.empty()) {
            // 同じ位置での連続エラーを無視
            return;
        }
        last_error_line_ = current_line;

        debug::par::log(debug::par::Id::Error, msg, debug::Level::Error);
        diagnostics_.emplace_back(DiagKind::Error, Span{current().start, current().end}, msg);
    }

    void synchronize() {
        // 無限ループ防止のための最大スキップ数
        const int MAX_SKIP = 1000;
        int skipped = 0;

        // 現在の位置を記憶（無限ループ検出用）
        size_t last_pos = pos_;

        advance();
        while (!is_at_end() && skipped < MAX_SKIP) {
            // 位置が進んでいるか確認
            if (pos_ == last_pos) {
                // 位置が進んでいない場合は強制的に進める
                if (pos_ < tokens_.size() - 1) {
                    pos_++;
                } else {
                    break;  // 既に最後にいる
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
                case TokenKind::Hash:  // ディレクティブの開始位置で停止
                    return;
                // 型キーワードでも停止
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
            error("Parser stuck in synchronization - too many tokens skipped");
        }
    }

    bool is_type_start();

    std::vector<Token> tokens_;
    size_t pos_;
    std::vector<Diagnostic> diagnostics_;
    uint32_t last_error_line_ = 0;  // 連続エラー抑制用
    int pending_gt_count_ = 0;  // ネストジェネリクス用: GtGtから分割された残りの'>'カウント
    bool in_operator_return_type_ =
        false;  // 演算子戻り値型パース中フラグ（*&の型サフィックス抑制）
};

}  // namespace cm
