#pragma once

#include "types.hpp"

#include <functional>
#include <string>

// ============================================================
// 暗黙変換の種別分類（受理と挿入の同表化）
// ============================================================
// checkerの受理判定（types_compatible）とMIR loweringの変換挿入（coerce_to_expected）が
// 「宛先型×値型がどの変換種に当たるか」を別々の条件分岐で再導出しており、新しい変換種・
// 新しいサイトが片側の連鎖から漏れる同型バグ（B2→Y系→Z5→Q3系譜）の温床だった。
// 本モジュールはその種別ディスパッチを単一の分類関数へ畳み、受理側は種別ごとの受理規則を、
// 挿入側は種別ごとの変換挿入を、同じ分類から導く（coercion統一ドライバの設計方針4）。
//
// 分類は「どの変換機構が担当するか」のディスパッチであり、受理の可否そのもの（変種の互換・
// interface実装の有無・数値ポリシー等）は各担当側の規則が判定する。

namespace cm::ast::convkind {

enum class Kind {
    // 変換対象外（同kind系の同一・深い互換はこの外で判定される）
    None,
    // 数値型間の暗黙変換（挿入はnumeric文脈coerce・受理はZ5の数値ポリシー表）
    NumericImplicit,
    // T -> Union（変種wrap。挿入はユニオン構築・受理は変種互換）
    UnionWrap,
    // 固定長配列 -> スライス（挿入はヒープスライス実体化）
    ArrayToSlice,
    // 具象構造体 -> interface値（挿入はfat pointer構築+boxing・受理はimpl実装検査）
    IfaceValueUpcast,
    // 具象構造体ポインタ -> interfaceポインタ（挿入はfat pointer構築・受理はimpl実装検査）
    IfacePtrUpcast,
};

// 分類に必要な環境（checker/MIRの双方が自分の実装を渡す）
struct Env {
    // typedefエイリアスの実体解決（nullを返した場合は入力をそのまま使う）
    std::function<TypePtr(const TypePtr&)> resolve;
    // interface名判定
    std::function<bool(const std::string&)> is_interface;
};

// 宛先型destへ値型srcを暗黙に合わせる場合の変換種を分類する。
// dest/srcはresolve適用前でよい（内部で解決する）
Kind classify(const TypePtr& dest, const TypePtr& src, const Env& env);

}  // namespace cm::ast::convkind
