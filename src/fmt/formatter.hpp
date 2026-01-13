#pragma once

// ============================================================
// Formatter - DiagnosticEngineを使用
// ============================================================

#include "diagnostics/engine.hpp"
#include "frontend/ast/nodes.hpp"

#include <fstream>

namespace cm {
namespace fmt {

/// フォーマット結果
struct FormatResult {
    std::string formatted_code;
    bool modified = false;
    size_t fixes_applied = 0;
};

/// Formatter - lint + 自動修正
class Formatter {
   public:
    Formatter() : engine_() {}

    /// コードをフォーマット
    FormatResult format(const ast::Program& program, const std::string& original_code) {
        FormatResult result;
        result.formatted_code = original_code;

        // TODO: フォーマットロジックを実装
        // 1. Lintルールを実行
        // 2. 自動修正可能な問題を収集
        // 3. 修正を適用

        (void)program;  // unused warning回避

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

    /// チェックのみ
    bool check(const ast::Program& program, const std::string& code) {
        auto result = format(program, code);
        return result.formatted_code == code;
    }

    /// 結果を表示
    void print(const Source& source, std::ostream& out = std::cout) const {
        engine_.print(source, out);

        // 修正サマリーも表示
        // TODO: 修正数を表示
    }

    /// DiagnosticEngineへのアクセス
    diagnostics::DiagnosticEngine& engine() { return engine_; }

   private:
    diagnostics::DiagnosticEngine engine_;
};

}  // namespace fmt
}  // namespace cm
