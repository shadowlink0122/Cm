---
title: privateフィールドの外部アクセスが無検査（X2）
parent: v0.17.0 Design
---

# privateフィールドの外部アクセスが無検査（X2）

## 概要

構造体の`private`フィールドに対する外部（implブロック外）からの読み書きが、check・check --strict・lintのいずれでも診断されず、そのまま実行できる。
privateメソッドの外部呼び出しは正しくエラーになる（`Cannot call private method ... from outside impl block`）ため、可視性検査がメソッドにのみ実装されフィールドに未実装という非対称になっている。
チュートリアル（docs/tutorials/ja/types/structs.md系）とテストコメント（tests/common/structs/struct_modifiers.cm「外部アクセス不可（impl内のみ）」）が明記する仕様に反する。

## 再現コード

```cm
import std::io::println;
struct S {
    int open;
    private int secret;
}
int main() {
    S s;
    s.open = 1;
    s.secret = 42;
    // 仕様上エラーであるべきだが無診断で通過する
    println("{s.secret}");
    // 42が出力される（読みも通過）
    return 0;
}
```

`cm check` / `cm check --strict` / `cm lint` すべて errors: 0, warnings: 0。

## 対比（正しく動く可視性検査）

- privateメソッドの外部呼び出し → エラー（p01系）
- 他構造体のimplメソッドからのprivateメソッド呼び出し → エラー
- 構造体リテラル経由のprivateフィールド初期化（`S s = {open: 1, secret: 2};`）も検査対象に含めるか要仕様決定

## 原因

型検査のメンバアクセス解決（`src/internal/types/checking/`のフィールドアクセス経路）が、メソッド呼び出し側にある`private`判定（`Cannot call private method`を出す検査）に相当するフィールド版を持たない。
フィールドの`private`修飾はパース・保持されるが（struct定義には残る）、アクセスサイトでの参照チェックが未接続。

## 修正方針

1. フィールドアクセス（読み・書き・複合代入・&取得・補間内参照）の型検査時に、対象フィールドが`private`かつアクセス元が当該構造体のimplブロック外であればエラーを出す（メソッドと同じ「impl block of 'S'」粒度）。
2. 構造体リテラルでのprivateフィールド初期化の扱いを仕様として決める（Rustは不可・コンストラクタ相当のメソッド経由を要求。実装の破壊度を考慮し、まず警告→--strictでエラーの段階導入も可）。
3. 既存テストのprivateフィールドを含む構造体（struct_modifiers.cm等）が新検査でエラーにならないよう、テスト側のアクセスがすべてimpl内である事を確認する。

## テスト計画

- errorsスイート: 外部からのprivateフィールド読み・書き・リテラル初期化の3ケースを期待エラー付きで追加する。
- 既存のimpl内アクセス（getter/setter経由）が引き続き通ることの回帰。

## 検出経緯

native/jit網羅検証第3ラウンドで検出。最小再現は `.tmp/nativejit-bughunt3/private/p02_private_field_err.cm`。

## 解決記録（実装済み）

型検査のフィールドアクセス解決（infer_member）へ、メソッド側と同じimpl粒度のprivate検査を追加した（Cannot access private field ... from outside impl block of 'S'）。
読み・書き・複合代入・補間内参照はすべてinfer_member経由のため同検査で拒否される。
構造体リテラルでのprivateフィールド初期化は現状許容のまま（仕様決定は将来。リテラル構築がコンストラクタ代替として広く使われているため段階導入とする）。
回帰テスト tests/common/errors/private_field_access.cm（外部書き込み拒否）と tests/common/impl/private_field_impl_access.cm（impl内getter/setter許可）を追加した。
