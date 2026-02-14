// ============================================================
// Diagnostics 実装
// ============================================================

#include "diagnostics.hpp"

namespace cm {

void Diagnostics::print(std::ostream& out) const {
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

void Diagnostics::print_diagnostic(std::ostream& out, const Diagnostic& diag) const {
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

}  // namespace cm
