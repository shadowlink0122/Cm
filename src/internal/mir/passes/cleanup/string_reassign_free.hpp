#pragma once

#include "internal/mir/passes/core/base.hpp"

#include <string>

namespace cm::mir::opt {

// ============================================================
// 文字列再代入時の旧バッファ解放（C12の変数上書き）
// ============================================================
// s = s + "x" のような文字列ローカルの再代入は旧バッファを解放せずリークする。
// このパスは以下の3条件を全て満たすローカルに限り、再代入の直前へ旧値のcm_string_freeを挿入する。
// (1) 全ての定義がfresh所有バッファ（cm_string_concat・cm_*_to_string等の新規確保結果で、
//     その一時が当該コピー以外で消費されていない）
// (2) エイリアスされない（他ローカルへのUseコピー・保持しうる呼び出しへの引き渡し・
//     Aggregate/Cast/Ref・投影使用・Asmが一切ない。読み取り専用ランタイム・returnは許容）
// (3) 再代入地点への到達定義が全てfreshで未初期化を含まない（リテラル初期化が混ざる
//     アキュムレータ等は保守的にスキップ。到達定義解析で判定）
// freshバッファ同士は生存期間が重なる別mallocなので旧値と新値のポインタ一致は起きず、
// 挿入位置は定義文の直前（RHSの読み取り完了後）のため解放後の読みも構造的に発生しない
class StringReassignFree : public OptimizationPass {
   public:
    std::string name() const override { return "String Reassign Free"; }

    bool run(MirFunction& func) override;
};

}  // namespace cm::mir::opt
