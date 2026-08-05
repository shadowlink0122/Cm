---
title: インターフェース戻り値のfat pointer構築欠落（Q3）
parent: v0.17.0 Design
---

# インターフェース戻り値のfat pointer構築欠落（Q3）

## 概要

具象構造体をインターフェース戻り値型で返す関数（`Shape make() { return Rect{...}; }`）で、戻り値のfat pointer（データ+vtable）が構築されず、戻り値経由のメソッド呼び出しがjit=ゴミ値・native=別の誤値と分裂する。
ローカルでのupcast（`Shape s = rect;`）と引数渡し（`use_shape(rect)`）はH1/H2で対応済みで正常であり、**returnサイトだけが欠落**している。Y2（ユニオン戻り値）・Y5（配列→スライス戻り値は対応済み）と同型の「returnサイトの暗黙変換欠落」ファミリである。

## 再現コード

```cm
interface Shape {
    int area();
}
struct Rect { int w; int h; }
impl Rect for Shape {
    int area() {
        return self.w * self.h;
    }
}
Shape make() {
    return Rect { w: 3, h: 4 };
}
int main() {
    Rect direct = {w: 2, h: 5};
    Shape sd = direct;
    println("{sd.area()}");
    // 10（正常）
    Shape s = make();
    println("{s.area()}");
    // 期待12 → jit: -1342177280 / native O0: 9
    return 0;
}
```

## 修正方針

1. return lowering（stmt/control.cpp）の暗黙変換チェーン（coerce_to_float_context→coerce_to_union）に、具象構造体→インターフェース型のfat pointer構築（H1のiface一時代入と同じ機構）を追加する。
2. LoweringContextへ`coerce_to_interface(value, dest_type)`を追加し、returnに加えて構造体リテラルフィールド・スライスリテラル要素等の既存H1サイトと実装を共有できるか確認する（既存サイトの重複実装があれば統合）。
3. 回帰: 戻り値経由のメソッド呼び出し（直接呼び・変数受け・引数転送）をnative/jit/wasm/jsで検証する。

## 検出経緯

第5ラウンドで検出。最小再現は `.tmp/bughunt5/q_r02min.cm`（ローカルupcast正常との対照付き）、引数正常の確認は `q_r02arg.cm`。
