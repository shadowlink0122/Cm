---
title: B2 整数値→浮動小数文脈のビット再解釈
parent: v0.17.0 Design
---

# B2: 整数値→浮動小数文脈のビット再解釈

## 対象バグ

| # | 領域 | 概要 | 重大度 | 状態 |
|---|------|------|--------|------|
| B2 | 数値変換 | intリテラルのdouble文脈代入が変換でなくビット再解釈になり誤値を出力（native/jit共通） | Critical | 修正済み |

## 再現コード

```cm
int main() {
    double d = 1;
    println("{d}");  // 期待: 1 実際: 5e-324（jit）・2.1e-314（AOT O0）
    return 0;
}
```

## 根因（確定）

整数値→浮動小数文脈の暗黙変換で、MIR loweringが型不一致のまま`Use(copy)`を発行していた。
整数リテラルはint型一時（`store i32 1`）になり、それがdoubleのallocaへ生ビットのままstoreされるため、sitofpされず整数ビットパターンのdouble再解釈になる。
codegen側にはMIR Cast用のsitofp/uitofp/fpext/fptrunc発行と定数畳み込みが既に存在するため、修正はMIR loweringでのCast挿入のみで完結する。

## 実装内容

共通ヘルパー`LoweringContext::coerce_to_float_context`（src/internal/mir/lowering/context.hpp・context.cpp）を追加し、値の型が整数系でターゲットが浮動小数系ならCastを挿入する（符号はcodegenがsitofp/uitofpを選択、f32↔f64の幅違いもCastで揃える）。
以下の7文脈へ適用した。

- 宣言初期化（stmt/let.cpp）
- 代入式（expr/binary.cpp、変数・フィールド・配列要素の全左辺値）
- HirAssign文経路（stmt/assign.cpp）
- 関数引数（expr_call.cpp）
- return値（stmt/control.cpp）
- 構造体リテラルのフィールド初期化（expr/construct.cpp）

浮動小数→整数の既存動作（as要求・飽和intrinsic）は無変更。

## テスト

- tests/common/casting/int_to_float_context.cm — double/float×（宣言・代入・引数・return・構造体フィールド初期化/代入）の全文脈と、uint大値のuitofp検証
- 修正後バイナリでcasting llvm/interpreter/js・types llvm・formatting llvmの各スイートPASSを確認済み
