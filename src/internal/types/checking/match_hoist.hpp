#pragma once

#include "internal/syntax/ast/nodes.hpp"

#include <functional>

namespace cm {

// 式形式matchのscrutineeに関数呼び出しを含む式が書かれた場合、文の直前に一時変数（auto推論のLetStmt）へ退避してscrutineeを識別子参照に書き換える。
// HIRの三項演算子脱糖はscrutineeをアームごとにクローンするため、クローンできないCallExpr等は単一評価を保証できず誤動作していた（型チェック前に実行するASTプリパス）
void hoist_match_call_scrutinees(ast::Program& program);

// 文字列リテラルの補間プレースホルダを実ASTへ脱糖する（実装はutils/interp.cpp）。
// hoistパスと型検査の両方から呼ばれ、interp_scannedフラグで冪等
void desugar_string_interpolation(ast::LiteralExpr& lit, const Span& span);

}  // namespace cm
