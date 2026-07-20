---
title: 型同一性の構造化（再帰的型キー）
parent: v0.17.0 Design
---

# 型同一性の構造化（再帰的型キー）

## 対象所見

| # | 領域 | 所見 | 状態 |
|---|------|------|------|
| C7 | ジェネリクス | ネストした型引数（`Box<Pair<int,string>>`等）が全バックエンドで誤コンパイル（`__`マングリングが非単射） | 未着手 |
| C8 | ジェネリクス | ユーザーが`Foo__int`という構造体を書くと`Foo<int>`の特殊化と同名衝突（レイアウト差でGEP検証エラー、同一なら黙って混同） | 未着手 |
| C9 | ジェネリクス | `Box<T>`の`T[N]`配列フィールドがnativeでゴミ（MIRに未置換の`T[]`型が残り、バックエンド間で結果が分裂） | 未着手 |

## 背景と根本原因

型の同一性が「基底名 + `__` + 型引数」のフラットな連結文字列で表現されており、この連結が非単射である。

特殊化名の生成は `src/internal/mir/lowering/mono/typeinfo.cpp` の3関数に集約されている。
- `normalize_type_arg`（22-76行）: `*int`を`ptr_int`へ、`Box<Box<int>>`のような`<...>`表記を`__`区切りへ平坦化する。ネスト引数は`depth`を数えてカンマ分割するが、区切りには常に`__`しか使わないため階層情報が失われる。
- `make_specialized_struct_name`（79-86行）: `base_name + "__" + normalize_type_arg(arg)` を型引数分だけ連結する。
- `make_specialized_name`（89-111行）: 関数特殊化名を同じ規則で生成する。

この結果、`__`が有効な識別子文字である以上、以下の3組が同一キーへ縮退する。
- `Box<Box<int>>` → `Box__Box__int`
- `Box<Box, int>`（2引数） → `Box__Box__int`
- ユーザー定義構造体 `Box__Box__int`（C8） → 同上

分解側も同じ曖昧さを抱える。
- `Monomorphization::collect_struct_specializations`（`src/internal/mir/lowering/mono_structs.cpp` 126-159行）は `Node__int` を「`__`で最初に切って基底名、残りを`__`で再分割して型引数」と復元する。ネストがあると復元結果が原型と一致しない。
- `substitute_type_in_type`（`src/internal/mir/lowering/mono_internal.cpp` 50-374行）も `__`・`<>`・`,` の3種の区切りを場当たり的に再パースしており、置換後に `type_args.clear()`（128・144・160行）で「二重マングリング防止」を都度行っている。この`clear()`の散在自体が、フラット名とtype_argsの二重管理が破綻している兆候である。

C9（配列フィールド）については、`substitute_type_in_type` の Array ケース（199-208行）が element_type を再帰置換するよう修正済みだが、置換の起点となる型キーが非単射なため、ネストしたジェネリック構造体の内側で `T[]` の `T` が解決されず未置換のまま残る経路が存在する。
`generate_specialized_struct` のフィールドサイズ算出（`mono_structs.cpp` 241-275行）は `switch(field_type->kind)` で `Array` を `default: size=8, align=8` に落とすため、未置換の `T[N]` が8バイトblobとして扱われ stride が食い違う。

補足として、`calculate_specialized_type_size`（`typeinfo.cpp` 163-228行）は自然アライメントのCレイアウトで正しく計算するが、`generate_specialized_struct` 側（`mono_structs.cpp` 241-275行）は独自の手書き`switch`を持ち、`String=16`・`default=8`と別基準で二重管理している。型キーの構造化と併せてこの二重管理も解消対象とする。

## 設計方針

中核は「フラットな連結文字列を、ネストを保持する可逆エンコーディングへ置換する」ことである。
既存の`std::string`ベースのキー流通を大きく壊さないため、`hir::Type`ツリーを唯一の真実とし、文字列キーは可逆エンコーディングに限定する。

### データ構造

1. 正規の型キーは `hir::TypePtr`（`name` + `type_args` の再帰ツリー）とする。特殊化の収集・生成・参照書き換えは、フラット名の再パースをやめ、この再帰ツリーを直接走査する。
2. 文字列キーが必要な箇所（`std::map`のキー、MIR/コード生成のシンボル名）には、長さプレフィックス付きの可逆エンコーディングを導入する。
   - 例: `Box<Pair<int,string>>` → `Box$1$3Pair$2$3int$6string`（`$N$` = 引数個数、`$M名` = 長さM+識別子）のような、区切り文字に依存せず一意に逆変換できる形式。
   - 区切りに使う記号（`$`等）はCmの識別子に出現しない文字を選び、ユーザー識別子（C8）との縮退を原理的に排除する。
3. 既存の`__`連結名は「表示用（デバッグ・エラーメッセージ）」に限定し、同一性判定には使わない。

### アルゴリズム変更

