#pragma once

// ============================================================
// 診断エンジン - 診断の報告と出力
// ============================================================

#include "catalog.hpp"
#include "common/diagnostics.hpp"

#include <iostream>
#include <unordered_set>
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
    void report(const std::string& id, Span span, const std::vector<std::string>& args = {});

    /// 直接メッセージで報告
    void report_direct(const std::string& id, const std::string& name, DiagnosticLevel level,
                       Span span, const std::string& message);

    /// エラーがあるかチェック
    bool has_errors() const;

    /// 警告数を取得
    size_t warning_count() const;

    /// 診断数を取得
    size_t count() const { return diagnostics_.size(); }

    /// 診断を取得
    const std::vector<Diagnostic>& diagnostics() const { return diagnostics_; }

    /// 結果を表示
    void print(const Source& source, std::ostream& out = std::cout) const;

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
