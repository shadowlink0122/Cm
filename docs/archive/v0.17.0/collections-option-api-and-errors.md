# コレクションのOption返しAPIとエラー型統合（実装済み）

## 対象所見

| # | 領域 | 所見 | 状態 |
|---|------|------|------|
| H8 | 言語 | HashMap/TreeMapの`get`がキー不在時に未初期化メモリまたは無関係な値を返す（Option返しAPIが無い）、stdの`CmResult`は`unwrap`が`is_ok`を確認しない | 実装済み（`get -> Option<V>` + `get_or(key, default)`へ変更。TreeMapの「不在時に根ノードの値を返す」危険な契約を廃止） |
| M17 | 言語 | エラー処理が三重分裂（組み込み`Result`/`Option`+`?`は健全だが、stdはHazard #34回避のため`unwrap`が`is_ok`を見ない別物`CmResult`を出荷、ioはさらに別方式） | 実装済み（Hazard #34の解消を確認し`CmResult`を撤去、ioのパース関数を`Option`返しへ移行。エラー処理は組み込み`Result`/`Option`+`?`の1系統に統合） |

## 背景と根本原因

### マップの`get`が不在キーを区別できない（H8）

`HashMap<K, V>::get`はキーが見つからない場合、未初期化のローカル変数をそのまま返している。

libs/std/collections/hashmap.cm:66-83
```cm
V get(K key) {
    // ... 線形探索 ...
    // デフォルト値（型に依存）
    V default_val;
    return default_val;
}
```

`V default_val;`は初期化されないローカルであり、監査所見H4（未初期化構造体フィールドがnative=スタックゴミ・jit=クラッシュ・wasm/js=ゼロ初期化）と同じ分裂を引き起こす。呼び出し側は「値が無い」ことと「値がゼロ/空だった」ことを区別できない。

`TreeMap<K, V>::get`はさらに危険で、不在時にindexを0へ丸めて別ノードの値を返す。

libs/std/collections/treemap.cm:170-176
```cm
V get(K key) {
    int idx = self.find_index(key);
    if (idx < 0) {
        idx = 0;
    }
    return self.vals[idx];
}
```

`find_index`は-1（不在）を返すが（treemap.cm:154）、`get`はそれを0に置換するため、空でないマップでは常に根ノードの値を返す。コメント（treemap.cm:169）も「戻り値は未規定なので事前にcontainsで確認すること」と、安全でない契約を明文化してしまっている。

いずれのマップにも「不在」を型で表現する経路が無い。組み込み`Option<T>`は型検査器に登録済み（src/internal/types/checking/auto_impl.cpp:470-513でSome/None・is_some/is_none/unwrap/unwrap_or/expectを登録）で、prelude enumとしてHIR/MIRへ注入される（src/internal/types/checking/decl.cpp:27-56）にもかかわらず、コレクションAPIはこれを一切使っていない。

### エラー型の三重分裂（M17）

Cmには「エラーを表す型」が3系統存在し、互換が無い。

1. 組み込み`Result<T, E>` / `Option<T>`（enum + `?`演算子、auto_impl.cpp:470-513・decl.cpp:27-56で登録）。これは健全。
2. std独自の`CmResult<T, E>`（struct方式）。libs/std/core/result.cm:27-31で構造体として定義され、`result_unwrap`が`is_ok`を検査せず常に`value`を返す。

   libs/std/core/result.cm:47-49
   ```cm
   <T, E> T result_unwrap(CmResult<T, E> r) {
       return r.value;
   }
   ```

   result.cm:1-7のヘッダコメントが、これは「Hazard #34（ジェネリックenum版Resultのmatch内型解決問題）」回避のためstruct方式で出荷している、と経緯を記している。組み込み`Result`名との衝突回避のため内部名`CmResult`を使う設計になっている。
3. ioは第3の方式。libs/std/io/console/input.cmの`parse_int`/`parse_double`等（input.cm:101-227）はパース失敗を0/0.0で握り潰し、エラーを一切返さない（input_boolも未知入力をfalseに丸める、input.cm:83-89）。

結果として、利用者はモジュールごとに異なる「失敗の表し方」を学ぶ必要があり、`?`演算子は組み込み`Result`/`Option`にしか効かないため`CmResult`とは連結できない。

