# 型同一性の構造化（再帰的型キー）（実装済み）

## 対象所見

| # | 領域 | 所見 | 状態 |
|---|------|------|------|
| C7 | ジェネリクス | ネストした型引数（`Box<Pair<int,string>>`等）が全バックエンドで誤コンパイル（`__`マングリングが非単射） | 実装済み |
| C8 | ジェネリクス | ユーザーが`Foo__int`という構造体を書くと`Foo<int>`の特殊化と同名衝突（レイアウト差でGEP検証エラー、同一なら黙って混同） | 実装済み |
| C9 | ジェネリクス | `Box<T>`の`T[N]`配列フィールドがnativeでゴミ（MIRに未置換の`T[]`型が残り、バックエンド間で結果が分裂） | 実装済み |

## 背景と根本原因

型の同一性が「基底名 + `__` + 型引数」のフラットな連結文字列で表現されており、この連結が非単射である。

特殊化名の生成は `src/internal/mir/lowering/mono/typeinfo.cpp` の3関数に集約されていた。
- `normalize_type_arg`: `*int`を`ptr_int`へ、`Box<Box<int>>`のような`<...>`表記を`__`区切りへ平坦化する。ネスト引数は`depth`を数えてカンマ分割するが、区切りには常に`__`しか使わないため階層情報が失われる。
- `make_specialized_struct_name`: `base_name + "__" + normalize_type_arg(arg)` を型引数分だけ連結する。
- `make_specialized_name`: 関数特殊化名を同じ規則で生成する。

この結果、`__`が有効な識別子文字である以上、`Box<Box<int>>`・`Box<Box, int>`（2引数）・ユーザー定義構造体`Box__Box__int`（C8）の3組が同一キー`Box__Box__int`へ縮退していた。

分解側も同じ曖昧さを抱えていた。`collect_struct_specializations`は`Node__int`を「`__`で最初に切って基底名、残りを`__`で再分割して型引数」と復元し、ネストがあると復元結果が原型と一致しなかった。`generate_specialized_struct`のフィールドサイズ算出は独自の手書き`switch`で`Array`を8バイトblob扱いし（C9のstride不一致）、`calculate_specialized_type_size`との二重管理になっていた。

さらに実装調査で、書き込み側のMIR lowering（`expr/binary.cpp`・`stmt/assign.cpp`の左辺値構築）がフィールド型の型パラメータ置換を`kind == Generic`でしか行っておらず、HIRで`T`が`Struct` kindになるケースでネストメンバへの代入文が黙って脱落する欠陥が見つかった（C7の実害の主因）。読み取り側（`expr/access.cpp`）はgeneric_params名で照合しており非対称だった。

## 実装した設計

中核は「特殊化の収集・生成・参照書き換えから、フラット名の再パースを排除し、`hir::Type`ツリー（`name` + `type_args`の再帰構造）を唯一の真実とする」ことである。

### 型キーの可逆エンコーディング（typekey モジュール）

`src/internal/mir/lowering/mono/typekey.{hpp,cpp}` に、区切り文字 `$`（Cmの識別子に出現しない）による長さプレフィックス付き可逆エンコーディングを実装した。
- 例: `Pair<int,string>` → `Pair$2$3$int6$string`、`Box<Pair<int,string>>` → `Box$1$20$Pair$2$3$int6$string`
- ポインタは`$P`、参照は`$R`、固定長配列は`$A<N>$`のマーカーで表現する。
- `encode_type_key` / `decode_type_key` は往復不変（`decode(encode(t))`が構造一致）をユニットテスト（`tests/unit/typekey_test.cpp`、13ケース）で保証する。
- `base_name_of` / `decode_type_args` / `display_name`（人間可読の`Box<Pair<int, string>>`形式）を提供する。

### シンボルキーの割り当て（ハイブリッド方式）

特殊化構造体のシンボル名は `struct_symbol_key(base, args)` で一元生成する。
- 通常は従来の`base__arg`フラット連結を維持する。関数特殊化名（`Vector__Vector__int__push`等）・型検査/HIR loweringが生成する呼び出し名と同じ規則であり、既存プログラムのシンボルが変わらない（破壊的変更回避）。
- フラット名がユーザー定義構造体と同名になる場合（C8）のみ、`$`区切りの可逆エンコード名へ退避して衝突を原理的に排除する。ユーザー識別子に`$`は使えないため逆方向の衝突は起こらない。
- フラット名の縮退が実害になる残りのケース（`Box<Box,int>`と`Box<Box<int>>`の共存等）は、[[mangling-collision-detection]]（C16）の単一シンボルテーブルでハードエラー化して防波堤とする。

