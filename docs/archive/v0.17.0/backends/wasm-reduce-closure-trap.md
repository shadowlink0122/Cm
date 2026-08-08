---
title: wasmのreduceクロージャのランタイムトラップ
parent: v0.17.0 Design
---

# wasmのreduceクロージャのランタイムトラップ

## 概要

キャプチャ付きクロージャを`reduce`へ渡すと、wasmターゲットのみ`__builtin_array_reduce_i32`内でランタイムトラップ（unreachable相当）になり実行が落ちる。
同一プログラムの`filter`/`map`（同じくキャプチャ付き）は正常に動作し、native/jit/jsのreduceも正しい結果を返すため、wasmのreduce経路に固有の欠陥。
C6（複数キャプチャクロージャの高階関数対応）はreduceを含む全ファミリ対応としてクローズ済みだが、wasmのreduceは検証から漏れていたとみられる。

## 再現コード

```cm
import std::io::println;

int main() {
    int a = 100;
    int[] xs = [];
    xs.push(1);
    xs.push(2);
    xs.push(3);
    int[] f = xs.filter((int x) => { return x > 1; });
    int[] m = f.map((int x) => { return x * a; });
    println("{m.len()} {m[0]} {m[1]}");                          // 全バックエンド: 2 200 300
    long total = m.reduce((long acc, int x) => { return acc + x; }, 0);
    println(total);                                              // native/jit/js: 500   wasm: トラップ
    return 0;
}
```

wasmtimeのエラー出力:

```
error while executing at wasm backtrace:
    0: 0x2928 - <unknown>!__builtin_array_reduce_i32
    1:  0x52e - <unknown>!__original_main
```

## 根因候補

reduceのクロージャ呼び出し規約はfilter/mapと異なり、環境ポインタに加えてアキュムレータと要素の2引数を取る（C6実装ノート: caps_start=4でacc+elemの2追加パラメータ）。
wasmランタイムの`__builtin_array_reduce_i32`が関数ポインタをcall_indirectで呼ぶ際のシグネチャ（型インデックス）がこの規約とずれている場合、wasmはシグネチャ不一致でトラップする（nativeは同じずれでもABI上たまたま動くことがある）。
アキュムレータがlong（i64）で要素がint（i32）という混合幅も、wasm32でのシグネチャ不一致の有力因子。

## 修正方針

wasmランタイムのreduce系ビルトイン（i32/i64/f64等の要素幅別）のコールバックシグネチャを、クロージャサンクの実シグネチャ（env, acc, elem）と一致させる。
アキュムレータ幅は言語仕様上longのためi64で統一し、要素幅だけをビルトイン名でディスパッチする（cm_slice系のwidth規約に合わせる）。
修正後、reduceに限らずsome/every/findIndex/forEachもwasmでキャプチャ付きクロージャの実行検証を行い、同型のずれがないか確認する。

## テスト計画

- キャプチャ付きreduce（int要素×longアキュムレータ、double要素、string要素があれば併せて）の回帰テストを追加し、wasmスイートを含む全バックエンドで検証する
- 既存のarray_higher_orderカテゴリのwasm実行状況を棚卸しし、skipされているならreduceだけでも有効化する

## 解決記録（実装済み）

根因は推定どおりシグネチャ不一致で、reduceのビルトイン選択が要素型のみ（int[]→reduce_i32）でアキュムレータ型を無視するため、`(long acc, int x)`のコールバックが`(i32,i32)->i32`として呼ばれていた（nativeはABIの偶然で動作、wasmはcall_indirectの型検査でトラップ）。
修正: 混合幅版`__builtin_array_reduce_i32_acc64`（+_closure）をnative/wasm両ランタイムへ追加し、HIR loweringでコールバック第1引数の型が64bitかつ要素が32bit以下の場合に選択、reduce式の結果型もアキュムレータ型を返すようにした。
LLVM宣言・js写像・MIRビルトイン表への登録と、acc64初期値のi64拡張（呼び出しサイト）も追加した。
回帰テスト tests/common/array_higher_order/reduce_acc64.cm（filter/mapチェーン・素のreduce・同幅reduce）をjit/native/js/wasmで検証した。