## 設計方針

### 1. マップの`get`をOption返しAPIへ

`get`の戻り値型を`V`から`Option<V>`へ変更する。不在時は`Option::None`、存在時は`Option::Some(value)`を返す。

- 破壊的変更の回避（Cm言語設計原則）のため、既存の`get(K) -> V`は残さず**新APIへ寄せる**のではなく、以下の二段構えにする。
  - `get(K key) -> Option<V>`: 新しい安全なAPI（Option返し）。
  - `get_or(K key, V default) -> V`: デフォルト値を明示する非Option版（旧`get`の安全な代替。unwrap_orの糖衣）。
- 旧来の「不在時に未規定値を返す`get`」は挙動が危険であり互換を保つ価値が無いため、`get`のシグネチャ変更を許容する（後述の非互換性で移行を扱う）。
- `contains`は据え置き（`find_index(key) >= 0`のまま、hashmap.cm:86-101 / treemap.cm:165-167）。

`TreeMap::get`は`find_index`が既に-1で不在を返せるため、その-1をNoneへ写像するだけでよい（treemap.cm:171の`idx = 0`置換を削除）。`HashMap::get`は探索ループ内でヒット時にSomeを、ループ脱出後にNoneを返す形へ書き換える。

### 2. stdのエラー型を組み込み`Result`/`Option`へ統合

`CmResult`を段階的に廃止し、std全体を組み込み`Result<T, E>` / `Option<T>`へ寄せる。

- `CmResult`を出荷した理由（Hazard #34）は「ジェネリックenumのmatch内型解決」だが、現在は組み込み`Result`/`Option`がprelude enumとして注入され（decl.cpp:27-56）、`is_some`/`unwrap`等のメソッドも登録済み（auto_impl.cpp:470-513）で健全化されている。前提が解消しているかをHazard #34の再現テストで確認し、解消済みなら`CmResult`は不要になる。
- `result_unwrap`のような「`is_ok`を見ない」ヘルパは、組み込み`Option`/`Result`の`unwrap`（不在/失敗でパニックする契約）へ置換する。
- ioのパース関数（parse_int等）は`Option<int>`返しへ移行する。失敗を0で握り潰す現状（input.cm:101-136）をやめ、非数字先頭や空文字列でNoneを返す。既存の`input_int` -> `int`は`parse_int(...).unwrap_or(0)`等で内部的に糖衣として残し、外部契約を壊さない。

### 3. `?`演算子との連結

統合後は`get(k)?`や`parse_int(s)?`が`Option`/`Result`を返す関数内でそのまま早期returnに使え、モジュールをまたいだエラー伝播が1つの仕組みに揃う。

## 構文例・出力例

移行後の利用イメージ（設計目標。現時点では未実装）。

```cm
import std::collections::hashmap::*;

HashMap<string, int> m();
m.insert("apple", 3);

// Option返しAPI
Option<int> v = m.get("apple");
if (v.is_some()) {
    println(v.unwrap());        // 3
}
println(m.get("banana").is_none());  // true

// デフォルト値版
int n = m.get_or("banana", 0);       // 0

// ? による伝播（Optionを返す関数内）
Option<int> lookup(HashMap<string, int> m, string k) {
    int found = m.get(k)?;   // NoneならここでNoneをreturn
    return Option::Some(found + 1);
}
```

TreeMapも同一のAPI形状に揃える（`get -> Option<V>`, `get_or`, `contains`）。

## 実装の段階分割

1. 第1段: `HashMap::get`と`TreeMap::get`を`Option<V>`返しへ変更し、`get_or`を追加。`contains`は据え置き。マップ利用テストを新APIへ更新。
2. 第2段: Hazard #34再現テストで組み込みジェネリックenumのmatch型解決が健全か確認。健全なら`CmResult`利用箇所を洗い出し、組み込み`Result`/`Option`へ置換。
3. 第3段: `libs/std/core/result.cm`の`CmResult`および`result_unwrap`系ヘルパを非推奨化し、参照ゼロを確認後に削除（未実装のまま破棄でなく置換完了後に撤去）。
4. 第4段: ioのパース関数（parse_int/parse_long/parse_double/input_bool）を`Option`返しへ移行し、`input_int`等の公開関数は内部で`unwrap_or`する糖衣として維持。

