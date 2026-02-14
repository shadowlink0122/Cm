#pragma once

#include "source.hpp"
#include "span.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace cm {

/// 診断メッセージの重大度
enum class Severity {
    Error,
    Warning,
    Note,
};

/// 単一の診断メッセージ
struct Diagnostic {
    Severity severity;
    Span span;
    std::string message;

    Diagnostic(Severity sev, Span sp, std::string msg)
        : severity(sev), span(sp), message(std::move(msg)) {}
};

/// 診断メッセージを収集・表示するクラス
class Diagnostics {
   public:
    explicit Diagnostics(const Source& source)
        : source_(source), error_count_(0), warning_count_(0) {}

    /// エラーを追加
    void error(Span span, const std::string& message) {
        diagnostics_.emplace_back(Severity::Error, span, message);
        ++error_count_;
    }

    /// 警告を追加
    void warning(Span span, const std::string& message) {
        diagnostics_.emplace_back(Severity::Warning, span, message);
        ++warning_count_;
    }

    /// ノートを追加
    void note(Span span, const std::string& message) {
        diagnostics_.emplace_back(Severity::Note, span, message);
    }

    /// エラーがあるか
    bool has_errors() const { return error_count_ > 0; }

    /// エラー数を取得
    size_t error_count() const { return error_count_; }

    /// 警告数を取得
    size_t warning_count() const { return warning_count_; }

    /// 全ての診断メッセージを表示
    void print(std::ostream& out = std::cerr) const;

    /// 診断メッセージをクリア
    void clear() {
        diagnostics_.clear();
        error_count_ = 0;
        warning_count_ = 0;
    }

   private:
    void print_diagnostic(std::ostream& out, const Diagnostic& diag) const;

    const Source& source_;
    std::vector<Diagnostic> diagnostics_;
    size_t error_count_;
    size_t warning_count_;
};

}  // namespace cm
