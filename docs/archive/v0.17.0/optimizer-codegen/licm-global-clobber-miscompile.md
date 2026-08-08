---
title: LICMがグローバル変数を呼び出し越しに不変とみなすmiscompile（W4）
parent: v0.17.0 Design
---

# LICMがグローバル変数を呼び出し越しに不変とみなすmiscompile（W4）

## 概要

ループ本体の関数呼び出しがグローバル変数を書き換える場合でも、MIRのLICM（ループ不変コード移動）がそのグローバルの読みをループ不変と誤判定してpre-headerへ巻き上げ、O1以上でループ内の読みが初期値に固定される。
JIT・nativeで同一に誤るため差分では見えず、O0との比較で発覚する。ループ外の読みは正しいため、途中結果だけが静かに壊れる。

## 再現コード

```cm
import std::io::println;
int g = 0;
void tick() {
    g = g + 2;
}
int main() {
    int acc = 0;
    for (int i = 0; i < 4; i++) {
        tick();
        acc = acc + g;
    }
    println("acc={acc}");
    // 期待20（2+4+6+8） → -O0: 20 / -O1〜-O3（JIT・nativeとも）: 0
    return 0;
}
```

ループを含まない形（`tick(); int a = g; tick(); int b = g;`）はO1でも正しく、ループ＋LICMの組み合わせに限定される。

## 原因

`src/internal/mir/passes/loop/licm.cpp` の不変判定は、ループ内で代入されたローカル集合 `modified_locals` にオペランドのlocalが含まれるかだけを見る（`is_invariant`、254〜267行）。
グローバル変数はMIR上 `is_global` フラグ付きのlocalとして表現されるが、その書き込みが呼び出し先関数（`tick`）内にある場合は当該関数のループ本体に代入文が存在しないため、`use(copy g)` が不変と判定されロードが巻き上がる。
SCCP（`scalar/sccp.cpp:549`）は `is_global || is_static` を格子から除外して同種の誤りを避けており、LICMだけこの考慮が抜けている。

## 修正方針

1. `is_invariant` で `func.locals[local].is_global || is_static` のオペランドを不変としない（最小修正・保守的）。
2. 将来の精密化として、ループ内に呼び出しが存在しない場合に限りグローバルの不変判定を許す（呼び出しのmod-ref情報が入るまでは呼び出し有無での二値判定で十分）。
3. GVN・propagation等の他パスにも同じ観点の監査を行い、グローバル/静的変数の呼び出し越しCSEがないことを確認する（今回の検証ではo06b直列パターンは正常だったが、パス実装の明示的なガード有無をコードで確認する）。

## テスト計画

- unit: 手組みMIR（ループ内にグローバル読みと呼び出し、呼び出し先でグローバル書き）でLICMが巻き上げないことを検証する。
- regression: 再現コードをO0/O1で実行し出力一致を検証する（最適化パイプライン経由）。
- integration: native/jit両スイートに `-O1` 実行の期待値テストを追加する。

## 検出経緯

native/jit網羅検証（W系: 最適化正しさ検証）で検出。最小再現は `.tmp/nativejit-bughunt2/min/m_o06.cm`（6経路比較は `harness2.sh`）。

## 解決記録（実装済み）

方針1（LICMのis_invariantでis_global/is_staticを不変としない）に加え、監査（方針3）で同種の欠落を2箇所検出し修正した。
(1) ConstantFolding: 関数全体で共有するconstants表にグローバルの初期化値が記録され、呼び出し越しに伝播していた（グローバル/静的を追跡から除外）。
(2) SCCP: 関数内に代入が無いグローバルが格子でUndefined（楽観）のまま残り、acc + g = Undefined・merge(Const, Undefined) = Constの楽観連鎖でループ内の読みが初期値定数へ畳まれていた（マージ時にグローバル/静的を常時Overdefined化。can_bind_constantの束縛ガードだけでは不十分だった）。
回帰テスト tests/common/global_var/global_call_clobber.cm をjit/native両系のO0〜O3で検証した（全レベルacc=20）。
