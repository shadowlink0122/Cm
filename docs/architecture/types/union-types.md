# ユニオン型の設計

Cmのユニオン型（`int | string`、`typedef Value = int | string | bool`）は、型検査段では変種リストを持つ`UnionType`、実行時には`{i32 tag, [N x i8] payload}`のタグ付きunion構造体として表現される。値の出し入れは`as`キャスト（構築とタグ検査付き取り出し）、実行時判別は`is`演算子とmatchの型パターンが担い、いずれもHIRの`HirCast`（`check_only`フラグ）という単一の表現に脱糖される。enumのタグ付きunionと同じメモリレイアウトを共有するが、タグの意味（変種インデックス）と構築経路（`as`キャスト）が異なる。

## 概要

パーサは`|`区切りの型並びを`parse_type_with_union`で`UnionType`に組み立て（`src/internal/syntax/parser/parser_type.cpp:383-416`）、各変種は型の文字列表現をタグ名とする`UnionVariant`になり、union全体の`name`は`"int | string"`のような表示名を持つ。`typedef Value = int | bool`は型検査で`typedef_defs_`に登録され（`src/internal/types/checking/decl.cpp:978-982`）、以後は`resolve_typedef`で本体の`UnionType`へ解決される。組み込みの`Result<T,E>`/`Option<T>`も同じ`UnionType`表現を`make_result_type`/`make_option_type`で構築する（`src/internal/syntax/ast/typedef.hpp:122-153`）。null許容は専用機構ではなく`int | null`という`Null`変種付きユニオンで表現する。

## データ構造とアルゴリズム

### 型検査段の表現と代入互換

型ノードは`Type`を継承した`UnionType { std::vector<UnionVariant> variants }`（`src/internal/syntax/ast/typedef.hpp:32-50`、`TypeKind::Union`は`src/internal/syntax/ast/types.hpp:52`）である。変種一覧の取得は`ast::union_variant_types`（`typedef.hpp:102-119`）に一元化されており、`type_args`形式（モノモーフ化後）と`UnionType::variants`形式（typedef union）の両対応になっている。ユニオンへの代入互換は`types_compatible`の冒頭で判定する。

```cpp
// src/internal/types/checking/utils/compat.cpp:101-124（抜粋）
// 例: int | null x = null; → a=Union{int,null}, b=Void
// 例: int | null x = 42;   → a=Union{int,null}, b=Int
if (a->kind == ast::TypeKind::Union) {
    ...
    for (const auto& variant : union_type->variants) {
        if (types_compatible(variant.fields[0], b)) {
            return true;
        }
    }
}
```

nullリテラル（Void型）は`Null`変種を持つユニオンにのみ代入できる（`compat.cpp:107-115`）。なお`typedef HttpMethod = "GET" | "POST"`のようなリテラルユニオンは別種の`LiteralUnionType`（`typedef.hpp:55-61`）で、基底型（string/int/float）との互換判定（`compat.cpp:306-332`）と、リテラル代入時の許容値検査`check_literal_assignment`（`compat.cpp:342` 以降）を持つ。こちらは実行時タグを持たず、基底型の値そのものとして扱われる。

### 実行時の値表現: `{i32 tag, [N x i8]}`

LLVM型への変換は`src/internal/codegen/llvm/core/types.cpp:531-623`にある。ペイロードサイズは全変種の最大サイズ（最低8バイト）で、構造体/ネストunion変種は`DataLayout`の実サイズを使う（`types.cpp:574-582`）。タグは変種リスト内のインデックス（`int | string`なら int=0、string=1）である。名前付きunionは名前で、匿名unionはペイロードサイズをキーに構造体をキャッシュ・共有する（`types.cpp:597-620`）。enumのタグ付きunionは`__TaggedUnion_<enum名>`という名前の同レイアウト構造体へ正規化され（`types.cpp:385-408`）、レイアウトは同じだがタグ値はenum宣言のバリアント順、構築はHIRの`HirEnumConstruct`（`src/internal/hir/nodes.hpp:178-183`）経由という違いがある（詳細は[../lowering/enums-and-match.md](../lowering/enums-and-match.md)）。

