---
title: 固定長配列→スライス引数の暗黙変換欠落でゴミ値（Y5）
parent: v0.17.0 Design
---

# 固定長配列→スライス引数の暗黙変換欠落でゴミ値（Y5）

## 概要

固定長配列を `int[]`（スライス）引数の関数へ渡すと、checkerは受理するが実引数の固定長配列blobがCmSliceヘッダとして解釈され、`len()`がゴミ値を返し要素読みが不定になる。
全バックエンド・全最適化レベルで同一に誤る無診断のデータ破壊で、O2では畳み込みにより別の誤値（0）になる。
メソッドレシーバ経路（`fixed.len()` 等）にはHIR loweringの`needs_array_to_slice`による`cm_array_to_slice`変換が実装済みであり、関数引数経路だけが変換を欠いている。

## 再現コード

```cm
import std::io::println;
int sum_slice(int[] xs) {
    int s = 0;
    for (int i = 0; i < xs.len(); i++) {
        s = s + xs[i];
    }
    return s;
}
int main() {
    int[3] fixed = [1, 2, 3];
    println("{sum_slice(fixed)}");
    // 期待6 → -101222490等のゴミ値（O2では0）
    return 0;
}
```

## 原因

型検査の`types_compatible`は固定長配列→スライスを互換として受理するが、HIR/MIRの関数呼び出しloweringに引数位置での`cm_array_to_slice`変換が無く、固定長blobのアドレス（または値）がそのままスライス引数へ渡る。
メソッドレシーバ側（`expr_member.cpp`の`needs_array_to_slice`、X3のpush引数変換）と同じ変換が呼び出し引数に必要である。

## 修正方針

1. 呼び出し引数loweringで「宣言パラメータ型がスライス・実引数が固定長配列」の組を検出し、`cm_array_to_slice(&arr, len, elem_size)`で正規ヘッダへ実体化してから渡す（要素サイズは`elem_size_of`共通ヘルパを使用）。
2. 適用サイトはユーザー関数・関数ポインタ呼び出し・デフォルト引数・可変長引数の固定部の全てとし、既存のメソッドレシーバ経路の変換と実装を共有する（変換ロジックの再複製を避ける）。
3. 仕様判断: 変換は読み取りビューではなくヒープコピーであるため、呼び出し先でのpush等が呼び出し元の固定長配列へ反映されない意味論を明文化する（チュートリアルの配列/スライス頁に追記）。

## テスト計画

- regression: `int[3]`/`string[2]`/構造体配列をスライス引数へ渡し、len・要素値・呼び出し先変異の非伝搬を検証する。
- integration（native/jit/wasm/js）: 再現コードの期待値実行と4系一致。

## 検出経緯

v0.17.0全修正後のレイヤー別レビュー（第4ラウンド）で検出。最小再現は `.tmp/bughunt4/a/a01_fixed_to_slice.cm`。
