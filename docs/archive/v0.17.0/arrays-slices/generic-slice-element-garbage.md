---
title: ジェネリック関数のT[]要素読みがnative/jitでガベージを返す
parent: v0.17.0 Design
---

# ジェネリック関数のT[]要素読みがnative/jitでガベージを返す

## 概要

型パラメータのスライス（`T[]`）を引数に取るジェネリック関数内で要素を添字読みして返すと、native/jitがガベージ値（未定義メモリ）を返す。
`xs.len()`は正しく、jsバックエンドは要素読みも正しいため、LLVM経路のモノモーフ化における`T[]`要素アクセスの型・幅解決の欠陥とみられる。
int要素で数値ガベージ、string要素では不正ポインタになりうる（クラッシュ危険）。

## 再現コード

```cm
import std::io::println;

<T> long count_all(T[] xs) {
    return xs.len();
}

<T> T first_of(T[] xs) {
    return xs[0];
}

int main() {
    int[] a = [];
    a.push(5);
    a.push(6);
    println(count_all(a));   // 全バックエンド: 2
    println(first_of(a));    // js: 5   native/jit O0/O3: ガベージ（例: 1833761696、実行ごとに変動）
    string[] s = [];
    s.push("x");
    println(count_all(s));   // 全バックエンド: 1
    println(first_of(s));    // js: x   native/jit: ガベージ（不正ポインタの危険）
    return 0;
}
```

## 現象

| 式 | native/jit | js |
|---|---|---|
| `count_all(a)`（len） | 2（正しい） | 2 |
| `first_of(a)`（int要素読み） | ガベージ | 5（正しい） |
| `first_of(s)`（string要素読み） | ガベージ | x（正しい） |

O0/O3とも再現し、値は実行ごとに変動する（未初期化スタック/誤アドレス読みの典型）。

## 根因候補

ジェネリック関数のモノモーフ化で、`T[]`引数の要素読み（`xs[0]`）が要素型Tの解決前の形（既定幅・既定getビルトイン）でloweringされ、特殊化時に`cm_slice_get_<width>`の選択やGEP幅がTの実型で再解決されていない可能性が高い。
`len()`はヘッダ読みで要素型に依存しないため正しく、要素アクセスだけが壊れるという症状と整合する。
C7/C8/C9のtypekeyモノモーフ化はフィールドアクセス型伝播を修正済みだが、関数引数の`T[]`添字読みの経路が漏れているとみられる（box_array_fieldはT[N]固定配列フィールドで、`T[]`スライス引数のケースは既存テストにない）。

## 修正方針

モノモーフ化の型置換で、関数引数・ローカルの`T[]`の要素アクセス（Index・get系ビルトイン呼び出し）のディスパッチ幅を特殊化後のTで再解決する。
具体的にはmono経路のスライス要素ディスパッチをslice_dispatch.hppの共通表引きに寄せ、T未解決のままビルトイン名が確定するポイントを潰す。
`xs[0]`の直接読みに加え、`for (T v in xs)`・`xs.pop()`・`xs[i] = v`（書き込み）・多次元`T[][]`も同じ置換経路を通ることを確認する。

## テスト計画

- `T[]`引数の要素読み・書き・for-in・pop・戻り値返しを、int/long/short/double/string/構造体要素で検証する回帰テストを追加する（O0/O3、全バックエンド）
- 既存のgenericsカテゴリに`T[]`スライス引数のケースが無いことを確認して補完する
- string要素はASan（--sanitize=address相当のローカル検証）でも不正アクセスがないことを確認する

## 解決記録（実装済み）

根因は3点の複合だった。
(1) mono/scan.cppのinfer_type_argsにT[]引数からの推論が無く（get_type_nameがArray型で空文字を返すため到達もしなかった）、(2) 戻り値先の一時がT型のままの自己推論（T = T）で無置換の特殊化first_of__Tが生成され、(3) 呼び出し結果ローカルがT型のまま残るためprintln引数変換がptrtointへフォールバックし値でなくアドレスが流れていた。
修正: get_type_nameへArrayケース（要素名+[]）を追加、スライス引数からの型推論（1b）と自己推論ガードを追加、specialize.cppの呼び出しサイト書き換えで宛先ローカルのT/T[]型を具体型へ差し替え、パッチしたローカルを引数に取るcm_println_int/cm_print_intのディスパッチを具体型（string/double/long/ulong/uint/bool）へ選び直すfixup_println_dispatchを追加した。
回帰テスト tests/common/generics/slice_param_element.cm（int/string/double要素）をjit/native/js/wasmで検証した。