構築（`42 as Value`）はCast右辺値のユニオン構築パス（`src/internal/codegen/llvm/core/rvalue.cpp:181-294`）が行い、ソースのHIR型を変種リストと照合してタグ値を決め（kind一致、構造体は名前も一致。`rvalue.cpp:222-251`）、一時allocaへタグをstoreしペイロードへ値を書く（構造体変種はmemcpy、プリミティブはbitcastしてstore）。取り出し（`v as int`）は逆方向のパス（`rvalue.cpp:394-435`・ポインタ経由は`:460-496`）で、必ず実行時タグ検査を挿入し、アクティブな変種とターゲット型が不一致なら`invalid union cast`でパニックする（`emitUnionTagCheck` `rvalue.cpp:336-353`）。この検査が無いと不一致キャストは無検査のビット再解釈となり、silentな値化けやクラッシュになる。

### 実行時判別: `is`・型ガード・matchの型パターン

`v is int`はパーサが`CastExpr`に`type_check=true`を立てる形で表現され（`src/internal/syntax/parser/expr/binary.cpp:337-345`）、型検査は「オペランドがユニオンであること」「ターゲットが変種のいずれかであること」を検査してboolを返す（`src/internal/types/checking/expr/primary.cpp:118-140`）。HIR（`HirCast::check_only` `src/internal/hir/nodes.hpp:171-173`）→MIR（`CastData::check_only` `src/internal/mir/nodes.hpp:227-228`、`src/internal/mir/lowering/expr/cast.cpp:104-111`）と運ばれ、codegenはタグをloadして期待インデックスと`icmp eq`するだけでペイロードには触れない（`rvalue.cpp:355-392`）。

```cm
typedef Value = int | string | bool;
Value v = 42;
if (v is int) {
    const int n = v as int;  // 型ガード後の取り出し（タグ検査は成功する）
}
```

`is`による自動的な型絞り込み（フロー型付け）は行わない設計で、取り出しには明示の`as`が必要だが、不一致時はタグ検査パニックが安全網になる。matchの型パターン（`MatchPatternKind::Type` `src/internal/syntax/ast/expr.hpp:408`）はこの2つの合成として脱糖される: アーム条件は`is`相当の`check_only`キャスト（`src/internal/hir/lowering/expr_match.cpp:230-237`）、束縛変数は`as`キャストの`HirLet`（`src/internal/hir/lowering/stmt.cpp:777-791`）である。型検査側はscrutineeがユニオンであることとパターン型が変種であることを検査し、束縛変数をパターン型で定義する（`src/internal/types/checking/expr/match.cpp:407-436`）。

```cm
match (v) {
    int i => println("int: {i}"),
    string s => println("str: {s}"),
    bool b => println("bool: {b}"),
    _ => println("other"),
}
```

### Result/Option・enumタグ付きunionとの関係と違い

組み込みの`Result<T,E>`/`Option<T>`は`make_result_type`/`make_option_type`が`ok`/`err`・`some`/`none`という名前付き変種の`UnionType`として構築する（`src/internal/syntax/ast/typedef.hpp:122-153`）。一方、ユーザ定義のペイロード付きenumは`resolve_typedef`が「ペイロード付きバリアントを持つenumはintへ解決せずタグ付きunionとして保持する」規則で温存される（`src/internal/types/checking/utils/compat.cpp:34-71`）。両者は最終的に同じ`{i32 tag, [N x i8]}`レイアウトに落ちるが、経路が異なる。

| | typedefユニオン（`int \| string`） | enumタグ付きunion（`Result`等） |
|---|---|---|
| タグの意味 | 変種リストのインデックス | enum宣言のバリアント順のタグ値 |
| 構築 | `値 as Union`のCast（`rvalue.cpp:181-294`） | `HirEnumConstruct`（`src/internal/hir/nodes.hpp:178-183`） |
| 取り出し | `union as 変種型`のタグ検査付きCast | matchの`__tag`比較+`HirEnumPayload`抽出 |
| 判別 | `is`／matchの型パターン | matchのバリアントパターン |

