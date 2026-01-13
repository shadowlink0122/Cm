#pragma once

// ============================================================
// 診断レベル定義
// ============================================================

namespace cm {
namespace diagnostics {

/// 診断レベル
enum class DiagnosticLevel {
    // コンパイルを停止
    Error = 0,  // 構文エラー、型エラーなど

    // コンパイルは継続するが警告
    Warning = 1,  // 未使用変数、到達不可能コードなど

    // Lintレベル（オプショナル）
    Suggestion = 2,  // スタイル違反、改善提案
    Hint = 3,        // ベストプラクティス、パフォーマンス

    // 内部用
    Note = 4,  // エラーの補足情報
    Help = 5   // 修正方法の提案
};

/// 検出段階
enum class DetectionStage {
    Lexer,      // 字句解析
    Parser,     // 構文解析
    Semantic,   // 意味解析
    TypeCheck,  // 型検査
    Lint,       // Lint専用
    Codegen     // コード生成
};

/// レベルを文字列に変換
inline const char* level_to_string(DiagnosticLevel level) {
    switch (level) {
        case DiagnosticLevel::Error:
            return "error";
        case DiagnosticLevel::Warning:
            return "warning";
        case DiagnosticLevel::Suggestion:
            return "suggestion";
        case DiagnosticLevel::Hint:
            return "hint";
        case DiagnosticLevel::Note:
            return "note";
        case DiagnosticLevel::Help:
            return "help";
        default:
            return "unknown";
    }
}

/// レベルに対応するANSI色コード
inline const char* level_to_color(DiagnosticLevel level) {
    switch (level) {
        case DiagnosticLevel::Error:
            return "\033[31m";  // 赤
        case DiagnosticLevel::Warning:
            return "\033[33m";  // 黄
        case DiagnosticLevel::Suggestion:
            return "\033[36m";  // シアン
        case DiagnosticLevel::Hint:
            return "\033[34m";  // 青
        case DiagnosticLevel::Note:
            return "\033[37m";  // 白
        case DiagnosticLevel::Help:
            return "\033[32m";  // 緑
        default:
            return "\033[0m";
    }
}

}  // namespace diagnostics
}  // namespace cm
