#pragma once

// ============================================================
// 統一エラー型 - 全コンパイルフェーズで共通のエラー表現
// ============================================================

#include "span.hpp"

#include <iostream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace cm {

/// エラーの種類
enum class ErrorKind {
    Parse,    // パースエラー
    Type,     // 型チェックエラー
    Codegen,  // コード生成エラー
    IO,       // 入出力エラー
    Internal  // 内部エラー
};

/// 統一エラー型
struct Error {
    ErrorKind kind;
    std::string code;  // "E001", "SV002" など
    std::string message;
    Span span;

    /// パースエラーを生成
    static Error parse(const std::string& msg, Span s) {
        return Error{ErrorKind::Parse, "P001", msg, s};
    }

    /// 型エラーを生成
    static Error type(const std::string& msg, Span s) {
        return Error{ErrorKind::Type, "T001", msg, s};
    }

    /// コード生成エラーを生成
    static Error codegen(const std::string& code, const std::string& msg) {
        return Error{ErrorKind::Codegen, code, msg, Span::empty()};
    }

    /// IOエラーを生成
    static Error io(const std::string& msg) {
        return Error{ErrorKind::IO, "IO001", msg, Span::empty()};
    }

    /// 内部エラーを生成
    static Error internal(const std::string& msg) {
        return Error{ErrorKind::Internal, "INT001", msg, Span::empty()};
    }

    /// エラー種別の文字列表現
    std::string kind_string() const {
        switch (kind) {
            case ErrorKind::Parse:
                return "parse";
            case ErrorKind::Type:
                return "type";
            case ErrorKind::Codegen:
                return "codegen";
            case ErrorKind::IO:
                return "io";
            case ErrorKind::Internal:
                return "internal";
        }
        return "unknown";
    }
};

/// Result型 - 成功値またはエラーを保持
template <typename T>
using Result = std::variant<T, Error>;

/// Resultがエラーかチェック
template <typename T>
bool is_error(const Result<T>& r) {
    return std::holds_alternative<Error>(r);
}

/// Resultから値を取得（エラーの場合はデフォルト値）
template <typename T>
T unwrap_or(Result<T>&& r, T default_value) {
    if (auto* val = std::get_if<T>(&r)) {
        return std::move(*val);
    }
    return default_value;
}

/// Resultからエラーを取得（成功の場合はnullopt）
template <typename T>
const Error* get_error(const Result<T>& r) {
    return std::get_if<Error>(&r);
}

/// エラー収集クラス
class ErrorCollector {
   public:
    /// エラーを追加
    void add(Error e) {
        if (e.kind == ErrorKind::Internal) {
            // 内部エラーは警告として扱うオプション
            warnings_.push_back(std::move(e));
        } else {
            errors_.push_back(std::move(e));
        }
    }

    /// 警告を追加
    void add_warning(Error e) { warnings_.push_back(std::move(e)); }

    /// エラーがあるか
    bool has_errors() const { return !errors_.empty(); }

    /// エラー数
    size_t error_count() const { return errors_.size(); }

    /// 警告数
    size_t warning_count() const { return warnings_.size(); }

    /// 全エラーを取得
    const std::vector<Error>& errors() const { return errors_; }

    /// 全警告を取得
    const std::vector<Error>& warnings() const { return warnings_; }

    /// 全メッセージを報告
    void report_all(std::ostream& os) const {
        for (const auto& e : errors_) {
            os << "error[" << e.code << "]: " << e.message;
            if (!e.span.is_empty()) {
                os << " (at offset " << e.span.start << ")";
            }
            os << "\n";
        }
        for (const auto& w : warnings_) {
            os << "warning[" << w.code << "]: " << w.message;
            if (!w.span.is_empty()) {
                os << " (at offset " << w.span.start << ")";
            }
            os << "\n";
        }
    }

    /// クリア
    void clear() {
        errors_.clear();
        warnings_.clear();
    }

   private:
    std::vector<Error> errors_;
    std::vector<Error> warnings_;
};

}  // namespace cm
