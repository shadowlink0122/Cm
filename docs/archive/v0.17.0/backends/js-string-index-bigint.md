---
title: jsの文字列添字にlong型変数を使うとTypeError
parent: v0.17.0 Design
---

# jsの文字列添字にlong型変数を使うとTypeError

## 概要

jsバックエンドで文字列の添字アクセス（`s[i]`）の添字にlong型の変数を使うと、生成コードが`"...".charCodeAt(BigInt値)`となり`TypeError: Cannot convert a BigInt value to a number`で実行時クラッシュする。
H5（long/ulongのBigInt表現化）で64ビット整数はBigIntになったが、文字列添字の発行経路（charCodeAt/charAt）にNumber正規化が入っていない残欠。
int添字・数値リテラル添字は正常で、native/jit/wasmも正常。

## 再現コード

```cm
import std::io::println;

int main() {
    string s = "hello";
    println(s[1]);                       // 全バックエンド: e（リテラル添字は正常）
    for (long i = 0; i < 3; i++) {
        println(s[i]);                   // js: TypeError（charCodeAt(BigInt)）   他: h e l
    }
    return 0;
}
```

生成JS（抜粋）:

```js
_t1015_18 = "hello".charCodeAt(_t1014_17);  // _t1014_17はBigInt（long型ループ変数）
```

## 根因候補

jsコード生成の文字列添字経路が添字オペランドをそのまま`charCodeAt(idx)`へ埋め込んでおり、H5でlong系ローカルがBigInt値になったことへの追従（`Number(...)`正規化）が漏れている。
同型の問題は`__cm_str_slice`（substring系）ではH5実装時に`start = Number(start)`で対処済みで、charCodeAt/charAt経路だけが未対応。

## 修正方針

jsコード生成の文字列添字・charAt発行箇所で、添字式を`Number(__cm_big(idx))`相当（H5の冪等ヘルパー経由）または`Number(idx)`でラップする。
文字列APIでBigIntが流入しうる他の引数位置（indexOfの将来のfromIndex、repeat等があれば）も同時に棚卸しして正規化を統一する。

## テスト計画

- long/isize添字での文字列添字・charAtアクセスをループで検証する回帰テストを追加し、jsスイート（O0/O3両方）を含む全バックエンドで実行する
- H5の境界変換テスト（long_bigint_precision）へ文字列添字ケースを追記する

## 解決記録（実装済み）

js/builtins.cppの__builtin_string_charAt写像を`charCodeAt(Number(添字))`へ、codepoint_atの添字も`Number()`正規化へ変更した（H5の__cm_str_sliceと同型の対処）。
回帰テスト tests/common/string/index_long_var.cm（longループ変数添字・charAt）をjit/native/js/wasmで検証した。
