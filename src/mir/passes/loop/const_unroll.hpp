#pragma once

#include "../core/base.hpp"

namespace cm::mir::opt {

// ============================================================
// 定数トリップカウントループの静的展開（SVバックエンド用）
// ============================================================
// `for (uint i = 0; i < 4; i = i + 1) { ... }` のように
// 初期値・境界・増分がすべて定数のループを、ループ構造を持たない
// 直列のブロック列に展開する。
//
// SVバックエンドでは while ループとして出力されるが、合成ツールは
// 動的な while を展開できないことが多い。本パスでMIRレベルで
// 静的展開することで generate/genvar 相当の繰り返し構造生成を実現する
// （sv_backend_missing_features.md 項目6）。
class ConstantLoopUnroll : public OptimizationPass {
   public:
    std::string name() const override { return "Constant Loop Unroll"; }

    bool run(MirFunction& func) override;

   private:
    // 1つのループを検出して展開する（変更があればtrue）
    bool try_unroll_one(MirFunction& func);
};

// SVターゲット用エントリポイント: プログラム内の全関数に適用する
void unroll_constant_loops(MirProgram& program);

}  // namespace cm::mir::opt
