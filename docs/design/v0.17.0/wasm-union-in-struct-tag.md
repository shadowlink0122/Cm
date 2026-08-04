---
title: 構造体内ユニオンフィールドのタグがwasmで読めない（Z3）
parent: v0.17.0 Design
---

# 構造体内ユニオンフィールドのタグがwasmで読めない（Z3）

## 概要

`string[2]`等のポインタ幅依存フィールドを含む構造体のユニオンフィールドで、`is`判定がnative/jit=true・wasm=falseに分裂する。
値フィールド（文字列・int）はwasmでも正しく読めており、ユニオンのタグ読みだけが壊れることから、構造体内オフセット計算のどこかがポインタ幅8を仮定し、wasm32（ポインタ幅4）の実レイアウトとずれてタグを別位置から読んでいるとみられる。
Y1（構造体リテラルのユニオンフィールドのタグ喪失）とは別事象で、native/jitで正しく動く形がwasmだけで壊れる（Y1の再現形はnativeでも壊れる。なお本調査でY1の再現は単一フィールド構造体`{v: 7}`に限られ、複数フィールドの`{names: [...], n: 7, v: 42}`ではnative/jitのタグ構築が正しく行われることも判明した——Y1のサイト依存性の追加証拠として記録する）。

## 再現コード

```cm
import std::io::println;
typedef Val = int | string;
struct Rec {
    string[2] names;
    int n;
    Val v;
}
int main() {
    Rec r = {names: ["x", "y"], n: 7, v: 42};
    println("{r.names[0]} {r.names[1]} {r.n}");
    // 全バックエンド正常（x y 7）
    println("{r.v is int}");
    // native/jit: true / wasm: false
    return 0;
}
```

## 原因の見立て

構造体レイアウトはMIRの`layout_size`/`layout_align`（`string`=`target_pointer_size()`でwasm=4）とLLVM codegenのDataLayout（wasm32ターゲットでptr=4）の2系統で計算される。
フィールドオフセットを8バイトポインタ前提で計算する残存箇所（構造体リテラルloweringのフィールドストア、またはユニオンタグ読みのGEP構築）があると、`string[2]`（wasm実レイアウト8バイト・8仮定なら16バイト）以降のフィールドオフセットが8ずれ、タグを`n`やペイロード相当の位置から読む。
値フィールドが正しいことから、ずれはフィールド書き込み側でなくユニオンタグの読み出し経路（`is`のタグ比較GEP）に限定される可能性が高い。

## 修正方針

1. `is`判定（タグ読み）のGEP構築経路を特定し、オフセット計算がDataLayout由来か手計算かを確認する。手計算ならLLVM StructTypeのフィールドインデックスGEP（`CreateStructGEP`）へ置換してターゲット非依存にする。
2. 同経路の8バイト仮定を`target_pointer_size()`または DataLayout へ置換する。
3. 修正後、フィールド順（ユニオン先頭・中間・末尾）×前置フィールド型（string配列・int・構造体）のマトリクスをwasm含む4系で検証する。

## テスト計画

- regression: 上記マトリクスの`is`/`as`/ペイロード読み検証。
- integration: llvm-wasmスイートへ本再現を追加（wasmはポインタ幅差の検出に必須）。

## 検出経緯

第4ラウンド追補（ユニオン・文字列要素の配列/スライス整合性調査）で検出。最小再現は `.tmp/bughunt4/z/z06_struct_with_string_array.cm`（native/jit/wasm比較）。
