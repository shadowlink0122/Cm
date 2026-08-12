---
title: ネストしたジェネリック型引数のstringフィールド読みが無言死（Q2）
parent: v0.17.0 Design
---

# ネストしたジェネリック型引数のstringフィールド読みが無言死（Q2）

## 概要

ジェネリック構造体の型引数に別のジェネリック特殊化を渡した場合（`Pair<Box<int>, Box<string>>`等）、`Box<string>`側のフィールドを経由した文字列読みで**プロセスが終了コード0のまま無言終了**する（jit/native全最適化レベル共通）。
出力が途中で途切れるのに終了コードが0という最悪の失敗様式で、checkerは無診断。`Box<int>`側（数値）は正常に読める。
`Box<string>`単独・兄弟インスタンス化の共存（`Box<int>`と`Box<string>`をローカルに並置）・逆ネスト（`Box<Pair<int, string>>`）はいずれも正常で、「ジェネリック特殊化を型引数として持つ構造体のstring系フィールド」に限定して壊れる。

## 再現コード

```cm
import std::io::println;
struct Box<T> { T v; }
struct Pair<A, B> { A first; B second; }
int main() {
    Pair<Box<int>, Box<string>> nested = Pair { first: Box { v: 42 }, second: Box { v: "deep" } };
    println("start");
    Box<string> bs = nested.second;
    println("got box");
    string s = bs.v;
    // ここで無言終了（rc=0。以後の出力なし）
    println("s={s}");
    return 0;
}
```

`Pair<Box<string>, Box<int>>`（第1引数側）でも同様に`rev.first.v`の読みで死ぬため、引数位置には依存しない。

## 真因（静的追跡で特定済み）

`struct_symbol_key`（src/internal/mir/lowering/mono/typeinfo.cpp:60）の「simple高速パス」（:74-81）が、引数キーに`$`が含まれない限りフラット名`base__k1__k2…`を生成するため、`Pair<Box<int>, Box<string>>`は曖昧なフラット名`Pair__Box__int__Box__string`になる。
このフラット名しか持たない経路（mono_structs.cpp:114-137のマングリング名からの発見等）で逆算器`parse_flat_type_args`（typeinfo.cpp:89-128）が呼ばれると、`Box|int|Box|string`の4セグメントを4つの型引数と誤解し、substが先頭2つだけを採用して`A:=Box（裸）, B:=int`になる。
結果、フィールド`second`が`Box<string>`（ポインタ8バイト）でなく`int`（4バイト）としてレイアウトされ、`.second.v`が壊れたポインタを読み、printランタイムがそれを辿ってrc=0のまま無言終了する。
`Box<string>`単独が正常なのはparam_count==1の結合特例（:114-122）があるため、兄弟共存が正常なのは各Boxが構造化された型ツリー経路（mono_structs.cpp:100-112）で直接発見されるためで、症状の限定条件と完全に一致する。
可逆な`$`長さ接頭辞エンコーダ（typekey.cpp:216 make_struct_key）は存在するが、simple高速パスがネスト特殊化のときに限ってそれを迂回するのが設計欠陥である。

## 修正方針

1. 即修: `struct_symbol_key`のsimple高速パス判定に「引数キーが`__`を含む（=引数自体が特殊化）場合は`$`エンコード分岐へ」を追加する。`$`エンコード名の消費側（resolve_struct_field_types:193-201・mono_structs.cpp:499-506等）は対応済みのため、この1箇所でQ2は修正できる。
2. 恒久: フラット名逆算そのものの全廃は[mono-flat-name-elimination.md](mono-flat-name-elimination.md)で扱う（parse_flat_type_args削除・typekey全面化）。
3. 回帰: ネスト特殊化のマトリクス（`Pair<Box<int>, Box<string>>`・逆順・3型引数・`Box<Pair<int,string>>`）を6経路+wasm/jsで追加し、いかなる場合もrc=0で無言死しないことを検証する。

## 検出経緯

未修正バグ調査（Q系）で検出。最小再現は `.tmp/bughunt5/q_r01e.cm` / `q_r01f.cm`、対照は `q_r01g.cm`（兄弟共存・正常）。

## 実装記録（修正済み）

実装の結果、真因は2系統あり、修正方針1の即修だけでは不十分だった（即修適用後は無言死からSIGSEGVへ変わり、読み出しレイアウトは直ったが書き込み経路が壊れたままだった）。

1. **名前系（読み出しレイアウト）**: `struct_symbol_key`のsimple高速パス判定へ「複数引数基底で引数キー自体が`__`入り（=特殊化）の場合は`$`エンコードへ退避」を追加した（src/internal/mir/lowering/mono/typeinfo.cpp）。`Pair<Box<int>, Box<string>>`は`Pair$2$8$Box__int11$Box__string`となり逆算器を通らない。1引数基底（`Vector<Vector<int>>`等）はparse_flat_type_argsの全セグメント結合で可逆のため、既存特殊化名の変動を避けてフラット名を維持した（方針1からの意図的な縮小）。
2. **型注釈系（書き込み経路・新発見）**: 内側リテラル`Box { v: "deep" }`の一時変数が裸の`Box`型のままlowerされ、LLVMの裸`%Box`（フィールドi32）へstringポインタの指す先頭4バイトをload/storeして値を破壊していた。真因は`propagate_literal_expected_type`（src/internal/types/checking/expr/primary.cpp）の上書き: 外側リテラルからの伝播で`Box<string>`を注釈した後、`infer_struct_literal`のフィールド走査が生のフィールド宣言型（ジェネリックパラメータ名`A`。パーサ上はStruct kind）で再伝播し、`expr.type = A`で上書きしていた。構造体表にない名前は既存のより具体的な注釈を上書きしないよう修正し、あわせて期待型が特殊化型のときはフィールド型中のジェネリックパラメータを実引数へ置換してから伝播するようにした（`substitute_generic_type`を再利用）。
3. **回帰テスト**: `tests/common/generics/nested_specialization_args.cm`（基本形・逆順・特殊化×プリミティブ混在・3型引数・逆ネスト`Box<Pair<int,string>>`・関数戻り値経由・チェーン読み）をjit・native O0/O2/O3・wasm・jsで出力一致確認し、TS広域ゲート（578件）も通過した。

恒久対応（`parse_flat_type_args`の全廃・typekey全面化）は[mono-flat-name-elimination.md](mono-flat-name-elimination.md)が引き続き扱う。