この分離により、ユニオンのCast経路にenumのタグ規約を混ぜない（逆も同様）ことが実装上の境界になっている。enum側の詳細は[../lowering/enums-and-match.md](../lowering/enums-and-match.md)を参照。

## 実装箇所

| ファイル | 役割 |
|---|---|
| `src/internal/syntax/ast/typedef.hpp:32-119` | `UnionType`/`UnionVariant`/`LiteralUnionType`と`union_variant_types` |
| `src/internal/syntax/parser/parser_type.cpp:383-416` | `A \| B \| C`構文の`UnionType`構築 |
| `src/internal/types/checking/utils/compat.cpp:101-128`・`:306-332` | ユニオンへの代入互換とリテラルユニオン互換 |
| `src/internal/types/checking/expr/primary.cpp:118-140` | `is`の型検査（ユニオン限定・変種検査） |
| `src/internal/types/checking/expr/match.cpp:407-436` | matchの型パターン検査と束縛変数の型付け |
| `src/internal/hir/lowering/expr_match.cpp:230-237` / `src/internal/hir/lowering/stmt.cpp:777-791` | 型パターンの`is`条件と`as`束縛への脱糖 |
| `src/internal/codegen/llvm/core/types.cpp:531-623` | `{i32, [N x i8]}`レイアウト生成とキャッシュ |
| `src/internal/codegen/llvm/core/rvalue.cpp:181-294`・`:336-496` | ユニオン構築・タグ検査・取り出し・`is`のタグ比較 |

## 落とし穴とケア

- タグ値は「変種リストのインデックス」であり、構築側（`rvalue.cpp:222-251`）と取り出し側（`computeExpectedUnionTag` `rvalue.cpp:298-334`）が同じ`union_variant_types`の順序を見ることが不変条件である。片側だけ変種列挙の方法を変えるとタグの対応が崩れ、正しいコードがタグ検査パニックする。
- typedefユニオンはMIR lowering時に必ず本体へ解決してからCast/型変換に渡す（`src/internal/mir/lowering/expr/cast.cpp:97-102`）。未解決のエイリアス名がcodegenに届くとユニオン構築が認識されず、素通しのビット再解釈になる。
- 取り出し`as`のタグ検査（`rvalue.cpp:336-353`）を外してはならない。この検査は「アクティブでない変種の取り出しが無検査でゴミ値を返す」というバグのクラスを実行時パニックへ変換している（負面テスト: `tests/common/errors/union_cast_mismatch.cm`）。
- 匿名unionのLLVM構造体はペイロードサイズでキャッシュ共有される（`types.cpp:602-605`）。codegenがunion判定を「`{i32, [N x i8]}`の形」で行う箇所（`rvalue.cpp:186-190` 等）はこの形状規約に依存しており、レイアウトへのフィールド追加は全判定箇所の同時更新が必要になる。
- ユニオンのタグ更新は再代入時のユニオン構築を経由して行われるため、`v = "hello"`のような再代入で構築パスを迂回する最適化を入れるとタグとペイロードが食い違う（回帰: `tests/common/types/union_is.cm`の再代入ケース）。
- 回帰テストの場所: `tests/common/types/union_is.cm`（`is`とタグ更新）、`union_match_type.cm`（matchの型パターン）、`typedef_union_comprehensive.cm`・`union_array.cm`・`union_array_func.cm`（typedef unionの配列・引数・戻り値）、`inline_union_null.cm`（`int | null`）、`union_of_structs.cm`（構造体変種）、負面は`tests/common/errors/union_is_non_union.cm`・`union_is_invalid_variant.cm`・`union_cast_mismatch.cm`。

## 関連資料

- [enumとmatchのlowering](../lowering/enums-and-match.md) — 同レイアウトを使うenumタグ付きunionの構築・match脱糖・網羅性検査
- [`as`キャストの設計](casts.md) — ユニオン出し入れを含むキャストパイプラインの全体像
- [型推論の設計](inference.md) — ユニオン代入互換が使われる推論・検査の流れ
- [MIR→LLVM IR変換の構造](../codegen-native/mir-to-llvm.md) — 型マッピングとRvalue変換の位置づけ
