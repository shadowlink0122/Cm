#pragma once

// SVターゲット向け: implメソッドの構造体ポインタ引数（self等）を値渡しへ変換するMIR前処理
// SVにはポインタが存在しないため、読み取り専用のselfを構造体の値渡しへ書き換えることでinterface/implメソッドを合成可能にする。
// 値渡しで意味が変わるケース（selfへの書き込み・ポインタ値の逃避・動的ディスパッチ）は明確な診断でエラーにする

#include "internal/mir/nodes.hpp"

#include <string>
#include <vector>

namespace cm::codegen::sv {

// 構造体ポインタ引数の値渡し化を適用する。エラーメッセージのリストを返す（空なら成功）
std::vector<std::string> lower_self_pointer_params(mir::MirProgram& program);

}  // namespace cm::codegen::sv
