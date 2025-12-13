#pragma once

// ============================================================
// MIR Lowering
// このファイルは互換性のために維持されています。
// 実際の実装は以下のファイルに分割されています：
// - lowering/lowering_base.hpp    : 基底クラスと共通機能
// - lowering/lowering_context.hpp : loweringコンテキスト
// - lowering/stmt_lowering.hpp    : 文のlowering
// - lowering/expr_lowering.hpp    : 式のlowering
// - lowering/monomorphization.hpp : モノモーフィゼーション
// - lowering/mir_lowering.hpp     : メインクラス
// ============================================================

#include "lowering/mir_lowering.hpp"

// 後方互換性のためのエイリアス
namespace cm::mir {
// MirLoweringは新しい場所から使用可能
using MirLowering = MirLowering;
}  // namespace cm::mir