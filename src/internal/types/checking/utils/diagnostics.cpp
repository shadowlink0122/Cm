// ============================================================
// TypeChecker 実装 - 診断出力（error/warning）とconst推奨・未使用変数・未初期化使用のLint検査
// ============================================================

#include "internal/base/i18n.hpp"
#include "internal/types/type_checker.hpp"

#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace cm {

void TypeChecker::error(Span span, const std::string& msg) {
    debug::tc::log(debug::tc::Id::TypeError, msg, debug::Level::Error);
    diagnostics_.emplace_back(DiagKind::Error, span, msg);
}

void TypeChecker::warning(Span span, const std::string& msg) {
    debug::tc::log(debug::tc::Id::TypeError, msg, debug::Level::Warn);
    diagnostics_.emplace_back(DiagKind::Warning, span, msg);
}

// 変数が変更されたことをマーク
void TypeChecker::mark_variable_modified(const std::string& name) {
    modified_variables_.insert(name);
}

// const推奨警告をチェック（関数終了時に呼び出す）
void TypeChecker::check_const_recommendations() {
    // Lint警告が無効な場合はスキップ（クリアのみ実行）
    if (!enable_lint_warnings_) {
        modified_variables_.clear();
        non_const_variable_spans_.clear();
        return;
    }

    for (const auto& [name, span] : non_const_variable_spans_) {
        // 変更されていない変数に対してconst推奨警告
        if (modified_variables_.count(name) == 0) {
            warning(span, "Variable '" + name + "' is never modified, consider using 'const'");
        }
    }
    // 次の関数用にクリア
    modified_variables_.clear();
    non_const_variable_spans_.clear();
}

// 未使用変数チェック (W001)
void TypeChecker::check_unused_variables() {
    // 現在のスコープから未使用シンボルを取得
    auto unused_symbols = scopes_.current().get_unused_symbols();

    for (const auto& sym : unused_symbols) {
        // パラメータ名がアンダースコアで始まる場合は警告しない（意図的な未使用）
        if (!sym.name.empty() && sym.name[0] == '_') {
            continue;
        }
        // self は常に警告しない
        if (sym.name == "self") {
            continue;
        }

        warning(sym.span, "Variable '" + sym.name + "' is never used [W001]");
    }
}

void TypeChecker::mark_variable_initialized(const std::string& name) {
    initialized_variables_.insert(name);
}

void TypeChecker::check_uninitialized_use(const std::string& name, Span span) {
    // Lint警告が無効な場合はスキップ
    if (!enable_lint_warnings_) {
        return;
    }

    // 初期化されていない変数の使用を検出
    // 注: 関数パラメータは常に初期化されているとみなす
    auto sym = scopes_.current().lookup(name);
    if (!sym) {
        return;  // 変数が見つからない場合は他のエラー処理に任せる
    }

    // グローバル変数/定数は宣言時点で初期化される（SVの信号・定数を含む）ため対象外。
    // このチェックはローカル変数のフローのみを見る
    if (scopes_.global().has_local(name)) {
        return;
    }

    // initialized_variables_に含まれていない場合は診断を出す。
    // H6段階3: --strict（check/lint --strict）ではエラーへ昇格し、通常のcheck/lintは警告のまま
    if (initialized_variables_.count(name) == 0) {
        if (enable_naming_check_) {
            error(span, "Variable '" + name + "' may be used before initialization");
        } else {
            warning(span, "Variable '" + name + "' may be used before initialization");
        }
    }
}

}  // namespace cm
