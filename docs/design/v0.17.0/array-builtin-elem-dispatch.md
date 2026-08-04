---
title: 配列検索ビルトインの要素型ディスパッチ欠落（Z1）
parent: v0.17.0 Design
---

# 配列検索ビルトインの要素型ディスパッチ欠落（Z1）

## 概要

配列・スライスの検索系ビルトイン `includes`/`contains`/`indexOf`/`findIndex`/`some`/`every` のHIR脱糖が、要素型を無視して一律 `__builtin_array_*_i32` を選択している（`src/internal/hir/lowering/expr_member.cpp:413-437`）。
直下の `map`/`filter`/`reduce` には `elem_is_i64` によるi64変種ディスパッチが実装済みであり（コメントに「従来は常にi32版が選ばれ黙って壊れていた」とある）、同じ修正が検索系に適用されていない非対称が原因。
要素型ごとに症状が分裂し、いずれもcheckerは無診断で受理する。

| 要素型 | 症状 |
|---|---|
| `string` | ポインタ値のi32切り詰め比較になり意味的に破綻。`contains`がjit=true・native=false・wasm=true、`indexOf`がjit/native=-1・wasm=2と**3バックエンド3様**（jit/wasmはリテラルの定数プール同一化による偶然の一致） |
| `short`/`tiny` | stride不一致（2/1バイト要素をi32で走査）で全件不一致。`contains`が常にfalse・`indexOf`が常に-1の誤値 |
| `float`/`double` | i32シグネチャへdouble引数が渡り不正IR（Call parameter type does not match）で**全経路コンパイル不能** |
| `long`/`ulong` | ランタイムに`__builtin_array_includes_i64`等の変種が存在するのに未使用。stride不一致で`indexOf`誤値 |

## 再現コード

```cm
import std::io::println;
int main() {
    string[3] arr = ["aa", "bb", "cc"];
    println("{arr.contains("bb")} {arr.indexOf("cc")}");
    // jit: true -1 / native: false -1 / wasm: true 2（期待: true 2）
    short[4] sh = [10, 20, 30, 40];
    println("{sh.contains(20)} {sh.indexOf(30)}");
    // 全経路 false -1（期待 true 2）
    long[3] ls = [9000000000, 2, 3];
    println("{ls.indexOf(3)}");
    // -1（期待 2）
    float[3] fl = [1.5, 2.5, 3.5];
    println("{fl.contains(2.5)}");
    // 全経路コンパイル不能（LLVM検証エラー）
    return 0;
}
```

スライス版（`string[] xs; xs.push("bb"); xs.contains("bb")`）も同様にfalseになる（push済み文字列と引数リテラルは別ポインタのため）。

## 修正方針

1. 検索系6ビルトインの脱糖に要素型ディスパッチを追加する。スカラは`slice_scalar_info`の幅サフィックス（i8/i16/i32/i64/f32/f64のランタイム変種を補完）、`string`は値比較でなくEq比較の専用変種（`__builtin_array_includes_str`等、`cm_strcmp`ベース）を新設する。
2. ランタイム変種の追加は`runtime-builtin-registry`のレジストリ表経由で行い、native/wasm両ランタイムへ同時に追加する（シグネチャ乖離lintが検査）。
3. map/filter系と検索系でディスパッチ実装を共通ヘルパへ寄せ、「新しい配列ビルトインを追加すると特定の型だけ壊れる」構造を解消する。
4. 未対応の要素型（構造体・ユニオン等、Eq未定義）は診断付きコンパイルエラーにして黙殺を排除する。

## テスト計画

- regression: 要素型×ビルトインのマトリクス（string/short/tiny/long/float/int × contains/indexOf/findIndex/some/every）で期待値検証。
- integration（native/jit/wasm/js）: 上記の4系一致（特にstringはリテラル同一化の偶然一致があるため、実行時生成文字列での検証を含める）。

## 検出経緯

第4ラウンド追補（ユニオン・文字列要素の配列/スライス整合性調査）で検出。最小再現は `.tmp/bughunt4/z/z02_string_array_receiver.cm` / `z/z03_short_array_receiver.cm` / `z07_short_only.cm` / `z09_long_contains.cm`。
