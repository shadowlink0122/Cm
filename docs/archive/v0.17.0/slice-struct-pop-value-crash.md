---
title: 構造体要素スライスのpop()戻り値受け取りがSIGSEGV（W3）
parent: v0.17.0 Design
---

# 構造体要素スライスのpop()戻り値受け取りがSIGSEGV（W3）

## 概要

要素型が構造体のスライスに対する `pop()` の戻り値を変数へ受けると、JIT・native全最適化レベルでSIGSEGVする。
戻り値を捨てる文（`leaves.pop();`）は正常で、len減算も正しく行われる。
スカラ要素（int等）のpop()は正常。`leaves.pop().v` のようなチェーン読みも同様にクラッシュする。

## 再現コード

```cm
import std::io::println;
struct Leaf { int v; }
int main() {
    Leaf[] leaves = [];
    leaves.push(Leaf { v: 10 });
    leaves.push(Leaf { v: 20 });
    println("len {leaves.len()}");
    Leaf popped = leaves.pop();
    // ここでJIT・native -O0〜-O3すべてSIGSEGV（rc=139）
    println("popped {popped.v}");
    return 0;
}
```

正常動作する対比:

```cm
leaves.pop();
// 戻り値を使わなければ正常（lenは1減る）
int[] xs = [1, 2];
int x = xs.pop();
// スカラ要素のpopは正常
```

## 原因

`src/internal/mir/lowering/expr_slice.cpp` の `__builtin_slice_pop` lowering（167行〜）が、要素型ディスパッチで `Struct` を `String`/`Pointer` と同じ `cm_slice_pop_ptr` に振り分けている（184〜192行）。
`cm_slice_pop_ptr` はポインタ幅の値を返すが、呼び出しの宛先ローカル `result` は構造体型（elem_type）のまま作られるため、LLVMレベルで「ポインタ戻り値を構造体宛先へ格納」という型不整合の呼び出しになり、受け取り側のコピーで不正メモリアクセスになる。
戻り値を捨てる場合はこの格納が起きないためクラッシュしない。
なお同ファイルの要素読み出し（`cm_slice_get_element_ptr`）はポインタ宛先＋deref格納で正しく処理しており、popだけがblob要素の扱いを欠いている。

## 修正方針

1. blob（構造体/union）要素のpopは、`cm_slice_get_element_ptr` で末尾要素ポインタを取得して構造体宛先へderefコピーし、その後len減算を行う脱糖に変更する（既存の読み出し経路と同じパターン）。
2. またはランタイムに `cm_slice_pop_blob(slice, out_ptr)` を追加して要素バイト列を書き出す。elem_sizeはヘッダから取得できるため引数追加は不要。
3. `pop().field` チェーン・`match`引数への直接渡し等、戻り値が一時に受かる形も同経路で解決されることを確認する。

## テスト計画

- regression: 構造体要素（intのみ/string持ち/ネスト構造体）のpop戻り値受け取り・チェーン読み・戻り値破棄それぞれの値とlen検証。
- integration（native/jit両スイート）: 再現コードの正常終了確認。string持ち構造体はguard mallocでの二重解放検査も行う。

## 検出経緯

native/jit網羅検証第2ラウンドで検出。最小再現は `.tmp/nativejit-bughunt2/min/m_d03_pop.cm` / `min/m_pop2.cm`。

## 解決記録（実装済み）

方針1どおり、blob（構造体/union）要素のpopを「cm_slice_get_element_ptrで末尾要素ポインタ取得→構造体宛先へderefコピー→len減算」の脱糖へ変更した。
len減算はランタイム新設のcm_slice_pop_blob（native/wasm両実装、js写像は.pop()破棄）で行う。
回帰テスト tests/common/dynamic_array/struct_pop_value.cm（戻り値受け取り・チェーン読み・破棄・string持ち構造体・スカラ対比）を7モードで検証した。
