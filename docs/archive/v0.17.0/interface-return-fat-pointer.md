---
title: インターフェース戻り値のfat pointer構築欠落（Q3）
parent: v0.17.0 Design
---

# インターフェース戻り値のfat pointer構築欠落（Q3）

## 概要

具象構造体をインターフェース戻り値型で返す関数（`Shape make() { return Rect{...}; }`）で、戻り値経由のメソッド呼び出しがjit=ゴミ値・native=別の誤値と分裂する。
ローカルでのupcast（`Shape s = rect;`）と引数渡し（`use_shape(rect)`）はH1/H2で対応済みで正常であり、returnサイトだけが壊れる。Y2（ユニオン戻り値）・Y5（配列→スライス戻り値）と同型の「returnサイトの暗黙変換欠落」ファミリとして起票された。

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
    Shape s = make();
    println("{s.area()}");
    // 期待12 → jit O2: -1342177280 / wasm: 9
    return 0;
}
```

## 実測で確定した真因（2026-08-05）

fat pointerの構築自体はreturnサイトでも行われていた（`retval = 具象ローカル`の代入をcodegenがpackする）。
真因は**ペイロードの寿命**で、fat pointerのdataポインタが呼び出し先スタックの具象ローカル（alloca）を指したまま返るため、return後にダングリングになる。
O0ではスタックが偶然無傷で正しく読め（jit/native O0=12）、O2ではスタック再利用でゴミ値、wasmはスタックレイアウト差で誤値（9）という「O0だけ動く」分裂だった。
`Shape s = r; return s;`（インターフェース値ローカル経由のreturn）も同じダングリングで、O2で壊れることを確認した。

## 実装記録

### 1. fat pointerペイロードのヒープboxing（llvm系: jit/native/wasm共通）

codegenの具象構造体→インターフェース代入のpack時（`statement/assign.cpp` Case A）に、ペイロードを`malloc`+`memcpy`でヒープへ実体化してからfat pointerへ包むようにした。
returnサイト限定でなく常時boxingとした: upcastはMIRがコピー一時を挟むスナップショット（値）意味論（`Shape s = r; r.w = 100;`でもsは旧値を見る——全バックエンド一致を実測確認）のため、boxingしても観測挙動は変わらず、return・エイリアス経由return・将来のエスケープ経路（集約格納等）が一括で安全になる。
wasmはnoStdだがランタイム（runtime_wasm.c）がmallocを提供するため自前宣言で呼ぶ。malloc無しのベアメタル系noStdターゲットのみboxingをスキップする（従来挙動のまま。インターフェースreturnの利用は想定外）。
ヒープペイロードの解放は未実装（既存のスライスreturn実体化と同じ扱い。インターフェース値のdrop対応は将来課題）。

### 2. jsの転送引数の再ラップ修正

検証マトリクスで、jsのみ`consume(make_rect())`（インターフェース値をインターフェース引数へ転送）が`Shape_Shape_vtable is not defined`の実行時エラーになることを追加検出した。
js codegenの引数梱包2サイトがソース型名のインターフェース判定を欠き、既にfat pointerである値を具象扱いで再ラップしていたため、let代入サイトと同じ`interface_names_`ガードを追加した（jsのデータ参照はGC管理のためboxing自体は不要）。

## 回帰テスト

- `tests/common/interface/iface_return_value.cm`: 戻り値の変数受け・直接メソッド呼び・引数転送・複数impl（Rect/Circle）・stringフィールド持ち・具象ローカル経由return・インターフェース値エイリアス経由return・upcast後変異のスナップショット意味論を、jit O0/O2・native O0/O2・wasm O0/O2・js・tsの全経路で出力一致検証。

## 検出経緯

未修正バグ調査（Q系）で検出。最小再現は `.tmp/bughunt5/q_r02min.cm`（ローカルupcast正常との対照付き）、引数正常の確認は `q_r02arg.cm`。
