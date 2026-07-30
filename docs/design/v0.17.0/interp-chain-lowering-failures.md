---
title: 補間内チェーン式の誤lowering（クラッシュ・誤値・未解決シンボル）（W5）
parent: v0.17.0 Design
---

# 補間内チェーン式の誤lowering（クラッシュ・誤値・未解決シンボル）（W5）

## 概要

文字列補間 `{...}` の中にメンバ・添字・メソッド呼び出し・アロー参照を2段以上組み合わせたチェーン式を書くと、補間ミニパイプライン（`expr_println.cpp` のテキスト再解析）が正しくlowerできず、組み合わせに応じてSIGSEGV・ゴミ値（バックエンド/最適化レベルごとに異なる値）・`__error__len` 未解決シンボルによるビルド不能のいずれかになる。
すべて直接式（変数へ受けてから `{変数}` で出力）では正しく動作し、補間経路に限定して壊れる。
V1〜V4（archive/v0.17.0/move-closure-interp-audit.md）で修正した式添字と同根で、テキスト再解析の特別扱いが任意チェーンに対して破綻している。

## 再現コードと現象

### (a) メンバ→スライス→添字→メンバ: SIGSEGV

```cm
import std::io::println;
struct Leaf { int v; }
struct Bag { Leaf[] leaves; }
int main() {
    Bag b = {leaves: []};
    b.leaves.push(Leaf { v: 10 });
    b.leaves.push(Leaf { v: 20 });
    println("{b.leaves[1].v}");
    // JIT・native全レベルでSIGSEGV（直接式 int x = b.leaves[1].v; は正常）
    return 0;
}
```

スライス要素経由の3段（`{d.bags[0].leaves[1].v}`）はゴミ値出力後にSIGSEGVとなり、同根で症状のみ異なる。

### (b) メソッド呼び出しチェーン: 誤値・経路間分裂

```cm
println("{world[0].shifted(100).pos.x}");
// 期待105 → jitO0/natO0: 0、jitO2: ゴミ値、natO1/O2/O3: スタック残渣（477等）
// int a = world[0].shifted(100).pos.x; println(a); は全経路で正常
```

### (c) ポインタのアローチェーン: 誤値

```cm
println("{p->next->data.v}");
// 期待2 → JIT: ゴミ値、natO0: 1（誤った場所の値）、natO1+: ゴミ値
// 単段の {head.next->data.v} は正常、2段以上で壊れる
```

### (d) チェーンレシーバへのメソッド呼び出し: `__error__len` でビルド不能

```cm
struct Box<T> { T v; }
Box<Box<string>> s2 = ...;
println("{s2.v.v.len()}");
// JIT: Symbols not found: [ ___error__len ] / native: リンクエラー
// {make().data.len()} など関数呼び出しレシーバでも同様
// 非ジェネリックの {o.i.s.len()} は正常
```

checkerはいずれも無診断で受理する。(d)はレシーバ型の解決が`<error>`へ落ちたまま `__error__len` という関数名を発行しており、型検査段階の失敗が診断されずcodegenまで素通りしている。

## 原因

`src/internal/mir/lowering/expr_println.cpp` の補間ミニパイプラインは、プレースホルダ内容をMIR lowering時にテキストとして再解析し、`name`・`name.field`・`name[idx]`・単段アロー等の限られたパターンを手書きで場所化している。
2段以上のチェーン・メソッド呼び出し・多段アローはこのパターン集に該当せず、部分的に一致した接頭辞で誤った場所を構築（(a)(b)(c)）するか、型解決不能の`<error>`型でメソッド名をマングリング（(d)）する。
複雑式と判定されたもの（括弧付き・演算子付き）は正規の式パイプラインへ委譲され正しく動くため、判定の隙間に落ちるパターンだけが壊れるという構造もV1〜V4と同一。

## 修正方針

1. 恒久対応として、補間プレースホルダの内容は識別子単体等の自明なケースを除きすべて正規の式パイプライン（HIR式としてパース→型検査→通常のexpr lowering）へ委譲し、`expr_println.cpp` のテキスト再解析パターン集を段階的に廃止する（V1〜V4修正時の方針を全チェーンへ拡張）。
2. 型検査で解決不能になったプレースホルダ式は`<error>`型のままloweringへ流さず、その場で診断付きコンパイルエラーにする（`__error__*` シンボル発行の禁止）。
3. 移行期の安全策として、ミニパイプラインでパターン不一致となった内容は黙って部分一致処理せず、複雑式経路へフォールバックする。

## テスト計画

- regression: (a)〜(d)各形の補間出力と直接式の値一致（ジェネリックレシーバ・関数呼び出しレシーバ・多段アロー・スライス3段を含む）。
- integration（native/jit両スイート）: 再現コード4種の期待値実行。`__error__*` シンボルが発行されないことのIR検査をregressionに追加する。

## 検出経緯

native/jit網羅検証第2ラウンド（深いネスト・チェーン検証）で検出。最小再現は `.tmp/nativejit-bughunt2/min/m_d03_interp.cm` / `min/m_interp_arrow.cm` / `min/m_interp_len.cm`、チェーン網羅は同 `nest/` ディレクトリ。
