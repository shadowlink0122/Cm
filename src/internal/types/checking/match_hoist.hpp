#pragma once

#include "internal/syntax/ast/nodes.hpp"

namespace cm {

// 式形式matchのscrutineeに関数呼び出しを含む式が書かれた場合、文の直前に一時変数（auto推論のLetStmt）へ退避してscrutineeを識別子参照に書き換える。
// HIRの三項演算子脱糖はscrutineeをアームごとにクローンするため、クローンできないCallExpr等は単一評価を保証できず誤動作していた（型チェック前に実行するASTプリパス）
void hoist_match_call_scrutinees(ast::Program& program);

}  // namespace cm
