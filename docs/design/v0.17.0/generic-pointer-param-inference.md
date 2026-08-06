# R3: ジェネリック`T*`引数の型パラメータ束縛失敗（swap等が全経路SIGSEGV）

**ステータス:** 未修正（第6ラウンド検出）
**重大度:** Critical

## 症状（実測: cm 0.17.0、プローブ `.tmp/bughunt6/{stdlib,verify}/`）

ポインタ型引数`T*`から型パラメータ`T`が推論も明示指定でも束縛されず、型未確定のままcodegenに落ちてSIGSEGVする。stdlibの`swap`（`std::iter::adapters::swap`）を含む`T*`引数のジェネリック関数全般が該当。

最小再現（stdlib非依存）:
```cm
import std::io::println;
<T> void my_swap(T* a, T* b) {
    T tmp = *a;
    *a = *b;
    *b = tmp;
}
int main() {
    int a = 1;
    int b = 2;
    my_swap(&a, &b);
    println("a={a} b={b}");
    return 0;
}
```
実測: jit O0・jit O2・native O2 いずれも**無出力でSIGSEGV（rc=139）**。非ジェネリック版`my_swap_int(int*, int*)`は正常（`a=2 b=1`）。

関連プローブ: `<T> T deref(T* a)`は明示`deref<int>(&x)`でも`error: expected 'int', got 'T'`となり、**`T*`引数からのT推論も明示型引数の代入も機能していない**。空ボディの`<T> void noop(T* a)`はコンパイルが通るため、本体で`T`を実体として使う（`T tmp = *a`のデリファレンス・値コピー）と型未解決が露見してクラッシュする。

## 真因の見立て

引数の型マッチング（infer/unify）がポインタ型`T*`の内側の`T`を実引数`int*`の`int`と単一化していない。値パラメータ（`T x`）では機能する（既存テストで確認済み）ため、ポインタ被覆下の型変数のunifyが欠けている。第5ラウンドmono型駆動化（monomorphization-typed-instantiation）の範囲外だった経路の可能性が高く、`substitute_generic_type`がポインタ内型変数へ実引数を差し込めていない疑い。要ソース特定。

## 修正方針

型推論のunifyへ「実引数型と宣言型を構造再帰でマッチし、宣言側の型変数へ実引数の対応部分型を束縛する」処理を追加（`T*`↔`int*`で`T=int`、`T[]`↔`int[]`等も同様）。明示型引数`f<int>(...)`の経路も`T`の実体化を適用する。無置換のまま`T`がcodegenへ到達したら診断で停止する防衛層（mono型駆動化の「無置換特殊化の常時検査」を`T*`経路にも効かせる）を追加する。

## テスト計画

`tests/common/generics/`へ: `<T> void swap(T*, T*)`・`<T> T deref(T*)`（推論・明示型引数の両方）・`T[]`引数・ネスト`T**`の値一致をjit/native O0〜O3/wasm/jsで確認。無置換で残った場合にSIGSEGVでなく診断で止まる負のテスト。stdlibの`std::iter::adapters::swap`の実動作テスト（R9のmod.cm修正後）。