## テスト計画（tests/common/配下）

既存の`tests/common/collections/`（`hashmap_test.cm`+`.expect`, `treemap_test.cm`+`.expect`の形式）に追加する。

- `tests/common/collections/hashmap_option_get_test.cm` + `.expect`: 存在キーで`Some`・不在キーで`None`・`unwrap`/`unwrap_or`/`get_or`を全バックエンド（jit/native/wasm/js/ts）で一致確認。空マップと非空マップ両方で不在検索し、negative check（不在時に他の値が返らないこと）を含める。
- `tests/common/collections/treemap_option_get_test.cm` + `.expect`: 根ノードが存在する状態で不在キーを引き、旧バグ（idx=0で根の値を返す）が再発しないことを検証。
- `tests/common/errors/result_option_unification_test.cm` + `.expect`: 組み込み`Result`/`Option`の`unwrap`が失敗時にパニックすること、`?`演算子でのマップlookup伝播、`get_or`のデフォルト経路を検証。
- `tests/common/errors/parse_option_test.cm` + `.expect`: `parse_int`系のOption返しで、空文字列・非数字先頭がNoneになり、有効入力がSomeになることを確認。

## リスクと非互換性

- `get`の戻り値型変更（`V` -> `Option<V>`）は破壊的変更。既存の`int v = m.get(k);`は`int v = m.get(k).unwrap();`または`int v = m.get_or(k, default);`への書き換えが必要。v0.17.0のリリースノートに移行手順を明記し、tests/common内の既存マップ利用箇所を一括更新する。
- `CmResult`削除はstd内・利用者コード双方の参照を壊す。参照ゼロを確認するまで削除しない（第3段の順序を厳守）。
- 組み込みジェネリックenumの`match`型解決（Hazard #34）が未解消の場合、`CmResult`廃止は保留し、マップの`get`のみ先行する（第1段は`CmResult`に依存しないため独立して実施可能）。
- ioパース関数のOption化は`input_int`等の内部実装のみ変更で公開契約は不変（糖衣で吸収）だが、失敗時に0を返す従来挙動に依存したコードは`unwrap_or`の既定値と挙動が一致することを確認する。

## 関連

- 監査レポート: docs/design/v0.17.0/large-scale-bottleneck-audit.md（H8, M17, および関連するH4未初期化フィールド分裂）
- 組み込みOption/Result登録: src/internal/types/checking/auto_impl.cpp:470-513, src/internal/types/checking/decl.cpp:27-56
- 対象std実装: libs/std/collections/hashmap.cm, libs/std/collections/treemap.cm, libs/std/core/result.cm, libs/std/io/console/input.cm

## 実装記録

- HashMap/TreeMapの`get`を`Option<V>`返しへ変更し、`get_or(key, default)`を追加。既存テストは`get_or`へ移行。
- Hazard #34（ジェネリックenumのmatch内型解決）は解消済みであることをプロトタイプで確認し（ジェネリックimplメソッドからの`Option<T>`返却がjit/native/wasm/jsで健全）、`libs/std/core/result.cm`（`CmResult`と`is_ok`を見ない`result_unwrap`）を撤去。`tests/common/result/result_methods.cm`は組み込み`Result`のメソッドテストへ書き換え。
- ioのパース関数を`export Option<T>`返しへ移行（`parse_int`/`parse_long`/`parse_double`、新規`parse_bool`）。空文字列・非数字先頭はNone。`input_int`等の公開関数は`unwrap_or`の糖衣として契約を維持。あわせて`input.cm`の潜在構文エラー（`as double * fraction`の優先順位）を修正。
- 実装中に、組み込みResult/Optionメソッドの脱糖がレシーバを複製するため`map.get(k).is_none()`のような呼び出しレシーバのチェーンで多重評価により誤った値を返す欠陥を発見。matchのscrutinee退避プリパス（match_hoist.cpp）を拡張し、呼び出しを含むレシーバを一時変数へ退避して修正した。
- テスト: `hashmap_option_get_test.cm`・`treemap_option_get_test.cm`（不在キーのnegative check含む）・`std/parse_option_test.cm`（チェーン形レシーバ検証含む）。
