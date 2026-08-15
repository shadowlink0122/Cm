# v0.17.2 バグ修正: HashSetコンストラクタの二重解放（dtor持ち構造体の暗黙コピー）

セルフホスティング向け標準ライブラリの実装中に発見した4件目の不具合の記録。
コンパイラ本体の欠陥ではなく、Cmの現行セマンティクス（構造体代入は浅いコピー・コピー時に所有権の概念なし）とdtor持ち構造体の組み合わせで生じるライブラリ側の二重解放で、v0.17.2でライブラリ側を修正済み。

## 症状

`HashSet<T>` を使うプログラムが**非決定的に**SIGABRT（`pointer being freed was not allocated`）またはSIGTRAPで異常終了する。
同一バイナリ・同一プログラムでも実行ごとに完走・途中クラッシュが揺れ（10回中3回程度失敗）、ヒープの再利用状況に依存するため再現条件が安定しない。
表面上は「大量挿入でハングする」ように見え、当初はパフォーマンス問題やコンパイラのループと誤認しやすい。

## 真因

`HashSet.self()` がローカルで `HashMap<T, bool> m()` を構築し `self.map = m;` で代入していた。
Cmの構造体代入は浅いコピーであり、コンストラクタ終端で `m` のデストラクタが走って `entries` バッファを解放するため、`self.map.entries` は最初からdanglingポインタになる。
以後の挿入は解放済みメモリへの書き込みとして偶然動作し、`grow()` が旧バッファを `dealloc` した時点で二重解放としてmallocに検出される（検出されない実行では完走する）。

デバッグの決め手はハング中プロセスの `sample` によるスタック採取で、JITフレーム内からの `___BUG_IN_CLIENT_OF_LIBMALLOC_POINTER_BEING_FREED_WAS_NOT_ALLOCATED` が直接得られた。

## 修正

ライブラリ側（`libs/std/collections/hashset.cm`）で、代入後にローカル `m` の保持ポインタを明示的に無効化して所有権移動を表現した。
`HashMap` のデストラクタはnullポインタを解放しないため、`m` のdtorは無害化される。

```cm
self() {
    HashMap<T, bool> m();
    self.map = m;
    // mのdtorによる二重解放を防ぐ（所有権はself.mapへ移動済み）
    m.entries = 0 as Entry<T, bool>*;
}
```

同型のラッパーである `TreeSet`（`TreeMap` を内包）は、`TreeMap` がスライス（ランタイム管理）ベースでデストラクタを持たないため影響しない。
`libs/std` 全体を「ローカル構築→フィールド代入」パターンで横断確認し、dtor持ち構造体の該当箇所は `HashSet` のみだった。

## 言語側のフォローアップ（v0.17.2で実装済み）

この事故はライブラリ利用者が同じパターン（dtor持ち構造体を値としてコピー・フィールドへ代入）を書けば誰でも踏む。
恒久対策として挙げた「dtor持ち構造体の暗黙コピーへの警告」はv0.17.2内で実装した（[設計文書](dtor-copy-diagnostic.html)。let初期化・代入・return・構造体リテラルフィールドで警告、--strictでエラー）。
「move代入（`self.map = move m;`）のサポート」は既存のmove機構で既に機能することを確認し、本記録の回避策（ポインタの手動無効化）は `self.map = move m;` へ置き換えた。

## 回帰テスト

- `tests/common/stdlib/collections/hashset_basic.cm` — 機能検証（既存）
- `tests/common/stdlib/collections/hashset_load.cm` — 10000件の挿入・参照・半数削除のパフォーマンススモーク（本修正で新規追加。修正前は非決定的クラッシュ）
- `tests/benchmarks/cm/09_hashset_ops.cm` — 50000件のベンチマーク
