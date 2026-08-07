---
title: ブロックスコープstatic変数の初期化子喪失とO1以上での値破壊（X1）
parent: v0.17.0 Design
---

# ブロックスコープstatic変数の初期化子喪失とO1以上での値破壊（X1）

## 概要

ループ本体・ブロック・if分岐などネストしたスコープ内で宣言した`static`変数は、初期化子が全経路（JIT・native、-O0〜-O3）で無視されゼロ初期化になる。
さらにループ内staticの値を外側の変数へ代入して読み出すと、O1以上でその代入自体が消え初期値0に固定される（O0は正しい）。
関数トップレベルの`static`は初期化子・値保持・最適化レベル間の一貫性ともすべて正常で、ネストスコープ宣言だけが壊れている。
既存テスト（tests/common/types/static_modifier.cm）は関数トップレベルのみをカバーしている。

## 再現コード

### 症状1: 初期化子の喪失（全経路共通）

```cm
import std::io::println;
int f() {
    {
        static int n = 100;
        n = n + 1;
        return n;
    }
}
int main() {
    println("{f()} {f()}");
    // 期待 101 102 → 全経路で 1 2（初期値100が消えゼロ開始）
    return 0;
}
```

if分岐内（`static int a = 50;`）・ループ本体内（`static int hits = 10;`）でも同様にゼロ開始になる。値の保持自体（呼び出し間・イテレーション間で増えること）は機能している。

### 症状2: O1以上でループ内staticの読みが消える

```cm
import std::io::println;
int main() {
    int total = 0;
    for (int i = 0; i < 3; i++) {
        static int hits = 0;
        hits = hits + 1;
        total = hits;
    }
    println("total={total}");
    // 期待3 → -O0: 3 / -O1〜-O3（JIT・nativeとも）: 0
    return 0;
}
```

初期化子を0にして症状1を除外しても発生するため、独立した崩れ方である。

## 原因の見立て

関数トップレベルのstaticは専用のグローバル格納＋初期化ガードでlowerされるが、ネストスコープ内の宣言はこの経路に乗らず、格納だけグローバル化されて初期化子が捨てられている（ゼロ初期化のグローバルとして発行）とみられる。
症状2は、ブロックスコープstaticのMIR表現が通常ローカルと区別されないままO1+のパス（DCE/SCCP/LICM系）に渡り、ループ外から到達しない書き込みとして消されている可能性が高い。
W4（licm-global-clobber-miscompile.md）で確認した`is_static`ガード欠落と同じ観点で、各最適化パスのstatic格納の扱いを監査する必要がある。

## 修正方針

1. HIR/MIR loweringでstatic宣言をスコープ位置に関わらず関数トップレベルと同一経路（一意なグローバル格納＋初回初期化ガード）へ正規化する。
2. ブロックスコープstaticの初期化子を初回到達時に1回だけ実行するガードを、宣言位置のブロックに挿入する（C++のfunction-local static初期化と同じ意味論）。
3. MIRの`is_static`ローカルを最適化パス（DCE・SCCP・LICM・GVN・propagation）で外部可視として扱い、消去・巻き上げの対象から外す。
4. 同名staticを持つ別関数・別ブロックの独立性（現状は関数トップレベル間で正常）に回帰がないことを確認する。

## テスト計画

- regression: ブロック/if/ループ内staticの初期化子適用・呼び出し間保持・O0/O1出力一致（MIR最適化パイプライン経由）。
- integration（native/jit両スイート）: 症状1・2の再現コードを期待値つきで追加する。

## 検出経緯

native/jit網羅検証（X系: static・private・複合push検証）で検出。最小再現は `.tmp/nativejit-bughunt3/min_static_block.cm` / `min_static_if.cm` / `min_static_loop2.cm`。

## 解決記録（実装済み）

症状1: MIR loweringのstatic宣言へ初回到達ガード（name__static_guardのstatic bool＋分岐）を実装し、初期化子を初回到達時に1回だけ実行するようにした（C++のfunction-local static意味論）。調査の結果、非ゼロ初期化子は関数トップレベルでも未実装（宣言時コメントに「将来実装」）だったため、全スコープ統一で解決した。
症状2: W4の監査で実施したSCCP/ConstantFoldingのis_staticガード（グローバルと同時に除外）で解消した。
回帰テスト tests/common/types/static_block_scope.cm（ブロック/if/ループ/トップレベル、O0/O1一致）を7モードで検証した。
