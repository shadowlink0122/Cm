---
title: push(無名構造体リテラル)のフィールドずれ・喪失（X4）
parent: v0.17.0 Design
---

# push(無名構造体リテラル)のフィールドずれ・喪失（X4）

## 概要

構造体スライスへの無名構造体リテラル直接push（`ps.push({x: 3, y: 4})`）で、格納された要素のフィールド値がずれる・失われる。
2フィールド（int, int）では`{x: 3, y: 4}`が`x=4, y=0`として格納され、stringフィールドを含む場合はintフィールドがゼロ化しstringフィールドの読みで全経路SIGSEGVする。
フィールド1個のintのみの構造体は偶然正しく動く。型名付きリテラル（`ps.push(P { x: 3, y: 4 })`）は完全に正常。
W1（nested-anonymous-struct-literal-loss.md）と同じ「無名リテラルへの期待型伝播欠落」ファミリだが、経路はスライスリテラル初期化ではなくpush引数のloweringで、独立に修正が必要。

## 再現コード

```cm
import std::io::println;
struct P { int x; int y; }
int main() {
    P[] ps = [];
    ps.push({x: 3, y: 4});
    int a = ps[0].x;
    int b = ps[0].y;
    println("{a} {b}");
    // 期待 3 4 → 4 0（フィールドずれ）
    return 0;
}
```

stringフィールドを含む場合:

```cm
struct Q { string name; int age; }
Q[] qs = [];
qs.push({name: "alice", age: 30});
int a = qs[0].age;
// 期待30 → 0
string nm = qs[0].name;
// 読みで全経路SIGSEGV
```

対照（正常）:

```cm
ps.push(P { x: 3, y: 4 });
// 型名付きリテラルは正しく 3 4 になる
```

メンバチェーンレシーバ経由（`hs[0].cells.push({data: [9]})`）でも同様にクラッシュする（X系調査のc07）。

## 原因

X3（slice-push-array-literal-corruption.md）と同じく、`expr_slice.cpp` の `__builtin_slice_push` がpush引数を期待型なしでlowerするため、無名構造体リテラルが要素型`P`のレイアウトで構築されない。
型不明の無名リテラルが何らかの推測レイアウト（フィールド並び・サイズが不一致のblob）でlowerされ、`cm_slice_push_blob`がそのblobを要素サイズ分インラインコピーするため、ずれた値・ゴミポインタが格納される。
checkerが無名リテラルと要素型の不一致を検出していない点も併発問題（型名付きは型検査で正しく解決される）。

## 修正方針

1. push引数のloweringおよび型検査に、レシーバのスライス要素型を期待型として伝播し、無名構造体リテラルを要素型のレイアウトで構築する（W1の期待型伝播修正と共通機構にする）。
2. 期待型に対してフィールド名・型が一致しない無名リテラルは診断付きエラーにする。
3. 修正後、`push({...})`と`push(P {...})`の格納結果一致をフィールド数1/2/string持ち/ネストスライス持ちで検証する。

## テスト計画

- regression: 上記4形状の無名リテラルpushと型名付きpushの結果一致（フィールド値・len）。string持ちはguard mallocでの読み書き健全性も確認する。
- errorsスイート: フィールド名不一致の無名リテラルpushが診断されること。

## 検出経緯

native/jit網羅検証（X系）で検出。最小再現は `.tmp/nativejit-bughunt3/min_push_anon2int.cm` / `min_push_anon_str.cm`。

## 解決記録（実装済み）

W1と共通の期待型伝播機構（propagate_literal_expected_type）をpush引数の型検査へ適用し、無名構造体リテラルが要素型のレイアウトで構築されるようにした（型名補完によりcheckerのフィールド検査も働く）。
メンバチェーンレシーバ経由のpushはW5の補間チェーン修正とX3のスライス実体化で併せて解消した。
回帰テスト tests/common/structs/anon_literal_in_array_field.cm（2フィールド・string持ちの無名リテラルpush）を7モードで検証した。
