#pragma once

#include "../ast/types.hpp"
#include <string>
#include <set>
#include <map>

namespace cm {

// ============================================================
// ジェネリック型推論器
// ============================================================
class GenericInference {
public:
    // 型名がジェネリックパラメータかどうか判定
    static bool is_generic_param(const std::string& type_name,
                                 const std::set<std::string>& known_types) {
        // すでに既知の型なら具体型
        if (known_types.count(type_name)) {
            return false;
        }

        // 単一大文字のパターン（T, U, V, K, E, R など）
        if (type_name.length() == 1 && std::isupper(type_name[0])) {
            return true;
        }

        // 2文字の大文字パターン（KV, TV など）
        if (type_name.length() == 2 &&
            std::isupper(type_name[0]) &&
            std::isupper(type_name[1])) {
            return true;
        }

        // それ以外は具体型として扱う（エラーになる可能性）
        return false;
    }

    // 関数シグネチャからジェネリックパラメータを抽出
    static std::set<std::string> extract_generic_params(
        const ast::TypePtr& return_type,
        const std::vector<ast::Param>& params,
        const std::set<std::string>& known_types) {

        std::set<std::string> generic_params;

        // 戻り値型から抽出
        extract_from_type(return_type, generic_params, known_types);

        // パラメータ型から抽出
        for (const auto& param : params) {
            extract_from_type(param.type, generic_params, known_types);
        }

        return generic_params;
    }

private:
    // 型から再帰的にジェネリックパラメータを抽出
    static void extract_from_type(const ast::TypePtr& type,
                                  std::set<std::string>& generic_params,
                                  const std::set<std::string>& known_types) {
        if (!type) return;

        switch (type->kind) {
            case ast::TypeKind::Generic:
            case ast::TypeKind::Struct:
            case ast::TypeKind::Interface:
                // 型名をチェック
                if (is_generic_param(type->name, known_types)) {
                    generic_params.insert(type->name);
                }

                // 型引数も再帰的にチェック
                for (const auto& arg : type->type_args) {
                    extract_from_type(arg, generic_params, known_types);
                }
                break;

            case ast::TypeKind::Pointer:
            case ast::TypeKind::Reference:
            case ast::TypeKind::Array:
                // 要素型をチェック
                extract_from_type(type->element_type, generic_params, known_types);
                break;

            case ast::TypeKind::Function:
                // 関数型のパラメータと戻り値をチェック
                for (const auto& param : type->param_types) {
                    extract_from_type(param, generic_params, known_types);
                }
                extract_from_type(type->return_type, generic_params, known_types);
                break;

            default:
                // プリミティブ型などは何もしない
                break;
        }
    }
};

// ============================================================
// パーサー拡張（使用例）
// ============================================================
class EnhancedParser {
    std::set<std::string> known_types;
    std::map<std::string, std::set<std::string>> function_generics;

public:
    EnhancedParser() {
        // 組み込み型を登録
        known_types = {
            "void", "bool", "int", "uint", "short", "ushort",
            "long", "ulong", "float", "double", "char", "string",
            "tiny", "utiny",
            // 標準ライブラリ型
            "Vec", "Map", "Set", "Option", "Result",
            "String", "File", "Thread", "Mutex"
        };
    }

    // 関数解析時の処理
    ast::DeclPtr parse_function_with_inference() {
        // 通常の関数解析
        auto return_type = parse_type();
        std::string name = expect_ident();

        expect(TokenKind::LParen);
        auto params = parse_params();
        expect(TokenKind::RParen);

        // ジェネリックパラメータを推論
        auto generic_params = GenericInference::extract_generic_params(
            return_type, params, known_types
        );

        // where句の解析（オプション）
        std::map<std::string, std::vector<std::string>> constraints;
        if (check_keyword("where")) {
            constraints = parse_where_clause();
        }

        auto body = parse_block();

        // 関数を作成
        auto func = std::make_unique<ast::FunctionDecl>(
            std::move(name),
            std::move(params),
            std::move(return_type),
            std::move(body)
        );

        // ジェネリックパラメータを設定
        for (const auto& param : generic_params) {
            func->generic_params.push_back(param);
        }

        // 診断メッセージ（デバッグ/警告用）
        if (!generic_params.empty()) {
            std::string msg = "Inferred generic parameters: ";
            for (const auto& p : generic_params) {
                msg += p + " ";
            }
            debug_log(msg);

            // 単一大文字でない場合は警告
            for (const auto& p : generic_params) {
                if (p.length() > 1) {
                    warning("'" + p + "' is inferred as generic. " +
                           "Consider using single letter (e.g., 'T') " +
                           "or add explicit generic declaration.");
                }
            }
        }

        return std::make_unique<ast::Decl>(std::move(func));
    }

    // 構造体解析時の処理
    ast::DeclPtr parse_struct_with_inference() {
        expect(TokenKind::KwStruct);
        std::string name = expect_ident();

        // 明示的なジェネリックパラメータ（オプション）
        std::set<std::string> explicit_generics;
        if (consume_if(TokenKind::Lt)) {
            do {
                explicit_generics.insert(expect_ident());
            } while (consume_if(TokenKind::Comma));
            expect(TokenKind::Gt);
        }

        expect(TokenKind::LBrace);

        std::vector<ast::Field> fields;
        std::set<std::string> inferred_generics;

        // フィールドを解析しながらジェネリックパラメータを収集
        while (!check(TokenKind::RBrace)) {
            auto field_type = parse_type();
            std::string field_name = expect_ident();

            // 型からジェネリックパラメータを抽出
            std::set<std::string> field_generics;
            GenericInference::extract_from_type(
                field_type, field_generics, known_types
            );

            // 明示的に宣言されていないものを追加
            for (const auto& g : field_generics) {
                if (!explicit_generics.count(g)) {
                    inferred_generics.insert(g);
                }
            }

            fields.push_back(ast::Field{
                std::move(field_name),
                std::move(field_type)
            });

            expect(TokenKind::Semicolon);
        }

        expect(TokenKind::RBrace);

        auto struct_decl = std::make_unique<ast::StructDecl>(
            std::move(name),
            std::move(fields)
        );

        // すべてのジェネリックパラメータを設定
        for (const auto& g : explicit_generics) {
            struct_decl->generic_params.push_back(g);
        }
        for (const auto& g : inferred_generics) {
            struct_decl->generic_params.push_back(g);
        }

        return std::make_unique<ast::Decl>(std::move(struct_decl));
    }

private:
    // 既存のパーサーメソッド（省略）
    ast::TypePtr parse_type();
    std::string expect_ident();
    std::vector<ast::Param> parse_params();
    std::vector<ast::StmtPtr> parse_block();
    bool check(TokenKind kind);
    bool consume_if(TokenKind kind);
    void expect(TokenKind kind);
    bool check_keyword(const std::string& keyword);
    std::map<std::string, std::vector<std::string>> parse_where_clause();
    void debug_log(const std::string& msg);
    void warning(const std::string& msg);
};

}  // namespace cm