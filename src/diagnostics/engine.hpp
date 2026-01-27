#pragma once

// ============================================================
// 診断エンジン - 診断の報告と出力
// ============================================================

#include "catalog.hpp"
#include "common/diagnostics.hpp"

#include <algorithm>
#include <iostream>
#include <vector>

namespace cm {
namespace diagnostics {

/// 診断エンジン - 診断の報告と表示を管理
class DiagnosticEngine {
   public:
    DiagnosticEngine() {
        // カタログの初期化を確実に行う
        (void)DiagnosticCatalog::instance();
    }

    /// 診断を報告（IDとメッセージ引数で）
    void report(const std::string& id, Span span, const std::vector<std::string>& args = {}) {
        auto* def = DiagnosticCatalog::instance().get(id);
        if (!def)
            return;

        // 無効化されている場合はスキップ
        if (disabled_ids_.count(id) > 0)
            return;

        // カスタムレベルがあれば使用
        DiagnosticLevel level = def->default_level;
        auto it = level_overrides_.find(id);
        if (it != level_overrides_.end()) {
            level = it->second;
        }

        diagnostics_.push_back(
            {id, def->name, level, span, format_message(def->message_template, args)});
    }

    /// 直接メッセージで報告
    void report_direct(const std::string& id, const std::string& name, DiagnosticLevel level,
                       Span span, const std::string& message) {
        diagnostics_.push_back({id, name, level, span, message});
    }

    /// エラーがあるかチェック
    bool has_errors() const {
        return std::any_of(diagnostics_.begin(), diagnostics_.end(),
                           [](const auto& d) { return d.level == DiagnosticLevel::Error; });
    }

    /// 警告数を取得
    size_t warning_count() const {
        return std::count_if(diagnostics_.begin(), diagnostics_.end(),
                             [](const auto& d) { return d.level == DiagnosticLevel::Warning; });
    }

    /// 診断数を取得
    size_t count() const { return diagnostics_.size(); }

    /// 診断を取得
    const std::vector<Diagnostic>& diagnostics() const { return diagnostics_; }

    /// 結果を表示
    void print(const Source& source, std::ostream& out = std::cout) const {
        const char* reset = "\033[0m";
        const char* bold = "\033[1m";

        for (const auto& diag : diagnostics_) {
            auto loc = source.get_line_column(diag.span.start);

            // ファイル:行:列
            out << bold << source.filename() << ":" << loc.line << ":" << loc.column << ": "
                << reset;

            // 重大度とルールID
            const char* color = level_to_color(diag.level);
            out << bold << color << level_to_string(diag.level) << reset << "[" << diag.id << "]: ";
            out << diag.message << "\n";

            // ソース行を表示
            auto line = source.get_line(loc.line);
            out << "    " << line << "\n";

            // キャレット
            out << "    ";
            for (uint32_t i = 1; i < loc.column; ++i) {
                out << ' ';
            }
            out << bold << color << "^" << reset << "\n\n";
        }

        // サマリー
        size_t errors = 0, warnings = 0, hints = 0;
        for (const auto& d : diagnostics_) {
            switch (d.level) {
                case DiagnosticLevel::Error:
                    ++errors;
                    break;
                case DiagnosticLevel::Warning:
                    ++warnings;
                    break;
                default:
                    ++hints;
                    break;
            }
        }

        if (errors + warnings + hints > 0) {
            out << bold;
            if (errors > 0) {
                out << "\033[31merror\033[0m: " << errors << " 件";
            }
            if (warnings > 0) {
                if (errors > 0)
                    out << ", ";
                out << "\033[33mwarning\033[0m: " << warnings << " 件";
            }
            if (hints > 0) {
                if (errors + warnings > 0)
                    out << ", ";
                out << "\033[34mhint\033[0m: " << hints << " 件";
            }
            out << reset << "\n";
        }
    }

    /// ルールを無効化
    void disable(const std::string& id) { disabled_ids_.insert(id); }

    /// レベルをオーバーライド
    void set_level(const std::string& id, DiagnosticLevel level) { level_overrides_[id] = level; }

    /// 診断をクリア
    void clear() { diagnostics_.clear(); }

   private:
    std::vector<Diagnostic> diagnostics_;
    std::unordered_set<std::string> disabled_ids_;
    std::unordered_map<std::string, DiagnosticLevel> level_overrides_;
};

}  // namespace diagnostics
}  // namespace cm
