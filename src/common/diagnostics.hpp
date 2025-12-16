#pragma once

#include "source.hpp"
#include "span.hpp"

#include <iostream>
#include <sstream>
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
    void print(std::ostream& out = std::cerr) const {
        for (const auto& diag : diagnostics_) {
            print_diagnostic(out, diag);
        }

        // サマリー
        if (error_count_ > 0 || warning_count_ > 0) {
            out << "\n";
            if (error_count_ > 0) {
                out << "error: " << error_count_ << " error(s)";
            }
            if (warning_count_ > 0) {
                if (error_count_ > 0)
                    out << ", ";
                out << warning_count_ << " warning(s)";
            }
            out << " generated.\n";
        }
    }

    /// 診断メッセージをクリア
    void clear() {
        diagnostics_.clear();
        error_count_ = 0;
        warning_count_ = 0;
    }

   private:
    void print_diagnostic(std::ostream& out, const Diagnostic& diag) const {
        // 位置情報
        auto loc = source_.get_line_column(diag.span.start);

        // 色付け用のANSIコード
        const char* color_reset = "\033[0m";
        const char* color_bold = "\033[1m";
        const char* color_red = "\033[31m";
        const char* color_yellow = "\033[33m";
        const char* color_cyan = "\033[36m";

        // ファイル:行:列
        out << color_bold << source_.filename() << ":" << loc.line << ":" << loc.column << ": "
            << color_reset;

        // 重大度
        switch (diag.severity) {
            case Severity::Error:
                out << color_bold << color_red << "error: " << color_reset;
                break;
            case Severity::Warning:
                out << color_bold << color_yellow << "warning: " << color_reset;
                break;
            case Severity::Note:
                out << color_bold << color_cyan << "note: " << color_reset;
                break;
        }

        // メッセージ
        out << color_bold << diag.message << color_reset << "\n";

        // ソース行を表示
        auto line = source_.get_line(loc.line);
        out << "    " << line << "\n";

        // キャレット（^）を表示
        out << "    ";
        for (uint32_t i = 1; i < loc.column; ++i) {
            out << ' ';
        }
        out << color_bold << color_red << "^" << color_reset << "\n";
    }

    const Source& source_;
    std::vector<Diagnostic> diagnostics_;
    size_t error_count_;
    size_t warning_count_;
};

}  // namespace cm
