#pragma once

// ============================================================
// with/derive自動実装のソース展開（derive-as-source-expansion 第1段）
// `struct S with Eq` から対応するCm実装（impl S for Eq { operator bool ==(...) }）のソースを合成し、
// パースして通常のパイプライン（型検査→HIR→MIR）へ流す。rustc_expandのderive展開に相当する。
// 手組みMIR生成（mir/lowering/auto_impl）は展開済みトレイトについて無効化され、生成コードはdropパス・診断・最適化を通常経路として受ける
// ============================================================

#include "internal/syntax/ast/nodes.hpp"

#include <string>

namespace cm::macro_expand {

// programに含まれるwith/derive指定からソース展開対象（第1段: Eq）の実装ソースを合成して返す。
// 対象が無ければ空文字列（regressionのスナップショット検証にも使用する）
std::string synthesize_derive_impls(const ast::Program& program);

// 合成ソースをパースしてprogramの宣言列へ追加する。戻り値は追加した宣言数
int expand_derives(ast::Program& program);

}  // namespace cm::macro_expand
