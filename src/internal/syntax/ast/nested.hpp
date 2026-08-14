#pragma once

#include "nodes.hpp"

namespace cm::ast {

// ネスト型宣言（struct/enum本体内のstruct/enum）をトップレベル宣言へ平坦化する。
// 内側の型はOuter::Inner名へ改名して外側宣言の直前に挿入し、本体内の非修飾型参照（Inner・Mid::Inner）を平坦名へ書き換える。
// パース直後（型チェック前）にコンパイルパイプラインから呼び出す（フォーマッタはトークンベースのため対象外）
void hoist_nested_types(Program& program);

}  // namespace cm::ast