### 構造体特殊化経路のツリー化

- `collect_struct_specializations`: `local.type->type_args`（`hir::TypePtr`ツリー）を文字列化せず直接収集する。ローカル型名がユーザー定義構造体と完全一致する場合は特殊化と混同しない（C8）。
- `generate_specialized_struct`: 型引数ツリーをそのまま置換マップにし、`substitute_type_tree`（名前の平坦化を行わない構造保存置換）でフィールド型を求める。レイアウトは`calculate_specialized_type_size`/`calculate_specialized_type_align`（自然アライメントのCレイアウト）へ統合し、独自`switch`の二重管理を廃止した（C9: `T[N]`配列も要素型の再帰計算で正しいstrideになる）。
- `to_symbol_type`: 置換後ツリー内のネストしたジェネリックインスタンス（`Box<Pair<int,string>>`のフィールド`Pair<int,string>`等）の特殊化を再帰的に生成し、フィールド型をシンボル名参照へ正規化する。
- `resolve_struct_field_types`: 特殊化名（フラット名・`$`エンコード名・基底名+型引数ツリー）から置換済みフィールド型を復元する共通ヘルパー。サイズ/アライメント計算のStructケースはこれに一本化した。
- `parse_flat_type_args`: フラット名の残り部分の復元を共通化し、基底の型パラメータが1個の場合は全セグメントを1引数として結合する（`Vector__Vector__int` → `[Vector__int]`。従来はセグメントごとに分割され誤引数化していた）。

### 代入経路の型パラメータ置換の対称化

`expr/binary.cpp`・`stmt/assign.cpp`の左辺値構築で、フィールド型の置換を読み取り側（`expr/access.cpp`）と同じ「generic_params名での照合」に統一した。これによりネストしたジェネリック構造体メンバへの代入（`b.value.first = 42`）が正しくMIRへloweringされる（従来は文ごと黙って脱落していた）。

## 構文例・出力例

```
struct Pair<T, U> { T first; U second; }
struct Box<T> { T value; }

int main() {
    Box<Pair<int, string>> b;
    b.value.first = 42;
    b.value.second = "hi";
    println("{b.value.first} {b.value.second}");
    return 0;
}
```

出力（全バックエンド一致）:

```
42 hi
```

C8の識別子衝突例:

```
struct Box<T> { T value; }
struct Box__int { int raw; }   // ユーザーが偶然この名前を使う

int main() {
    Box<int> a;      // 特殊化キーは Box$1$3$int へ退避
    Box__int c;      // ユーザー型 Box__int はそのまま
    return 0;
}
```

## テスト

- `tests/unit/typekey_test.cpp`: encode/decodeの往復不変・縮退組の分離（`Box<Box<int>>`/`Box<Box,int>`/`Box__Box__int`が別キー）・不正キーの拒否（13ケース）。
- `tests/common/generics/nested_type_args.cm`: `Box<Pair<int,string>>`・`Box<Box<int>>`・`Pair<Box<int>, Box<string>>`のフィールド読み書き（C7）。
- `tests/common/generics/box_array_field.cm`: `T[4]`配列フィールドのshort/long/double要素でのstride検証（C9）。
- `tests/common/generics/user_type_name_collision.cm`: ユーザー定義`Box__int`とジェネリック`Box<int>`の共存（C8）。
- いずれもjit/native/js/wasmで出力一致を確認。`Vector<Vector<int>>`等の既存コレクションスイートも全通過。

## 残課題（後続設計へ引き継ぎ）

- 関数特殊化経路（型検査・HIR loweringが生成するフラットな呼び出し名）の可逆キー化は行っていない。フラット名はユーザー衝突時のみ`$`退避するハイブリッドであり、`Box<Box,int>`と`Box<Box<int>>`が同一プログラムに共存するケースの検出は[[mangling-collision-detection]]（C16）の単一シンボルテーブルが担う。
- 複数型パラメータかつネスト引数のフラット名復元（`Pair__Box__int__string`）は一意に分解できないため、該当ケースはツリー経由（`type_args`保持）の経路のみが正となる。

## 関連

- [[mangling-collision-detection]]（C16/M2）: 特殊化名を含む全マングル名の単一シンボルテーブル登録と衝突検出。C8の最終防波堤を共有する。
- [[generic-instantiation-diagnostics]]（H15/L8）: 不正なインスタンス化の診断。型キー構造化と同じ単相化経路に触れる。
- 監査レポート `large-scale-bottleneck-audit.md` の「ジェネリクス/モノモーフ化」節、構造的テーマ3「文字列ベースの型同一性」。
