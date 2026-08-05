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

## 原因の見立て

モノモーフ化（typed instantiation化後）の型引数がそれ自体特殊化名（`Box__string`）である場合の置換・レイアウト解決に欠陥があり、`Pair__Box__int__Box__string`のフィールドから取り出した`Box<string>`のフィールド読みが誤った位置/型で行われるとみられる。
rc=0の無言終了は、壊れた文字列ポインタがランタイムのprint系でEOF/exit相当の挙動を踏んでいる可能性がある（要調査: クラッシュですらない点が異常）。

## 修正方針

1. `Pair<Box<int>, Box<string>>`のモノモーフ化結果（MIR構造体定義とフィールド型）をダンプし、`second`フィールドの型が`Box__string`として正しく特殊化されているかを確認する。
2. ネストした特殊化名の型引数分解（`Box__int`と`Box__string`の区別）を типed instantiationの型キー（typekey）で検証する回帰をunitテストに追加する。
3. 無言終了の経路（printランタイムか、それ以前のロードか）を特定し、いかなる場合もrc=0で死なないようにする（診断またはクラッシュへ）。

## 検出経緯

第5ラウンド（未修正クリティカル調査）で検出。最小再現は `.tmp/bughunt5/q_r01e.cm` / `q_r01f.cm`、対照は `q_r01g.cm`（兄弟共存・正常）。
