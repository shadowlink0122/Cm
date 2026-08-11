#pragma once

#include "internal/mir/nodes.hpp"

namespace cm::mir::opt {

// SVターゲット専用: 定数計算可能な浮動小数チェーンの整数畳み込み。
// const宣言では展開できる「整数定数×浮動小数リテラル→整数格納」（例: CLK_FREQ * 0.02）が、関数本体ではdouble型テンポラリが残りSV004（float非対応）で拒否されていた非対称を解消する。
// 値が全て定数のfloat演算チェーンをfloat→int縮小castの位置で整数定数へ畳み、死んだfloat定義文をNop化・未参照になったfloatローカルを整数型へ差し替える。
// 実行時値が絡むチェーンは書き換えず、従来どおりSV004の対象のまま残す
void sv_fold_constant_float_chains(MirProgram& program);

}  // namespace cm::mir::opt
