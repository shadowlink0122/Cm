#pragma once

// ============================================================
// Formatter - 命名規則違反の自動修正
// ============================================================

#include "common/source.hpp"
#include "common/span.hpp"
#include "frontend/ast/decl.hpp"
#include "frontend/ast/nodes.hpp"
#include "frontend/ast/stmt.hpp"
#include "lint/config.hpp"
#include "lint/naming.hpp"

#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace cm {
namespace fmt {

/// 修正情報
struct Fix {
    Span span;                // 修正対象の位置
    std::string original;     // 元の名前
    std::string replacement;  // 修正後の名前
    std::string rule_id;      // 適用ルール
};

/// フォーマット結果
struct FormatResult {
    std::string formatted_code;
    bool modified = false;
    size_t fixes_applied = 0;
    std::vector<Fix> fixes;  // 適用された修正
};

/// Formatter - 命名規則違反を自動修正
class Formatter {
   public:
    Formatter() = default;

    /// 設定をセット
    void set_config(lint::ConfigLoader* config) { config_ = config; }

    /// コードをフォーマット（修正情報を収集して適用）
    FormatResult format(const ast::Program& program, const std::string& original_code) {
        FormatResult result;
        result.formatted_code = original_code;
        fixes_.clear();

        // ASTを走査して修正対象を収集
        collect_fixes(program);

        // 修正がなければそのまま返す
        if (fixes_.empty()) {
            return result;
        }

        // 修正を適用（後ろから適用してオフセットがずれないように）
        std::sort(fixes_.begin(), fixes_.end(),
                  [](const Fix& a, const Fix& b) { return a.span.start > b.span.start; });

        std::string code = original_code;
        for (const auto& fix : fixes_) {
            // 範囲チェック
            if (fix.span.start >= code.size() || fix.span.end > code.size()) {
                continue;
            }

            // 元の文字列が一致するか確認
            std::string existing = code.substr(fix.span.start, fix.span.end - fix.span.start);
            if (existing != fix.original) {
                continue;  // 不一致ならスキップ
            }

            // 置換
            code = code.substr(0, fix.span.start) + fix.replacement + code.substr(fix.span.end);
            result.fixes.push_back(fix);
            result.fixes_applied++;
        }

        result.formatted_code = code;
        result.modified = (result.fixes_applied > 0);

        return result;
    }

    /// ファイルをフォーマットして上書き
    bool format_file(const std::string& filepath, const ast::Program& program,
                     const std::string& code) {
        auto result = format(program, code);

        if (result.modified) {
            std::ofstream ofs(filepath);
            if (!ofs) {
                std::cerr << "エラー: ファイルを書き込めません: " << filepath << "\n";
                return false;
            }
            ofs << result.formatted_code;
        }

        return true;
    }

    /// チェックのみ（修正せずに差分を表示）
    bool check(const ast::Program& program, const std::string& code) {
        auto result = format(program, code);
        return result.formatted_code == code;
    }

    /// 修正サマリーを表示
    void print_summary(const FormatResult& result, std::ostream& out = std::cout) const {
        if (result.fixes_applied > 0) {
            out << "✓ " << result.fixes_applied << " 件の修正を適用しました\n";
            for (const auto& fix : result.fixes) {
                out << "  [" << fix.rule_id << "] " << fix.original << " → " << fix.replacement
                    << "\n";
            }
        }
    }

   private:
    lint::ConfigLoader* config_ = nullptr;
    std::vector<Fix> fixes_;

    /// ルールが有効かチェック
    bool is_rule_enabled(const std::string& rule_id) const {
        if (!config_)
            return true;  // 設定なしなら全て有効
        return !config_->is_disabled(rule_id);
    }

    /// ASTを走査して修正対象を収集
    void collect_fixes(const ast::Program& program) {
        for (const auto& decl : program.declarations) {
            visit_decl(*decl);
        }
    }

    /// 宣言を訪問
    void visit_decl(ast::Decl& decl) {
        // 関数宣言
        if (auto* func = decl.as<ast::FunctionDecl>()) {
            // L100: 関数名は snake_case
            if (is_rule_enabled("L100") && !lint::is_snake_case(func->name)) {
                if (!func->name_span.is_empty()) {
                    Fix fix;
                    fix.span = func->name_span;
                    fix.original = func->name;
                    fix.replacement = lint::to_snake_case(func->name);
                    fix.rule_id = "L100";
                    fixes_.push_back(fix);
                }
            }

            // 関数本体を訪問
            for (auto& s : func->body) {
                visit_stmt(*s);
            }
        }
        // 構造体宣言
        else if (auto* st = decl.as<ast::StructDecl>()) {
            // L103: 型名は PascalCase
            if (is_rule_enabled("L103") && !lint::is_pascal_case(st->name)) {
                if (!st->name_span.is_empty()) {
                    Fix fix;
                    fix.span = st->name_span;
                    fix.original = st->name;
                    fix.replacement = lint::to_pascal_case(st->name);
                    fix.rule_id = "L103";
                    fixes_.push_back(fix);
                }
            }
        }
    }

    /// 文を訪問
    void visit_stmt(ast::Stmt& stmt) {
        // ブロック文
        if (auto* block = stmt.as<ast::BlockStmt>()) {
            for (auto& s : block->stmts) {
                visit_stmt(*s);
            }
        }
        // Let文
        else if (auto* let = stmt.as<ast::LetStmt>()) {
            if (let->is_const) {
                // L102: 定数名は UPPER_SNAKE_CASE（snake_caseも許容）
                if (is_rule_enabled("L102") && !lint::is_upper_snake_case(let->name) &&
                    !lint::is_snake_case(let->name)) {
                    if (!let->name_span.is_empty()) {
                        Fix fix;
                        fix.span = let->name_span;
                        fix.original = let->name;
                        fix.replacement = lint::to_upper_snake_case(let->name);
                        fix.rule_id = "L102";
                        fixes_.push_back(fix);
                    }
                }
            } else {
                // L101: 変数名は snake_case
                if (is_rule_enabled("L101") && !lint::is_snake_case(let->name)) {
                    if (!let->name_span.is_empty()) {
                        Fix fix;
                        fix.span = let->name_span;
                        fix.original = let->name;
                        fix.replacement = lint::to_snake_case(let->name);
                        fix.rule_id = "L101";
                        fixes_.push_back(fix);
                    }
                }
            }
        }
        // If文
        else if (auto* if_stmt = stmt.as<ast::IfStmt>()) {
            for (auto& s : if_stmt->then_block) {
                visit_stmt(*s);
            }
            for (auto& s : if_stmt->else_block) {
                visit_stmt(*s);
            }
        }
        // While文
        else if (auto* while_stmt = stmt.as<ast::WhileStmt>()) {
            for (auto& s : while_stmt->body) {
                visit_stmt(*s);
            }
        }
        // For文
        else if (auto* for_stmt = stmt.as<ast::ForStmt>()) {
            for (auto& s : for_stmt->body) {
                visit_stmt(*s);
            }
        }
    }
};

}  // namespace fmt
}  // namespace cm
