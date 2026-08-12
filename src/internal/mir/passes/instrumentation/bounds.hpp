#pragma once

// --sanitize=bounds: MIRレベルのスライス境界検査（M1）
// スライスアクセスのランタイム関数（cm_slice_get_* / cm_slice_get_element_ptr / cm_slice_delete）
// 呼び出しの直前に index の範囲検査（0 <= index < len）を挿入し、違反時は
// cm_bounds_error(index, len) へ分岐して即時終了する。
// MIRへ挿入するためnative/wasm/jit/jsの全実行系で同一の検出動作になる
// （従来はOOB読み=センチネル0/undefined、OOB書き=SIGSEGV/不定書き込み/自動拡張と分裂し、
// LLVMのBoundsCheckingPassは固定長配列にしか効かなかった）。

#include "internal/mir/passes/core/base.hpp"

namespace cm::mir::opt {

// スライスアクセス呼び出しへ境界ガードを挿入する計装パス
// 最適化パイプラインとは独立に、MIR最適化の完了後へ1回だけ適用する
class BoundsCheckInstrumentation : public OptimizationPass {
   public:
    std::string name() const override { return "BoundsCheckInstrumentation"; }
    bool run(MirFunction& func) override;
};

// プログラム全体へ計装を適用する（build.cppのMIR最適化後に呼び出す）
void instrument_bounds_checks(MirProgram& program);

}  // namespace cm::mir::opt