- `normalize_type_arg` / `make_specialized_struct_name` / `make_specialized_name`（`typeinfo.cpp`）を、`TypePtr`を受け取り可逆エンコード文字列を返す `encode_type_key(const hir::TypePtr&)` と、その逆の `decode_type_key(const std::string&) -> hir::TypePtr` の対に置き換える。エンコード/デコードは往復不変（`decode(encode(t))` が構造一致）をユニットテストで保証する。
- `collect_struct_specializations`（`mono_structs.cpp` 126-159行）の「`Node__int`を`__`で再分割」する分岐を削除し、`local.type->type_args` の再帰走査へ一本化する。
- `substitute_type_in_type`（`mono_internal.cpp`）の `__`/`<>`/`,` 再パース分岐（81-108行・110-147行・210-268行・270-342行）を、`type_args`の再帰置換（56-70行の既存ロジック）へ集約し、散在する `type_args.clear()`（128・144・160行）を除去する。置換後の名前は必ず `encode_type_key` 経由で生成する。
- フィールドサイズ算出を `generate_specialized_struct`（`mono_structs.cpp` 241-275行）の独自`switch`から `calculate_specialized_type_size` / `calculate_specialized_type_align`（`typeinfo.cpp`）呼び出しへ統合し、Arrayも正しく再帰計算させる（C9のstride不一致を根絶）。
- C8: ユーザー識別子との衝突検出は、エンコード後キーの名前空間をジェネリック特殊化専用の記号で分離することで原理的に回避する。加えて[[mangling-collision-detection]]の単一シンボルテーブルへ特殊化名も登録し、万一の衝突をハードエラー化する。

## 構文例・出力例

```
struct Pair<T, U> { T first; U second; }
struct Box<T> { T value; }

int main() {
    Box<Pair<int, string>> b;   // C7: 現状は Box__Pair__int__string へ平坦化され誤コンパイル
    b.value.first = 42;
    b.value.second = "hi";
    println("{b.value.first} {b.value.second}");
    return 0;
}
```

期待出力:

```
42 hi
```

C8の識別子衝突例:

```
struct Box<T> { T value; }
struct Box__int { int raw; }   // ユーザーが偶然この名前を使う

int main() {
    Box<int> a;      // 特殊化名が Box__int
    Box__int c;      // ユーザー型 Box__int
    // 現状: 同名衝突。設計後: 特殊化キーは別名前空間なので衝突しない
    return 0;
}
```

## 実装の段階分割

- Phase 1: `encode_type_key` / `decode_type_key` を新規追加し往復不変をユニットテストで固定する。既存のフラット名生成関数はそのまま残し、新関数は未接続で共存させる（挙動不変）。
- Phase 2: 構造体特殊化経路（`collect_struct_specializations` → `generate_specialized_struct` → `update_type_references`）を再帰ツリー走査＋`encode_type_key`へ切り替える。フィールドサイズ算出を`calculate_specialized_type_size`へ統合。ここでC9のstride不一致とネスト構造体（C7の一部）が解消する。
- Phase 3: 関数特殊化経路（`substitute_type_in_type`・`make_specialized_name`・`mono/driver.cpp`の不動点ループ）を同様に切り替え、`type_args.clear()`の散在を除去。C7の残りとC8の名前空間分離を完了。
- Phase 4: フラット`__`名を表示専用に降格し、旧再パース分岐をデッドコードとして削除。

## テスト計画

- `tests/common/generics/nested_type_args.cm` / `.expect`: `Box<Pair<int,string>>`・`Box<Box<int>>`・`Pair<Box<int>, Box<string>>` を構築しフィールド読み書き結果を出力（C7）。全バックエンド一致を確認する。
- `tests/common/generics/box_array_field.cm` / `.expect`: `Box<T>` の `T[N]` 配列フィールドに複数要素を書き込み、`short`/`long`/`double`要素で stride が正しいことを検証（C9）。既存 `box_short_field.cm` の兄弟として要素型を拡張する。
- `tests/common/generics/user_type_name_collision.cm` / `.expect`: ユーザー定義 `Box__int` とジェネリック `Box<int>` を同一プログラムに置き、両者が独立に動くことを検証（C8）。
- `tests/regression/cases/mir_lowering/type_key/`: `encode_type_key`/`decode_type_key` の往復不変と、ネスト・複数引数の一意性（縮退が起きないこと）をgtestで検証する（手組みの`hir::Type`ツリーを入力）。
- 検証観点: 全バックエンド（jit/native/wasm/js/ts）で出力一致、native LLVM検証エラーが出ないこと、wasmで木構造が壊れないこと。

## リスクと非互換性

- 型キーは内部表現であり、ユーザー可視の構文・意味論は変わらないため破壊的変更は無い（Cmの破壊的変更回避方針に適合）。
- 可逆エンコード名はデバッグ出力やMIRダンプの見た目を変える。既存の回帰テストがMIRシンボル名の文字列一致に依存している場合は追従が必要（表示専用の`__`名を維持することで影響を局所化する）。
- Phase移行中はフラット名と再帰キーが併存するため、両者の同期漏れが一時的なリスクとなる。Phase境界ごとに全バックデンドスイートを通し、`type_args.clear()`除去は最後にまとめて行う。

## 関連

- [[mangling-collision-detection]]（C16/M2）: 特殊化名を含む全マングル名の単一シンボルテーブル登録と衝突検出。C8の最終防波堤を共有する。
- [[generic-instantiation-diagnostics]]（H15/L8）: 不正なインスタンス化の診断。型キー構造化と同じ単相化経路に触れる。
- 監査レポート `docs/design/v0.17.0/large-scale-bottleneck-audit.md` の「ジェネリクス/モノモーフ化」節（105-108行）、構造的テーマ3「文字列ベースの型同一性」。
- 関連所見 M11（`primitive_kind_from_name` の型kind復元、`mono_internal.cpp` 15-47行）・M13（MIR側手計算レイアウトのデッドコード）。
