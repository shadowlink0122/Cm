#pragma once

// --sanitize=undefined: MIRレベルのCm独自ランタイム検査（ゼロ除算・null参照）
// LLVM等のバックエンドに依存せずMIRへ検査コードを挿入するため、native/wasm/jitの全LLVM系実行系で同一の検出動作になる。
// 検出時は panic("runtime error: ...") へ分岐して即時終了する（メッセージ出力と非0終了はpanicランタイムの挙動に従う）

#include "internal/mir/passes/core/base.hpp"

namespace cm::mir::opt {

// ゼロ除算（整数Div/Mod）とnullポインタ参照（Derefを含むPlaceアクセス）の直前にガードを挿入する計装パス
// 最適化パイプラインとは独立に、MIR最適化の完了後へ1回だけ適用する（挿入したガードが定数伝播で消されるのを防ぐ）
class UndefinedCheckInstrumentation : public OptimizationPass {
   public:
    std::string name() const override { return "UndefinedCheckInstrumentation"; }
    bool run(MirFunction& func) override;
};

// プログラム全体へ計装を適用する（build.cppのMIR最適化後に呼び出す）
void instrument_undefined_checks(MirProgram& program);

}  // namespace cm::mir::opt
