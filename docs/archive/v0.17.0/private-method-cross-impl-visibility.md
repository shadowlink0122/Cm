---
title: privateメソッドが同一構造体の別implブロックから呼べる（X6）
parent: v0.17.0 Design
---

# privateメソッドが同一構造体の別implブロックから呼べる（X6）

## 概要

`impl S for A` 内で定義したprivateメソッドを、同じ構造体Sの別インターフェース実装 `impl S for B` のメソッドから `self.helper()` で呼び出せる。
チュートリアル（docs/tutorials/ja/types/interfaces.md）はprivateメソッドを「この impl ブロック内でのみ呼び出し可能」と定義しており、現実装の「同一構造体のimplブロック群で共有」はこれと乖離する。
mainなど非implコンテキストからの呼び出しと、他構造体のimplメソッドからの呼び出しは正しく拒否されるため、検査粒度がimplブロック単位でなく構造体単位になっているのが原因。
重大度は低い（可視性が想定より広いだけで誤動作はしない）が、仕様と実装のどちらかに合わせる決定が必要。

## 再現コード

```cm
import std::io::println;
interface A {
    int fa();
}
interface B {
    int fb();
}
struct S { int v; }
impl S for A {
    private int helper() {
        return 10;
    }
    int fa() {
        return self.helper();
    }
}
impl S for B {
    int fb() {
        return self.helper() + 1;
        // チュートリアル仕様ではエラーであるべきだが受理され、実行結果は11
    }
}
int main() {
    S s = {v: 0};
    println("{s.fa()} {s.fb()}");
    // 10 11 が出力される
    return 0;
}
```

対比（正しく拒否される）:

- mainからの `s.helper()` → `Cannot call private method 'helper' from outside impl block of 'S'`
- 他構造体のimplメソッドからの `target.helper(7)` → 同エラー

## 原因

privateメソッドの可視性検査がエラーメッセージ（`from outside impl block of 'S'`）どおり「構造体Sのimplブロック集合の外か」だけを判定し、呼び出し元がどのimplブロックかを区別していない。

## 修正方針（仕様決定を含む）

1. 仕様をどちらかに確定する。案A: チュートリアルどおりimplブロック単位に絞る（検査に呼び出し元implブロックIDの一致判定を追加）。案B: 現実装の構造体単位を正とし、チュートリアルの記述を「同じ構造体のimpl内でのみ」に修正する。
2. 案Bを採る場合もエラーメッセージとドキュメントの表現を統一する（C++のprivate=クラス単位に近い意味論であることを明記）。
3. どちらの案でも、インターフェース値経由（動的ディスパッチ）でprivateメソッドが見えないことの回帰を追加する（現状は`Unknown method 'helper' for type 'I'`で拒否され正常）。

## テスト計画

- errorsスイートまたはcommon: 同一構造体・別implからのprivate呼び出しの期待挙動（仕様決定後にエラーor受理で固定）。
- 他構造体impl・非implコンテキスト・インターフェース値経由の3拒否ケースの回帰。

## 検出経緯

native/jit網羅検証第3ラウンドで検出。最小再現は `.tmp/nativejit-bughunt3/private/p03_private_cross_impl.cm`。

## 解決記録（案B採用: 構造体単位の可視性を正とする）

仕様を現実装（構造体単位の可視性、C++のprivate相当）に確定し、チュートリアル（ja/en interfaces.md）の記述を「同じ構造体のimplブロック内でのみ」へ修正した。
エラーメッセージ（from outside impl block of 'S'）は構造体粒度の表現として現状のままとする。
回帰テスト tests/common/impl/private_method_cross_impl.cm（同一構造体の別implからの呼び出し受理）を追加し、非implコンテキストからの拒否は既存テストで担保する。
