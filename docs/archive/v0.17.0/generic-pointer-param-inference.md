# R3: ジェネリック`T*`引数の型パラメータ束縛失敗（swap等が全経路SIGSEGV）

**ステータス:** 修正済み
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

引数の型マッチング（infer/unify）がポインタ型`T*`の内側の`T`を実引数`int*`の`int`と単一化していない。値パラメータ（`T x`）では機能する（既存テストで確認済み）ため、ポインタ被覆下の型変数のunifyが欠けている。mono型駆動化（monomorphization-typed-instantiation）の範囲外だった経路の可能性が高く、`substitute_generic_type`がポインタ内型変数へ実引数を差し込めていない疑い。要ソース特定。

## 修正方針

型推論のunifyへ「実引数型と宣言型を構造再帰でマッチし、宣言側の型変数へ実引数の対応部分型を束縛する」処理を追加（`T*`↔`int*`で`T=int`、`T[]`↔`int[]`等も同様）。明示型引数`f<int>(...)`の経路も`T`の実体化を適用する。無置換のまま`T`がcodegenへ到達したら診断で停止する防衛層（mono型駆動化の「無置換特殊化の常時検査」を`T*`経路にも効かせる）を追加する。

## テスト計画

`tests/common/generics/`へ: `<T> void swap(T*, T*)`・`<T> T deref(T*)`（推論・明示型引数の両方）・`T[]`引数・ネスト`T**`の値一致をjit/native O0〜O3/wasm/jsで確認。無置換で残った場合にSIGSEGVでなく診断で止まる負のテスト。stdlibの`std::iter::adapters::swap`の実動作テスト（R9のmod.cm修正後）。

## 実装記録（修正済み）

見立てどおり2系統の欠落で、checker推論とcodegen最適化の両方を修正した。

1. **checker側（型推論の構造再帰化）**: `infer_generic_call`（src/internal/types/checking/generic.cpp）は「`T`」「`Box<T>`」「`Node<T>*`（内側がジェネリック構造体のポインタ）」の3ケースを個別のif連鎖で扱い、**裸の型変数へのポインタ`T*`・配列`T[]`・多段`T**`が漏れていた**。宣言型と実引数型を並行に辿る再帰unifyヘルパー（型変数→束縛・Pointer/Reference/Array→要素型を剥がして再帰・同名ジェネリック構造体→型引数を1対1で再帰）へ置換し、既存3ケースを包含した。これにより`deref`の戻り値`T`もTが束縛されるようになった。
2. **codegen側（selfコピー最適化の誤ヒット）**: checker修正だけでは無言死がSIGSEGVのまま残った。真因はLLVM codegen（src/internal/codegen/llvm/core/translate/function.cpp）の「プリミティブimplメソッドのselfコピー先一時変数を要素型で確保する」最適化のシード条件が**「関数名に`__`を含む」だけ**だったこと。特殊化されたジェネリック関数`deref__int`も`__`を含むため第0引数`a`（`*int`）がselfと誤認され、`_6 = copy(a)`のコピー先が`int`（i32）で確保→ポインタ値をi32として読み、ユーザーの`*a`が壊れたアドレスを辿ってクラッシュしていた。シード条件へ「第0引数のローカル名が`self`である」を追加した（プリミティブimplメソッドのselfは名前が`self`・ジェネリック関数の引数は`a`等で区別できる）。
3. **回帰**: `tests/common/generics/pointer_param_inference.cm`（swapのint/double/string値スワップ・推論deref・`T[]`のfirst・`T**`のderef2・ジェネリック構造体unboxの非回帰）をjit・native O0/O2/O3・wasm・jsの出力一致で確認。プリミティブimplメソッド（`impl int { doubled() }`）の非回帰も確認した。stdlibの`std::iter::adapters::swap`は直接importで動作確認済み（`import std::iter::*`はR9で別途未解決）。
