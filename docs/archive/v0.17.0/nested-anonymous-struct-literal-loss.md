---
title: 構造体リテラル内配列の無名構造体リテラル喪失（W1）
parent: v0.17.0 Design
---

# 構造体リテラル内配列の無名構造体リテラル喪失（W1）

## 概要

構造体リテラルの配列フィールドの中に型名を省略した無名構造体リテラル（`{name: ..., weight: ...}`）を書くと、要素の全フィールドが失われ、固定長配列フィールドではゼロ値/空文字列、スライスフィールドではゴミ値になる。
checkerは無診断で受理し、native/jitとも同一の誤値を返すため差分検証でも検出できない。
喪失したstringフィールドへ後から再代入するとnativeのみSIGSEGVし、JITは続行するというバックエンド分裂も併発する。
型名を明示したリテラル（`Tag { ... }`）、トップレベル配列変数への無名リテラル、配列を介さない構造体in構造体の無名リテラルはいずれも正常で、「構造体リテラル > 配列リテラル > 無名構造体リテラル」の組み合わせに限定して壊れる。

## 再現コード

```cm
import std::io::println;
struct Tag { string name; int weight; }
struct Item { Tag[2] tags; }
int main() {
    Item it = {tags: [{name: "a0", weight: 1}, {name: "a1", weight: 2}]};
    println(it.tags[0].weight);
    // 期待1 → 0
    println(it.tags[1].name);
    // 期待a1 → 空文字列
    return 0;
}
```

スライスフィールド版はゼロ値ではなくゴミ値になる（len()は正しい）:

```cm
struct Item2 { Tag[] tags; }
Item2 it = {tags: [{name: "a0", weight: 1}, {name: "a1", weight: 2}]};
// it.tags.len() == 2 は正しいが it.tags[0].weight はゴミ値（例: 1840773888）
```

喪失フィールドへの再代入はnativeのみクラッシュする:

```cm
Item it = {tags: [{name: "a0", weight: 1}, {name: "a1", weight: 2}]};
it.tags[0].name = "mod" + "!";
// JIT: 続行して"mod!"になる / native -O0〜-O3: SIGSEGV
```

## 正常動作する境界（対比）

- `Tag[2] tags = [{name: "x", weight: 5}, ...];`（トップレベル配列変数）→ 正常
- `Item it = Item { tags: [Tag { name: "a0", weight: 1 }, ...] };`（型名明示）→ 正常
- `Out o = {inner: {s: "ok", n: 7}};`（配列を介さない無名ネスト）→ 正常

## 原因の見立て

無名構造体リテラルの型解決は宣言型からの期待型伝播に依存するが、「構造体リテラルのフィールド → 配列リテラル → 要素」の経路で要素側の期待型が伝わらず、要素リテラルが型不明のままゼロ/未初期化blobとしてlowerされている。
N5（generic-struct-literal.md）で実装した期待型推論と同系統の欠落で、配列リテラルを1段挟んだ場合の要素型伝播が抜けている。
nativeクラッシュは、喪失したstringフィールド（ヌル/未初期化ポインタ）への再代入時に旧値のdrop・SDSヘッダ参照が不正アドレスへ触れるためとみられる（JITはゼロ初期化メモリに当たり延命している可能性が高い）。

## 修正方針

1. 型検査で構造体リテラルの配列フィールドを検査する際、配列要素の期待型（フィールド型の要素型）を無名構造体リテラルへ伝播する。
2. 期待型が決められない無名リテラルはlowering前に診断付きエラーにして黙殺を排除する（ゼロ化やゴミ値での続行を禁止）。
3. 修正後、スライスフィールド版（push経由でなくリテラル初期化）とネスト2段（`Store > items > tags`）の回帰を追加する。

## テスト計画

- regression: 固定長配列フィールド・スライスフィールド・2段ネスト（構造体>配列>構造体>配列）それぞれで、無名リテラル・型名明示リテラルの読み出し値一致を検証する。
- integration（native/jit両スイート）: 上記再現コードの期待値実行と、喪失フィールド再代入ケースがクラッシュしないことの確認。

## 検出経緯

native/jit網羅検証第2ラウンド（深いネスト・チェーン・最適化検証）で検出。再現ハーネスは `.tmp/nativejit-bughunt2/harness2.sh`、最小再現は同 `min/m_agg2.cm` / `min/m_agg_slice.cm` / `min/m_agg_write.cm`。

## 解決記録（実装済み）

型検査へ期待型の再帰伝播ヘルパー（propagate_literal_expected_type）を新設し、構造体リテラルのフィールド値へフィールド型を、配列リテラルの要素へ要素型を、無名構造体リテラルへ型名を再帰的に伝播するようにした（infer前に適用）。
併せてMIR loweringの構造体リテラル・スライスフィールド経路で、構造体要素のpushがcm_slice_push_ptr（一時のアドレス格納）になっていたのをcm_slice_push_blob（値のインラインコピー）へ修正し、ゴミ値と再代入SIGSEGVを解消した。
回帰テスト tests/common/structs/anon_literal_in_array_field.cm（固定長・スライス・再代入・push無名リテラル）をjit O0/O1/O3・native O0/O2・js・wasmの7モードで検証した。
